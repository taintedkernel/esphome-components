from esphome import pins
import esphome.codegen as cg
from esphome.components import output, sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, ICON_FAN, STATE_CLASS_MEASUREMENT, UNIT_PERCENT

CODEOWNERS = ["@taintedkernel"]
AUTO_LOAD = ["sensor"]

CONF_STROBE_PIN = "strobe_pin"
CONF_CLOCK_PIN = "clk_pin"
CONF_DIO_PIN = "dio_pin"
CONF_HIGH_FREQ = "high_freq"
CONF_PWM_ID = "pwm_id"
CONF_PM_SENSOR = "pm_sensor"
CONF_FAN_SPEED = "fan_speed"

air_filter_controller_ns = cg.esphome_ns.namespace("air_filter_controller")
AirFilterController = air_filter_controller_ns.class_(
    "AirFilterController", cg.Component
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AirFilterController),
            cv.Required(CONF_STROBE_PIN): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_CLOCK_PIN): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_DIO_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_HIGH_FREQ, default=True): cv.boolean,
            cv.Required(CONF_PWM_ID): cv.use_id(output.FloatOutput),
            cv.Optional(CONF_PM_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_FAN_SPEED): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                icon=ICON_FAN,
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_arduino,
)


async def to_code(config):
    cg.add_library("gavinlyonsrepo/TM1638plus", "2.2.0")
    cg.add_library("bxparks/AceButton", "1.10.1")

    strobe_pin = await cg.gpio_pin_expression(config[CONF_STROBE_PIN])
    clock_pin = await cg.gpio_pin_expression(config[CONF_CLOCK_PIN])
    dio_pin = await cg.gpio_pin_expression(config[CONF_DIO_PIN])

    var = cg.new_Pvariable(
        config[CONF_ID], strobe_pin, clock_pin, dio_pin, config[CONF_HIGH_FREQ]
    )
    await cg.register_component(var, config)

    out = await cg.get_variable(config[CONF_PWM_ID])
    cg.add(var.set_output(out))

    if CONF_PM_SENSOR in config:
        pm_sensor = await cg.get_variable(config[CONF_PM_SENSOR])
        cg.add(var.set_pm_sensor(pm_sensor))

    if CONF_FAN_SPEED in config:
        fan_speed = await sensor.new_sensor(config[CONF_FAN_SPEED])
        cg.add(var.set_fan_speed_sensor(fan_speed))