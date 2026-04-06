import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, spi
from esphome import pins
from esphome.const import (
    CONF_ID, CONF_DC_PIN, CONF_BUSY_PIN, CONF_RESET_PIN,
    CONF_UPDATE_INTERVAL, CONF_LAMBDA, CONF_ROTATION,
)

DEPENDENCIES = ["spi"]
AUTO_LOAD = ["display"]

crowpanel_ns = cg.esphome_ns.namespace("crowpanel_579")
CrowPanel579 = crowpanel_ns.class_(
    "CrowPanel579",
    display.DisplayBuffer,
    spi.SPIDevice,
    cg.PollingComponent,
)

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(CrowPanel579),
            cv.Required(CONF_DC_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
            cv.Required(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional("power_pin"): pins.gpio_output_pin_schema,
            cv.Optional(CONF_UPDATE_INTERVAL, default="60s"): cv.update_interval,
        }
    ).extend(spi.spi_device_schema(cs_pin_required=True)),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    await spi.register_spi_device(var, config)

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA],
            [(display.Display.operator("ref"), "it")],
            return_type=cg.void,
        )
        cg.add(var.set_writer(lambda_))

    if CONF_ROTATION in config:
        cg.add(var.set_rotation(config[CONF_ROTATION]))

    dc_pin = await cg.gpio_pin_expression(config[CONF_DC_PIN])
    cg.add(var.set_dc_pin(dc_pin))

    busy_pin = await cg.gpio_pin_expression(config[CONF_BUSY_PIN])
    cg.add(var.set_busy_pin(busy_pin))

    reset_pin = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
    cg.add(var.set_reset_pin(reset_pin))

    if "power_pin" in config:
        power_pin = await cg.gpio_pin_expression(config["power_pin"])
        cg.add(var.set_power_pin(power_pin))

    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
