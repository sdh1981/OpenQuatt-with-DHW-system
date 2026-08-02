#pragma once

#include <cmath>
#include <cstdint>

namespace oq_dhw {

enum class State : uint8_t {
  IDLE_CV = 0,
  DHW_PREPARE = 1,
  DHW_HEAT_PUMP = 2,
  DHW_BOOST = 3,
  LEGIONELLA = 4,
  FAULT = 5,
};

enum class Fault : uint8_t {
  NONE = 0,
  SENSOR_IMPLAUSIBLE = 1,
  HP_FAULT = 2,
  VALVE_STUCK_CV = 3,
  VALVE_MISMATCH = 4,
  FLOW_OUT_OF_RANGE = 5,
  TIMEOUT = 6,
  LOCKOUT = 7,
};

struct Config {
  float start_top_c = 46.0f;
  float hp_stop_top_c = 49.0f;
  // When stop_on_bottom_enable=true, the regular DHW HP cycle uses tank_bottom_c
  // instead of tank_top_c for the HP stop check. This forces longer cycles that
  // fully reheat the tank — better COP (longer low-lift run), much more usable
  // hot water per cycle, and natural legionella suppression because the cold
  // bottom zone gets heated through every cycle.
  bool  stop_on_bottom_enable = true;   // Hard-enabled per user requirement
  float hp_stop_bottom_c = 52.0f;        // Hard-coded per user requirement (option C)
  // v0.40 temp: tijdens legionella-run de klep op DHW-stand HOUDEN (i.p.v.
  // terugzetten naar CV tijdens de element-only fase). Hiermee blijft de
  // pomp het tankwater door de DHW-spiraal circuleren → mixing → uniforme
  // legionella-pasteurisatie. Te disabelen zodra een fysieke circulatiepomp
  // tussen top en bottom van de tank is geïnstalleerd.
  bool  legionella_use_coil_circulation = true;
  float boost_target_c = 56.0f;
  // Handmatige snelboost. Hogere doeltemperatuur voor het element, terwijl de
  // HP's er al eerder uit gaan -- boven die tanktop loopt de condensatiedruk
  // te ver op als er ook nog een element in dezelfde tank staat te stoken.
  float max_boost_target_c = 60.0f;
  float max_boost_hp_stop_top_c = 55.0f;
  float legionella_target_c = 68.0f;  // Inventum boiler vereiste: 68°C
  float legionella_hp_stop_top_c = 53.0f;  // HP handover temp during legionella (element finishes the rest)
  // Harde bovengrens op tank_top tijdens de legionella HP-fase. De handover
  // hierboven toetst op tank_bottom (traagste zone), maar in fase 1 verwarmt het
  // element de TOP mee. Zonder plafond loopt de top -- en daarmee de condensor-
  // uittrede van de HP -- ver voorbij de handover-temp en tikt de ODU af op hoge
  // perszijdedruk (waargenomen op HP2: "High pressure detected"). Bij R32 is die
  // marge klein, dus dit is een beveiliging, geen optimalisatie.
  float legionella_hp_top_ceiling_c = 55.0f;
  float hp_target_flow_c = 55.0f;

  float temp_min_c = -10.0f;
  float temp_max_c = 85.0f;

  float flow_min_lph = 900.0f;
  float flow_max_lph = 1100.0f;

  uint32_t valve_prepare_timeout_ms = 20000UL;
  uint32_t valve_settle_ms = 4000UL;
  uint32_t valve_mismatch_fault_ms = 10000UL;
  uint32_t hp_max_runtime_ms = 75UL * 60UL * 1000UL;
  uint32_t boost_max_runtime_ms = 90UL * 60UL * 1000UL;
  uint32_t legionella_max_runtime_ms = 150UL * 60UL * 1000UL;
  uint32_t flow_fault_delay_ms = 30000UL;

  uint32_t legionella_interval_ms = 7UL * 24UL * 60UL * 60UL * 1000UL;
  uint32_t legionella_hold_ms = 15UL * 60UL * 1000UL;

  // Minimum idle time between normal DHW cycles (0 = disabled).
  uint32_t min_cycle_rest_ms = 0;

  uint8_t valve_retries = 1;
  bool enable_boost_after_hp = true;

  // DHW boost HP assist: when tank_bottom is cold at boost-entry, run both the
  // element and the HP together. Decision is latched at the start of boost to
  // prevent hunting (drawing hot water always cools the bottom).
  bool  boost_hp_assist_enable = false;
  float boost_hp_assist_bottom_c = 35.0f;   // activate assist when bottom < this at entry
  float boost_hp_assist_stop_top_c = 52.0f; // HP stops when top >= this during boost
};

struct Inputs {
  uint32_t now_ms = 0;

  float tank_top_c = NAN;
  float tank_bottom_c = NAN;  // NAN = not available (treated as absent, not faulty)
  float coil_in_c = NAN;
  float coil_out_c = NAN;

  bool valve_feedback_valid = false;
  bool valve_feedback_cv = true;  // true => valve reports CV position

  bool flow_valid = false;
  float flow_lph = NAN;

  bool hp_fault_active = false;
  bool lockout_active = false;

  // Blocks new cycle starts without faulting a running cycle (e.g. outside time window).
  // Does NOT block legionella (hygiene-critical).
  bool start_inhibit = false;

  bool solar_boost_request = false;
  // Handmatige snelboost: HP1 + HP2 + element tegelijk, voor snel herstel van
  // de tank. Anders dan solar_boost_request draaien de HP's hier altijd mee.
  bool max_boost_request = false;
  // Door de YAML-laag bewaakte thermische grens (persgas / water-uit). Zolang
  // die staat mogen de HP's niet meedraaien in de boost; het element gaat door.
  bool hp_thermal_limit_active = false;
  bool legionella_force_request = false;

  // Set to true once the persistent legionella timestamp has been seeded from
  // flash/NTP. The controller waits for this before scheduling automatic runs
  // to avoid triggering on stale millis() immediately after boot.
  bool legionella_seeded = false;
};

struct Outputs {
  State state = State::IDLE_CV;
  Fault fault = Fault::NONE;

  bool valve_to_boiler = false;
  bool element_on = false;
  bool hp_dhw_request = false;
  bool block_cv_priority = false;
  float target_flow_temp_c = NAN;

  bool state_changed = false;
  bool fault_changed = false;
};

inline const char *state_name(State s) {
  switch (s) {
    case State::IDLE_CV: return "IDLE_CV";
    case State::DHW_PREPARE: return "DHW_PREPARE";
    case State::DHW_HEAT_PUMP: return "DHW_HEAT_PUMP";
    case State::DHW_BOOST: return "DHW_BOOST";
    case State::LEGIONELLA: return "LEGIONELLA";
    case State::FAULT: return "FAULT";
  }
  return "UNKNOWN";
}

inline const char *fault_name(Fault f) {
  switch (f) {
    case Fault::NONE: return "NONE";
    case Fault::SENSOR_IMPLAUSIBLE: return "SENSOR_IMPLAUSIBLE";
    case Fault::HP_FAULT: return "HP_FAULT";
    case Fault::VALVE_STUCK_CV: return "VALVE_STUCK_CV";
    case Fault::VALVE_MISMATCH: return "VALVE_MISMATCH";
    case Fault::FLOW_OUT_OF_RANGE: return "FLOW_OUT_OF_RANGE";
    case Fault::TIMEOUT: return "TIMEOUT";
    case Fault::LOCKOUT: return "LOCKOUT";
  }
  return "UNKNOWN";
}

class Controller {
 public:
  Controller() = default;

  void clear_fault_latch() {
    if (state_ == State::FAULT || fault_ != Fault::NONE || fault_latched_) {
      state_ = State::IDLE_CV;
      fault_ = Fault::NONE;
      state_enter_ms_ = 0;
      fault_latched_ = false;
      reset_cycle_flags_();
    }
  }

  // Seeds the legionella last-done timestamp from a persistent wall-clock source
  // so the 7-day interval survives reboots. Call once at boot before the first tick.
  // Only takes effect if the internal value is still zero (unset).
  void seed_legionella_last_done_ms(uint32_t ms) {
    if (legionella_last_done_ms_ == 0 && ms != 0) {
      legionella_last_done_ms_ = ms;
    }
  }

  // Loopt de huidige cyclus als handmatige snelboost? De YAML-laag gebruikt dit
  // om beide HP's in te zetten en om de juiste bewaking te kiezen.
  bool max_boost_active() const { return active_max_boost_; }

  bool legionella_last_done_valid() const {
    return legionella_last_done_ms_ != 0;
  }

  uint32_t legionella_last_done_ms() const {
    return legionella_last_done_ms_;
  }

  // Returns the scheduled due timestamp (millis timeline).
  // If disabled (interval=0), returns 0.
  uint32_t legionella_next_due_ms(uint32_t now_ms, const Config &cfg) const {
    if (cfg.legionella_interval_ms == 0) return 0;
    if (legionella_last_done_ms_ == 0) {
      return cfg.legionella_interval_ms;
    }
    const uint32_t due = legionella_last_done_ms_ + cfg.legionella_interval_ms;
    // Overflow guard: if wrapped, treat as due now.
    if (due < legionella_last_done_ms_) return now_ms;
    return due;
  }

  Outputs tick(const Inputs &in, const Config &cfg) {
    Outputs out;
    out.state = state_;
    out.fault = fault_;

    const State prev_state = state_;
    const Fault prev_fault = fault_;

    evaluate_faults_(in, cfg);

    if (state_ == State::FAULT) {
      make_fail_safe_output_(out);
      finalize_output_(out, prev_state, prev_fault);
      return out;
    }

    switch (state_) {
      case State::IDLE_CV:
        if (should_start_legionella_(in, cfg)) {
          active_legionella_ = true;
          active_solar_boost_ = false;
          hp_phase_done_ = false;
          legionella_hold_start_ms_ = 0;
          valve_retry_count_ = 0;
          transition_(State::DHW_PREPARE, in.now_ms);
        } else if (should_start_max_boost_(in, cfg)) {
          // Handmatige snelboost gaat vóór de gewone cyclus: die zou op de
          // HP-fase beginnen en het element pas aan het eind inzetten.
          active_legionella_ = false;
          active_solar_boost_ = false;
          active_max_boost_ = true;
          hp_phase_done_ = false;
          legionella_hold_start_ms_ = 0;
          valve_retry_count_ = 0;
          transition_(State::DHW_PREPARE, in.now_ms);
        } else if (should_start_regular_dhw_(in, cfg)) {
          active_legionella_ = false;
          active_solar_boost_ = false;
          active_max_boost_ = false;
          hp_phase_done_ = false;
          legionella_hold_start_ms_ = 0;
          valve_retry_count_ = 0;
          transition_(State::DHW_PREPARE, in.now_ms);
        } else if (should_start_solar_boost_(in, cfg)) {
          active_legionella_ = false;
          active_solar_boost_ = true;
          hp_phase_done_ = true;
          legionella_hold_start_ms_ = 0;
          valve_retry_count_ = 0;
          transition_(State::DHW_PREPARE, in.now_ms);
        }
        break;

      case State::DHW_PREPARE:
        if (valve_ready_(in, cfg)) {
          if (active_legionella_) {
            transition_(State::LEGIONELLA, in.now_ms);
          } else if (active_max_boost_) {
            // Bij de snelboost draaien de HP's altijd mee -- geen koude-bodem
            // voorwaarde zoals bij de solar boost. Dat is juist het punt.
            boost_hp_assist_active_ = true;
            transition_(State::DHW_BOOST, in.now_ms);
          } else if (active_solar_boost_) {
            if (cfg.boost_hp_assist_enable &&
                plausible_temp_(in.tank_bottom_c, cfg) &&
                in.tank_bottom_c < cfg.boost_hp_assist_bottom_c) {
              boost_hp_assist_active_ = true;
            }
            transition_(State::DHW_BOOST, in.now_ms);
          } else {
            transition_(State::DHW_HEAT_PUMP, in.now_ms);
          }
        } else if (elapsed_in_state_(in.now_ms) >= cfg.valve_prepare_timeout_ms) {
          if (valve_retry_count_ < cfg.valve_retries) {
            ++valve_retry_count_;
            state_enter_ms_ = in.now_ms;
          } else {
            latch_fault_(Fault::VALVE_STUCK_CV, in.now_ms);
          }
        }
        break;

      case State::DHW_HEAT_PUMP: {
        // Decide which tank sensor drives the HP stop. Bottom-based stop
        // (when enabled and bottom sensor is plausible) ensures the full tank
        // is reheated through every cycle. Falls back to top-stop transparently
        // if the bottom sensor is missing/implausible.
        bool stop_temp_reached = false;
        if (cfg.stop_on_bottom_enable && plausible_temp_(in.tank_bottom_c, cfg)) {
          stop_temp_reached = (in.tank_bottom_c >= cfg.hp_stop_bottom_c);
        } else {
          stop_temp_reached = (in.tank_top_c >= cfg.hp_stop_top_c);
        }
        if (stop_temp_reached || elapsed_in_state_(in.now_ms) >= cfg.hp_max_runtime_ms) {
          if (cfg.enable_boost_after_hp && in.tank_top_c < cfg.boost_target_c) {
            if (cfg.boost_hp_assist_enable &&
                plausible_temp_(in.tank_bottom_c, cfg) &&
                in.tank_bottom_c < cfg.boost_hp_assist_bottom_c) {
              boost_hp_assist_active_ = true;
            }
            transition_(State::DHW_BOOST, in.now_ms);
          } else {
            cycle_end_ms_ = in.now_ms;
            transition_(State::IDLE_CV, in.now_ms);
            reset_cycle_flags_();
          }
        }
        break;
      }

      case State::DHW_BOOST: {
        // Bij de snelboost geldt een eigen, hogere HP-grens (default 55 C) en
        // een hogere doeltemperatuur voor het element (default 60 C). De HP's
        // gaan er dus eerder uit dan het element klaar is -- boven die tanktop
        // loopt de condensatiedruk te ver op met een element in dezelfde tank.
        const float hp_stop_top_c = active_max_boost_ ? cfg.max_boost_hp_stop_top_c
                                                      : cfg.boost_hp_assist_stop_top_c;
        const float target_top_c = active_max_boost_ ? cfg.max_boost_target_c
                                                     : cfg.boost_target_c;
        if (boost_hp_assist_active_ &&
            plausible_temp_(in.tank_top_c, cfg) &&
            in.tank_top_c >= hp_stop_top_c) {
          boost_hp_assist_active_ = false;
        }
        // Door de YAML bewaakte persgas-/water-uit grens: HP's eruit, element
        // maakt de rest af. Bewust niet terugkeerbaar binnen dezelfde boost --
        // opnieuw opstarten in een al hete tank levert alleen nieuwe drukpieken.
        if (boost_hp_assist_active_ && in.hp_thermal_limit_active) {
          boost_hp_assist_active_ = false;
        }
        if (in.tank_top_c >= target_top_c) {
          cycle_end_ms_ = in.now_ms;
          transition_(State::IDLE_CV, in.now_ms);
          reset_cycle_flags_();
        } else if (elapsed_in_state_(in.now_ms) >= cfg.boost_max_runtime_ms) {
          latch_fault_(Fault::TIMEOUT, in.now_ms);
        }
        break;
      }

      case State::LEGIONELLA:
        handle_legionella_(in, cfg);
        break;

      case State::FAULT:
        break;
    }

    out.state = state_;
    out.fault = fault_;

    if (state_ == State::FAULT) {
      make_fail_safe_output_(out);
      finalize_output_(out, prev_state, prev_fault);
      return out;
    }

    // During legionella element-only phase (HP done, element finishes to target):
    // historically we released the valve to CV. v0.40 temp: when
    // legionella_use_coil_circulation is true (default, no physical circ pump),
    // we KEEP the valve on DHW throughout the entire legionella run so the
    // OpenQuatt pump can circulate tank water through the DHW coil — this
    // mixes the tank and ensures uniform pasteurisation. Set the config flag
    // to false once a real circulation pump between top and bottom is fitted.
    const bool legionella_element_only = (state_ == State::LEGIONELLA && hp_phase_done_);
    const bool release_valve = legionella_element_only && !cfg.legionella_use_coil_circulation;

    out.valve_to_boiler   = (state_ != State::IDLE_CV) && !release_valve;
    out.block_cv_priority = (state_ != State::IDLE_CV) && !release_valve;

    if (state_ == State::DHW_HEAT_PUMP) {
      out.hp_dhw_request = true;
      out.target_flow_temp_c = cfg.hp_target_flow_c;
    } else if (state_ == State::LEGIONELLA && !hp_phase_done_) {
      out.hp_dhw_request = true;
      out.target_flow_temp_c = cfg.hp_target_flow_c;
    } else if (state_ == State::DHW_BOOST && boost_hp_assist_active_) {
      out.hp_dhw_request = true;
      out.target_flow_temp_c = cfg.hp_target_flow_c;
    }

    const bool valve_confirms_boiler = valve_is_boiler_(in);
    const float element_target_c =
        (state_ == State::LEGIONELLA) ? cfg.legionella_target_c
        : (active_max_boost_ ? cfg.max_boost_target_c : cfg.boost_target_c);
    const bool element_state_allowed = (state_ == State::DHW_BOOST || state_ == State::LEGIONELLA);

    // Element control: if klep wordt teruggezet naar CV tijdens element-only
    // (release_valve = true), dan moet element ook draaien zonder boiler-bevestiging.
    // Met de coil-circulation fix (default ON) staat klep op DHW → reguliere
    // valve_confirms_boiler check werkt gewoon.
    const bool element_valve_ok = release_valve || valve_confirms_boiler;

    out.element_on =
        element_state_allowed &&
        element_valve_ok &&
        !in.lockout_active &&
        !in.hp_fault_active &&
        !std::isnan(in.tank_top_c) &&
        in.tank_top_c < element_target_c;

    // Valve mismatch fault: only check when the valve is supposed to be in boiler
    // position. Tijdens release_valve staat klep bewust in CV → mismatch oké.
    const bool valve_should_be_boiler =
        (state_ == State::DHW_HEAT_PUMP) ||
        (state_ == State::DHW_BOOST) ||
        (state_ == State::LEGIONELLA && !release_valve);
    if (valve_should_be_boiler && in.valve_feedback_valid && in.valve_feedback_cv) {
      if (valve_mismatch_since_ms_ == 0) valve_mismatch_since_ms_ = in.now_ms;
      if ((in.now_ms - valve_mismatch_since_ms_) >= cfg.valve_mismatch_fault_ms) {
        latch_fault_(Fault::VALVE_MISMATCH, in.now_ms);
      }
    } else {
      valve_mismatch_since_ms_ = 0;
    }

    if (state_ == State::DHW_HEAT_PUMP || (state_ == State::LEGIONELLA && !hp_phase_done_) ||
        (state_ == State::DHW_BOOST && boost_hp_assist_active_)) {
      if (in.flow_valid && !std::isnan(in.flow_lph) &&
          (in.flow_lph < cfg.flow_min_lph || in.flow_lph > cfg.flow_max_lph)) {
        if (flow_fault_since_ms_ == 0) flow_fault_since_ms_ = in.now_ms;
        if ((in.now_ms - flow_fault_since_ms_) >= cfg.flow_fault_delay_ms) {
          latch_fault_(Fault::FLOW_OUT_OF_RANGE, in.now_ms);
        }
      } else {
        flow_fault_since_ms_ = 0;
      }
    } else {
      flow_fault_since_ms_ = 0;
    }

    if (state_ == State::FAULT) {
      make_fail_safe_output_(out);
    }

    finalize_output_(out, prev_state, prev_fault);
    return out;
  }

 private:
  State state_ = State::IDLE_CV;
  Fault fault_ = Fault::NONE;

  uint32_t state_enter_ms_ = 0;
  uint32_t valve_mismatch_since_ms_ = 0;
  uint32_t flow_fault_since_ms_ = 0;
  uint32_t legionella_last_done_ms_ = 0;
  uint32_t legionella_hold_start_ms_ = 0;
  uint32_t cycle_end_ms_ = 0;

  uint8_t valve_retry_count_ = 0;
  bool active_legionella_ = false;
  bool active_solar_boost_ = false;
  bool active_max_boost_ = false;
  bool hp_phase_done_ = false;
  bool fault_latched_ = false;
  bool boost_hp_assist_active_ = false;

  static bool plausible_temp_(float t, const Config &cfg) {
    return !std::isnan(t) && t >= cfg.temp_min_c && t <= cfg.temp_max_c;
  }

  bool valve_is_boiler_(const Inputs &in) const {
    if (!in.valve_feedback_valid) return true;
    return !in.valve_feedback_cv;
  }

  bool valve_ready_(const Inputs &in, const Config &cfg) const {
    if (elapsed_in_state_(in.now_ms) < cfg.valve_settle_ms) return false;
    return valve_is_boiler_(in);
  }

  void make_fail_safe_output_(Outputs &out) const {
    out.state = State::FAULT;
    out.fault = fault_;
    out.valve_to_boiler = false;
    out.element_on = false;
    out.hp_dhw_request = false;
    out.block_cv_priority = false;
    out.target_flow_temp_c = NAN;
  }

  void finalize_output_(Outputs &out, State prev_state, Fault prev_fault) {
    out.state_changed = (prev_state != out.state);
    out.fault_changed = (prev_fault != out.fault);
  }

  void transition_(State s, uint32_t now_ms) {
    state_ = s;
    state_enter_ms_ = now_ms;
  }

  uint32_t elapsed_in_state_(uint32_t now_ms) const {
    if (state_enter_ms_ == 0 || now_ms <= state_enter_ms_) return 0;
    return now_ms - state_enter_ms_;
  }

  bool should_start_regular_dhw_(const Inputs &in, const Config &cfg) const {
    if (fault_latched_ || in.lockout_active || in.hp_fault_active || in.start_inhibit)
      return false;
    if (cfg.min_cycle_rest_ms > 0 && cycle_end_ms_ > 0 &&
        (in.now_ms - cycle_end_ms_) < cfg.min_cycle_rest_ms)
      return false;
    return plausible_temp_(in.tank_top_c, cfg) && (in.tank_top_c < cfg.start_top_c);
  }

  // Handmatige snelboost. Zelfde blokkades als de andere starts, en alleen
  // zinvol als de tank nog onder het snelboost-doel zit.
  bool should_start_max_boost_(const Inputs &in, const Config &cfg) const {
    return !fault_latched_ && !in.lockout_active && !in.hp_fault_active &&
           in.max_boost_request &&
           plausible_temp_(in.tank_top_c, cfg) &&
           (in.tank_top_c < cfg.max_boost_target_c);
  }

  bool should_start_solar_boost_(const Inputs &in, const Config &cfg) const {
    return !fault_latched_ && !in.lockout_active && !in.hp_fault_active && !in.start_inhibit &&
           in.solar_boost_request &&
           plausible_temp_(in.tank_top_c, cfg) &&
           (in.tank_top_c < cfg.boost_target_c);
  }

  bool should_start_legionella_(const Inputs &in, const Config &cfg) const {
    if (fault_latched_ || in.lockout_active || in.hp_fault_active) return false;
    if (in.legionella_force_request) return true;
    if (cfg.legionella_interval_ms == 0) return false;
    // Wait until the persistent timestamp has been seeded (NTP + flash restore).
    // This replaces the old `now_ms < interval` uptime guard which incorrectly
    // blocked legionella scheduling after every reboot/OTA.
    if (!in.legionella_seeded) return false;
    // Never done: schedule after a short boot-settle period (30 min) so that
    // the system is stable before the first legionella run.
    if (legionella_last_done_ms_ == 0) {
      return in.now_ms >= (30UL * 60UL * 1000UL);
    }
    return (in.now_ms - legionella_last_done_ms_) >= cfg.legionella_interval_ms;
  }

  void handle_legionella_(const Inputs &in, const Config &cfg) {
    if (elapsed_in_state_(in.now_ms) >= cfg.legionella_max_runtime_ms) {
      latch_fault_(Fault::TIMEOUT, in.now_ms);
      return;
    }

    // HP-fase: tank_bottom is leidend (moeilijkst te verwarmen zone). Als
    // bottom op handover-temp is, neemt het element het over voor de laatste
    // pasteurisatie-graden.
    if (!hp_phase_done_) {
      const float hp_check_c = plausible_temp_(in.tank_bottom_c, cfg)
          ? in.tank_bottom_c : in.tank_top_c;
      const float leg_hp_stop = cfg.legionella_hp_stop_top_c;
      // Bovengrens op tank_top: in deze fase draaien HP en element samen, dus de
      // top stijgt veel sneller dan de bodem waar de handover op toetst. Wordt de
      // top te heet, dan moet de HP eruit ongeacht de bodem -- anders loopt de
      // condensordruk op tot de ODU zelf afslaat. Het element maakt de resterende
      // graden naar legionella_target_c alleen af.
      const bool top_ceiling_hit =
          plausible_temp_(in.tank_top_c, cfg) &&
          in.tank_top_c >= cfg.legionella_hp_top_ceiling_c;
      if (hp_check_c >= leg_hp_stop || top_ceiling_hit ||
          elapsed_in_state_(in.now_ms) >= cfg.hp_max_runtime_ms) {
        hp_phase_done_ = true;
        legionella_hold_start_ms_ = 0;
      }
      return;
    }

    // Element-fase / pasteurisatie-hold: het 3kW element zit fysiek bij de TOP.
    // Bottom volgt slechts traag (zelfs met coil-circulation), dus voor de
    // hold-check is tank_top de juiste sensor — dat is óók waar het hete water
    // verzamelt en waar pasteurisatie het eerst bereikt is.
    //
    // Hysterese: zodra de hold-timer eenmaal loopt, mag de temperatuur 1°C
    // onder target zakken zonder de timer te resetten (meet-ruis, korte dips
    // tijdens stratificatie). Pas bij >1°C dip wordt de hold-timer afgebroken.
    const float pasteur_check_c = plausible_temp_(in.tank_top_c, cfg)
        ? in.tank_top_c : in.tank_bottom_c;
    const float target_c = cfg.legionella_target_c;
    const float reset_threshold_c = target_c - 1.0f;  // hysterese 1°C

    if (pasteur_check_c >= target_c) {
      // Op of boven target → start (of houd) de hold-timer
      if (legionella_hold_start_ms_ == 0) legionella_hold_start_ms_ = in.now_ms;
    } else if (legionella_hold_start_ms_ != 0 && pasteur_check_c >= reset_threshold_c) {
      // Hold loopt, kleine dip binnen hysterese → niet resetten
    } else {
      // Hold niet gestart, óf significante dip → reset timer
      legionella_hold_start_ms_ = 0;
    }

    // Hold-duur bereikt? → pasteurisatie compleet
    if (legionella_hold_start_ms_ != 0 &&
        (in.now_ms - legionella_hold_start_ms_) >= cfg.legionella_hold_ms) {
      legionella_last_done_ms_ = in.now_ms;
      cycle_end_ms_ = in.now_ms;
      transition_(State::IDLE_CV, in.now_ms);
      reset_cycle_flags_();
    }
  }

  void evaluate_faults_(const Inputs &in, const Config &cfg) {
    if (state_ == State::FAULT) return;
    if (in.lockout_active && state_ != State::IDLE_CV) {
      latch_fault_(Fault::LOCKOUT, in.now_ms);
      return;
    }
    if (in.hp_fault_active) {
      latch_fault_(Fault::HP_FAULT, in.now_ms);
      return;
    }

    const bool top_ok = plausible_temp_(in.tank_top_c, cfg);
    // Bottom sensor is optional: NAN means absent (not installed), not faulty.
    const bool bottom_ok = std::isnan(in.tank_bottom_c) || plausible_temp_(in.tank_bottom_c, cfg);
    if (!top_ok || !bottom_ok) {
      latch_fault_(Fault::SENSOR_IMPLAUSIBLE, in.now_ms);
      return;
    }

    // Only check stratification when a real bottom reading is available.
    // Threshold is 12°C to prevent false faults from a fallback value stuck above
    // a cooling tank-top sensor (e.g. fallback=45°C, top cooling to 38°C → Δ7°C).
    if (!std::isnan(in.tank_bottom_c) && in.tank_bottom_c > in.tank_top_c + 12.0f) {
      latch_fault_(Fault::SENSOR_IMPLAUSIBLE, in.now_ms);
      return;
    }

    const bool coil_in_ok = std::isnan(in.coil_in_c) || plausible_temp_(in.coil_in_c, cfg);
    const bool coil_out_ok = std::isnan(in.coil_out_c) || plausible_temp_(in.coil_out_c, cfg);
    if (!coil_in_ok || !coil_out_ok) {
      latch_fault_(Fault::SENSOR_IMPLAUSIBLE, in.now_ms);
      return;
    }

    if (!std::isnan(in.coil_in_c) && !std::isnan(in.coil_out_c) &&
        std::fabs(in.coil_in_c - in.coil_out_c) > 40.0f) {
      latch_fault_(Fault::SENSOR_IMPLAUSIBLE, in.now_ms);
      return;
    }
  }

  void latch_fault_(Fault f, uint32_t now_ms) {
    fault_ = f;
    fault_latched_ = true;
    transition_(State::FAULT, now_ms);
    active_legionella_ = false;
    active_solar_boost_ = false;
    hp_phase_done_ = false;
    legionella_hold_start_ms_ = 0;
  }

  void reset_cycle_flags_() {
    active_legionella_ = false;
    active_solar_boost_ = false;
    active_max_boost_ = false;
    hp_phase_done_ = false;
    boost_hp_assist_active_ = false;
    legionella_hold_start_ms_ = 0;
    valve_retry_count_ = 0;
  }
};

}  // namespace oq_dhw
