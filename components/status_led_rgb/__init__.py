import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_BRIGHTNESS, CONF_ID

# A `status_led`-style status indicator, but for an ADDRESSABLE LED (WS2812).
# ESPHome core only provides `status_led` (GPIO on/off) — nothing for a color
# LED. This component reads App.get_app_state() (same bits as the native
# `status_led`) and drives an existing addressable light in color:
#   OK -> solid, WARNING -> slow blink, ERROR -> fast blink.

DEPENDENCIES = ['light']
MULTI_CONF = True

CONF_LIGHT_ID = 'light_id'
CONF_OK_COLOR = 'ok_color'
CONF_WARNING_COLOR = 'warning_color'
CONF_ERROR_COLOR = 'error_color'

status_led_rgb_ns = cg.esphome_ns.namespace('status_led_rgb')
StatusLEDRGB = status_led_rgb_ns.class_('StatusLEDRGB', cg.Component)


def color(value):
    """A color = list [R, G, B], each channel 0-255."""
    value = cv.All(
        cv.ensure_list(cv.int_range(min=0, max=255)),
        cv.Length(min=3, max=3),
    )(value)
    return value


CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(StatusLEDRGB),
    # The addressable light (e.g. esp32_rmt_led_strip) to drive.
    cv.Required(CONF_LIGHT_ID): cv.use_id(light.LightState),
    cv.Optional(CONF_OK_COLOR, default=[0, 255, 0]): color,        # green
    cv.Optional(CONF_WARNING_COLOR, default=[255, 130, 0]): color,  # orange
    cv.Optional(CONF_ERROR_COLOR, default=[255, 0, 0]): color,      # red
    cv.Optional(CONF_BRIGHTNESS, default='30%'): cv.percentage,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    ls = await cg.get_variable(config[CONF_LIGHT_ID])
    cg.add(var.set_light(ls))

    for key, setter in (
        (CONF_OK_COLOR, var.set_ok_color),
        (CONF_WARNING_COLOR, var.set_warning_color),
        (CONF_ERROR_COLOR, var.set_error_color),
    ):
        r, g, b = config[key]
        cg.add(setter(r, g, b))

    cg.add(var.set_brightness(config[CONF_BRIGHTNESS]))
