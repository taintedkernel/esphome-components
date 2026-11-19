import esphome.codegen as cg
import esphome.config_validation as cv

from esphome import pins
from esphome.components import sensor #, output
from esphome.components.ledc import output as ledcout
from esphome.const import (
    CONF_ID, CONF_FREQUENCY, UNIT_PERCENT, ICON_FAN, STATE_CLASS_MEASUREMENT
)

AUTO_LOAD = ["ledc", "sensor"]

CONF_PM_SENSOR = "pm_sensor"
CONF_FAN_SPEED = "fan_speed"

CONF_STROBE_PIN = "strobe_pin"
CONF_CLOCK_PIN = "clk_pin"
CONF_DIO_PIN = "dio_pin"
CONF_PWM_ID = "pwm_id"
CONF_HIGH_FREQ = "high_freq"
# PWM_FREQ = "4200Hz"

air_filter_controller_ns = cg.esphome_ns.namespace("air_filter_controller")
AirFilterController = air_filter_controller_ns.class_(
    "AirFilterController",
    # output.FloatOutput,
    cg.Component
)


# CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend(
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AirFilterController),
        # cv.GenerateID(CONF_PWM_ID): cv.use_id(
        cv.Required(CONF_PWM_ID): cv.use_id(
            ledcout.LEDCOutput
        ),
        cv.Required(CONF_STROBE_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_CLOCK_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_DIO_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_HIGH_FREQ, default=True): cv.boolean,
        cv.Optional(CONF_PM_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_FAN_SPEED): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            icon=ICON_FAN,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        # cv.Required(CONF_PWM_PIN): pins.gpio_output_pin_schema,
        # cv.Optional(CONF_FREQUENCY, default=PWM_FREQ): cv.frequency,
    }
).extend(cv.COMPONENT_SCHEMA) #.extend(ledc.LEDC_DEVICE_SCHEMA)
 # cv.polling_component_schema("30s"))


async def to_code(config):
    strobe_pin = await cg.gpio_pin_expression(config[CONF_STROBE_PIN])
    clk_pin = await cg.gpio_pin_expression(config[CONF_CLOCK_PIN])
    dio_pin = await cg.gpio_pin_expression(config[CONF_DIO_PIN])
    # pwm_pin = await cg.gpio_pin_expression(config[CONF_PWM_PIN])

    # Declare new component
    var = cg.new_Pvariable(config[CONF_ID], strobe_pin, clk_pin, dio_pin, config[CONF_HIGH_FREQ])
    await cg.register_component(var, config)

    # Set our PWM component
    ledc = await cg.get_variable(config[CONF_PWM_ID])
    cg.add(var.setLEDC(ledc))

    # Set our PM sensor if provided
    if CONF_PM_SENSOR in config:
        sens = await cg.get_variable(config[CONF_PM_SENSOR])
        cg.add(var.set_pm_sensor(sens))

    # Set fan speed sensor if provided
    if CONF_FAN_SPEED in config:
        sens = await sensor.new_sensor(config[CONF_FAN_SPEED])
        cg.add(var.set_fan_speed_sensor(sens))
