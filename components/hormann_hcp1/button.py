import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID

from . import hormann_hcp1_ns, HormannHCP1Component, CONF_HORMANN_HCP1_ID

DEPENDENCIES = ['hormann_hcp1']

CONF_ACTION = 'action'

HormannButton = hormann_hcp1_ns.class_('HormannButton', button.Button, cg.Component)

ACTION_OPTIONS = {
    'impulse': 'ACTION_IMPULSE',
    'venting': 'ACTION_VENTING',
    'emergency_stop': 'ACTION_EMERGENCY_STOP',
}

CONFIG_SCHEMA = (
    button.button_schema(HormannButton)
    .extend({
        cv.GenerateID(CONF_HORMANN_HCP1_ID): cv.use_id(HormannHCP1Component),
        cv.Required(CONF_ACTION): cv.enum(ACTION_OPTIONS, lower=True),
    })
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await button.register_button(var, config)
    
    parent = await cg.get_variable(config[CONF_HORMANN_HCP1_ID])
    cg.add(var.set_parent(parent))
    
    action = config[CONF_ACTION]
    cg.add(var.set_action(getattr(hormann_hcp1_ns, ACTION_OPTIONS[action])))
