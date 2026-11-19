#pragma once

// #include "esphome/components/output/float_output.h"
#include "esphome/components/ledc/ledc_output.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include <esphome/core/hal.h>

#include "timer.h"
#include "button.h"

#include <map>
#include <ctime>
#include <cmath>
#include <chrono>
#include <string.h>
#include <stdlib.h>
#include <TM1638plus.h>
#include <AceButton.h>

namespace esphome {
namespace air_filter_controller {

#define MODULE "airfilter.control"
#define NUM_LED_BUTTONS 8
#define GARAGE_PM_SENSOR "sensor.garage_pm_1_0um"
#define MAX_RAMP_PWM_PER_SEC 0.0625
#define RAMP_STEP_LEN_MS 20
#define LEDC_OUT_ID "air_filter_fan_pwm_output"

// ....
#define TM_RED_LED   0x02	// Model 3
#define TM_GREEN_LED 0x01	// Model 3
#define LED_OFF 0
#define LED_RED TM_GREEN_LED
#define LED_GREEN TM_RED_LED

using namespace ace_button;
using std::chrono::system_clock;

typedef std::chrono::duration<int> seconds;
typedef std::chrono::duration<int,std::milli> milliseconds;

const int flashRedOff[2] = {LED_RED, LED_OFF};
const int flashGreenOff[2] = {LED_GREEN, LED_OFF};
const int flashRedGreen[2] = {LED_RED, LED_GREEN};
const int flashRedOffGreenOff[4] = {LED_RED, LED_OFF, LED_GREEN, LED_OFF};

typedef struct {
	int led;
	const int* pattern;
	int patternLen;
	int currentIdx;
	int count;
	void *endState;
} tFlashLedState;

typedef struct {
	int led;
	int state;
} tSetLedState;

typedef struct {
	float stepValue;
	float stepTime;		// Should be std::chrono::duration<int,std::milli>
	int count;
	float origValue;
	float totalChange;
	// float currentValue;
} tRampValueState;

// const int fan_speeds[8] = {0, 128, 144, 160, 176, 192, 224, 255};
// const int fan_speeds[8] = {0, 64, 128, 160, 176, 192, 224, 255};
const int fan_speeds[8] = {0, 48, 96, 140, 176, 192, 224, 255};
const std::map<double, int> aq_fan_speed_map {{1.0, 128}, {3.0, 160}, {5.0, 192}, {10.0, 224}, {15.0, 255}};

static const char *TAG = "air_filter_controller.sensor";

// class AirFilterController : public PollingComponent, public CustomAPIDevice, public Sensor
// class AirFilterController : public PollingComponent, public IEventHandler /* public output::FloatOutput */ /* public sensor::Sensor */
class AirFilterController : public Component, public IEventHandler /* public output::FloatOutput */ /* public sensor::Sensor */
{
private:
	bool enabled, automode;
	float pwm_out = 0.0, pm_value = -1.0;

	// int fan_speeds[8];
	// std::map<double, int> pm_speed_map;

	TM1638plus* tm;
	ledc::LEDCOutput *ledc;
	uint8_t leds[NUM_LED_BUTTONS];
	AceButton buttons[NUM_LED_BUTTONS];
	CustomButtonConfig* buttonConfig;

	std::vector<CountdownTimer> timers;
	sensor::Sensor *pm_sensor_{nullptr};
	sensor::Sensor *fan_speed_sensor_{nullptr};

	// Set output as an indexed value in our fixed array
	void setPwmOutput(int index) {
		setPwmOutput(float(fan_speeds[index]) / 255);
	}

	// Set output as a specific value
	// TODO: This might need minimum non-zero value validation,
	// some fans will not spin below a minimum power threshold
	// TODO: Change this gradually, ramp up or down as necessary
	// to match behavior of original controller
	// Parameter domain is [0.0, 1.0]
	void setPwmOutput(float new_pwm_out)
	{
		ESP_LOGI(MODULE, "Setting PWM output to %f (from %f)", new_pwm_out, pwm_out);

		// Calculate changes and length of change, given a
		// maximum of MAX_RAMP_PWM_PER_SEC speed change/second
		float diff = new_pwm_out - pwm_out;
		if (abs(diff) < 0.001)
		{
			ESP_LOGI(MODULE, "Difference from current value %f too small, aborting update", pwm_out);
			return;
		}
		float rampTimeTotal = abs(diff) / MAX_RAMP_PWM_PER_SEC;

		// We adjust as RAMP_STEP_LEN_MS intervals
		auto rampStepMS = milliseconds(RAMP_STEP_LEN_MS);

		tRampValueState* rampState = new tRampValueState;
		float count = rampTimeTotal * 1000 / RAMP_STEP_LEN_MS;
		rampState->count = ceil(count);		// Round up
		rampState->stepValue = diff / count;
		rampState->stepTime = RAMP_STEP_LEN_MS;
		rampState->totalChange = diff;
		rampState->origValue = pwm_out;

		ESP_LOGI(MODULE, "diff %f count %f %d", diff*256, count, rampState->count);
		ESP_LOGI(MODULE, "rampTimeTotal %f stepValue %f", rampTimeTotal, rampState->stepValue);

		if (count > 0)
			createTimer(&AirFilterController::tRampPWM, rampStepMS, (void *)rampState, rampState->count);

		// pwm_out = new_pwm_out;
		// id(air_filter_fan_pwm_output).set_level(pwm_out);
		// free(rampState);
		updateDisplay();
	}

	void turnOffLEDs(void)
	{
		for (int i=0; i<NUM_LED_BUTTONS; i++) {
			if (leds[i] != LED_OFF)
				setLED(i, LED_OFF);
		}
	}

	void setLED(int led, int state, bool clear=false)
	{
		if (clear)
			turnOffLEDs();

		leds[led] = state;
		tm->setLED(led, state);
	}

	// Update LED states
	// Turn off other LEDs, enable LED on this button
	void handleButtonClick(int button)
	{
		automode = false;
		setLED(button, LED_RED, true);
		setPwmOutput(button);
	}

	// Event handler for AceButton
	void handleEvent(AceButton* button, uint8_t eventType, uint8_t buttonState) // override
	{
		uint8_t btn = button->getPin();

		switch (eventType) {
		case AceButton::kEventClicked:
			handleButtonClick(btn);
			break;

		case AceButton::kEventLongPressed:
			if (btn == 0)
			{
				ESP_LOGD(MODULE, "Turning off display");
				tm->reset();
			}
			else
			{
				// Flash LED a few times to indicate acknowledgement
				tSetLedState* setGreen = new tSetLedState;
				setGreen->led = btn;
				setGreen->state = LED_GREEN;

				tFlashLedState* flashState = new tFlashLedState;
				flashState->led = btn;
				flashState->pattern = flashGreenOff;
				flashState->patternLen = sizeof(flashGreenOff) / sizeof(int);
				flashState->currentIdx = 0;
				flashState->count = 8;
				flashState->endState = setGreen;

				createTimer(&AirFilterController::tFlashLED, milliseconds(250), (void *)flashState, flashState->count);

				// TODO: Invoke call to calculate necessary speed
				// Needs to test for and fail if PM data is not available
				automode = true;
				turnOffLEDs();
				updateDisplay();
				// free(flashState);
			}
			break;

		case AceButton::kEventPressed:
			break;

		default:
			ESP_LOGW(MODULE, "unhandled event %d on pin %d", eventType, btn);
			break;
		}
    }

	// Send new data to display
	void updateDisplay(void)
	{
		static char out_1[5], output[11];
		const char* format;
		int len;

		// Format text, but ignore the decimal point if present since it's
		// included in the 7-segment displays (not a separate character)
		if (pm_value < 0) {
			len = snprintf(out_1, 5, "----");
		} else {
			len = snprintf(out_1, 5, "%-4.0f", pm_value);
			// if (strchr(out_1, '.') != NULL) {
			// 	len += 1;
			// }
		}

		if (automode)
			format = "%-*sA%3d";
		else
			format = "%-*s%4d";

		int len2 = snprintf(output, 11, format, len, out_1, int(pwm_out*100.0));

		ESP_LOGD(MODULE, "rendering display of '%s' (len %d, len2 %d)", output, len, len2);
		tm->displayText(output);
	}

public:
 	AirFilterController(
		InternalGPIOPin *strobe,
		InternalGPIOPin *clock,
		InternalGPIOPin *dio,
		bool high_freq
	)
	{
		tm = new TM1638plus(strobe->get_pin(), clock->get_pin(), dio->get_pin(), high_freq);

		buttonConfig = new CustomButtonConfig();
		buttonConfig->setTM(tm);
		buttonConfig->setIEventHandler(this);
		// buttonConfig->setEventHandler(handleEvent);
		buttonConfig->setFeature(ButtonConfig::kFeatureClick);
		buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
		buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterClick);
		buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
		buttonConfig->setClickDelay(400);

		// ::facepalm:: 2nd argument to init() is defaultReleasedState,
		// not high state, would have saved some time if I read that
		// more carefully
		for (int i=0; i<NUM_LED_BUTTONS; i++)
		{
			leds[i] = 0;
			buttons[i].init(i, 0, i);
			buttons[i].setButtonConfig(buttonConfig);
		}

		// Read fan speeds & PM/speed map from config
		// TODO: Fix me.  This isn't working quite yet.
		// for (int i=0; i<8; i++) {
		// 	fan_speeds[i] = id(button_fan_speed_map)[i];
		// 	// pm_speed_map.insert({id(pm_fan_speed_map)[i], id(button_fan_speed_map)[i]});
		// }

		automode = false;
	}

	void setLEDC(ledc::LEDCOutput *ledc) {
		this->ledc = ledc;
	}

	void set_pm_sensor(sensor::Sensor *s) { this->pm_sensor_ = s; }
	void set_fan_speed_sensor(sensor::Sensor *s) { this->fan_speed_sensor_ = s; }

	// Calculate an appropriate fan speed for PM value
	// If set to auto, then update fan speed
	// If not on auto, then we should notify user
	// - Choices could vary depending if fan is just too slow
	//     or fan is completely off
	// - Could also delay or re-check, if PM levels are not dropping
	// - Flash the calculated fan speed LED
	// - Flash the PM display
	void onPmStateChanged(float pm)
	{
		// ESP_LOGD(MODULE, "Received updated PM value %s", pm.c_str());
		// pm_value = atof(pm.c_str());
		if (std::isnan(pm)) return;
    ESP_LOGD(MODULE, "Received updated PM value %.1f", pm);
    pm_value = pm;
    updateDisplay();
	}

	// TODO: Integrate with a IR motion sensor

	// Timer helpers & callbacks
	void createTimer(
		std::function<void(AirFilterController*)> f,
		std::chrono::duration<int,std::milli> length,
		int count=1)
	{
		CountdownTimer new_timer;

		std::function<void()> fb = std::bind(f, this);
		new_timer.start(length, fb, count);
		timers.push_back(new_timer);
	}

	// Callback with data
	void createTimer(
		std::function<void(AirFilterController*,void*)> f,
		std::chrono::duration<int,std::milli> length,
		void* data=NULL,
		int count=1)
	{
		CountdownTimer new_timer;

		if (data == NULL) {
			ESP_LOGE(MODULE, "createTimer() called with NULL data, skipping creation");
			return;
		}
		std::function<void(void*)> fb = std::bind(f, this, data);
		new_timer.start(length, fb, data, count);
		timers.push_back(new_timer);
	}

	// "Async" timer calbacks
	// Func to clear initialization status and message
	void tLeaveInitFunc()
	{
		setLED(0, LED_GREEN);
		setPwmOutput(0);
		enabled = true;
		publishFanSpeed();
	}

	void tFlashLED(void* vstate)
	{
		tFlashLedState* state = (tFlashLedState*)vstate;
		setLED(state->led, state->pattern[state->currentIdx++]);

		if (state->currentIdx >= state->patternLen)
			state->currentIdx = 0;

		if (--state->count == 0 && state->endState != NULL)
			tSetLED(state->endState);
	}

	void tSetLED(void *vstate)
	{
		tSetLedState* state = (tSetLedState*)vstate;
		setLED(state->led, state->state);
	}

	void tRampPWM(void *vstate)
	{
		tRampValueState* state = (tRampValueState*)vstate;

		// On last run, just set the speed to the final value
		// This is necessary because the change value does not likely
		// fit into an integer multiple of steps, and thus a partial
		// (part of a stepValue) update is necessary.
		if (--state->count == 0)
		{
			float new_value = state->origValue + state->totalChange;

			if (abs(pwm_out - new_value) > (1.0 / 256)) {
				ESP_LOGE(MODULE, "error difference between current %f and final %f too large, aborting final update", pwm_out, new_value);
			} else {
				pwm_out = new_value;
			}

			// update();
			publishFanSpeed();
		}
		else
		{
			// TODO: stepValue is an upper bound and updates may not
			// hit this target if CPU is busy, check and compensate if necessary
			pwm_out += state->stepValue;
		}

		// state->currentValue = pwm_out;
		// auto ledc = id(LEDC_OUT_ID);
		ledc->set_level(pwm_out);
		updateDisplay();
	}

	// void on_update_fan_speed(int button)
	void on_update_fan_speed(float speed)
	{
		ESP_LOGI(MODULE, "API call to set fan speed to %f", speed);
		setPwmOutput(speed);
	}

	void publishFanSpeed() {
    if (fan_speed_sensor_ != nullptr)
        fan_speed_sensor_->publish_state(pwm_out * 100.0f);
	}

	float get_setup_priority() const override {
		return esphome::setup_priority::PROCESSOR;
	}

	// void update() override {
	// // 	publish_state(pwm_out*100);
	// }

	void setup() override
	{
		ESP_LOGI(MODULE, "Initializing");
    	tm->displayBegin();
		tm->displayText("- init -");

		// Set timer to set display from init to actual data
		createTimer(&AirFilterController::tLeaveInitFunc, std::chrono::seconds(5));

		// Configure HASS API
		if (pm_sensor_ != nullptr) {
				pm_sensor_->add_on_state_callback(
					[this](float v) { this->onPmStateChanged(v); }
				);
		}
	}

	void loop() override
	{
		// Check buttons
		for (uint8_t i=0; i<NUM_LED_BUTTONS; i++)
			buttons[i].check();

		// Check timers
		auto it = timers.begin();
		while (it != timers.end())
		{
			if (it->runIfExpired())
				it = timers.erase(it);	// TODO: GC?
			else
				it++;
		}
	}

	void dump_config() {
		ESP_LOGCONFIG(TAG, "AirFilterController sensor");
	}

};

}  // namespace air_filter_controller
}  // namespace esphome