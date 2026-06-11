import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_BRIGHTNESS, CONF_ID

# Indicateur d'état type `status_led` mais pour une LED ADRESSABLE (WS2812).
# Le core ESPHome ne fournit que `status_led` (GPIO on/off) — rien pour une
# LED couleur. Ce composant lit App.get_app_state() (mêmes bits que le
# `status_led` natif) et pilote une light adressable existante en couleur :
#   OK -> fixe, WARNING -> clignotement lent, ERROR -> clignotement rapide.

DEPENDENCIES = ['light']
MULTI_CONF = True

CONF_LIGHT_ID = 'light_id'
CONF_OK_COLOR = 'ok_color'
CONF_WARNING_COLOR = 'warning_color'
CONF_ERROR_COLOR = 'error_color'

status_led_rgb_ns = cg.esphome_ns.namespace('status_led_rgb')
StatusLEDRGB = status_led_rgb_ns.class_('StatusLEDRGB', cg.Component)


def color(value):
    """Une couleur = liste [R, G, B] avec chaque canal 0-255."""
    value = cv.All(
        cv.ensure_list(cv.int_range(min=0, max=255)),
        cv.Length(min=3, max=3),
    )(value)
    return value


CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(StatusLEDRGB),
    # La light adressable (ex: esp32_rmt_led_strip) à piloter.
    cv.Required(CONF_LIGHT_ID): cv.use_id(light.LightState),
    cv.Optional(CONF_OK_COLOR, default=[0, 255, 0]): color,        # vert
    cv.Optional(CONF_WARNING_COLOR, default=[255, 130, 0]): color,  # orange
    cv.Optional(CONF_ERROR_COLOR, default=[255, 0, 0]): color,      # rouge
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
