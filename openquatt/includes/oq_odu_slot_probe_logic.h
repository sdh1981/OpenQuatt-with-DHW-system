#pragma once

// ============================================================================
// OpenQuatt - sonde op de slots 3051-3070: rekenwerk zonder ESPHome
// ============================================================================
//
// Host-testbaar (tests/host/oq_odu_slot_probe_logic_test.cpp). Het Modbus-
// verkeer zit in oq_odu_slot_probe.h.
//
// WAT DIT IS
//
// Geen editor voor een F11-F20-ladder. Op R32 klemt de regelprint de
// compressorstand op tien, en die grens staat in de programmaflash -- geen
// enkele schrijfactie in de schaduw verandert daar iets aan. Zie
// oq_odu_step_ceiling_logic.h.
//
// Dit is een sonde: per register uitzoeken OF de unit er een waarde in
// aanneemt en vasthoudt. Die vraag is open, want op deze firmware lezen alle
// twintig slots nul, en dat kan "leeg" betekenen of "deze unit antwoordt overal
// met nul".
//
// WAAROM ZO VOORZICHTIG
//
// Op R32 dragen deze bytes andere parameters dan de frequentieladder die ze op
// andere platformen zijn. We weten niet welke. Een schrijfactie in dit blok
// werkt bovendien meteen door: het is de schaduw waar de regeling uit leest.
// Vandaar: een register tegelijk, compressor uit, kleine waarden, en na twee
// minuten gaat de oorspronkelijke waarde er vanzelf weer in.
//
// WAT HIER NOOIT IN KOMT
//
// Register 5000. Dat is het vastlegcommando richting EEPROM. Deze sonde schrijft
// uitsluitend in de vluchtige schaduw, zodat een power cycle van de buitenunit
// altijd alles terugzet. Laat dat zo.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace oq_odu_slot_probe {

// Bladadressen van de slots. Modbus-adres is bladadres min 1, dezelfde regel als
// in oq_odu_runtime_frequency_table_logic.h.
inline constexpr uint16_t FIRST_SHEET_ADDRESS = 3051;
inline constexpr uint16_t LAST_SHEET_ADDRESS = 3070;

// Onbekende parameters: klein houden. Een typefout van een orde van grootte kan
// hier niet opgevangen worden met "dat is geen geldige frequentie", want we
// weten niet wat een geldige waarde is.
inline constexpr uint16_t MAX_VALUE = 100;

// Na deze tijd gaat de oorspronkelijke waarde er vanzelf weer in. Twee minuten
// is lang genoeg om een teruglezing te doen en kort genoeg om een parameter die
// we niet kennen niet te laten staan.
inline constexpr uint32_t RESTORE_AFTER_MS = 120000UL;

// Verboden adres: het vastlegcommando naar EEPROM. Staat hier expliciet zodat
// het niet per ongeluk in het bereik van de sonde kan belanden.
inline constexpr uint16_t FORBIDDEN_PERSIST_SHEET_ADDRESS = 5000;

inline bool valid_sheet_address(uint32_t sheet_address) {
  if (sheet_address == FORBIDDEN_PERSIST_SHEET_ADDRESS) return false;
  return sheet_address >= FIRST_SHEET_ADDRESS && sheet_address <= LAST_SHEET_ADDRESS;
}

inline bool valid_value(uint32_t value) { return value <= MAX_VALUE; }

// Blad -> modbus. Alleen aanroepen voor een adres dat valid_sheet_address
// goedkeurde; anders 0, en dat adres schrijft de sonde nooit.
inline uint16_t modbus_address(uint32_t sheet_address) {
  return valid_sheet_address(sheet_address) ? static_cast<uint16_t>(sheet_address - 1) : 0;
}

// Boekhouding van één proef: wat er stond, wat erin ging, wanneer.
struct ProbeState {
  bool restore_pending{false};
  uint16_t sheet_address{0};
  uint16_t original{0};
  uint16_t written{0};
  uint32_t written_ms{0};
};

inline bool restore_due(const ProbeState &state, uint32_t now_ms) {
  if (!state.restore_pending) return false;
  return static_cast<uint32_t>(now_ms - state.written_ms) >= RESTORE_AFTER_MS;
}

inline std::string refused_text(const char *reason) { return std::string("GEWEIGERD: ") + reason; }

// "3055: was 0, geschreven 1, teruggelezen 1"
inline std::string result_text(uint16_t sheet_address, uint16_t original, uint16_t written, uint16_t readback) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%u: was %u, geschreven %u, teruggelezen %u", static_cast<unsigned>(sheet_address),
           static_cast<unsigned>(original), static_cast<unsigned>(written), static_cast<unsigned>(readback));
  return std::string(buf);
}

// De uitkomst in woorden, want dat is waar de proef om draait.
inline std::string verdict_text(uint16_t written, uint16_t readback) {
  if (readback == written) return std::string(" - vastgehouden");
  if (readback == 0 && written != 0) return std::string(" - genegeerd");
  return std::string(" - afwijkend");
}

inline std::string restored_text(uint16_t sheet_address, uint16_t original, uint16_t readback) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%u: hersteld naar %u, teruggelezen %u", static_cast<unsigned>(sheet_address),
           static_cast<unsigned>(original), static_cast<unsigned>(readback));
  return std::string(buf);
}

}  // namespace oq_odu_slot_probe
