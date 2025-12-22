import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.const import CONF_ID

from . import hormann_hcp1_ns, HormannHCP1Component, CONF_HORMANN_HCP1_ID

DEPENDENCIES = ['hormann_hcp1']

HormannCover = hormann_hcp1_ns.class_('HormannCover', cover.Cover, cg.Component)

CONFIG_SCHEMA = (
    cover.cover_schema(HormannCover)
    .extend({
        cv.GenerateID(CONF_HORMANN_HCP1_ID): cv.use_id(HormannHCP1Component),
    })
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cover.register_cover(var, config)
    
    parent = await cg.get_variable(config[CONF_HORMANN_HCP1_ID])
    cg.add(var.set_parent(parent))
