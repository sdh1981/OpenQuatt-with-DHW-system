#pragma once

// ============================================================================
// OpenQuatt - Power House v0.50.0: aansluiting op deze fork (rekenwerk)
// ============================================================================
//
// De upstream-logica in v050/ blijft ongewijzigd. Alles wat deze fork anders
// doet, zit hier, zonder ESPHome-afhankelijkheden, zodat het host-testbaar is
// (tests/host/oq_power_house_v050_adapter_test.cpp).
//
// Wat hier aansluit:
//   - Frequentiecontext voor V1.5: stand -> Hz uit de ODU-tabel die de fork
//     uitleest, en toegestaan = de fork-eigen standregels (Day/Silent max level,
//     Excluded level A/B). Geen nieuwe Hz-instellingen.
//   - Kandidaattoestand zonder incident manager: uit online-status, low-flow-
//     en watertemperatuur-trip, opstartblokkade, minimale uit-tijd en de
//     startlimiet (6/uur).
//   - De v0.32-uitbreidingen van de fork, zo dat ze met dezelfde formules
//     meerekenen als in oq_power_house_strategy.yaml:
//       #2 zon/interne winst   -> feedforward min correctie
//       #3 voorverwarmen       -> ondergrens op het ruwe vermogen
//       smart #1 tarief        -> na de begrenzing opgeteld
//       smart #2 PV-boost      -> na de begrenzing opgeteld
//       smart #3 raam open     -> vermogen 0
//       #4 vorstzone-derating  -> thermisch vermogen per kandidaat
//       #5 effectieve aanvoer  -> aanvoertemperatuur voor het prestatiemodel
// ============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "v050/control/oq_hp_candidate_logic.h"
#include "v050/control/oq_power_house_demand_logic.h"
#include "v050/odu/oq_odu_variant.h"

namespace oq_ph_v050 {

inline constexpr int kLevels = 10;

inline float clampf(float value, float low, float high) { return value < low ? low : (value > high ? high : value); }

// ---------------------------------------------------------------------------
// Frequentiecontext (V1.5)
// ---------------------------------------------------------------------------

// Verwarmingskromme F0..F10 zoals de ODU hem nu gebruikt. F0 is uit.
struct HeatingTable {
  std::array<uint16_t, kLevels + 1> hz{};
  bool known{false};
};

struct FrequencyContext {
  HeatingTable hp1{};
  HeatingTable hp2{};
  int level_cap{kLevels};          // Day max level, of Silent max level in het stille venster
  uint16_t hp1_excluded_mask{0};   // bit L gezet = stand L uitgesloten
  uint16_t hp2_excluded_mask{0};

  int automatic_frequency_hz(bool is_hp1, int /*mode_code*/, int level) const {
    const HeatingTable& table = is_hp1 ? hp1 : hp2;
    if (!table.known || level < 1 || level > kLevels) return -1;
    const int hz = table.hz[static_cast<size_t>(level)];
    return hz > 0 ? hz : -1;
  }

  bool frequency_allowed(bool is_hp1, int /*mode_code*/, int level) const {
    if (level < 1 || level > kLevels || level > level_cap) return false;
    const uint16_t mask = is_hp1 ? hp1_excluded_mask : hp2_excluded_mask;
    return (mask & (1U << level)) == 0U;
  }

  oq_odu::Variant performance_variant(bool /*is_hp1*/) const { return oq_odu::Variant::V1_5; }
};

// ---------------------------------------------------------------------------
// Kandidaattoestand (vervangt de incident manager van upstream)
// ---------------------------------------------------------------------------

struct CandidateInputs {
  int applied_level{0};
  bool online{false};
  bool must_stop{false};          // low-flow-fout of watertemperatuur-trip
  bool startup_inhibit{false};    // opstartblokkade na een herstart van de controller
  uint32_t now_ms{0};
  uint32_t last_stop_ms{0};
  uint32_t minimum_off_ms{0};
  uint32_t start_limit_remaining_ms{0};  // startlimiet 6/uur (oq_compressor_start_limit.h)
};

inline oq_hp_candidate::HpCandidateState candidate_state(const CandidateInputs& in) {
  oq_hp_candidate::HpCandidateState state;
  state.previous_applied_level = in.applied_level > 0 ? in.applied_level : 0;
  state.available_for_start = in.online && !in.startup_inhibit;
  state.must_stop = in.must_stop;
  // Upstream: een korte linkstoring mag geen nieuwe start of eigenaarwissel
  // veroorzaken. Hier: Modbus offline.
  state.link_suspect = !in.online;
  // Een draaiende unit is altijd "klaar". Een stilstaande pas na de minimale
  // uit-tijd EN als de startlimiet een nieuwe start toelaat; anders kiest de
  // dispatch een HP die de actuator vervolgens tegenhoudt.
  state.minimum_off_ready = oq_hp_candidate::minimum_off_ready(in.now_ms, in.last_stop_ms, in.minimum_off_ms,
                                                               state.previous_applied_level) &&
                            (state.previous_applied_level > 0 || in.start_limit_remaining_ms == 0U);
  return state;
}

// ---------------------------------------------------------------------------
// v0.32 #5 effectieve aanvoertemperatuur, #4 vorstzone-derating
// ---------------------------------------------------------------------------

// Zelfde formule als eff_supply_temp_c in oq_power_house_strategy.yaml:
// 30 °C in rust, 55 °C bij nominaal vermogen; nooit lager dan gemeten.
inline float effective_supply_temp_c(float requested_w, float rated_w, float measured_supply_c) {
  float steady_c = 35.0f;
  if (!std::isnan(rated_w) && rated_w > 0.0f) {
    float fraction = requested_w / rated_w;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    steady_c = 30.0f + 25.0f * fraction;
  }
  if (std::isnan(measured_supply_c)) return steady_c;
  return std::fmax(measured_supply_c, steady_c);
}

// Zelfde formule als frost_zone_derate: klokvorm rond +1,5 °C, alleen tussen
// -10 en +6 °C, nooit meer dan 30 % minder. Een ongeldige instelling telt als 0
// (geen derating); de YAML gaf daar per ongeluk de maximale 30 %.
inline float frost_zone_derate(float outside_c, float max_derate_setting) {
  if (std::isnan(outside_c)) return 1.0f;
  if (outside_c > 6.0f || outside_c < -10.0f) return 1.0f;
  if (!std::isfinite(max_derate_setting)) return 1.0f;
  const float max_derate = clampf(max_derate_setting, 0.0f, 0.30f);
  if (max_derate <= 0.0f) return 1.0f;
  const float dx = (outside_c - 1.5f) / 3.5f;
  return std::fmax(0.70f, 1.0f - max_derate * std::exp(-dx * dx));
}

// Tijdens echte defrost (4-wegklep) de defrostfactor, anders de vorstzone.
inline float thermal_factor(bool valve_defrost, float defrost_factor, float frost_derate) {
  return valve_defrost ? defrost_factor : frost_derate;
}

// ---------------------------------------------------------------------------
// v0.32-uitbreidingen op de vraag
// ---------------------------------------------------------------------------

// Stateloze voorverwarming (#3). Zelfde formule als de YAML.
inline float preheat_floor_w(float gain_setting, float setpoint_c, float setpoint_next_c, float outside_c,
                             float outside_forecast_c, float zero_power_c, float cold_c, float rated_w) {
  if (!std::isfinite(gain_setting)) return 0.0f;
  const float gain = clampf(gain_setting, 0.0f, 1.0f);
  if (gain <= 0.0f) return 0.0f;
  if (std::isnan(setpoint_next_c) || !(setpoint_next_c > setpoint_c + 0.3f)) return 0.0f;
  const float effective_outside_c = std::isnan(outside_forecast_c) ? outside_c : outside_forecast_c;
  const float x = (zero_power_c - effective_outside_c) / (zero_power_c - cold_c);
  if (!std::isfinite(x) || !std::isfinite(rated_w)) return 0.0f;
  return gain * 0.4f * rated_w * clampf(x, 0.0f, 1.0f);
}

struct DemandExtensions {
  float solar_correction_w{0.0f};  // ph_solar_gain_alpha * oq_phouse_solar_gain_w
  float preheat_floor_w{0.0f};     // preheat_floor_w(...)
  float tariff_w{0.0f};            // ph_kp_w_per_k * oq_phouse_tariff_shift_c, begrensd op +-nominaal
  float pv_boost_w{0.0f};          // oq_phouse_pv_boost_w
  bool window_open{false};         // oq_phouse_window_open_active
};

inline float finite_or_zero(float value) { return std::isfinite(value) ? value : 0.0f; }

// De upstream-vraagberekening, met de v0.32-uitbreidingen op dezelfde plek als
// in de fork. Upstream's decide_demand blijft ongewijzigd; de aanhaakpunten zijn
// de feedforward (zon), een tweede aanroep met Kp = 0 als de voorverwarming de
// ondergrens zet, en de nabewerking (tarief, PV, raam).
inline oq_power_house::DemandDecision decide_demand_with_extensions(const oq_power_house::DemandInput& base_input,
                                                                     const oq_power_house::DemandTuning& tuning,
                                                                     const oq_power_house::DemandState& state,
                                                                     const DemandExtensions& ext) {
  using namespace oq_power_house;
  DemandInput input = base_input;
  const float modelled_w = modelled_house_power_w(input.zero_power_c, input.cold_c, input.outside_c, input.rated_w);
  if (!std::isfinite(modelled_w)) {
    input.external_valid = false;
    return decide_demand(input, tuning, state);
  }

  // #2 zon/interne winst: P_house = max(0, P_house - correctie).
  const float feedforward_w = std::max(0.0f, modelled_w - finite_or_zero(ext.solar_correction_w));
  input.external_w = feedforward_w;
  input.external_valid = true;
  DemandDecision out = decide_demand(input, tuning, state);
  if (!out.valid) return out;

  // #3 voorverwarmen: P_raw = max(P_house + Kp*e, P_preheat). decide_demand
  // geeft P_raw niet terug, dus die hier opnieuw uitrekenen met de fout uit het
  // zojuist bijgewerkte comfortgeheugen. Zet de voorverwarming de ondergrens,
  // dan nog een keer met feedforward = P_preheat en Kp = 0: dan is P_raw exact
  // P_preheat, en comfortgeheugen en begrenzer lopen identiek.
  const float preheat_w = finite_or_zero(ext.preheat_floor_w);
  if (preheat_w > 0.0f) {
    const float below_c = clampf(tuning.comfort_below_c, 0.0f, 2.0f);
    const float low_c = input.setpoint_c + out.next.comfort_memory_c - below_c;
    float error_c = 0.0f;
    if (input.room_c < low_c)
      error_c = low_c - input.room_c;
    else if (input.room_c > input.setpoint_c)
      error_c = input.setpoint_c - input.room_c;
    const float raw_w = clampf(feedforward_w + tuning.reaction_w_per_k * error_c, 0.0f, input.rated_w);
    if (preheat_w > raw_w) {
      DemandTuning floor_tuning = tuning;
      floor_tuning.reaction_w_per_k = 0.0f;
      input.external_w = preheat_w;
      out = decide_demand(input, floor_tuning, state);
      if (!out.valid) return out;
    }
  }

  // smart #1 tarief en #2 PV: na de begrenzer, tot 1,2x nominaal. smart #3 raam: 0.
  const float limited_w = out.requested_w;
  float requested_w = limited_w + finite_or_zero(ext.tariff_w) + finite_or_zero(ext.pv_boost_w);
  requested_w = clampf(requested_w, 0.0f, input.rated_w * 1.20f);
  if (ext.window_open) requested_w = 0.0f;

  out.requested_w = requested_w;
  // De begrenzer onthoudt het vermogen VOOR tarief en PV. Onthoudt hij het erna,
  // dan telt de boost elke cyclus opnieuw mee: zodra de boost groter is dan wat
  // de begrenzer per cyclus laat zakken, loopt het vermogen op tot 1,2x nominaal,
  // los van de warmtevraag. Op 60 s gebeurt dat bij profiel Calm (1404 W/min) al
  // met de standaard-tariefgrens; op dit ritme van 10 s bij elke boost boven
  // 390 W. De fork-YAML deed dat eerst wel en is meeverbeterd.
  // Raam open: 0, zodat het vermogen daarna weer rustig opbouwt (zoals de YAML).
  out.next.last_w = ext.window_open ? 0.0f : limited_w;
  out.raw_demand = static_cast<int>(std::lround(tuning.demand_max * (requested_w / input.rated_w)));
  if (out.raw_demand < 0) out.raw_demand = 0;
  if (out.raw_demand > tuning.demand_max) out.raw_demand = tuning.demand_max;
  out.external = false;  // "extern" betekent upstream een HA/API-vermogen; dat is dit niet
  return out;
}

}  // namespace oq_ph_v050
