import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_LIGHT,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_MOVING,
)

from . import hormann_hcp1_ns, HormannHCP1Component, CONF_HORMANN_HCP1_ID

DEPENDENCIES = ['hormann_hcp1']

CONF_LIGHT = 'light'
CONF_ERROR = 'error'
CONF_VENTING = 'venting'
CONF_PREWARN = 'prewarn'

HormannBinarySensor = hormann_hcp1_ns.class_('HormannBinarySensor', binary_sensor.BinarySensor, cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_HORMANN_HCP1_ID): cv.use_id(HormannHCP1Component),
    cv.Optional(CONF_LIGHT): binary_sensor.binary_sensor_schema(
        HormannBinarySensor,
        device_class=DEVICE_CLASS_LIGHT,
    ),
    cv.Optional(CONF_ERROR): binary_sensor.binary_sensor_schema(
        HormannBinarySensor,
        device_class=DEVICE_CLASS_PROBLEM,
    ),
    cv.Optional(CONF_VENTING): binary_sensor.binary_sensor_schema(
        HormannBinarySensor,
    ),
    cv.Optional(CONF_PREWARN): binary_sensor.binary_sensor_schema(
        HormannBinarySensor,
        device_class=DEVICE_CLASS_MOVING,
    ),
})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_HORMANN_HCP1_ID])
    
    if CONF_LIGHT in config:
        conf = config[CONF_LIGHT]
        var = await binary_sensor.new_binary_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_sensor_type(0))  # 0 = light
    
    if CONF_ERROR in config:
        conf = config[CONF_ERROR]
        var = await binary_sensor.new_binary_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_sensor_type(1))  # 1 = error
    
    if CONF_VENTING in config:
        conf = config[CONF_VENTING]
        var = await binary_sensor.new_binary_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_sensor_type(2))  # 2 = venting
    
    if CONF_PREWARN in config:
        conf = config[CONF_PREWARN]
        var = await binary_sensor.new_binary_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_sensor_type(3))  # 3 = prewarn
