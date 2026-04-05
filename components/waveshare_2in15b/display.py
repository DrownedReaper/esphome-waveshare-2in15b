import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import display, spi
from esphome.const import (
    CONF_BUSY_PIN,
    CONF_DC_PIN,
    CONF_ID,
    CONF_LAMBDA,
    CONF_RESET_PIN,
)

DEPENDENCIES = ["spi"]
AUTO_LOAD = ["display"]

CONF_ROTATION = "rotation"

waveshare_2in15b_ns = cg.esphome_ns.namespace("waveshare_2in15b")

WaveshareEPaper2in15B = waveshare_2in15b_ns.class_(
    "WaveshareEPaper2in15B",
    display.DisplayBuffer,
    spi.SPIDevice,
    cg.PollingComponent,
)

DisplayRotation = display.display_ns.enum("DisplayRotation")

ROTATIONS = {
    0:   DisplayRotation.DISPLAY_ROTATION_0_DEGREES,
    90:  DisplayRotation.DISPLAY_ROTATION_90_DEGREES,
    180: DisplayRotation.DISPLAY_ROTATION_180_DEGREES,
    270: DisplayRotation.DISPLAY_ROTATION_270_DEGREES,
}

CONFIG_SCHEMA = (
    display.BASIC_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(WaveshareEPaper2in15B),
            cv.Required(CONF_DC_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_ROTATION, default=0): cv.one_of(0, 90, 180, 270, int=True),
        }
    )
    .extend(cv.polling_component_schema("300s"))
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    await spi.register_spi_device(var, config)

    dc = await cg.gpio_pin_expression(config[CONF_DC_PIN])
    cg.add(var.set_dc_pin(dc))

    if CONF_RESET_PIN in config:
        rst = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(rst))

    if CONF_BUSY_PIN in config:
        busy = await cg.gpio_pin_expression(config[CONF_BUSY_PIN])
        cg.add(var.set_busy_pin(busy))

    rotation = config[CONF_ROTATION]
    cg.add(var.set_rotation(ROTATIONS[rotation]))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA],
            [(display.DisplayRef, "it")],
            return_type=cg.void,
        )
        cg.add(var.set_writer(lambda_))
