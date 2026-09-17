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
// verwarmingskromme F0-F10. Verder wordt er niets in de EEPROM aangeraakt.
// Adressen, grenzen en de beslissingen staan in
// oq_odu_runtime_frequency_table_logic.h (host-getest).
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
//
// --- Modbus sinds ESPHome 2026.9.0 ---
// Tot 2026.9.0 hing elke stap als ModbusCommandItem met een lambda in de
// wachtrij van de modbus_controller, en startte de volgende stap vanuit die
// lambda. ModbusCommandItem en queue_command() zijn deprecated en verdwijnen in
// 2027.3.0.
//
// Nu heeft elke HP een vast RuntimeFrequencyTableDevice op de hub, met hub en
// adres van zijn controller. De keten is dezelfde, maar dan als toestandsmachine:
//
//   ophalen:    LOAD
//   toepassen:  GUARD -> STEP_CHECK -> WRITE -> READBACK
//
// Twee dingen worden daardoor beter:
//   1. Een foutcode, niet-standaard antwoord of uitblijvend antwoord komt nu als
//      afsluitende callback binnen en eindigt in een statusmelding. Voorheen
//      bleef de status dan op de laatste tussenstap staan.
//   2. Per unit loopt er hooguit een opdracht tegelijk. Een tweede druk tijdens
//      een lopende keten wordt geweigerd in plaats van ertussen geschoven.
// ============================================================================

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

#include "oq_odu_runtime_frequency_table_logic.h"

#include "esphome/components/modbus/modbus.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/number/number.h"
#include "esphome/components/openquatt_odu_eeprom_dump/OpenQuattOduEepromDump.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace oq_odu_runtime_frequency {

static const char *const TAG = "oq_odu_freq";

struct RuntimeFrequencyTableRefs {
  esphome::modbus_controller::ModbusController *controller;
  esphome::openquatt_odu_eeprom_dump::OpenQuattOduEepromDump *eeprom_dump;
  esphome::switch_::Switch *enable_switch;
  esphome::text_sensor::TextSensor *status;
  const char *prefix;
  std::array<esphome::number::Number *, CURVE_POINTS> cooling_desired;
  std::array<esphome::number::Number *, CURVE_POINTS> heating_desired;
};

inline void publish_status(const RuntimeFrequencyTableRefs &refs, const char *message) {
  refs.status->publish_state(message);
  ESP_LOGW(TAG, "%s%s", refs.prefix, message);
}

inline void publish_runtime_table(const RuntimeFrequencyTableRefs &refs, const FrequencyCurve &cooling,
                                  const FrequencyCurve &heating) {
  for (size_t i = 0; i < cooling.size(); i++) refs.cooling_desired[i]->publish_state(cooling[i]);
  for (size_t i = 0; i < heating.size(); i++) refs.heating_desired[i]->publish_state(heating[i]);
}

// ----------------------------------------------------------------------------
// Stille uitlezing van de tabel die de ODU nu gebruikt
// ----------------------------------------------------------------------------
// Voor de Power House v0.50-berekening, die per stand wil weten op hoeveel Hz
// de compressor werkelijk draait. Leest dezelfde 22 registers als "tabel
// ophalen", maar raakt geen invoervakken of statusmelding aan.
//
//   - eerste lezing 20 s na opstart, daarna elke 15 min
//   - bij een mislukte lezing na 60 s opnieuw; de laatst bekende tabel blijft
//   - overgeslagen zolang een EEPROM-dump loopt
//   - na "tabel ophalen" of een geslaagde teruglezing na schrijven wordt de
//     opgeslagen tabel meteen bijgewerkt (store), zonder extra lezing
//
// De periodieke lezing vangt ook een power cycle van de ODU: die zet de
// fabriekstabel terug zonder dat de ESP het merkt.
class RuntimeFrequencySnapshotReader : public esphome::modbus::ModbusClientDevice {
 public:
  static constexpr uint32_t FIRST_READ_AFTER_MS = 20000UL;
  static constexpr uint32_t REFRESH_MS = 15UL * 60UL * 1000UL;
  static constexpr uint32_t RETRY_MS = 60000UL;
  static constexpr uint32_t PENDING_TIMEOUT_MS = 30000UL;

  bool bound_to(const esphome::modbus_controller::ModbusController *controller) const {
    return this->controller_ == controller;
  }
  bool unbound() const { return this->controller_ == nullptr; }

  void poll(esphome::modbus_controller::ModbusController *controller, uint32_t now_ms, bool bus_busy) {
    if (controller == nullptr) return;
    if (this->controller_ == nullptr) this->bind_(controller, now_ms);
    if (this->pending_) {
      if (static_cast<uint32_t>(now_ms - this->queued_ms_) < PENDING_TIMEOUT_MS) return;
      // Vangnet: de hub belooft een afsluitende callback, maar blijft die uit,
      // dan loskoppelen en later opnieuw.
      this->clear_tx_queue_for_device();
      this->fail_(now_ms);
      return;
    }
    if (bus_busy || static_cast<int32_t>(now_ms - this->next_due_ms_) < 0) return;
    this->pending_ = true;
    this->queued_ms_ = now_ms;
    if (!this->read_holding_registers(RUNTIME_TABLE_START_ADDRESS, RUNTIME_TABLE_REGISTER_COUNT)) {
      this->fail_(now_ms);
    }
  }

  // Een geverifieerde tabel uit het schrijfpad of van "tabel ophalen".
  void store(const FrequencyCurve &cooling, const FrequencyCurve &heating, uint32_t now_ms) {
    if (!validate_monotonic_table(cooling) || !validate_monotonic_table(heating)) return;
    for (size_t i = 0; i < CURVE_POINTS; ++i) this->heating_hz_[i] = static_cast<uint16_t>(std::lround(heating[i]));
    this->known_ = true;
    this->updated_ms_ = now_ms;
    this->failures_ = 0;
    this->next_due_ms_ = now_ms + REFRESH_MS;
  }

  bool known() const { return this->known_; }
  const std::array<uint16_t, CURVE_POINTS> &heating_hz() const { return this->heating_hz_; }
  uint32_t updated_ms() const { return this->updated_ms_; }
  uint8_t failures() const { return this->failures_; }

 protected:
  void on_read_holding_registers(uint16_t start_address, std::span<const uint16_t> registers,
                                 esphome::modbus::ResponseStatus status) override {
    if (!this->pending_ || start_address != RUNTIME_TABLE_START_ADDRESS) return;
    this->pending_ = false;
    const uint32_t now_ms = esphome::millis();
    FrequencyCurve cooling{};
    FrequencyCurve heating{};
    int loaded = 0;
    if (!esphome::modbus::succeeded(status) || !parse_runtime_table(registers, cooling, heating, loaded) ||
        !validate_monotonic_table(cooling) || !validate_monotonic_table(heating)) {
      this->fail_(now_ms);
      return;
    }
    this->store(cooling, heating, now_ms);
  }
  void on_custom_response(std::span<const uint8_t>, std::span<const uint8_t>,
                          esphome::modbus::ResponseStatus) override {
    if (this->pending_) this->fail_(esphome::millis());
  }
  bool on_no_response(std::span<const uint8_t>) override {
    if (this->pending_) this->fail_(esphome::millis());
    return false;
  }
  void on_not_sent(std::span<const uint8_t>) override {
    if (this->pending_) this->fail_(esphome::millis());
  }

 private:
  void bind_(esphome::modbus_controller::ModbusController *controller, uint32_t now_ms) {
    this->controller_ = controller;
    this->set_parent(controller->hub());
    this->set_address(controller->device_address());
    this->next_due_ms_ = now_ms + FIRST_READ_AFTER_MS;
  }
  void fail_(uint32_t now_ms) {
    this->pending_ = false;
    if (this->failures_ < 255) this->failures_++;
    this->next_due_ms_ = now_ms + RETRY_MS;
  }

  esphome::modbus_controller::ModbusController *controller_{nullptr};
  std::array<uint16_t, CURVE_POINTS> heating_hz_{};
  bool known_{false};
  bool pending_{false};
  uint8_t failures_{0};
  uint32_t queued_ms_{0};
  uint32_t updated_ms_{0};
  uint32_t next_due_ms_{0};
};

// Een lezer per HP (0 = HP1, 1 = HP2). Leeft zo lang als het programma, net als
// de schrijf-devices: de hub moet zijn callback altijd kunnen afleveren.
inline RuntimeFrequencySnapshotReader &snapshot_reader(bool hp1) {
  static std::array<RuntimeFrequencySnapshotReader, 2> readers;
  return readers[hp1 ? 0 : 1];
}

inline RuntimeFrequencySnapshotReader *snapshot_reader_for(
    const esphome::modbus_controller::ModbusController *controller) {
  for (bool hp1 : {true, false}) {
    if (snapshot_reader(hp1).bound_to(controller)) return &snapshot_reader(hp1);
  }
  return nullptr;
}

class RuntimeFrequencyTableDevice : public esphome::modbus::ModbusClientDevice {
 public:
  enum class Stage : uint8_t { IDLE, LOAD, GUARD, STEP_CHECK, WRITE, READBACK };

  // Vangnet voor een keten die nooit afsluit. De hub belooft per geaccepteerd
  // verzoek precies een afsluitende callback, dus dit hoort niet te gebeuren;
  // zonder vangnet zou een fout daarin de unit tot de volgende herstart op
  // "BEZIG" laten staan.
  static constexpr uint32_t STALE_AFTER_MS = 30000;

  bool bound_to(const esphome::modbus_controller::ModbusController *controller) const {
    return this->controller_ == controller;
  }
  bool unbound() const { return this->controller_ == nullptr; }

  void bind(esphome::modbus_controller::ModbusController *controller) {
    this->controller_ = controller;
    this->set_parent(controller->hub());
    this->set_address(controller->device_address());
  }

  void start_load(const RuntimeFrequencyTableRefs &refs) {
    if (!this->claim_(refs)) return;
    publish_status(refs, "OPHALEN: tabel wordt gelezen");
    this->queue_read_(Stage::LOAD, RUNTIME_TABLE_START_ADDRESS, RUNTIME_TABLE_REGISTER_COUNT);
  }

  void start_guarded_write(const RuntimeFrequencyTableRefs &refs, const FrequencyCurve &cooling,
                           const FrequencyCurve &heating, bool allow_while_running) {
    if (!this->claim_(refs)) return;
    this->cooling_ = cooling;
    this->heating_ = heating;
    this->allow_while_running_ = allow_while_running;
    publish_status(refs, "CONTROLE: toestand van de unit wordt gelezen");
    this->queue_read_(Stage::GUARD, GUARD_START_ADDRESS, GUARD_REGISTER_COUNT);
  }

 protected:
  void on_read_holding_registers(uint16_t start_address, std::span<const uint16_t> registers,
                                 esphome::modbus::ResponseStatus status) override {
    if (this->stage_ == Stage::IDLE || this->stage_ == Stage::WRITE || start_address != this->expected_start_) return;
    if (!esphome::modbus::succeeded(status)) {
      char reason[24];
      snprintf(reason, sizeof(reason), "foutcode 0x%02X", static_cast<unsigned>(*status));
      this->fail_(reason);
      return;
    }
    switch (this->stage_) {
      case Stage::LOAD:
        this->handle_load_(registers);
        break;
      case Stage::GUARD:
        this->handle_guard_(registers);
        break;
      case Stage::STEP_CHECK:
        this->handle_step_check_(registers);
        break;
      case Stage::READBACK:
        this->handle_readback_(registers);
        break;
      default:
        break;
    }
  }

  // Let op: bij een meervoudige schrijfopdracht geeft de hub de GEVRAAGDE
  // waarden terug, ook bij een foutcode. Het antwoord bewijst alleen dat de unit
  // de opdracht aannam; wat er echt staat zegt pas de teruglezing.
  void on_write_multiple_registers(uint16_t start_address, std::span<const uint16_t> /*registers*/,
                                   esphome::modbus::ResponseStatus status) override {
    if (this->stage_ != Stage::WRITE || start_address != RUNTIME_TABLE_START_ADDRESS) return;
    if (!esphome::modbus::succeeded(status)) {
      char message[64];
      snprintf(message, sizeof(message), "SCHRIJVEN GEWEIGERD: unit gaf foutcode 0x%02X",
               static_cast<unsigned>(*status));
      this->finish_(message);
      return;
    }
    publish_status(this->refs_, "SCHRIJVEN: bevestigd door de unit");
    this->queue_read_(Stage::READBACK, RUNTIME_TABLE_START_ADDRESS, RUNTIME_TABLE_REGISTER_COUNT);
  }

  void on_custom_response(std::span<const uint8_t> /*request_pdu*/, std::span<const uint8_t> /*response_pdu*/,
                          esphome::modbus::ResponseStatus /*status*/) override {
    if (this->stage_ == Stage::IDLE) return;
    if (this->stage_ == Stage::WRITE) {
      this->write_unconfirmed_("onverwacht antwoord");
      return;
    }
    this->fail_("onverwacht antwoord");
  }

  bool on_no_response(std::span<const uint8_t> /*request_pdu*/) override {
    if (this->stage_ == Stage::IDLE) return false;
    if (this->stage_ == Stage::WRITE) {
      // Niet herhalen: misschien is hij wel geland. Teruglezen zegt het.
      this->write_unconfirmed_("geen antwoord");
      return false;
    }
    // Een lezing mag een keer opnieuw, net als max_cmd_retries 1 op de
    // controller. De hub stuurt hem dan zelf nog eens en er volgt opnieuw
    // precies een afsluitende callback.
    if (this->retry_available_) {
      this->retry_available_ = false;
      return true;
    }
    this->fail_("geen antwoord");
    return false;
  }

  void on_not_sent(std::span<const uint8_t> /*request_pdu*/) override {
    if (this->stage_ == Stage::IDLE) return;
    this->fail_("niet verzonden");
  }

 private:
  bool claim_(const RuntimeFrequencyTableRefs &refs) {
    if (this->stage_ != Stage::IDLE) {
      if (esphome::millis() - this->queued_ms_ < STALE_AFTER_MS) {
        publish_status(refs, "BEZIG: vorige opdracht voor deze unit loopt nog");
        return false;
      }
      ESP_LOGW(TAG, "%svorige opdracht kwam nooit af; losgekoppeld", refs.prefix);
      // Vanuit een knop-lambda, dus vanuit de hoofdlus: hier is dit veilig.
      this->clear_tx_queue_for_device();
      this->stage_ = Stage::IDLE;
    }
    this->refs_ = refs;
    return true;
  }

  void queue_read_(Stage stage, uint16_t start, uint16_t count) {
    this->stage_ = stage;
    this->expected_start_ = start;
    this->retry_available_ = true;
    this->queued_ms_ = esphome::millis();
    if (!this->read_holding_registers(start, count)) this->fail_("wachtrij weigert");
  }

  void queue_write_() {
    // De vrijgaveknop gaat er hier uit, niet later: een druk is een
    // schrijfactie, ook als er daarna iets misgaat.
    this->refs_.enable_switch->turn_off();
    publish_status(this->refs_, "SCHRIJVEN: opdracht in de wachtrij");
    this->stage_ = Stage::WRITE;
    this->expected_start_ = RUNTIME_TABLE_START_ADDRESS;
    this->retry_available_ = false;
    this->queued_ms_ = esphome::millis();
    const RuntimeTableWords values = build_runtime_write_values(this->cooling_, this->heating_);
    // 22 registers, dus functie 16 in een transactie.
    if (!this->write_multiple_registers(RUNTIME_TABLE_START_ADDRESS, std::span<const uint16_t>(values))) {
      this->finish_("SCHRIJVEN MISLUKT: wachtrij weigert, er is niets verstuurd");
    }
  }

  void handle_load_(std::span<const uint16_t> registers) {
    FrequencyCurve cooling{};
    FrequencyCurve heating{};
    int loaded = 0;
    char message[64];
    if (!parse_runtime_table(registers, cooling, heating, loaded)) {
      snprintf(message, sizeof(message), "OPHALEN MISLUKT: %d/22 registers gelezen", loaded);
      this->finish_(message);
      return;
    }
    publish_runtime_table(this->refs_, cooling, heating);
    this->remember_(cooling, heating);
    snprintf(message, sizeof(message), "OPGEHAALD: %d/22 registers", loaded);
    this->finish_(message);
  }

  void handle_guard_(std::span<const uint16_t> registers) {
    if (registers.size() < GUARD_REGISTER_COUNT) {
      this->finish_("GEBLOKKEERD: toestand van de unit onvolledig");
      return;
    }
    const uint16_t working_mode = registers[GUARD_WORKING_MODE_INDEX];
    const uint16_t compressor_hz = registers[GUARD_COMPRESSOR_FREQUENCY_INDEX];
    switch (decide_guard(working_mode, compressor_hz, this->allow_while_running_)) {
      case GuardDecision::BLOCK_NOT_STANDBY:
        this->finish_("GEBLOKKEERD: unit staat niet in standby");
        return;
      case GuardDecision::BLOCK_COMPRESSOR_RUNNING:
        this->finish_("GEBLOKKEERD: compressor draait");
        return;
      case GuardDecision::ALLOW_WHILE_RUNNING:
        // Niet blokkeren, wel vastleggen waarin je geschreven hebt. Loopt er
        // daarna iets vreemds, dan staat hier in het log op welke frequentie
        // de compressor draaide toen de tabel onder hem veranderde.
        ESP_LOGW(TAG, "%sschrijven TIJDENS BEDRIJF: werkmodus %u, compressor %u Hz", this->refs_.prefix,
                 static_cast<unsigned>(working_mode), static_cast<unsigned>(compressor_hz));
        break;
      case GuardDecision::ALLOW_STANDSTILL:
        break;
    }
    // Laatste zeef voor het schrijven: de tabel die NU in de unit staat. Kost een
    // extra lezing per schrijfactie; schrijven gebeurt zelden, en dit is de stap
    // die een vergissing tegenhoudt.
    publish_status(this->refs_, "CONTROLE: huidige tabel wordt gelezen");
    this->queue_read_(Stage::STEP_CHECK, RUNTIME_TABLE_START_ADDRESS, RUNTIME_TABLE_REGISTER_COUNT);
  }

  void handle_step_check_(std::span<const uint16_t> registers) {
    FrequencyCurve current_cooling{};
    FrequencyCurve current_heating{};
    int loaded = 0;
    if (!parse_runtime_table(registers, current_cooling, current_heating, loaded)) {
      this->finish_("GEBLOKKEERD: huidige tabel niet leesbaar");
      return;
    }
    this->remember_(current_cooling, current_heating);
    const StepCheck cooling_step = check_step_limit(this->cooling_, current_cooling);
    const StepCheck heating_step = check_step_limit(this->heating_, current_heating);
    if (!cooling_step.ok() || !heating_step.ok()) {
      const bool cooling_blocks = !cooling_step.ok();
      const StepCheck &step = cooling_blocks ? cooling_step : heating_step;
      char message[96];
      snprintf(message, sizeof(message), "GEBLOKKEERD: %s F%u wil %d Hz verschuiven, max %d per keer",
               cooling_blocks ? "koelen" : "verwarmen", static_cast<unsigned>(step.index), step.delta, MAX_STEP_HZ);
      this->finish_(message);
      return;
    }
    this->queue_write_();
  }

  // Leest terug wat er nu werkelijk staat en vergelijkt met wat we bedoelden.
  // Zonder deze stap weet je alleen dat de unit de opdracht heeft aangenomen,
  // niet dat hij hem heeft uitgevoerd.
  void handle_readback_(std::span<const uint16_t> registers) {
    FrequencyCurve cooling{};
    FrequencyCurve heating{};
    int loaded = 0;
    if (!parse_runtime_table(registers, cooling, heating, loaded)) {
      char message[64];
      snprintf(message, sizeof(message), "CONTROLE MISLUKT: %d/22 registers gelezen", loaded);
      this->finish_(message);
      return;
    }
    publish_runtime_table(this->refs_, cooling, heating);
    // Wat er gelezen is, staat in de unit, ook als het afwijkt van de bedoeling.
    this->remember_(cooling, heating);
    if (!tables_match(cooling, this->cooling_) || !tables_match(heating, this->heating_)) {
      this->finish_("CONTROLE MISLUKT: teruglezing wijkt af");
      return;
    }
    // Expliciet in de melding dat dit vluchtig is: de invoervakken houden hun
    // waarde vast over een herstart van de ESP, maar de buitenunit valt bij een
    // power cycle terug op fabriek.
    this->finish_("TOEGEPAST: actief tot de buitenunit spanningsloos is geweest");
  }

  // Schrijfopdracht zonder bevestiging: misschien is hij geland, misschien
  // niet. Niet herhalen maar teruglezen; de teruglezing meldt dan TOEGEPAST of
  // CONTROLE MISLUKT, en dat is de enige uitspraak die telt.
  void write_unconfirmed_(const char *reason) {
    char message[80];
    snprintf(message, sizeof(message), "SCHRIJVEN: %s, tabel wordt teruggelezen", reason);
    publish_status(this->refs_, message);
    this->queue_read_(Stage::READBACK, RUNTIME_TABLE_START_ADDRESS, RUNTIME_TABLE_REGISTER_COUNT);
  }

  void fail_(const char *reason) {
    const char *what = "OPDRACHT MISLUKT";
    switch (this->stage_) {
      case Stage::LOAD:
        what = "OPHALEN MISLUKT";
        break;
      case Stage::GUARD:
        what = "GEBLOKKEERD: toestand van de unit niet leesbaar";
        break;
      case Stage::STEP_CHECK:
        what = "GEBLOKKEERD: huidige tabel niet leesbaar";
        break;
      case Stage::WRITE:
        what = "SCHRIJVEN MISLUKT";
        break;
      case Stage::READBACK:
        what = "CONTROLE MISLUKT";
        break;
      case Stage::IDLE:
        break;
    }
    char message[96];
    snprintf(message, sizeof(message), "%s (%s)", what, reason);
    this->finish_(message);
  }

  void finish_(const char *message) {
    this->stage_ = Stage::IDLE;
    publish_status(this->refs_, message);
  }

  // Werkt de stille momentopname bij (zie RuntimeFrequencySnapshotReader).
  void remember_(const FrequencyCurve &cooling, const FrequencyCurve &heating) {
    if (RuntimeFrequencySnapshotReader *reader = snapshot_reader_for(this->controller_)) {
      reader->store(cooling, heating, esphome::millis());
    }
  }

  esphome::modbus_controller::ModbusController *controller_{nullptr};
  RuntimeFrequencyTableRefs refs_{};
  FrequencyCurve cooling_{};
  FrequencyCurve heating_{};
  Stage stage_{Stage::IDLE};
  uint16_t expected_start_{0};
  uint32_t queued_ms_{0};
  bool allow_while_running_{false};
  bool retry_available_{false};
};

// Een device per controller, dus per HP. Leeft zo lang als het programma: het
// moet er zijn wanneer de hub zijn callback aflevert, ook na de knop-lambda die
// de keten startte.
inline RuntimeFrequencyTableDevice *device_for(const RuntimeFrequencyTableRefs &refs) {
  static std::array<RuntimeFrequencyTableDevice, 2> devices;
  if (refs.controller == nullptr) return nullptr;
  for (auto &device : devices) {
    if (device.bound_to(refs.controller)) return &device;
  }
  for (auto &device : devices) {
    if (device.unbound()) {
      device.bind(refs.controller);
      return &device;
    }
  }
  return nullptr;
}

inline bool read_desired_values(const std::array<esphome::number::Number *, CURVE_POINTS> &entities,
                                FrequencyCurve &values) {
  for (size_t i = 0; i < entities.size(); i++) values[i] = entities[i]->state;
  return validate_monotonic_table(values);
}

// Haalt de tabel op die nu in de unit staat, zodat je bewerkt wat er
// werkelijk is in plaats van wat je denkt dat er is.
inline void load_runtime_table(RuntimeFrequencyTableRefs refs) {
  if (refs.eeprom_dump != nullptr && refs.eeprom_dump->is_active()) {
    publish_status(refs, "GEBLOKKEERD: EEPROM-dump loopt");
    return;
  }
  RuntimeFrequencyTableDevice *device = device_for(refs);
  if (device == nullptr) {
    publish_status(refs, "GEBLOKKEERD: geen Modbus-device beschikbaar");
    return;
  }
  device->start_load(refs);
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

  FrequencyCurve cooling{};
  FrequencyCurve heating{};
  if (!read_desired_values(refs.cooling_desired, cooling)) {
    publish_status(refs, "GEBLOKKEERD: koeltabel ongeldig of niet oplopend");
    return;
  }
  if (!read_desired_values(refs.heating_desired, heating)) {
    publish_status(refs, "GEBLOKKEERD: verwarmingstabel ongeldig of niet oplopend");
    return;
  }

  RuntimeFrequencyTableDevice *device = device_for(refs);
  if (device == nullptr) {
    publish_status(refs, "GEBLOKKEERD: geen Modbus-device beschikbaar");
    return;
  }
  device->start_guarded_write(refs, cooling, heating, allow_while_running);
}

}  // namespace oq_odu_runtime_frequency

#endif  // OPENQUATT_OQ_ODU_RUNTIME_FREQUENCY_TABLE_H
