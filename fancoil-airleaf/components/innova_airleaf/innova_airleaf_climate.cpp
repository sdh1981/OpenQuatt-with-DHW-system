#include "innova_airleaf_climate.h"

#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::innova_airleaf {

static const char *const TAG = "innova_airleaf";

void InnovaAirleafClimate::setup() {
  // Geen restore_state_(): na een herstart schrijft deze component niets naar de fancoil.
  // De toestand komt uitsluitend uit de eerste geslaagde Modbus-reads.
  auto on_change = [this](float) { this->refresh_(); };
  this->air_temperature_->add_on_state_callback(on_change);
  this->setpoint_->add_on_state_callback(on_change);
  this->program_->add_on_state_callback(on_change);
  this->season_->add_on_state_callback(on_change);
  if (this->fan_speed_ != nullptr)
    this->fan_speed_->add_on_state_callback(on_change);
  this->online_->add_on_state_callback([this](bool) { this->refresh_(); });

  this->current_temperature = NAN;
  this->target_temperature = NAN;
  this->mode = climate::CLIMATE_MODE_OFF;
}

void InnovaAirleafClimate::dump_config() {
  LOG_CLIMATE("", "Innova AirLeaf", this);
  ESP_LOGCONFIG(TAG, "  Setpointgrenzen: %.1f .. %.1f °C", this->min_setpoint_, this->max_setpoint_);
  ESP_LOGCONFIG(TAG, "  Vasthouden na schrijven: %u ms", (unsigned) this->write_hold_ms_);
}

climate::ClimateTraits InnovaAirleafClimate::traits() {
  climate::ClimateTraits traits;
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  if (this->fan_speed_ != nullptr)
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_ACTION);
  traits.add_supported_mode(climate::CLIMATE_MODE_OFF);
  traits.add_supported_mode(climate::CLIMATE_MODE_HEAT);
  traits.add_supported_mode(climate::CLIMATE_MODE_COOL);
  // HEAT_COOL = register 233 waarde 0 ("seizoen automatisch"). Staat erin zodat die toestand,
  // als hij op de print staat, eerlijk getoond wordt in plaats van als HEAT.
  traits.add_supported_mode(climate::CLIMATE_MODE_HEAT_COOL);
  // PRG bit0-2: 000 automatisch, 001 stil (MIN), 010 nacht, 011 maximum.
  traits.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO);
  traits.add_supported_fan_mode(climate::CLIMATE_FAN_QUIET);
  traits.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
  traits.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);
  traits.set_visual_min_temperature(this->min_setpoint_);
  traits.set_visual_max_temperature(this->max_setpoint_);
  traits.set_visual_target_temperature_step(0.5f);
  traits.set_visual_current_temperature_step(0.1f);
  return traits;
}

bool InnovaAirleafClimate::is_online_() const { return this->online_->has_state() && this->online_->state; }

bool InnovaAirleafClimate::hold_active_() const {
  return this->hold_set_ && static_cast<int32_t>(millis() - this->hold_until_) < 0;
}

void InnovaAirleafClimate::refresh_() {
  const auto old_mode = this->mode;
  const auto old_fan = this->fan_mode;
  const auto old_action = this->action;
  const float old_cur = this->current_temperature;
  const float old_target = this->target_temperature;

  if (!this->is_online_()) {
    // Communicatieverlies: geen oude waarden laten staan. Temperaturen worden "onbekend" in HA.
    this->current_temperature = NAN;
    this->target_temperature = NAN;
    this->action = climate::CLIMATE_ACTION_IDLE;
    // Na herstel eerst opnieuw lezen: een PRG van vóór de storing mag geen basis zijn voor
    // read-modify-write (de fancoil kan intussen via het toetsenbord of een herstart veranderd zijn).
    this->prg_ = -1;
    this->man_ = -1;
  } else {
    this->current_temperature = this->air_temperature_->state;

    if (!std::isnan(this->program_->state))
      this->prg_ = static_cast<int32_t>(this->program_->state);
    if (!std::isnan(this->season_->state))
      this->man_ = static_cast<int32_t>(this->season_->state);

    // Direct na een schrijfactie kan er nog een leesopdracht met de oude waarde in de
    // wachtrij staan. Die negeren we even voor mode/ventilator/setpoint, anders springt HA terug.
    if (!this->hold_active_()) {
      this->hold_set_ = false;
      this->target_temperature = this->setpoint_->state;

      if (this->prg_ >= 0 && (this->prg_ & PRG_STANDBY)) {
        this->mode = climate::CLIMATE_MODE_OFF;
      } else if (this->man_ == MAN_HEAT) {
        this->mode = climate::CLIMATE_MODE_HEAT;
      } else if (this->man_ == MAN_COOL) {
        this->mode = climate::CLIMATE_MODE_COOL;
      } else if (this->man_ == MAN_AUTO) {
        this->mode = climate::CLIMATE_MODE_HEAT_COOL;
      } else if (this->man_ >= 0) {
        ESP_LOGW(TAG, "[%s] onbekende waarde in register 233: %d", this->get_name().c_str(), (int) this->man_);
      }

      if (this->prg_ >= 0) {
        switch (this->prg_ & PRG_PROGRAM_MASK) {
          case 0:
            this->fan_mode = climate::CLIMATE_FAN_AUTO;
            break;
          case 1:
            this->fan_mode = climate::CLIMATE_FAN_LOW;
            break;
          case 2:
            this->fan_mode = climate::CLIMATE_FAN_QUIET;
            break;
          case 3:
            this->fan_mode = climate::CLIMATE_FAN_HIGH;
            break;
          default:
            ESP_LOGW(TAG, "[%s] onbekend programma in register 201: 0x%04X", this->get_name().c_str(),
                     (unsigned) this->prg_);
            break;
        }
      }
    }

    if (this->fan_speed_ != nullptr) {
      const float rpm = this->fan_speed_->state;
      if (this->mode == climate::CLIMATE_MODE_OFF) {
        this->action = climate::CLIMATE_ACTION_OFF;
      } else if (std::isnan(rpm) || rpm <= 0.0f) {
        this->action = climate::CLIMATE_ACTION_IDLE;
      } else if (this->mode == climate::CLIMATE_MODE_HEAT) {
        this->action = climate::CLIMATE_ACTION_HEATING;
      } else if (this->mode == climate::CLIMATE_MODE_COOL) {
        this->action = climate::CLIMATE_ACTION_COOLING;
      } else {
        this->action = climate::CLIMATE_ACTION_FAN;
      }
    }
  }

  auto same = [](float a, float b) { return (std::isnan(a) && std::isnan(b)) || a == b; };
  if (old_mode == this->mode && old_fan == this->fan_mode && old_action == this->action &&
      same(old_cur, this->current_temperature) && same(old_target, this->target_temperature))
    return;
  this->publish_state();
}

void InnovaAirleafClimate::write_register_(number::Number *target, uint16_t reg, float value, float previous) {
  ESP_LOGI(TAG, "[%s] schrijf register %u = %.1f, was %.1f", this->get_name().c_str(), reg, value, previous);
  target->make_call().set_value(value).perform();
}

void InnovaAirleafClimate::control(const climate::ClimateCall &call) {
  const char *name = this->get_name().c_str();
  if (!this->is_online_()) {
    ESP_LOGW(TAG, "[%s] geen Modbus-verbinding: opdracht niet verstuurd", name);
    this->publish_state();
    return;
  }

  const auto &mode = call.get_mode();
  const auto &fan = call.get_fan_mode();
  const auto &target = call.get_target_temperature();
  bool changed = false;

  // 1. Seizoen (233) eerst, zodat de unit niet even in het oude seizoen aanslaat.
  if (mode.has_value() && *mode != climate::CLIMATE_MODE_OFF) {
    int32_t man = *mode == climate::CLIMATE_MODE_COOL   ? MAN_COOL
                  : *mode == climate::CLIMATE_MODE_HEAT ? MAN_HEAT
                                                        : MAN_AUTO;
    if (man != this->man_) {
      this->write_register_(this->season_, REG_MAN, man, this->man_);
      this->man_ = man;
    }
  }

  // 2. PRG (201): alleen read-modify-write. Bit 8-15 zijn volgens Innova gereserveerd.
  if (mode.has_value() || fan.has_value()) {
    if (this->prg_ < 0) {
      ESP_LOGW(TAG, "[%s] register 201 nog niet gelezen: mode/ventilator niet geschreven", name);
    } else {
      int32_t prg = this->prg_;
      if (mode.has_value()) {
        if (*mode == climate::CLIMATE_MODE_OFF) {
          prg |= PRG_STANDBY;
        } else {
          prg &= ~PRG_STANDBY;
        }
      }
      if (fan.has_value()) {
        uint16_t code;
        switch (*fan) {
          case climate::CLIMATE_FAN_LOW:
            code = 1;
            break;
          case climate::CLIMATE_FAN_QUIET:
            code = 2;
            break;
          case climate::CLIMATE_FAN_HIGH:
            code = 3;
            break;
          default:
            code = 0;
            break;
        }
        prg = (prg & ~PRG_PROGRAM_MASK) | code;
      }
      if (prg != this->prg_) {
        this->write_register_(this->program_, REG_PRG, prg, this->prg_);
        this->prg_ = prg;
      }
      if (mode.has_value())
        this->mode = *mode;
      if (fan.has_value())
        this->fan_mode = *fan;
      changed = true;
    }
  }

  // 3. Setpoint (231): begrensd op de ingestelde grenzen, stap 0,5 °C.
  if (target.has_value()) {
    float sp = std::round(*target * 2.0f) / 2.0f;
    sp = std::max(this->min_setpoint_, std::min(this->max_setpoint_, sp));
    // De number heeft multiply: 10, dus hier in °C schrijven.
    const float old_sp = this->setpoint_->state;
    if (std::isnan(old_sp) || std::lround(sp * 10.0f) != std::lround(old_sp * 10.0f))
      this->write_register_(this->setpoint_, REG_SP, sp, old_sp);
    this->target_temperature = sp;
    changed = true;
  }

  if (changed) {
    this->hold_until_ = millis() + this->write_hold_ms_;
    this->hold_set_ = true;
  }
  this->publish_state();
}

}  // namespace esphome::innova_airleaf
