import esphome.codegen as cg
from esphome.components import binary_sensor, climate, number, sensor
import esphome.config_validation as cv

# Schrijven loopt via modbus_controller-number-entiteiten (registers 201/231/233), zodat deze
# component alleen de publieke Number/Sensor/Climate-API gebruikt en niet de interne
# modbus_controller-commandowachtrij.
DEPENDENCIES = ["modbus_controller", "number"]

CONF_AIR_TEMPERATURE = "air_temperature"
CONF_SETPOINT = "setpoint"
CONF_PROGRAM_REGISTER = "program_register"
CONF_SEASON_REGISTER = "season_register"
CONF_FAN_SPEED = "fan_speed"
CONF_ONLINE = "online"
CONF_MIN_SETPOINT = "min_setpoint"
CONF_MAX_SETPOINT = "max_setpoint"
CONF_WRITE_HOLD_TIME = "write_hold_time"

innova_ns = cg.esphome_ns.namespace("innova_airleaf")
InnovaAirleafClimate = innova_ns.class_(
    "InnovaAirleafClimate", climate.Climate, cg.Component
)

CONFIG_SCHEMA = (
    climate.climate_schema(InnovaAirleafClimate)
    .extend(
        {
            cv.Required(CONF_AIR_TEMPERATURE): cv.use_id(sensor.Sensor),
            cv.Required(CONF_SETPOINT): cv.use_id(number.Number),
            cv.Required(CONF_PROGRAM_REGISTER): cv.use_id(number.Number),
            cv.Required(CONF_SEASON_REGISTER): cv.use_id(number.Number),
            cv.Optional(CONF_FAN_SPEED): cv.use_id(sensor.Sensor),
            cv.Required(CONF_ONLINE): cv.use_id(binary_sensor.BinarySensor),
            cv.Optional(CONF_MIN_SETPOINT, default=16.0): cv.float_range(5.0, 40.0),
            cv.Optional(CONF_MAX_SETPOINT, default=28.0): cv.float_range(5.0, 40.0),
            cv.Optional(
                CONF_WRITE_HOLD_TIME, default="20s"
            ): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)

    cg.add(var.set_air_temperature_sensor(await cg.get_variable(config[CONF_AIR_TEMPERATURE])))
    cg.add(var.set_setpoint_number(await cg.get_variable(config[CONF_SETPOINT])))
    cg.add(var.set_program_number(await cg.get_variable(config[CONF_PROGRAM_REGISTER])))
    cg.add(var.set_season_number(await cg.get_variable(config[CONF_SEASON_REGISTER])))
    cg.add(var.set_online_sensor(await cg.get_variable(config[CONF_ONLINE])))
    if CONF_FAN_SPEED in config:
        cg.add(var.set_fan_speed_sensor(await cg.get_variable(config[CONF_FAN_SPEED])))
    cg.add(var.set_setpoint_limits(config[CONF_MIN_SETPOINT], config[CONF_MAX_SETPOINT]))
    cg.add(var.set_write_hold_time(config[CONF_WRITE_HOLD_TIME]))
