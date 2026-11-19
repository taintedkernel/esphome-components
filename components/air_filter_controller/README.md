# Air Filter Controller

Controls an air filter fan with a PWM output, using a TM1638 board
(8-digit 7-segment display, 8 bi-color LEDs, 8 buttons) as the local interface.

- Buttons 1-8 set the fan to a preset level (button 1 = off). Speed changes ramp
  smoothly; pressing another button mid-ramp redirects to the new level.
- Long-pressing button 1 blanks the display; long-pressing any other button
  enables auto mode (not yet implemented beyond the display indicator).
- The display shows the PM reading on the left and the fan speed in percent on
  the right (`A` marks auto mode).

Requires the Arduino framework. The TM1638plus and AceButton libraries are
added automatically.

```yaml
external_components:
  - source: github://taintedkernel/esphome-components@main
    components: [air_filter_controller]

api:
  actions:
    - action: set_fan_speed
      variables:
        speed: float  # 0.0 - 1.0
      then:
        - lambda: id(garage_afc)->set_fan_speed(speed);

air_filter_controller:
  id: garage_afc
  strobe_pin: GPIO15
  clk_pin: GPIO32
  dio_pin: GPIO14
  high_freq: true                    # optional, default true
  pwm_id: air_filter_fan_pwm_output  # any float output
  pm_sensor: garage_pm_1_0um         # optional, shown on the display
  fan_speed:                         # optional, reports the fan speed in %
    name: "Garage Air Filter Fan Speed"

output:
  - platform: ledc
    id: air_filter_fan_pwm_output
    pin: 21
    frequency: 4200Hz
```