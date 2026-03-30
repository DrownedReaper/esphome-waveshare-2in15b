import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import display, spi
from esphome.const import CONF_ID

waveshare_ns = cg.esphome_ns.namespace("waveshare")
Waveshare2in15B = waveshare_ns.class_(
    "Waveshare2in15B",
    display.DisplayBuffer,
    spi.SPIDevice,
)

CONFIG_SCHEMA = display.DISPLAY_SCHEMA.extend(
    spi.spi_device_schema()
).extend(
    {cv.GenerateID(): cv.declare_id(Waveshare2in15B)}
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await display.register_display(var, config)
    await spi.register_spi_device(var, config)