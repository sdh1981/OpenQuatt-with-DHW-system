#pragma once

// ============================================================================
// OpenQuatt - halve compressorstanden: het rekenwerk, zonder ESPHome
// ============================================================================
//
// De buitenunit kent tien standen. Wat elke stand aan hertz betekent, staat in
// een tabel die de unit tijdens bedrijf uit zijn eigen werkgeheugen leest; die
// tabel is over Modbus te herschrijven (zie oq_odu_runtime_frequency_table.h).
// Door tussen twee krommen te wisselen ontstaan halve stappen: kromme A is de
// fabriekskromme, kromme B ligt er per stand een halve stap boven.
//
// Deze header bevat alleen de beslissingen, zodat ze host-testbaar zijn
// (tests/host/oq_odu_halfstep_levels_logic_test.cpp). Het Modbus-verkeer en de
// ESPHome-koppeling zitten elders. In fase 1 wordt er niets geschreven: de
// uitkomst van evaluate() wordt alleen getoond, zodat we kunnen zien hoe vaak
// er gewisseld zou worden voordat er iets naar een unit gaat.
//
// Alleen <algorithm>/<array>/<cmath>/<cstddef>/<cstdint>.
// ============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace oq_odu_halfstep {

static constexpr size_t CURVE_POINTS = 11;   // F0 t/m F10
using FrequencyCurve = std::array<float, CURVE_POINTS>;

// Halve standen: 0 is uit, daarna 1..19 oplopend. Oneven = kromme B.
static constexpr int MAX_HALF_LEVEL = 19;

// Grenzen die we onszelf opleggen. MAX_SHIFT_HZ is gelijk aan de MAX_STEP_HZ
// van de bestaande schrijfbewaking: zo blijft die ongewijzigd gelden.
static constexpr float MIN_RUNNING_HZ = 30.0f;
static constexpr float MAX_RUNNING_HZ = 90.0f;
static constexpr int MAX_SHIFT_HZ = 5;

// Ritme. Waarom deze waarden: het oververhittingsdoel van de unit loopt na een
// start in 6 minuten naar zijn eindwaarde en na een ontdooiing in ongeveer 5.
// Binnen die vensters is de oververhitting geen maat voor de regeling, dus daar
// wisselen we niet.
static constexpr uint32_t MIN_SWITCH_INTERVAL_MS = 90UL * 1000UL;
static constexpr uint32_t REQUEST_STABLE_MS = 60UL * 1000UL;
static constexpr uint32_t AFTER_START_BLOCK_MS = 6UL * 60UL * 1000UL;
static constexpr uint32_t AFTER_DEFROST_BLOCK_MS = 5UL * 60UL * 1000UL;

enum class Curve : uint8_t { A = 0, B = 1 };

// Waarom een wisseling niet mag. Wordt als tekst getoond, dus ook de reden dat
// er niets gebeurt is af te lezen.
enum class Block : uint8_t {
  NONE = 0,
  MODE_NOT_HEATING,
  DEFROSTING,
  OIL_RETURN,
  AFTER_START,
  AFTER_DEFROST,
  FREQUENCY_LIMITED,
  TOO_SOON,
  NOT_STABLE,
  CURVE_UNUSABLE,
};

struct Inputs {
  uint32_t now_ms;
  uint16_t working_mode;        // 2 = verwarmen, 3 = tapwater
  bool defrosting;
  bool oil_return;
  bool frequency_limited;       // een beveiliging begrenst de stand al
  uint32_t last_start_ms;       // laatste compressorstart
  uint32_t last_defrost_end_ms; // 0 = nog geen ontdooiing gezien
  int requested_half_level;     // 0..MAX_HALF_LEVEL
  uint32_t request_since_ms;    // sinds wanneer die aanvraag ongewijzigd is
  Curve active_curve;           // welke kromme nu in de unit staat
  uint32_t last_switch_ms;      // laatste krommewisseling, 0 = nog nooit
  bool curve_b_usable;          // kromme B is te berekenen uit kromme A
};

struct Decision {
  int level;          // 0..10, wat er naar register 1999 zou gaan
  Curve curve;        // welke kromme daarbij hoort
  bool switch_needed; // curve wijkt af van de kromme die nu in de unit staat
  bool may_switch;    // en het mag ook
  Block block;        // zo niet: waarom niet
};

// Halve stand -> stand plus kromme. Half 0 is uit; daarna is half 2n-1 stand n
// op kromme A en half 2n stand n op kromme B.
inline int level_of_half(int half) {
  if (half <= 0) return 0;
  return (half + 1) / 2;
}

inline Curve curve_of_half(int half) {
  if (half <= 0) return Curve::A;
  return (half % 2 == 0) ? Curve::B : Curve::A;
}

inline int clamp_half_level(int half) {
  return std::max(0, std::min(MAX_HALF_LEVEL, half));
}

// Kromme B uit kromme A: per stand de helft van het gat naar de volgende stand,
// naar beneden afgerond, met de bovengrens van MAX_SHIFT_HZ. F0 en F10 blijven
// staan: F0 is uit en F10 is tevens de bovengrens van de regeling in de unit.
inline bool build_curve_b(const FrequencyCurve &a, FrequencyCurve &b) {
  b = a;
  if (!(a[0] == 0.0f)) return false;
  for (size_t i = 1; i + 1 < CURVE_POINTS; i++) {
    if (std::isnan(a[i]) || std::isnan(a[i + 1])) return false;
    if (a[i] < MIN_RUNNING_HZ || a[i + 1] > MAX_RUNNING_HZ) return false;
    if (a[i + 1] <= a[i]) return false;
    const float gap = a[i + 1] - a[i];
    float shift = std::floor(gap / 2.0f);
    if (shift > (float) MAX_SHIFT_HZ) shift = (float) MAX_SHIFT_HZ;
    if (shift < 1.0f) return false;  // geen zinnige halve stap te maken
    b[i] = a[i] + shift;
    if (b[i] >= a[i + 1]) return false;  // moet onder de volgende stand blijven
  }
  return true;
}

// De ladder die dit oplevert, oplopend: A1, B1, A2, B2, ... A10.
inline float frequency_of_half(const FrequencyCurve &a, const FrequencyCurve &b, int half) {
  const int level = level_of_half(half);
  if (level <= 0) return 0.0f;
  return (curve_of_half(half) == Curve::B) ? b[level] : a[level];
}

inline bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t window_ms) {
  if (since_ms == 0) return true;  // nooit gebeurd telt als lang geleden
  return (uint32_t) (now_ms - since_ms) >= window_ms;
}

inline Decision evaluate(const Inputs &in) {
  Decision out{};
  const int half = clamp_half_level(in.requested_half_level);
  out.level = level_of_half(half);
  out.curve = curve_of_half(half);
  out.switch_needed = (out.curve != in.active_curve);
  out.block = Block::NONE;

  if (!out.switch_needed) {
    out.may_switch = false;
    return out;
  }

  // Volgorde is bewust: eerst wat de unit doet, dan onze eigen regels.
  if (in.working_mode != 2 && in.working_mode != 3) out.block = Block::MODE_NOT_HEATING;
  else if (in.defrosting) out.block = Block::DEFROSTING;
  else if (in.oil_return) out.block = Block::OIL_RETURN;
  else if (!elapsed(in.now_ms, in.last_start_ms, AFTER_START_BLOCK_MS)) out.block = Block::AFTER_START;
  else if (!elapsed(in.now_ms, in.last_defrost_end_ms, AFTER_DEFROST_BLOCK_MS)) out.block = Block::AFTER_DEFROST;
  else if (in.frequency_limited) out.block = Block::FREQUENCY_LIMITED;
  else if (!elapsed(in.now_ms, in.last_switch_ms, MIN_SWITCH_INTERVAL_MS)) out.block = Block::TOO_SOON;
  else if (!elapsed(in.now_ms, in.request_since_ms, REQUEST_STABLE_MS)) out.block = Block::NOT_STABLE;
  else if (!in.curve_b_usable) out.block = Block::CURVE_UNUSABLE;

  out.may_switch = (out.block == Block::NONE);
  return out;
}

inline const char *block_text(Block b) {
  switch (b) {
    case Block::NONE: return "vrij";
    case Block::MODE_NOT_HEATING: return "niet in verwarmen of tapwater";
    case Block::DEFROSTING: return "ontdooien";
    case Block::OIL_RETURN: return "olieterugvoer";
    case Block::AFTER_START: return "binnen 6 min na start";
    case Block::AFTER_DEFROST: return "binnen 5 min na ontdooien";
    case Block::FREQUENCY_LIMITED: return "beveiliging begrenst de stand";
    case Block::TOO_SOON: return "te kort na de vorige wisseling";
    case Block::NOT_STABLE: return "aanvraag nog niet stabiel";
    case Block::CURVE_UNUSABLE: return "kromme B niet bruikbaar";
  }
  return "onbekend";
}

inline const char *curve_text(Curve c) { return c == Curve::B ? "B" : "A"; }

}  // namespace oq_odu_halfstep
