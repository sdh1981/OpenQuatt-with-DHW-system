#pragma once

#ifndef OPENQUATT_OQ_ODU_RUNTIME_FREQUENCY_TABLE_H
#define OPENQUATT_OQ_ODU_RUNTIME_FREQUENCY_TABLE_H

// ============================================================================
// OpenQuatt - EXPERIMENTEEL: frequentietabel van de buitenunit tijdelijk zetten
// ============================================================================
//
// Overgenomen van upstream (openquatt/includes/experimental/
// oq_odu_runtime_frequency_table.h). De logica is bewust vrijwel ongewijzigd:
// dit schrijft over Modbus naar een draaiende warmtepomp, en dat is niet de
// plek om eigen varianten te bedenken. Aangepast zijn de meldingen (Nederlands,
// met dezelfde hoofdletterprefixen zodat ze greppable blijven) en het feit dat
// de entiteiten bij ons zichtbaar zijn in Home Assistant in plaats van intern
// voor een webapp die deze fork niet meebouwt.
//
// --- Wat dit doet ---
// Schrijft 22 registers vanaf modbus 3000: de koelkromme F0-F10 en de
// verwarmingskromme F0-F10. Het documentblad rekent vanaf 1, de bus vanaf 0:
//   koelen  F0-F10  blad 3001..3011 -> modbus 3000..3010
//   verwarmen F0-F10 blad 3012..3022 -> modbus 3011..3021
// Verder wordt er niets in de EEPROM aangeraakt.
//
// --- Waarom het tijdelijk is ---
// Dit raakt de runtime-schaduw, niet de chip, en de checksum op blad 3510/3511
// blijft staan. Een volledige power cycle van de buitenunit zet de fabriekstabel
// terug. Dat is meteen je noodrem: spanning eraf en je bent terug bij af.
//
// --- Waarom de bewaking zo streng is ---
// Een frequentiekromme wijzigen terwijl de compressor draait betekent dat de
// unit midden in bedrijf een andere betekenis aan zijn eigen standen geeft.
// Daarom wordt eerst werkmodus en compressorfrequentie gelezen, en pas
// geschreven als beide nul zijn. Zie ook docs/odu-eeprom-parameters.md.
// ============================================================================

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
// <span> voor de lees-callbacks: ESPHome 2026.8.0 geeft de payload door als
// std::span<const uint8_t>. Zie min_version in openquatt_base.yaml.
// <vector> blijft nodig: create_write_multiple_command neemt nog steeds een
// std::vector<uint16_t> met de te schrijven waarden.
#include <span>
#include <vector>

#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/number/number.h"
#include "esphome/components/openquatt_odu_eeprom_dump/OpenQuattOduEepromDump.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/log.h"

namespace oq_odu_runtime_frequency {

static const char *const TAG = "oq_odu_freq";

// Modbus-adres, dus bladadres min 1. 22 registers = twee krommen van elf.
static constexpr uint16_t RUNTIME_TABLE_START_ADDRESS = 3000;
static constexpr uint16_t RUNTIME_TABLE_REGISTER_COUNT = 22;

// Bewakingslezing: modbus 2099..2103 = doc 2100..2104. Index 0 is de huidige
// werkmodus, index 4 de draaiende compressorfrequentie.
static constexpr uint16_t GUARD_START_ADDRESS = 2099;
static constexpr uint16_t GUARD_REGISTER_COUNT = 5;
static constexpr size_t GUARD_WORKING_MODE_INDEX = 0;
static constexpr size_t GUARD_COMPRESSOR_FREQUENCY_INDEX = 4;

static constexpr float MIN_FREQUENCY_HZ = 0.0f;
static constexpr float MAX_FREQUENCY_HZ = 120.0f;

// Grootste verschuiving die een stand in een keer mag maken, vergeleken met wat
// er OP DAT MOMENT in de unit staat -- niet met wat de invoervakken zich
// herinneren. Die vakjes hebben restore_value: true, dus een waarde uit een
// eerdere proef blijft staan en ziet er precies zo uit als een bedoelde
// instelling. Deze grens vangt dat op.
//
// Symmetrisch: een typo die verlaagt is net zo goed een typo, en te ver omlaag
// duikt onder de 30 Hz die in deze EEPROM overal als ondergrens staat.
//
// Een grote bedoelde wijziging maak je dus in stappen. Dat is traag, en dat is
// het punt.
static constexpr int MAX_STEP_HZ = 5;

struct RuntimeFrequencyTableRefs {
  esphome::modbus_controller::ModbusController *controller;
  esphome::openquatt_odu_eeprom_dump::OpenQuattOduEepromDump *eeprom_dump;
  esphome::switch_::Switch *enable_switch;
  esphome::text_sensor::TextSensor *status;
  const char *prefix;
  std::array<esphome::number::Number *, 11> cooling_desired;
  std::array<esphome::number::Number *, 11> heating_desired;
};

inline void publish_status(const RuntimeFrequencyTableRefs &refs, const char *message) {
  refs.status->publish_state(message);
  ESP_LOGW(TAG, "%s%s", refs.prefix, message);
}

inline bool valid_frequency(float value) {
  return !std::isnan(value) && value >= MIN_FREQUENCY_HZ && value <= MAX_FREQUENCY_HZ;
}

// Een kromme moet oplopend zijn: stand N mag nooit trager draaien dan stand N-1.
// Een dalende tabel zou de niveaulogica van de unit betekenisloos maken.
inline bool validate_monotonic_table(const std::array<float, 11> &values) {
  for (size_t i = 0; i < values.size(); i++) {
    if (!valid_frequency(values[i])) return false;
    if (i > 0 && values[i] < values[i - 1]) return false;
  }
  return true;
}

inline bool read_u16_word(std::span<const uint8_t> data, size_t index, uint16_t &value) {
  const size_t offset = index * 2U;
  if (data.size() < offset + 2U) return false;
  value = (uint16_t(data[offset]) << 8) | uint16_t(data[offset + 1U]);
  return true;
}

inline bool read_word_as_frequency(std::span<const uint8_t> data, size_t index, float &value) {
  uint16_t raw = 0;
  if (!read_u16_word(data, index, raw)) return false;
  value = float(raw);
  return valid_frequency(value);
}

inline bool parse_runtime_table(std::span<const uint8_t> data, std::array<float, 11> &cooling,
                               std::array<float, 11> &heating, int &loaded) {
  loaded = 0;
  float value = NAN;
  for (size_t i = 0; i < cooling.size(); i++) {
    if (!read_word_as_frequency(data, i, value)) return false;
    cooling[i] = value;
    loaded++;
  }
  for (size_t i = 0; i < heating.size(); i++) {
    if (!read_word_as_frequency(data, i + cooling.size(), value)) return false;
    heating[i] = value;
    loaded++;
  }
  return true;
}

inline void publish_runtime_table(const RuntimeFrequencyTableRefs &refs, const std::array<float, 11> &cooling,
                                 const std::array<float, 11> &heating) {
  for (size_t i = 0; i < cooling.size(); i++) refs.cooling_desired[i]->publish_state(cooling[i]);
  for (size_t i = 0; i < heating.size(); i++) refs.heating_desired[i]->publish_state(heating[i]);
}

inline bool tables_match(const std::array<float, 11> &actual, const std::array<float, 11> &expected) {
  for (size_t i = 0; i < actual.size(); i++) {
    if (lroundf(actual[i]) != lroundf(expected[i])) return false;
  }
  return true;
}

inline std::vector<uint16_t> build_runtime_write_values(const std::array<float, 11> &cooling,
                                                        const std::array<float, 11> &heating) {
  std::vector<uint16_t> values;
  values.reserve(RUNTIME_TABLE_REGISTER_COUNT);
  for (float value : cooling) values.push_back(static_cast<uint16_t>(lroundf(value)));
  for (float value : heating) values.push_back(static_cast<uint16_t>(lroundf(value)));
  return values;
}

inline void queue_apply_readback(RuntimeFrequencyTableRefs refs, std::array<float, 11> expected_cooling,
                                 std::array<float, 11> expected_heating);
inline void queue_step_limited_write(RuntimeFrequencyTableRefs refs, std::array<float, 11> cooling,
                                     std::array<float, 11> heating);

// Schrijft de 22 registers in een enkele functie-16 transactie. De
// inschakelknop gaat er hier uit, niet later: één druk is één schrijfactie,
// ook als er daarna iets misgaat.
inline void queue_runtime_write(RuntimeFrequencyTableRefs refs, std::array<float, 11> cooling,
                                std::array<float, 11> heating) {
  refs.enable_switch->turn_off();
  publish_status(refs, "SCHRIJVEN: opdracht in de wachtrij");
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_write_multiple_command(
      refs.controller, RUNTIME_TABLE_START_ADDRESS, RUNTIME_TABLE_REGISTER_COUNT,
      build_runtime_write_values(cooling, heating));
  cmd.on_data_func = [refs, cooling, heating](esphome::modbus::EntityType register_type,
                                              uint16_t start_address, std::span<const uint8_t> data) {
    publish_status(refs, "SCHRIJVEN: bevestigd door de unit");
    queue_apply_readback(refs, cooling, heating);
  };
  refs.controller->queue_command(cmd);
}

// Leest eerst de toestand van de unit. Schrijven mag alleen in stilstand:
// werkmodus 0 en compressorfrequentie 0.
// allow_while_running komt van een schakelaar per unit. Staat die uit, dan mag
// er alleen geschreven worden op een stilstaande machine. Staat hij aan, dan
// wordt de toestand nog steeds gelezen -- we willen in het log zien waarin we
// geschreven hebben -- maar niet meer geweigerd.
//
// Wat je daarmee overneemt: de buitenunit krijgt midden in bedrijf een andere
// betekenis voor zijn eigen standen. Draait hij op stand 8 en verschuif je die,
// dan verandert zijn toerental op een commando dat niet de normale niveauwissel
// is. De beveiligingen IN de unit blijven staan, en een power cycle zet de
// fabriekstabel terug -- dat blijft de noodrem.
inline void queue_guarded_runtime_write(RuntimeFrequencyTableRefs refs, std::array<float, 11> cooling,
                                        std::array<float, 11> heating, bool allow_while_running) {
  publish_status(refs, "CONTROLE: toestand van de unit wordt gelezen");
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, GUARD_START_ADDRESS, GUARD_REGISTER_COUNT,
      [refs, cooling, heating, allow_while_running](esphome::modbus::EntityType register_type,
                                                    uint16_t start_address, std::span<const uint8_t> data) {
        uint16_t working_mode = 0;
        uint16_t compressor_hz = 0;
        if (!read_u16_word(data, GUARD_WORKING_MODE_INDEX, working_mode)) {
          publish_status(refs, "GEBLOKKEERD: werkmodus onbekend");
          return;
        }
        if (!read_u16_word(data, GUARD_COMPRESSOR_FREQUENCY_INDEX, compressor_hz)) {
          publish_status(refs, "GEBLOKKEERD: compressorfrequentie onbekend");
          return;
        }
        const bool unit_running = (working_mode != 0) || (compressor_hz > 0);
        if (unit_running && !allow_while_running) {
          publish_status(refs, working_mode != 0 ? "GEBLOKKEERD: unit staat niet in standby"
                                                 : "GEBLOKKEERD: compressor draait");
          return;
        }
        if (unit_running) {
          // Niet blokkeren, wel vastleggen waarin je geschreven hebt. Loopt er
          // daarna iets vreemds, dan staat hier in het log op welke frequentie
          // de compressor draaide toen de tabel onder hem veranderde.
          ESP_LOGW(TAG, "%sschrijven TIJDENS BEDRIJF: werkmodus %u, compressor %u Hz", refs.prefix,
                   (unsigned) working_mode, (unsigned) compressor_hz);
        }
        queue_step_limited_write(refs, cooling, heating);
      });
  refs.controller->queue_command(cmd);
}

// Laatste zeef voor het schrijven: haalt de tabel op die NU in de unit staat en
// weigert als een stand er meer dan MAX_STEP_HZ van afwijkt.
//
// Waarom tegen de unit en niet tegen de invoervakken: die vakken onthouden hun
// waarde over een herstart heen, dus een getal uit een eerdere proef ziet er
// hetzelfde uit als een bedoelde instelling. Alleen de unit weet wat er echt
// staat.
//
// Kost een extra leescommando per schrijfactie. Dat is het waard -- schrijven
// gebeurt zelden, en dit is de stap die een vergissing tegenhoudt.
inline void queue_step_limited_write(RuntimeFrequencyTableRefs refs, std::array<float, 11> cooling,
                                     std::array<float, 11> heating) {
  publish_status(refs, "CONTROLE: huidige tabel wordt gelezen");
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, RUNTIME_TABLE_START_ADDRESS,
      RUNTIME_TABLE_REGISTER_COUNT,
      [refs, cooling, heating](esphome::modbus::EntityType register_type, uint16_t start_address,
                               std::span<const uint8_t> data) {
        std::array<float, 11> current_cooling{};
        std::array<float, 11> current_heating{};
        int loaded = 0;
        if (!parse_runtime_table(data, current_cooling, current_heating, loaded)) {
          publish_status(refs, "GEBLOKKEERD: huidige tabel niet leesbaar");
          return;
        }

        char status[96];
        auto within_step = [&](const std::array<float, 11> &wanted, const std::array<float, 11> &current,
                               const char *label) -> bool {
          for (size_t i = 0; i < wanted.size(); i++) {
            const int delta = (int) lroundf(wanted[i]) - (int) lroundf(current[i]);
            const int distance = delta < 0 ? -delta : delta;
            if (distance > MAX_STEP_HZ) {
              snprintf(status, sizeof(status), "GEBLOKKEERD: %s F%u wil %d Hz verschuiven, max %d per keer",
                       label, (unsigned) i, delta, MAX_STEP_HZ);
              publish_status(refs, status);
              return false;
            }
          }
          return true;
        };

        if (!within_step(cooling, current_cooling, "koelen")) return;
        if (!within_step(heating, current_heating, "verwarmen")) return;

        queue_runtime_write(refs, cooling, heating);
      });
  refs.controller->queue_command(cmd);
}

// Leest terug wat er nu werkelijk staat en vergelijkt met wat we bedoelden.
// Zonder deze stap weet je alleen dat de unit de opdracht heeft aangenomen,
// niet dat hij hem heeft uitgevoerd.
inline void queue_apply_readback(RuntimeFrequencyTableRefs refs, std::array<float, 11> expected_cooling,
                                 std::array<float, 11> expected_heating) {
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, RUNTIME_TABLE_START_ADDRESS,
      RUNTIME_TABLE_REGISTER_COUNT,
      [refs, expected_cooling, expected_heating](esphome::modbus::EntityType register_type,
                                                 uint16_t start_address, std::span<const uint8_t> data) {
        std::array<float, 11> cooling{};
        std::array<float, 11> heating{};
        int loaded = 0;
        if (!parse_runtime_table(data, cooling, heating, loaded)) {
          char status[64];
          snprintf(status, sizeof(status), "CONTROLE MISLUKT: %d/22 registers gelezen", loaded);
          publish_status(refs, status);
          return;
        }
        publish_runtime_table(refs, cooling, heating);
        if (!tables_match(cooling, expected_cooling) || !tables_match(heating, expected_heating)) {
          publish_status(refs, "CONTROLE MISLUKT: teruglezing wijkt af");
          return;
        }
        // Expliciet in de melding dat dit vluchtig is: de invoervakken houden
        // hun waarde vast over een herstart van de ESP, maar de buitenunit valt
        // bij een power cycle terug op fabriek. Zonder deze toevoeging blijft
        // hier "toegepast" staan terwijl de unit allang is teruggevallen.
        publish_status(refs, "TOEGEPAST: actief tot de buitenunit spanningsloos is geweest");
      });
  refs.controller->queue_command(cmd);
}

// Haalt de tabel op die nu in de unit staat, zodat je bewerkt wat er
// werkelijk is in plaats van wat je denkt dat er is.
inline void load_runtime_table(RuntimeFrequencyTableRefs refs) {
  if (refs.eeprom_dump != nullptr && refs.eeprom_dump->is_active()) {
    publish_status(refs, "GEBLOKKEERD: EEPROM-dump loopt");
    return;
  }
  publish_status(refs, "OPHALEN: tabel wordt gelezen");
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, RUNTIME_TABLE_START_ADDRESS,
      RUNTIME_TABLE_REGISTER_COUNT,
      [refs](esphome::modbus::EntityType register_type, uint16_t start_address,
             std::span<const uint8_t> data) {
        std::array<float, 11> cooling{};
        std::array<float, 11> heating{};
        int loaded = 0;
        if (!parse_runtime_table(data, cooling, heating, loaded)) {
          char status[64];
          snprintf(status, sizeof(status), "OPHALEN MISLUKT: %d/22 registers gelezen", loaded);
          publish_status(refs, status);
          return;
        }
        publish_runtime_table(refs, cooling, heating);
        char status[64];
        snprintf(status, sizeof(status), "OPGEHAALD: %d/22 registers", loaded);
        publish_status(refs, status);
      });
  refs.controller->queue_command(cmd);
}

inline bool read_desired_values(const std::array<esphome::number::Number *, 11> &entities,
                                std::array<float, 11> &values) {
  for (size_t i = 0; i < entities.size(); i++) values[i] = entities[i]->state;
  return validate_monotonic_table(values);
}

inline void apply_runtime_table(RuntimeFrequencyTableRefs refs, bool enabled, bool allow_while_running) {
  if (refs.eeprom_dump != nullptr && refs.eeprom_dump->is_active()) {
    publish_status(refs, "GEBLOKKEERD: EEPROM-dump loopt");
    return;
  }
  if (!enabled) {
    publish_status(refs, "GEBLOKKEERD: schrijven staat niet vrijgegeven");
    return;
  }

  std::array<float, 11> cooling{};
  std::array<float, 11> heating{};
  if (!read_desired_values(refs.cooling_desired, cooling)) {
    publish_status(refs, "GEBLOKKEERD: koeltabel ongeldig of niet oplopend");
    return;
  }
  if (!read_desired_values(refs.heating_desired, heating)) {
    publish_status(refs, "GEBLOKKEERD: verwarmingstabel ongeldig of niet oplopend");
    return;
  }

  queue_guarded_runtime_write(refs, cooling, heating, allow_while_running);
}

}  // namespace oq_odu_runtime_frequency

#endif  // OPENQUATT_OQ_ODU_RUNTIME_FREQUENCY_TABLE_H
