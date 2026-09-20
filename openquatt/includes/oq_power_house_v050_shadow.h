#pragma once

// ============================================================================
// OpenQuatt - Power House v0.50.0 in SCHADUW
// ============================================================================
//
// Rekent upstream's Power House (v050/) naast de huidige Power House van deze
// fork mee, met dezelfde invoer en de v0.32-uitbreidingen, en publiceert wat hij
// zou kiezen. Hij STUURT NIETS AAN:
//   - schrijft geen oq_ph_request_*, oq_demand_*, oq_phouse_*, oq_P_*,
//     oq_last_lead_hp of oq_duo_optimizer_reason;
//   - houdt zijn eigen vraag-, snelle-start- en dispatchtoestand bij;
//   - leest de toestand van de v0.32-uitbreidingen (zon-EMA, tariefverschuiving,
//     PV-boost, raam open) zoals de huidige Power House die bijwerkt.
//
// Volgt upstream's oq_power_house_runtime.h (v0.50.0) stap voor stap. Waar de
// fork anders is, staat dat erbij; het rekenwerk daarvan zit in
// oq_power_house_v050_adapter.h en is host-getest.
//
// Verschillen met de huidige fork-Power House die je in de schaduw zult zien:
//   - rekenritme 10 s in plaats van 60 s;
//   - geen extra stappenfilter op de vraag (Demand filter ramp up);
//   - prestatiemodel op de werkelijke Hz uit de ODU-tabel;
//   - een HP in zijn minimale uit-tijd of startlimiet wordt niet gekozen;
//   - snelle eerste start op de laagste haalbare stand.
// ============================================================================

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "oq_compressor_start_limit.h"
#include "oq_odu_runtime_frequency_table.h"
#include "oq_power_house_v050_adapter.h"
#include "oq_rolling_hour_counter.h"
#include "oq_thermal_request_logic.h"
#include "v050/control/oq_heat_intent_logic.h"
#include "v050/control/oq_power_house_dispatch_logic.h"
#include "v050/performance/hp_perf_frequency.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_ph_v050 {

struct TickConfig {
  uint32_t loop_ms;
  uint32_t minimum_off_ms;
  int demand_max_f;
  float temperature_guard_c;
  float defrost_power_factor;
  float soft_limit_w;
  float peak_limit_w;
  float optimizer_penalty_per_w;
  float topology_power_margin_w;
  float topology_heat_advantage_w;
  int defrost_comp_min_f;
  int defrost_comp_boost_steps;
};

struct Output {
  bool active{false};
  bool output_valid{false};
  int hp1_level{0};
  int hp2_level{0};
  int owner_hp{0};
  int reason{0};
  int demand{0};
  float requested_w{NAN};
  float expected_w{NAN};
  float capacity_w{NAN};
  float model_supply_c{NAN};
  float frost_derate{1.0f};
  int intent{0};
  // intent is alleen gevuld bij een snelle start. intent_reason staat er altijd,
  // dus ook tijdens room_recovery -- en juist die toestand houdt het vermogen op
  // de ondergrens, wat anders onverklaarbaar lijkt.
  int intent_reason{0};
  float floor_w{NAN};
  // Vergelijking met de huidige Power House (oq_ph_request_hp1/2_level).
  bool differs{false};
  int active_hp1_level{0};
  int active_hp2_level{0};
  float differs_minutes{0.0f};
  // Twee cijfers voor de beslissing bij stap 3, zie
  // docs/power-house-v050-stap3-omschakelaar.md:
  //   no_start_minutes  de enige afwijking die een koud huis oplevert
  //   level_changes_*   de maat voor de ritmekeuze (10 s of trager)
  float no_start_minutes{0.0f};
  int level_changes_hp1{0};
  int level_changes_hp2{0};
  // Nu, op dit moment: hij zou beide units stil laten staan terwijl de andere
  // motor wil stoken en de kamer onder setpoint zit. De vangnet in
  // oq_power_house_engine.h hangt hieraan.
  bool starving{false};
};

class Shadow {
 public:
  const Output& output() const { return this->out_; }

  void tick(const TickConfig& config) {
    const uint32_t now_ms = static_cast<uint32_t>(esphome::millis());
    // Fork: Power House actief = geen koelen (CM5) en strategie Power House (0).
    const bool active = id(oq_control_mode_code) != 5 && id(oq_heat_mode_code) == 0;
    if (!active) {
      this->reset_();
      return;
    }

    const bool hp1_valve_defrost = id(hp1_4_way_valve).state;
    const bool hp2_valve_defrost = id(hp2_4_way_valve).state;
    const bool oil_return = id(hp1_prot_oil_return).state || id(hp2_prot_oil_return).state;
    const int hp1_applied = id(hp1_last_applied_level);
    const int hp2_applied = id(hp2_last_applied_level);
    this->dispatch_state_ = oq_power_house_dispatch::observe_protection(
        this->dispatch_state_, {now_ms, hp1_valve_defrost, hp2_valve_defrost, oil_return, hp1_applied, hp2_applied});

    const auto cadence = oq_power_house::decide_cadence(now_ms, this->last_loop_ms_, config.loop_ms);
    if (!cadence.due) return;
    this->last_loop_ms_ = now_ms == 0 ? UINT32_MAX : now_ms;

    // ---- Vraag: upstream + v0.32-uitbreidingen ----
    const float outside_c = id(outside_temp_selected).state;
    const float cold_c = id(house_cold_temp_c).state;
    const float zero_c = id(house_zero_power_temp_c).state;
    const float rated_w = id(house_rated_power_w).state;
    const float room_c = id(room_temp_selected).state;
    const float setpoint_c = id(room_setpoint_selected).state;
    const float reaction_w_per_k = id(ph_kp_w_per_k).state;

    const oq_power_house::DemandInput demand_input{
        now_ms, outside_c, cold_c, zero_c, rated_w, room_c, setpoint_c, NAN, id(oq_water_temp_limit_factor), false,
    };
    const oq_power_house::DemandTuning demand_tuning{
        config.temperature_guard_c,           reaction_w_per_k,
        id(ph_comfort_band_below_c).state,    id(ph_comfort_band_above_c).state,
        id(ph_demand_rise_time_min).state,    id(ph_demand_fall_time_min).state,
        config.demand_max_f,
    };
    DemandExtensions ext;
    ext.solar_correction_w = clampf(id(ph_solar_gain_alpha).state, 0.0f, 1.0f) * id(oq_phouse_solar_gain_w);
    ext.preheat_floor_w = preheat_floor_w(
        id(ph_preheat_gain).state, setpoint_c,
        id(room_setpoint_next_ha).has_state() ? id(room_setpoint_next_ha).state : NAN, outside_c,
        id(outside_temp_forecast_ha).has_state() ? id(outside_temp_forecast_ha).state : NAN, zero_c, cold_c,
        rated_w);
    ext.tariff_w = std::isfinite(rated_w)
                       ? clampf(reaction_w_per_k * id(oq_phouse_tariff_shift_c), -rated_w, rated_w)
                       : 0.0f;
    ext.pv_boost_w = id(oq_phouse_pv_boost_w);
    ext.window_open = id(oq_phouse_window_open_active);

    const int applied_total = std::max(0, hp1_applied) + std::max(0, hp2_applied);
    if (applied_total > 0 && this->fast_floor_w_ > 0.0f) {
      this->demand_state_.last_w = std::max(this->demand_state_.last_w, this->fast_floor_w_);
      this->fast_floor_w_ = 0.0f;
    }
    const auto demand = decide_demand_with_extensions(demand_input, demand_tuning, this->demand_state_, ext);
    this->demand_state_ = demand.next;
    float requested_w = demand.requested_w;
    int raw_demand = demand.raw_demand;

    // ---- Snelle eerste start ----
    // Fork: geen aparte verwarming-aan-bron; bronnen CIC en HA input.
    const bool room_fresh = std::isfinite(room_c) && std::isfinite(setpoint_c) && this->room_temperature_fresh_();
    const bool setpoint_fresh = this->room_setpoint_fresh_();
    const auto intent = oq_heat_intent::evaluate(
        {now_ms, true, true, true, room_fresh, setpoint_fresh, applied_total > 0, this->setpoint_source_code_(),
         room_c, setpoint_c, std::max(0.0f, demand_tuning.comfort_below_c), 0.20f, 10000UL},
        this->intent_state_);
    this->intent_state_ = intent.next;

    // ---- Kandidaten per HP ----
    const bool lead_is_hp1 = id(hp1_minutes) <= id(hp2_minutes);

    FrequencyContext frequency;
    const auto& reader1 = oq_odu_runtime_frequency::snapshot_reader(true);
    const auto& reader2 = oq_odu_runtime_frequency::snapshot_reader(false);
    frequency.hp1.known = reader1.known();
    frequency.hp2.known = reader2.known();
    for (size_t i = 0; i <= static_cast<size_t>(kLevels); ++i) {
      frequency.hp1.hz[i] = reader1.heating_hz()[i];
      frequency.hp2.hz[i] = reader2.heating_hz()[i];
    }
    int level_cap = static_cast<int>(std::round(id(oq_day_max_level).state));
    if (id(oq_silent_active).state) level_cap = static_cast<int>(std::round(id(oq_silent_max_level).state));
    frequency.level_cap = std::max(0, std::min(kLevels, level_cap));
    frequency.hp1_excluded_mask = this->excluded_mask_(true);
    frequency.hp2_excluded_mask = this->excluded_mask_(false);

    // v0.32 #5: aanvoertemperatuur voor het model; v0.32 #4: vorstzone-derating.
    const float supply_c = effective_supply_temp_c(requested_w, rated_w, id(oq_system_supply_temp).state);
    const float frost_derate = frost_zone_derate(outside_c, id(ph_frost_zone_max_derate).state);
    const bool performance_valid = std::isfinite(outside_c) && std::isfinite(supply_c);

    auto& limits = oq_thermal_actuator::shared_start_limits();
    const bool must_stop = id(oq_lowflow_fault_active) || id(oq_water_temp_hard_trip_active);
    const bool startup_inhibit = id(oq_boot_startup_inhibit_active);
    // Rechtstreeks aan de modbus_controller vragen in plaats van via de globals:
    // die hangen aan de on_online-trigger, en die vuurt alleen bij een overgang.
    const bool hp1_online = !id(hp1).get_module_offline();
    const bool hp2_online = !id(hp2).get_module_offline();
    CandidateInputs c1{hp1_applied,     hp1_online,           must_stop,
                       startup_inhibit, now_ms,               id(hp1_last_stop_ms),
                       config.minimum_off_ms, limits[0].remaining_ms(now_ms)};
    CandidateInputs c2{hp2_applied,     hp2_online,           must_stop,
                       startup_inhibit, now_ms,               id(hp2_last_stop_ms),
                       config.minimum_off_ms, limits[1].remaining_ms(now_ms)};
    const auto hp1_candidate = candidate_state(c1);
    const auto hp2_candidate = candidate_state(c2);

    float defrost_factor = config.defrost_power_factor;
    if (!std::isfinite(defrost_factor)) defrost_factor = 0.55f;
    defrost_factor = std::max(0.10f, std::min(1.00f, defrost_factor));
    bool hp1_model_available = false;
    bool hp2_model_available = false;
    bool hp1_runnable = false;
    bool hp2_runnable = false;
    const auto build_hp = [&](oq_power_house_dispatch::HpInput& result, bool hp1,
                              const oq_hp_candidate::HpCandidateState& candidate, bool defrost, bool valve_defrost) {
      result.candidate = candidate;
      result.defrost = defrost;
      result.valve_defrost = valve_defrost;
      result.levels[0] = {true, true, true, 0.0f, 0.0f};
      const float factor = thermal_factor(valve_defrost, defrost_factor, frost_derate);
      for (int level = 1; level <= oq_power_house_dispatch::kMaxLevel; ++level) {
        const auto prediction = oq_perf::predict_candidate(frequency, frequency.performance_variant(hp1), hp1, level,
                                                           outside_c, supply_c);
        const bool allowed = prediction.frequency_policy_allowed;
        float thermal_w = performance_valid ? prediction.performance.pth_w : NAN;
        const float electrical_w = performance_valid ? prediction.performance.pel_w : NAN;
        if (std::isfinite(thermal_w)) thermal_w *= factor;
        const bool thermal_valid = std::isfinite(thermal_w) && thermal_w >= 0.0f;
        const bool electrical_valid = std::isfinite(electrical_w) && electrical_w >= 0.0f;
        result.levels[level] = {allowed, thermal_valid, electrical_valid, thermal_w, electrical_w};
        bool& model_available = hp1 ? hp1_model_available : hp2_model_available;
        bool& runnable = hp1 ? hp1_runnable : hp2_runnable;
        model_available |= prediction.runtime_frequency_known && prediction.performance.available;
        runnable |= allowed && thermal_valid && electrical_valid;
      }
    };

    oq_power_house_dispatch::DispatchInput dispatch_input{now_ms, raw_demand, requested_w, true, performance_valid,
                                                          lead_is_hp1};
    build_hp(dispatch_input.hp1, true, hp1_candidate, id(hp1_defrost).state, hp1_valve_defrost);
    build_hp(dispatch_input.hp2, false, hp2_candidate, id(hp2_defrost).state, hp2_valve_defrost);
    const bool active_model_missing =
        (hp1_candidate.previous_applied_level > 0 && !hp1_candidate.must_stop && !hp1_model_available) ||
        (hp2_candidate.previous_applied_level > 0 && !hp2_candidate.must_stop && !hp2_model_available);
    const bool any_servable = (oq_hp_candidate::may_serve_candidate(hp1_candidate) && hp1_runnable) ||
                              (oq_hp_candidate::may_serve_candidate(hp2_candidate) && hp2_runnable);
    dispatch_input.performance_valid = performance_valid && any_servable && !active_model_missing;

    float floor_w = NAN;
    float minimum_viable_w = NAN;
    const auto include_minimum = [&](const oq_power_house_dispatch::HpInput& hp) {
      if (!oq_hp_candidate::may_serve_candidate(hp.candidate)) return;
      for (int level = 1; level <= oq_power_house_dispatch::kMaxLevel; ++level) {
        const auto& estimate = hp.levels[level];
        if (!estimate.allowed || !estimate.thermal_valid || !std::isfinite(estimate.thermal_w) ||
            estimate.thermal_w <= 0.0f)
          continue;
        minimum_viable_w =
            std::isfinite(minimum_viable_w) ? std::min(minimum_viable_w, estimate.thermal_w) : estimate.thermal_w;
        break;
      }
    };
    include_minimum(dispatch_input.hp1);
    include_minimum(dispatch_input.hp2);
    if ((intent.fast_start || intent.room_recovery_active) && std::isfinite(minimum_viable_w) &&
        id(oq_water_temp_limit_factor) >= 0.999f) {
      requested_w = std::max(requested_w, minimum_viable_w);
      floor_w = minimum_viable_w;
      this->fast_floor_w_ = requested_w;
      if (std::isfinite(rated_w) && rated_w > 0.0f && config.demand_max_f > 0)
        raw_demand = std::max(raw_demand, std::min(config.demand_max_f, static_cast<int>(std::ceil(
                                                                            requested_w * config.demand_max_f / rated_w))));
    } else if (applied_total == 0) {
      this->fast_floor_w_ = 0.0f;
    }

    const int capped_demand =
        std::min(raw_demand, std::max(0, std::min(config.demand_max_f, static_cast<int>(id(oq_power_cap_f)))));
    if (std::isfinite(requested_w) && std::isfinite(rated_w) && rated_w > 0.0f && config.demand_max_f > 0)
      requested_w = std::min(requested_w, rated_w * static_cast<float>(capped_demand) / config.demand_max_f);
    dispatch_input.demand_level = capped_demand;
    dispatch_input.requested_w = requested_w;

    const oq_power_house_dispatch::DispatchTuning dispatch_tuning{
        config.soft_limit_w,           config.peak_limit_w,           config.optimizer_penalty_per_w,
        config.topology_power_margin_w, config.topology_heat_advantage_w, config.defrost_comp_min_f,
        config.defrost_comp_boost_steps,
    };
    const auto dispatch =
        oq_power_house_dispatch::decide_dispatch(dispatch_input, dispatch_tuning, this->dispatch_state_);

    // ---- Uitvoer (alleen eigen) ----
    const float elapsed_min = cadence.dt_s / 60.0f;
    // Vergelijken met wat de FORK wil, niet met de gepubliceerde aanvraag. Stuurt
    // de v0.50-motor, dan staat daar zijn eigen keuze en zou het verschil altijd
    // nul zijn. Zo blijft het dezelfde vergelijking, welke motor er ook stuurt.
    const int active_hp1 = id(oq_ph_fork_request_hp1_level);
    const int active_hp2 = id(oq_ph_fork_request_hp2_level);
    const bool differs = dispatch.hp1_level != active_hp1 || dispatch.hp2_level != active_hp2;
    this->out_.active = true;
    this->out_.output_valid = dispatch.output_valid;
    this->out_.hp1_level = dispatch.hp1_level;
    this->out_.hp2_level = dispatch.hp2_level;
    this->out_.owner_hp = dispatch.owner_hp;
    this->out_.reason = static_cast<int>(dispatch.reason);
    this->out_.demand = capped_demand;
    this->out_.requested_w = requested_w;
    this->out_.expected_w = dispatch.expected_w;
    this->out_.capacity_w = dispatch.capacity_w;
    this->out_.model_supply_c = supply_c;
    this->out_.frost_derate = frost_derate;
    this->out_.intent = intent.fast_start ? static_cast<int>(intent.reason) : 0;
    this->out_.intent_reason = static_cast<int>(intent.reason);
    this->out_.floor_w = floor_w;
    this->out_.differs = differs;
    this->out_.active_hp1_level = active_hp1;
    this->out_.active_hp2_level = active_hp2;
    const bool elapsed_usable = std::isfinite(elapsed_min) && elapsed_min > 0.0f && elapsed_min < 5.0f;
    if (differs && elapsed_usable) this->out_.differs_minutes += elapsed_min;

    // Standwissels per uur: hoe vaak deze motor de stand zou verzetten. In
    // Power House-modus staat de slew-begrenzing van ±1 stand downstream uit
    // (oq_thermal_request_control.yaml), dus wat hier beweegt, beweegt straks
    // ook echt. Dit is de maat waarop het rekenritme wordt gekozen.
    if (this->prev_hp1_level_ >= 0 && dispatch.hp1_level != this->prev_hp1_level_)
      this->changes_hp1_.record(now_ms);
    if (this->prev_hp2_level_ >= 0 && dispatch.hp2_level != this->prev_hp2_level_)
      this->changes_hp2_.record(now_ms);
    this->prev_hp1_level_ = dispatch.hp1_level;
    this->prev_hp2_level_ = dispatch.hp2_level;
    this->out_.level_changes_hp1 = this->changes_hp1_.count(now_ms);
    this->out_.level_changes_hp2 = this->changes_hp2_.count(now_ms);

    // De enige afwijking die een koud huis oplevert: v0.50 zou beide units stil
    // laten staan terwijl de huidige motor wél stookt en de kamer onder het
    // setpoint zit. Alles daarbuiten is een verschil, dit is een risico.
    // Voorwaarde is de tabel, NIET output_valid. Juist als de dispatch zegt dat
    // hij geen bruikbaar besluit heeft en de standen vasthoudt op 0, moet dit
    // gaan tellen -- dat is precies de toestand die het huis koud laat.
    const bool would_idle = frequency.hp1.known && frequency.hp2.known &&
                            (dispatch.hp1_level + dispatch.hp2_level) == 0;
    const bool now_heating = (active_hp1 + active_hp2) > 0;
    const bool room_below = std::isfinite(room_c) && std::isfinite(setpoint_c) && room_c < setpoint_c;
    this->out_.starving = would_idle && now_heating && room_below;
    if (this->out_.starving) {
      if (elapsed_usable) this->out_.no_start_minutes += elapsed_min;
      if (!this->no_start_logged_) {
        this->no_start_logged_ = true;
        ESP_LOGW("quatt", "PH v0.50 schaduw zou niets draaien terwijl er gestookt wordt (%s); kamer %.1f < %.1f",
                 oq_power_house_dispatch::request_reason_name(static_cast<int>(dispatch.reason)), room_c, setpoint_c);
      }
    } else {
      this->no_start_logged_ = false;
    }
  }

 private:
  bool room_temperature_fresh_() const {
    if (!id(room_temp_source).has_state()) return false;
    const auto source = id(room_temp_source).current_option();
    if (source == "CIC")
      return id(feed_ok).has_state() && id(feed_ok).state && id(cic_data_stale).has_state() &&
             !id(cic_data_stale).state && id(cic_room_temp).has_state() && std::isfinite(id(cic_room_temp).state);
    if (source == "HA input")
      return id(thermostat_room_temp_ha).has_state() && std::isfinite(id(thermostat_room_temp_ha).state) &&
             !id(oq_room_temp_selected_hold_active);
    return false;
  }

  bool room_setpoint_fresh_() const {
    if (!id(room_setpoint_source).has_state()) return false;
    const auto source = id(room_setpoint_source).current_option();
    if (source == "CIC")
      return id(feed_ok).has_state() && id(feed_ok).state && id(cic_data_stale).has_state() &&
             !id(cic_data_stale).state && id(cic_room_setpoint).has_state() &&
             std::isfinite(id(cic_room_setpoint).state);
    if (source == "HA input")
      return id(thermostat_setpoint_ha).has_state() && std::isfinite(id(thermostat_setpoint_ha).state) &&
             !id(oq_room_setpoint_selected_hold_active);
    return false;
  }

  // Zelfde codes als upstream (1 = HA input, 3 = CIC).
  uint8_t setpoint_source_code_() const {
    if (!id(room_setpoint_source).has_state()) return 0;
    const auto source = id(room_setpoint_source).current_option();
    if (source == "HA input") return 1;
    if (source == "CIC") return 3;
    return 0;
  }

  uint16_t excluded_mask_(bool hp1) const {
    const oq_request::ExcludedLevels excluded =
        hp1 ? oq_request::ExcludedLevels{
                  id(hp1_excluded_level_a).has_state() ? id(hp1_excluded_level_a).current_option()
                                                       : std::string("None"),
                  id(hp1_excluded_level_b).has_state() ? id(hp1_excluded_level_b).current_option()
                                                       : std::string("None"),
              }
            : oq_request::ExcludedLevels{
                  id(hp2_excluded_level_a).has_state() ? id(hp2_excluded_level_a).current_option()
                                                       : std::string("None"),
                  id(hp2_excluded_level_b).has_state() ? id(hp2_excluded_level_b).current_option()
                                                       : std::string("None"),
              };
    uint16_t mask = 0;
    for (int level = 1; level <= kLevels; ++level) {
      if (!oq_request::level_allowed_for_excluded_levels(excluded, level))
        mask = static_cast<uint16_t>(mask | (1U << level));
    }
    return mask;
  }

  void reset_() {
    this->dispatch_state_ = {};
    this->intent_state_ = {};
    this->demand_state_ = {};
    this->fast_floor_w_ = 0.0f;
    this->last_loop_ms_ = 0;
    this->prev_hp1_level_ = -1;
    this->prev_hp2_level_ = -1;
    this->no_start_logged_ = false;
    // De optelling over de hele looptijd blijft staan; anders wist een uurtje
    // koelen of warm water het cijfer waar de beslissing op hangt. De
    // uurtellers lopen vanzelf leeg.
    const float differs_minutes = this->out_.differs_minutes;
    const float no_start_minutes = this->out_.no_start_minutes;
    this->out_ = {};
    this->out_.differs_minutes = differs_minutes;
    this->out_.no_start_minutes = no_start_minutes;
  }

  Output out_;
  oq_power_house_dispatch::DispatchState dispatch_state_;
  oq_heat_intent::State intent_state_;
  oq_power_house::DemandState demand_state_;
  oq_rate::RollingHourCounter changes_hp1_;
  oq_rate::RollingHourCounter changes_hp2_;
  float fast_floor_w_{0.0f};
  uint32_t last_loop_ms_{0};
  int prev_hp1_level_{-1};
  int prev_hp2_level_{-1};
  bool no_start_logged_{false};
};

inline Shadow& shadow() {
  static Shadow instance;
  return instance;
}

inline std::string heating_table_text(bool hp1) {
  const auto& reader = oq_odu_runtime_frequency::snapshot_reader(hp1);
  if (!reader.known()) {
    char buf[48];
    snprintf(buf, sizeof(buf), "onbekend (%u mislukt)", static_cast<unsigned>(reader.failures()));
    return std::string(buf);
  }
  char buf[96];
  const auto& hz = reader.heating_hz();
  const uint32_t age_min = (static_cast<uint32_t>(esphome::millis()) - reader.updated_ms()) / 60000UL;
  snprintf(buf, sizeof(buf), "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u Hz (%u min oud)", hz[1], hz[2], hz[3], hz[4], hz[5],
           hz[6], hz[7], hz[8], hz[9], hz[10], static_cast<unsigned>(age_min));
  return std::string(buf);
}

inline std::string difference_text() {
  const Output& out = shadow().output();
  if (!out.active) return std::string("inactief");
  if (!out.differs) return std::string("gelijk");
  char buf[64];
  snprintf(buf, sizeof(buf), "HP1 %d->%d, HP2 %d->%d", out.active_hp1_level, out.hp1_level, out.active_hp2_level,
           out.hp2_level);
  return std::string(buf);
}

}  // namespace oq_ph_v050
#endif
