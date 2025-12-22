import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID
from esphome import pins

DEPENDENCIES = ['uart']
MULTI_CONF = False

CONF_HORMANN_HCP1_ID = 'hormann_hcp1_id'
CONF_DE_PIN = 'de_pin'
CONF_RE_PIN = 're_pin'

hormann_hcp1_ns = cg.esphome_ns.namespace('hormann_hcp1')
HormannHCP1Component = hormann_hcp1_ns.class_('HormannHCP1Component', cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HormannHCP1Component),
    cv.Optional(CONF_DE_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_RE_PIN): pins.gpio_output_pin_schema,
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    if CONF_DE_PIN in config:
        de_pin = await cg.gpio_pin_expression(config[CONF_DE_PIN])
        cg.add(var.set_de_pin(de_pin))
    if CONF_RE_PIN in config:
        re_pin = await cg.gpio_pin_expression(config[CONF_RE_PIN])
        cg.add(var.set_re_pin(re_pin))
