#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

namespace esphome::innova_airleaf {

// Registernummers uit Innova N273025C rev.01. Het nummer in de tabel is direct het
// Modbus-PDU-adres (geen -1-offset): jjdejong/esphome-airleaf en lorenzo93/homeassistant-innova
// lezen register 0 als luchttemperatuur en schrijven 231 als setpoint.
static constexpr uint16_t REG_PRG = 201;  // R/W flags: bit0-2 programma, bit4 LOCK, bit7 Stby, bit8-15 gereserveerd
static constexpr uint16_t REG_SP = 231;   // R/W absoluut setpoint, x0.1 °C
static constexpr uint16_t REG_MAN = 233;  // R/W seizoen: 0 = auto, 3 = winter (verwarmen), 5 = zomer (koelen)

static constexpr uint16_t PRG_PROGRAM_MASK = 0x0007;
static constexpr uint16_t PRG_STANDBY = 0x0080;

static constexpr uint16_t MAN_AUTO = 0;
static constexpr uint16_t MAN_HEAT = 3;
static constexpr uint16_t MAN_COOL = 5;

/// Climate-entiteit die de toestand afleidt uit modbus_controller-sensoren en alleen schrijft
/// wanneer Home Assistant iets verandert. Er is geen eigen regeling: de ESE645II/INN-FR-B32
/// doet de PI-regeling van de ventilator zelf.
class InnovaAirleafClimate : public climate::Climate, public Component {
 public:
  void set_air_temperature_sensor(sensor::Sensor *s) { this->air_temperature_ = s; }
  void set_setpoint_number(number::Number *n) { this->setpoint_ = n; }
  void set_program_number(number::Number *n) { this->program_ = n; }
  void set_season_number(number::Number *n) { this->season_ = n; }
  void set_fan_speed_sensor(sensor::Sensor *s) { this->fan_speed_ = s; }
  void set_online_sensor(binary_sensor::BinarySensor *s) { this->online_ = s; }
  void set_setpoint_limits(float min_sp, float max_sp) {
    this->min_setpoint_ = min_sp;
    this->max_setpoint_ = max_sp;
  }
  void set_write_hold_time(uint32_t ms) { this->write_hold_ms_ = ms; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  climate::ClimateTraits traits() override;

 protected:
  void control(const climate::ClimateCall &call) override;

  bool is_online_() const;
  bool hold_active_() const;
  void refresh_();
  void write_register_(number::Number *target, uint16_t reg, float value, float previous);

  sensor::Sensor *air_temperature_{nullptr};
  // Registers 231/201/233 als modbus_controller-numbers: lezen via state, schrijven via make_call().
  number::Number *setpoint_{nullptr};
  number::Number *program_{nullptr};
  number::Number *season_{nullptr};
  sensor::Sensor *fan_speed_{nullptr};
  binary_sensor::BinarySensor *online_{nullptr};

  float min_setpoint_{16.0f};
  float max_setpoint_{28.0f};
  uint32_t write_hold_ms_{20000};

  // Laatst bekende registerwaarden; -1 = nog niet gelezen. PRG wordt alleen via
  // read-modify-write geschreven, dus zonder geldige leeswaarde schrijven we niet.
  int32_t prg_{-1};
  int32_t man_{-1};
  uint32_t hold_until_{0};
  bool hold_set_{false};
};

}  // namespace esphome::innova_airleaf
