import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import pins

DEPENDENCIES = []
MULTI_CONF = False
# Platform .cpp files (cover/light/binary_sensor/button) compile unconditionally,
# so their core headers must always be available — even when the hub is used alone
# (e.g. the witness sniffer declares no entities). AUTO_LOAD pulls them in.
AUTO_LOAD = ['binary_sensor', 'button', 'cover', 'light']

CONF_HORMANN_HCP1_ID = 'hormann_hcp1_id'
CONF_UART_NUM = 'uart_num'
CONF_TX_PIN = 'tx_pin'
CONF_RX_PIN = 'rx_pin'
CONF_DE_PIN = 'de_pin'
CONF_RE_PIN = 're_pin'
CONF_SLAVE_ADDR = 'slave_addr'
CONF_MASTER_ADDR = 'master_addr'
CONF_SLAVE_TYPE = 'slave_type'
CONF_AUTO_SCAN = 'auto_scan'
CONF_DE_INVERT = 'de_invert'
CONF_TX_TEST = 'tx_test'
CONF_BUSTASK_TX_TEST = 'bustask_tx_test'
CONF_AB_INVERTED = 'ab_inverted'
CONF_SNIFFER = 'sniffer'
CONF_REPLY_DELAY_US = 'reply_delay_us'

hormann_hcp1_ns = cg.esphome_ns.namespace('hormann_hcp1')
HormannHCP1Component = hormann_hcp1_ns.class_('HormannHCP1Component', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HormannHCP1Component),
    cv.Optional(CONF_UART_NUM, default=1): cv.int_range(min=1, max=2),
    cv.Required(CONF_TX_PIN): pins.internal_gpio_output_pin_number,
    cv.Required(CONF_RX_PIN): pins.internal_gpio_input_pin_number,
    cv.Optional(CONF_DE_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_RE_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_SLAVE_ADDR, default=0x28): cv.hex_uint8_t,
    cv.Optional(CONF_MASTER_ADDR, default=0x80): cv.hex_uint8_t,
    cv.Optional(CONF_SLAVE_TYPE, default=0x14): cv.hex_uint8_t,
    cv.Optional(CONF_AUTO_SCAN, default=False): cv.boolean,
    cv.Optional(CONF_DE_INVERT, default=False): cv.boolean,
    cv.Optional(CONF_TX_TEST, default=False): cv.boolean,
    # Test A: fire the real scan-reply from the bus_task context on an idle timeout.
    cv.Optional(CONF_BUSTASK_TX_TEST, default=False): cv.boolean,
    # A/B polarity (applies to RX and TX together — one differential pair):
    # false/true to force, or 'auto' to detect at boot (default).
    cv.Optional(CONF_AB_INVERTED, default='auto'): cv.Any(cv.boolean, cv.one_of('auto', lower=True)),
    # Bus witness: log only valid, categorized, de-duplicated frames (INFO level).
    cv.Optional(CONF_SNIFFER, default=False): cv.boolean,
    # Micro-delay (us) before sending our reply — tune to land in the master's window.
    cv.Optional(CONF_REPLY_DELAY_US, default=0): cv.int_range(min=0, max=10000),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_uart_num(config[CONF_UART_NUM]))
    cg.add(var.set_tx_pin(config[CONF_TX_PIN]))
    cg.add(var.set_rx_pin(config[CONF_RX_PIN]))
    cg.add(var.set_slave_addr(config[CONF_SLAVE_ADDR]))
    cg.add(var.set_master_addr(config[CONF_MASTER_ADDR]))
    cg.add(var.set_slave_type(config[CONF_SLAVE_TYPE]))
    cg.add(var.set_auto_scan(config[CONF_AUTO_SCAN]))
    cg.add(var.set_de_invert(config[CONF_DE_INVERT]))
    cg.add(var.set_tx_test(config[CONF_TX_TEST]))
    cg.add(var.set_bustask_tx_test(config[CONF_BUSTASK_TX_TEST]))

    # ab_inverted: 0 = off, 1 = on, 2 = auto-detect (applies to RX and TX)
    ab_inv = config[CONF_AB_INVERTED]
    ab_inv_mode = 2 if ab_inv == 'auto' else (1 if ab_inv else 0)
    cg.add(var.set_ab_inverted_mode(ab_inv_mode))

    cg.add(var.set_sniffer(config[CONF_SNIFFER]))
    cg.add(var.set_reply_delay_us(config[CONF_REPLY_DELAY_US]))

    if CONF_DE_PIN in config:
        de_pin = await cg.gpio_pin_expression(config[CONF_DE_PIN])
        cg.add(var.set_de_pin(de_pin))
    if CONF_RE_PIN in config:
        re_pin = await cg.gpio_pin_expression(config[CONF_RE_PIN])
        cg.add(var.set_re_pin(re_pin))
