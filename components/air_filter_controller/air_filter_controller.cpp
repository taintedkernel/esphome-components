#include "air_filter_controller.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome::air_filter_controller {

static const char *const TAG = "air_filter_controller";

// Scheduler item names (must have static lifetime)
static const char *const RAMP_INTERVAL = "ramp";
static const char *const FLASH_INTERVAL = "led_flash";
static const char *const INIT_TIMEOUT = "init";

// Ramp rate: full scale (0 -> 1) takes 16 s
static constexpr uint32_t RAMP_STEP_MS = 20;
static constexpr float RAMP_RATE_PER_SEC = 0.0625f;
static constexpr float RAMP_STEP_SIZE =
  RAMP_RATE_PER_SEC * RAMP_STEP_MS / 1000.0f;
static constexpr float LEVEL_EPSILON = 0.001f;

static constexpr uint32_t INIT_DELAY_MS = 5000;
static constexpr uint32_t FLASH_STEP_MS = 250;
static constexpr uint8_t FLASH_STEPS = 8;

// LED colors as they appear on this board. The TM1638plus "Model 3" constants
// are the other way round (0x02 = red, 0x01 = green) on this hardware.
static constexpr uint8_t LED_OFF = 0x00;
static constexpr uint8_t LED_RED = 0x01;
static constexpr uint8_t LED_GREEN = 0x02;

// Preset fan levels for buttons 0-7, as 8-bit duty cycle
static constexpr uint8_t FAN_LEVELS[NUM_BUTTONS] = {
  0, 48, 96, 140, 176, 192, 224, 255
};

AirFilterController::AirFilterController(
  InternalGPIOPin *strobe_pin,
  InternalGPIOPin *clock_pin,
  InternalGPIOPin *dio_pin,
  bool high_freq)
    : strobe_pin_(strobe_pin),
      clock_pin_(clock_pin),
      dio_pin_(dio_pin),
      high_freq_(high_freq),
      tm_(
        strobe_pin->get_pin(),
        clock_pin->get_pin(),
        dio_pin->get_pin(),
        high_freq
      ) {
  this->button_config_.setIEventHandler(this);
  this->button_config_.setFeature(ace_button::ButtonConfig::kFeatureClick);
  this->button_config_.setFeature(ace_button::ButtonConfig::kFeatureLongPress);
  this->button_config_.setFeature(
    ace_button::ButtonConfig::kFeatureSuppressAfterClick
  );
  this->button_config_.setFeature(
    ace_button::ButtonConfig::kFeatureSuppressAfterLongPress
  );
  this->button_config_.setClickDelay(400);

  // Buttons read 1 when pressed, so the released state is LOW (0)
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    this->buttons_[i].init(&this->button_config_, i, LOW, i);
  }
}

void AirFilterController::setup() {
  // Make sure the fan is off while we initialize
  this->apply_level_(0.0f);

  this->tm_.displayBegin();
  this->tm_.displayText("- init -");

  if (this->pm_sensor_ != nullptr) {
    this->pm_sensor_->add_on_state_callback(
      [this](float pm) { this->on_pm_state_(pm); }
    );
  }

  this->set_timeout(
    INIT_TIMEOUT, INIT_DELAY_MS, [this]() { this->finish_init_(); }
  );
}

void AirFilterController::finish_init_() {
  ESP_LOGI(TAG, "Initialization complete");
  this->ready_ = true;
  this->display_text_[0] = '\0';  // force a redraw over "- init -"
  this->set_led_(0, LED_GREEN);
  this->update_display_();
  this->publish_fan_speed_();
}

void AirFilterController::loop() {
  if (!this->ready_)
    return;

  // One serial read for all eight buttons, then let AceButton process them
  this->button_config_.set_state(this->tm_.readButtons());
  for (auto &button : this->buttons_)
    button.check();
}

void AirFilterController::dump_config() {
  ESP_LOGCONFIG(TAG, "Air Filter Controller:");
  LOG_PIN("  Strobe Pin: ", this->strobe_pin_);
  LOG_PIN("  Clock Pin: ", this->clock_pin_);
  LOG_PIN("  DIO Pin: ", this->dio_pin_);
  ESP_LOGCONFIG(TAG, "  High Frequency: %s", YESNO(this->high_freq_));
  ESP_LOGCONFIG(TAG, "  Ramp Rate: %.4f/s", RAMP_RATE_PER_SEC);
  ESP_LOGCONFIG(TAG, "  PM Sensor: %s",
    this->pm_sensor_ != nullptr ? "configured" : "none"
  );
  LOG_SENSOR("  ", "Fan Speed", this->fan_speed_sensor_);
}

// ---- Fan speed -------------------------------------------------------------

void AirFilterController::set_fan_speed(float level) {
  if (std::isnan(level)) {
    ESP_LOGW(TAG, "Ignoring NaN fan speed");
    return;
  }
  if (!this->ready_) {
    ESP_LOGW(TAG, "Ignoring fan speed request during initialization");
    return;
  }
  this->start_ramp_(clamp(level, 0.0f, 1.0f));
}

void AirFilterController::set_fan_level_index(uint8_t index) {
  if (index >= NUM_BUTTONS) {
    ESP_LOGW(TAG, "Invalid fan level index %u", index);
    return;
  }
  this->set_fan_speed(FAN_LEVELS[index] / 255.0f);
}

void AirFilterController::start_ramp_(float target) {
  this->target_level_ = target;

  if (std::fabs(target - this->level_) < LEVEL_EPSILON) {
    // Already there (or close enough): stop any ramp in progress
    this->cancel_interval(RAMP_INTERVAL);
    this->apply_level_(target);
    this->publish_fan_speed_();
    return;
  }

  ESP_LOGI(TAG, "Ramping fan from %.3f to %.3f", this->level_, target);
  // Re-using the name replaces any ramp already running, so there is only ever
  // one ramp and it always heads for the latest target.
  this->set_interval(
    RAMP_INTERVAL, RAMP_STEP_MS, [this]() { this->ramp_step_(); }
  );
}

void AirFilterController::ramp_step_() {
  float remaining = this->target_level_ - this->level_;

  if (std::fabs(remaining) <= RAMP_STEP_SIZE) {
    this->cancel_interval(RAMP_INTERVAL);
    this->apply_level_(this->target_level_);
    ESP_LOGI(TAG, "Fan at %.3f", this->level_);
    this->publish_fan_speed_();
    return;
  }

  this->apply_level_(this->level_ + std::copysign(RAMP_STEP_SIZE, remaining));
}

void AirFilterController::apply_level_(float level) {
  this->level_ = level;
  if (this->output_ != nullptr)
    this->output_->set_level(level);
  if (this->ready_)
    this->update_display_();
}

void AirFilterController::publish_fan_speed_() {
  if (this->fan_speed_sensor_ != nullptr)
    this->fan_speed_sensor_->publish_state(this->level_ * 100.0f);
}

// ---- Buttons ---------------------------------------------------------------

void AirFilterController::handleEvent(
  ace_button::AceButton *button, uint8_t event_type, uint8_t button_state
) {
  uint8_t index = button->getPin();

  switch (event_type) {
    case ace_button::AceButton::kEventClicked:
      this->on_click_(index);
      break;
    case ace_button::AceButton::kEventLongPressed:
      this->on_long_press_(index);
      break;
    case ace_button::AceButton::kEventPressed:
    case ace_button::AceButton::kEventReleased:
      break;
    default:
      ESP_LOGV(TAG, "Unhandled event %u on button %u", event_type, index);
      break;
  }
}

void AirFilterController::on_click_(uint8_t button) {
  this->auto_mode_ = false;
  this->cancel_interval(FLASH_INTERVAL);
  this->clear_leds_();
  this->set_led_(button, LED_RED);
  this->set_fan_level_index(button);
  this->update_display_();
}

void AirFilterController::on_long_press_(uint8_t button) {
  if (button == 0) {
    ESP_LOGD(TAG, "Turning off display");
    this->tm_.reset();
    this->display_text_[0] = '\0';
    return;
  }

  // TODO: auto mode does not yet pick a fan speed from the PM reading
  ESP_LOGD(TAG, "Auto mode enabled");
  this->auto_mode_ = true;
  this->clear_leds_();
  this->start_led_flash_(button);
  this->update_display_();
}

void AirFilterController::on_pm_state_(float pm) {
  if (std::isnan(pm))
    return;
  ESP_LOGD(TAG, "PM value updated: %.1f", pm);
  this->pm_value_ = pm;
  if (this->ready_)
    this->update_display_();
}

// ---- LEDs and display ------------------------------------------------------

void AirFilterController::set_led_(uint8_t index, uint8_t color) {
  this->leds_[index] = color;
  this->tm_.setLED(index, color);
}

void AirFilterController::clear_leds_() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    if (this->leds_[i] != LED_OFF)
      this->set_led_(i, LED_OFF);
  }
}

void AirFilterController::start_led_flash_(uint8_t index) {
  // Acknowledge with green/off flashes, ending on solid green.
  // Starting a new flash replaces any flash already running.
  this->flash_led_ = index;
  this->flash_remaining_ = FLASH_STEPS;
  this->set_interval(
    FLASH_INTERVAL, FLASH_STEP_MS, [this]() { this->led_flash_step_(); }
  );
}

void AirFilterController::led_flash_step_() {
  this->flash_remaining_--;
  if (this->flash_remaining_ == 0) {
    this->cancel_interval(FLASH_INTERVAL);
    this->set_led_(this->flash_led_, LED_GREEN);
    return;
  }
  this->set_led_(
    this->flash_led_, (this->flash_remaining_ % 2 == 1) ? LED_GREEN : LED_OFF
  );
}

void AirFilterController::update_display_() {
  // 8 characters: PM value left-aligned in 4, then 'A' for auto mode or a
  // space, then fan speed in percent right-aligned in 3.
  char pm_text[8];
  if (std::isnan(this->pm_value_)) {
    strcpy(pm_text, "----");
  } else {
    snprintf(
      pm_text, sizeof(pm_text), "%.0f", std::fmin(this->pm_value_, 9999.0f)
    );
  }

  char text[sizeof(this->display_text_)];
  snprintf(text, sizeof(text),
    "%-4.4s%c%3d",
    pm_text,
    this->auto_mode_ ? 'A' : ' ',
    static_cast<int>(this->level_ * 100.0f)
  );

  // The ramp calls this every 20 ms;
  // only talk to the TM1638 when the text changes
  if (strcmp(text, this->display_text_) == 0)
    return;
  memcpy(this->display_text_, text, sizeof(text));

  ESP_LOGV(TAG, "Display: '%s'", text);
  this->tm_.displayText(text);
}

}  // namespace esphome::air_filter_controller