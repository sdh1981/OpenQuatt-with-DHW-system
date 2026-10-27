#pragma once

// ============================================================================
// OpenQuatt - compressorstanden en hun plafond: uitleg zonder ESPHome
// ============================================================================
//
// Alleen lezen en benoemen. Host-testbaar
// (tests/host/oq_odu_step_ceiling_logic_test.cpp), dus geen ESPHome-headers.
//
// WAAROM DIT BESTAAT
//
// De buitenunits van deze fork zijn R32. Daar klemt de regelprint de
// compressorstand op tien; dat plafond zit in de programmaflash van de STM8 als
// een vaste vergelijking, niet in de EEPROM en niet in de schaduwregisters die
// een Modbus-master kan schrijven. Er is dus GEEN register dat stand 11 t/m 20
// vrijgeeft. Wie dat wil, moet die flash patchen en de unit opnieuw
// programmeren -- buiten het bereik van OpenQuatt.
//
// De frequentieslots die op andere platformen F11 t/m F20 zijn (bladadres
// 3051..3070), dragen op R32 andere parameters. Ze worden hier RAUW getoond en
// nadrukkelijk NIET als frequentie: geen Hz, geen eenheid, geen omrekening.
// Zonder dat label gaat iemand er ooit toch een ladder in lezen.
//
// ADRESSEN
//
// Modbus-adres is bladadres min 1. Diezelfde regel staat bij de ladder zelf in
// oq_odu_runtime_frequency_table_logic.h:
//   koelen    F0-F10  blad 3001..3011 -> modbus 3000..3010
//   verwarmen F0-F10  blad 3012..3022 -> modbus 3011..3021
// Daarmee volgt voor de slots: blad 3051..3070 -> modbus 3050..3069.
//
// De firmwareversie komt uit holding 2122, zoals oq_HP_io.yaml die al leest en
// decodeert (hoge byte major, lage byte minor).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace oq_odu_steps {

// Slots die op R32 geen frequentieladder zijn.
inline constexpr uint16_t SLOT_START_ADDRESS = 3050;
inline constexpr uint16_t SLOT_REGISTER_COUNT = 20;
inline constexpr size_t SLOT_GROUP_SIZE = 10;

// Holding 2122. V1 draait 1.25, V1.5 draait 1.30.
inline constexpr uint16_t FIRMWARE_V1 = 0x0119;
inline constexpr uint16_t FIRMWARE_V1_5 = 0x011E;

// Vast, door de flash bepaald. Geen instelling.
inline constexpr int R32_STEP_CEILING = 10;

// Bewust de twee bekende waarden en niet "major == 1": een onbekende
// R32-firmware hoort eerlijk te melden dat dit niet van toepassing is in plaats
// van stilzwijgend aan te nemen dat het plafond daar ook tien is.
inline bool is_r32_firmware(uint16_t raw_2122) {
  return raw_2122 == FIRMWARE_V1 || raw_2122 == FIRMWARE_V1_5;
}

// 0 = onbekend/niet van toepassing.
inline int step_ceiling(uint16_t raw_2122) { return is_r32_firmware(raw_2122) ? R32_STEP_CEILING : 0; }

inline std::string firmware_text(uint16_t raw_2122) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%u.%u", static_cast<unsigned>((raw_2122 >> 8) & 0xFF),
           static_cast<unsigned>(raw_2122 & 0xFF));
  return std::string(buf);
}

inline std::string unknown_text(uint8_t failures) {
  char buf[32];
  snprintf(buf, sizeof(buf), "onbekend (%u mislukt)", static_cast<unsigned>(failures));
  return std::string(buf);
}

// De ladder zoals de unit hem nu gebruikt, F0 t/m F10. F0 staat erbij omdat
// "uit" ook een stand is en het anders lijkt alsof er een punt ontbreekt.
inline std::string ladder_text(std::span<const uint16_t> hz, bool known, uint32_t age_min, uint8_t failures) {
  if (!known || hz.size() < 11) return unknown_text(failures);
  std::string out;
  for (size_t i = 0; i < 11; ++i) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%s%u", i == 0 ? "" : ",", static_cast<unsigned>(hz[i]));
    out += buf;
  }
  char tail[40];
  snprintf(tail, sizeof(tail), " Hz (F0-F10, %u min oud)", static_cast<unsigned>(age_min));
  out += tail;
  return out;
}

// Tien rauwe registerwaarden, decimaal. GEEN eenheid en nadrukkelijk geen Hz:
// op R32 zijn dit geen frequenties. De host-test bewaakt dat.
inline std::string slot_text(std::span<const uint16_t> words, size_t first, bool applicable, bool known,
                             uint8_t failures) {
  if (!applicable) return std::string("niet van toepassing");
  if (!known || words.size() < first + SLOT_GROUP_SIZE) return unknown_text(failures);
  std::string out;
  for (size_t i = 0; i < SLOT_GROUP_SIZE; ++i) {
    char buf[10];
    snprintf(buf, sizeof(buf), "%s%u", i == 0 ? "" : ",", static_cast<unsigned>(words[first + i]));
    out += buf;
  }
  return out;
}

// Eén regel die zegt waar je aan toe bent.
inline std::string status_text(bool firmware_known, uint16_t raw_2122, bool slots_known, uint8_t failures) {
  if (!firmware_known) return std::string("wacht op firmwareversie");
  const std::string version = firmware_text(raw_2122);
  if (!is_r32_firmware(raw_2122)) return "niet van toepassing: firmware " + version;

  std::string out = "R32 fw " + version + " - plafond 10 standen (firmware, niet instelbaar)";
  if (!slots_known) {
    if (failures > 0) {
      char buf[32];
      snprintf(buf, sizeof(buf), "; slots lezen mislukt (%u)", static_cast<unsigned>(failures));
      out += buf;
    } else {
      out += "; slots nog niet gelezen";
    }
  }
  return out;
}

}  // namespace oq_odu_steps
