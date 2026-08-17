import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import modbus_controller, time
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID

AUTO_LOAD = ["time", "web_server_base"]
# openquatt_web_auth is bewust niet overgenomen; dat component hangt aan de
# webapp van upstream die in deze fork niet meegebouwd wordt.
DEPENDENCIES = [
    "modbus_controller",
    "psram",
    "web_server",
]
MULTI_CONF = True

CONF_CLOCK = "clock"
CONF_CONTROLLER = "controller"
CONF_DEVICE_ADDRESS = "device_address"
CONF_HP_INDEX = "hp_index"

openquatt_odu_eeprom_dump_ns = cg.esphome_ns.namespace("openquatt_odu_eeprom_dump")
OpenQuattOduEepromDump = openquatt_odu_eeprom_dump_ns.class_(
    "OpenQuattOduEepromDump", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattOduEepromDump),
        cv.Required(CONF_CONTROLLER): cv.use_id(modbus_controller.ModbusController),
        cv.Required(CONF_CLOCK): cv.use_id(time.RealTimeClock),
        cv.Required(CONF_HP_INDEX): cv.int_range(min=1, max=2),
        cv.Required(CONF_DEVICE_ADDRESS): cv.int_range(min=1, max=247),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    include_builtin_idf_component("esp_http_server")

    cg.add_global(openquatt_odu_eeprom_dump_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    controller = await cg.get_variable(config[CONF_CONTROLLER])
    clock = await cg.get_variable(config[CONF_CLOCK])
    cg.add(var.set_controller(controller))
    cg.add(var.set_clock(clock))
    cg.add(var.set_hp_index(config[CONF_HP_INDEX]))
    cg.add(var.set_device_address(config[CONF_DEVICE_ADDRESS]))
