#pragma once

#include "esphome/components/output/float_output.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include <AceButton.h>
#include <TM1638plus.h>

namespace esphome::air_filter_controller {

static constexpr uint8_t NUM_BUTTONS = 8;

/// AceButton reads each button through ButtonConfig::readButton(). The TM1638
/// returns all eight buttons in one serial read, so the controller reads them
/// once per loop() and stores the bitmask here for AceButton to consume.
class TM1638ButtonConfig : public ace_button::ButtonConfig {
 public:
  void set_state(uint8_t state) { this->state_ = state; }
  int readButton(uint8_t pin) override { return (this->state_ >> pin) & 0x01; }

 protected:
  uint8_t state_{0};
};

/// Drives an air filter fan through a float output (PWM), with a TM1638
/// board for the 7-segment display, 8 bi-color LEDs and 8 buttons.
///
/// Button N sets the fan to level N (button 0 = off). Speed changes ramp at a
/// fixed rate; a new target while ramping redirects the ramp from the current
/// level instead of starting a second one.
class AirFilterController : public Component, public ace_button::IEventHandler {
 public:
  AirFilterController(
		InternalGPIOPin *strobe_pin,
		InternalGPIOPin *clock_pin,
		InternalGPIOPin *dio_pin,
		bool high_freq
	);

  void set_output(output::FloatOutput *output) { this->output_ = output; }
  void set_pm_sensor(sensor::Sensor *sensor) { this->pm_sensor_ = sensor; }
  void set_fan_speed_sensor(sensor::Sensor *sensor) {
		this->fan_speed_sensor_ = sensor;
	}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
		return setup_priority::PROCESSOR;
	}

  /// Ramp the fan to an arbitrary level in [0.0, 1.0].
  void set_fan_speed(float level);
  /// Ramp the fan to one of the preset button levels (0-7).
  void set_fan_level_index(uint8_t index);

  float get_fan_speed() const { return this->level_; }
  bool is_auto_mode() const { return this->auto_mode_; }

  // ace_button::IEventHandler (name fixed by the AceButton API)
  void handleEvent(
		ace_button::AceButton *button,
		uint8_t event_type,
		uint8_t button_state
	) override;

 protected:
  void finish_init_();

  void start_ramp_(float target);
  void ramp_step_();
  void apply_level_(float level);
  void publish_fan_speed_();

  void on_click_(uint8_t button);
  void on_long_press_(uint8_t button);
  void on_pm_state_(float pm);

  void set_led_(uint8_t index, uint8_t color);
  void clear_leds_();
  void start_led_flash_(uint8_t index);
  void led_flash_step_();
  void update_display_();

  InternalGPIOPin *strobe_pin_;
  InternalGPIOPin *clock_pin_;
  InternalGPIOPin *dio_pin_;
  bool high_freq_;

  TM1638plus tm_;
  TM1638ButtonConfig button_config_;
  ace_button::AceButton buttons_[NUM_BUTTONS];
  uint8_t leds_[NUM_BUTTONS]{};

  output::FloatOutput *output_{nullptr};
  sensor::Sensor *pm_sensor_{nullptr};
  sensor::Sensor *fan_speed_sensor_{nullptr};

  float level_{0.0f};         ///< Level currently applied to the output.
  float target_level_{0.0f};  ///< Level the active ramp is heading to.
  float pm_value_{NAN};
  bool ready_{false};
  bool auto_mode_{false};

  uint8_t flash_led_{0};
  uint8_t flash_remaining_{0};

  char display_text_[9]{};  ///< Last text sent, to skip redundant writes.
};

}  // namespace esphome::air_filter_controller