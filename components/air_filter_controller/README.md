```yaml
# Example configuration:

esphome:
  on_boot:
   then:
    - output.turn_off: air_filter_fan_pwm_output

api:
  actions:
    - action: set_fan_speed
      variables:
        speed: float
      then:
        - lambda: id(garage_afc)->on_update_fan_speed(speed);

# Our external component
# Interfaces with a TM1638 7-segment LCD + 8 buttons
# Sets the PWM duty cycle in the LEDC output component below
air_filter_controller:
  id: garage_afc
  strobe_pin: GPIO15
  clk_pin: GPIO32
  dio_pin: GPIO14
  high_freq: true
  pwm_id: air_filter_fan_pwm_output
  pm_sensor: garage_pm_1_0um
  fan_speed:
    id: garage_afc_fan_speed
    name: "Garage Air Filter Fan Speed"

# PWM output to control the air filter fan
output:
  - platform: ledc
    id: air_filter_fan_pwm_output
    pin: 21
    frequency: 4200Hz
```
