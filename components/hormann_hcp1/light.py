import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, output
from esphome.const import CONF_ID, CONF_OUTPUT_ID

from . import hormann_hcp1_ns, HormannHCP1Component, CONF_HORMANN_HCP1_ID

DEPENDENCIES = ['hormann_hcp1']

HormannLight = hormann_hcp1_ns.class_('HormannLight', light.LightOutput, cg.Component)

CONFIG_SCHEMA = light.BINARY_LIGHT_SCHEMA.extend({
    cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(HormannLight),
    cv.GenerateID(CONF_HORMANN_HCP1_ID): cv.use_id(HormannHCP1Component),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)
    
    parent = await cg.get_variable(config[CONF_HORMANN_HCP1_ID])
    cg.add(var.set_parent(parent))
