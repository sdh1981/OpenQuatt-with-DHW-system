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
  float boost_target_c = 56.0f;
  float legionella_target_c = 61.0f;
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

  uint8_t valve_retries = 1;
  bool enable_boost_after_hp = true;
};

struct Inputs {
  uint32_t now_ms = 0;

  float tank_top_c = NAN;
  float tank_bottom_c = NAN;
  float coil_in_c = NAN;
  float coil_out_c = NAN;

  bool valve_feedback_valid = false;
  bool valve_feedback_cv = true;  // true => valve reports CV position

  bool flow_valid = false;
  float flow_lph = NAN;

  bool hp_fault_active = false;
  bool lockout_active = false;

  bool solar_boost_request = false;
  bool legionella_force_request = false;
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
        } else if (should_start_regular_dhw_(in, cfg)) {
          active_legionella_ = false;
          active_solar_boost_ = false;
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
          } else if (active_solar_boost_) {
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

      case State::DHW_HEAT_PUMP:
        if (in.tank_top_c >= cfg.hp_stop_top_c || elapsed_in_state_(in.now_ms) >= cfg.hp_max_runtime_ms) {
          if (cfg.enable_boost_after_hp && in.tank_top_c < cfg.boost_target_c) {
            transition_(State::DHW_BOOST, in.now_ms);
          } else {
            transition_(State::IDLE_CV, in.now_ms);
            reset_cycle_flags_();
          }
        }
        break;

      case State::DHW_BOOST:
        if (in.tank_top_c >= cfg.boost_target_c) {
          transition_(State::IDLE_CV, in.now_ms);
          reset_cycle_flags_();
        } else if (elapsed_in_state_(in.now_ms) >= cfg.boost_max_runtime_ms) {
          latch_fault_(Fault::TIMEOUT, in.now_ms);
        }
        break;

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

    out.valve_to_boiler = (state_ != State::IDLE_CV);
    out.block_cv_priority = (state_ != State::IDLE_CV);

    if (state_ == State::DHW_HEAT_PUMP) {
      out.hp_dhw_request = true;
      out.target_flow_temp_c = cfg.hp_target_flow_c;
    } else if (state_ == State::LEGIONELLA && !hp_phase_done_) {
      out.hp_dhw_request = true;
      out.target_flow_temp_c = cfg.hp_target_flow_c;
    }

    const bool valve_confirms_boiler = valve_is_boiler_(in);
    const float element_target_c = (state_ == State::LEGIONELLA) ? cfg.legionella_target_c : cfg.boost_target_c;
    const bool element_state_allowed = (state_ == State::DHW_BOOST || state_ == State::LEGIONELLA);

    out.element_on =
        element_state_allowed &&
        valve_confirms_boiler &&
        !in.lockout_active &&
        !in.hp_fault_active &&
        in.tank_top_c < element_target_c;

    if ((state_ == State::DHW_HEAT_PUMP || state_ == State::DHW_BOOST || state_ == State::LEGIONELLA) &&
        in.valve_feedback_valid && in.valve_feedback_cv) {
      if (valve_mismatch_since_ms_ == 0) valve_mismatch_since_ms_ = in.now_ms;
      if ((in.now_ms - valve_mismatch_since_ms_) >= cfg.valve_mismatch_fault_ms) {
        latch_fault_(Fault::VALVE_MISMATCH, in.now_ms);
      }
    } else {
      valve_mismatch_since_ms_ = 0;
    }

    if (state_ == State::DHW_HEAT_PUMP || (state_ == State::LEGIONELLA && !hp_phase_done_)) {
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

  uint8_t valve_retry_count_ = 0;
  bool active_legionella_ = false;
  bool active_solar_boost_ = false;
  bool hp_phase_done_ = false;
  bool fault_latched_ = false;

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
    return !fault_latched_ &&
           !in.lockout_active &&
           !in.hp_fault_active &&
           plausible_temp_(in.tank_top_c, cfg) &&
           (in.tank_top_c < cfg.start_top_c);
  }

  bool should_start_solar_boost_(const Inputs &in, const Config &cfg) const {
    return !fault_latched_ &&
           !in.lockout_active &&
           !in.hp_fault_active &&
           in.solar_boost_request &&
           plausible_temp_(in.tank_top_c, cfg) &&
           (in.tank_top_c < cfg.boost_target_c);
  }

  bool should_start_legionella_(const Inputs &in, const Config &cfg) const {
    if (fault_latched_ || in.lockout_active || in.hp_fault_active) return false;
    if (in.legionella_force_request) return true;
    if (cfg.legionella_interval_ms == 0 || in.now_ms < cfg.legionella_interval_ms) return false;
    if (legionella_last_done_ms_ == 0) return true;
    return (in.now_ms - legionella_last_done_ms_) >= cfg.legionella_interval_ms;
  }

  void handle_legionella_(const Inputs &in, const Config &cfg) {
    if (elapsed_in_state_(in.now_ms) >= cfg.legionella_max_runtime_ms) {
      latch_fault_(Fault::TIMEOUT, in.now_ms);
      return;
    }

    if (!hp_phase_done_) {
      if (in.tank_top_c >= cfg.hp_stop_top_c || elapsed_in_state_(in.now_ms) >= cfg.hp_max_runtime_ms) {
        hp_phase_done_ = true;
        legionella_hold_start_ms_ = 0;
      }
      return;
    }

    if (in.tank_top_c >= cfg.legionella_target_c) {
      if (legionella_hold_start_ms_ == 0) legionella_hold_start_ms_ = in.now_ms;
      if ((in.now_ms - legionella_hold_start_ms_) >= cfg.legionella_hold_ms) {
        legionella_last_done_ms_ = in.now_ms;
        transition_(State::IDLE_CV, in.now_ms);
        reset_cycle_flags_();
      }
    } else {
      legionella_hold_start_ms_ = 0;
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
    const bool bottom_ok = plausible_temp_(in.tank_bottom_c, cfg);
    if (!top_ok || !bottom_ok) {
      latch_fault_(Fault::SENSOR_IMPLAUSIBLE, in.now_ms);
      return;
    }

    if (in.tank_bottom_c > in.tank_top_c + 5.0f) {
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
    hp_phase_done_ = false;
    legionella_hold_start_ms_ = 0;
    valve_retry_count_ = 0;
  }
};

}  // namespace oq_dhw
