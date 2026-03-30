import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import display, spi
from esphome import pins
from esphome.const import CONF_ID

CONF_DC_PIN = "dc_pin"
CONF_RESET_PIN = "reset_pin"
CONF_BUSY_PIN = "busy_pin"

waveshare_ns = cg.esphome_ns.namespace("waveshare")
Waveshare2in15B = waveshare_ns.class_(
    "Waveshare2in15B",
    display.DisplayBuffer,
    spi.SPIDevice,
)

CONFIG_SCHEMA = display.FULL_DISPLAY_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(Waveshare2in15B),
        cv.Optional(CONF_DC_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
    }
).extend(
    spi.spi_device_schema()
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await display.register_display(var, config)
    await spi.register_spi_device(var, config)

    if CONF_DC_PIN in config:
        pin = await pins.gpio_output_pin_expression(config[CONF_DC_PIN])
        cg.add(var.set_dc_pin(pin))

    if CONF_RESET_PIN in config:
        pin = await pins.gpio_output_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(pin))

    if CONF_BUSY_PIN in config:
        pin = await pins.gpio_input_pin_expression(config[CONF_BUSY_PIN])
        cg.add(var.set_busy_pin(pin))
