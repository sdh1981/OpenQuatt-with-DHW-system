#pragma once

// ============================================================================
// OpenQuatt - frequentietabel: rekenwerk zonder ESPHome
// ============================================================================
//
// De beslissingen achter "tabel toepassen", losgetrokken uit
// oq_odu_runtime_frequency_table.h zodat ze host-testbaar zijn
// (tests/host/oq_odu_runtime_frequency_table_logic_test.cpp) en een ESPHome-
// upgrade niet raken. Alleen <array>/<cmath>/<cstddef>/<cstdint>/<span>.
//
// Hier staat wat een vergissing moet tegenhouden: geldige Hz, oplopende kromme,
// hooguit MAX_STEP_HZ per stand tegen wat er NU in de unit staat, en schrijven
// alleen in stilstand tenzij expliciet vrijgegeven. Het Modbus-verkeer zelf zit
// in de andere header.
// ============================================================================

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace oq_odu_runtime_frequency {

// Modbus-adres, dus bladadres min 1. 22 registers = twee krommen van elf.
//   koelen    F0-F10  blad 3001..3011 -> modbus 3000..3010
//   verwarmen F0-F10  blad 3012..3022 -> modbus 3011..3021
static constexpr uint16_t RUNTIME_TABLE_START_ADDRESS = 3000;
static constexpr size_t CURVE_POINTS = 11;
static constexpr uint16_t RUNTIME_TABLE_REGISTER_COUNT = 2 * CURVE_POINTS;

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
static constexpr int MAX_STEP_HZ = 5;

using FrequencyCurve = std::array<float, CURVE_POINTS>;
using RuntimeTableWords = std::array<uint16_t, RUNTIME_TABLE_REGISTER_COUNT>;

inline bool valid_frequency(float value) {
  return !std::isnan(value) && value >= MIN_FREQUENCY_HZ && value <= MAX_FREQUENCY_HZ;
}

// Een kromme moet oplopend zijn: stand N mag nooit trager draaien dan stand N-1.
// Een dalende tabel zou de niveaulogica van de unit betekenisloos maken.
inline bool validate_monotonic_table(const FrequencyCurve &values) {
  for (size_t i = 0; i < values.size(); i++) {
    if (!valid_frequency(values[i])) return false;
    if (i > 0 && values[i] < values[i - 1]) return false;
  }
  return true;
}

// `loaded` telt hoeveel standen geldig waren voordat het misging, voor de
// statusmelding ("OPHALEN MISLUKT: 7/22 registers gelezen").
inline bool parse_runtime_table(std::span<const uint16_t> words, FrequencyCurve &cooling, FrequencyCurve &heating,
                                int &loaded) {
  loaded = 0;
  for (size_t i = 0; i < static_cast<size_t>(RUNTIME_TABLE_REGISTER_COUNT); i++) {
    if (i >= words.size()) return false;
    const float value = static_cast<float>(words[i]);
    if (!valid_frequency(value)) return false;
    (i < CURVE_POINTS ? cooling[i] : heating[i - CURVE_POINTS]) = value;
    loaded++;
  }
  return true;
}

inline bool tables_match(const FrequencyCurve &actual, const FrequencyCurve &expected) {
  for (size_t i = 0; i < actual.size(); i++) {
    if (std::lround(actual[i]) != std::lround(expected[i])) return false;
  }
  return true;
}

inline RuntimeTableWords build_runtime_write_values(const FrequencyCurve &cooling, const FrequencyCurve &heating) {
  RuntimeTableWords values{};
  for (size_t i = 0; i < CURVE_POINTS; i++) {
    values[i] = static_cast<uint16_t>(std::lround(cooling[i]));
    values[CURVE_POINTS + i] = static_cast<uint16_t>(std::lround(heating[i]));
  }
  return values;
}

// Eerste stand die verder wil schuiven dan MAX_STEP_HZ. index == CURVE_POINTS
// betekent: alles binnen de grens.
struct StepCheck {
  size_t index;
  int delta;
  bool ok() const { return this->index == CURVE_POINTS; }
};

inline StepCheck check_step_limit(const FrequencyCurve &wanted, const FrequencyCurve &current) {
  for (size_t i = 0; i < CURVE_POINTS; i++) {
    const int delta = static_cast<int>(std::lround(wanted[i])) - static_cast<int>(std::lround(current[i]));
    if ((delta < 0 ? -delta : delta) > MAX_STEP_HZ) return {i, delta};
  }
  return {CURVE_POINTS, 0};
}

// Schrijven mag alleen in stilstand: werkmodus 0 en compressorfrequentie 0.
// Met allow_while_running wordt de toestand nog steeds bekeken -- de log moet
// laten zien waarin geschreven is -- maar niet meer geweigerd.
enum class GuardDecision : uint8_t {
  ALLOW_STANDSTILL,
  ALLOW_WHILE_RUNNING,
  BLOCK_NOT_STANDBY,
  BLOCK_COMPRESSOR_RUNNING,
};

inline GuardDecision decide_guard(uint16_t working_mode, uint16_t compressor_hz, bool allow_while_running) {
  const bool running = working_mode != 0 || compressor_hz > 0;
  if (!running) return GuardDecision::ALLOW_STANDSTILL;
  if (allow_while_running) return GuardDecision::ALLOW_WHILE_RUNNING;
  return working_mode != 0 ? GuardDecision::BLOCK_NOT_STANDBY : GuardDecision::BLOCK_COMPRESSOR_RUNNING;
}

}  // namespace oq_odu_runtime_frequency
