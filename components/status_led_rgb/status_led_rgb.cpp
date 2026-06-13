#include "status_led_rgb.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace status_led_rgb {

static const char *const TAG = "status_led_rgb";

void StatusLEDRGB::loop() {
  // Same bits as the native `status_led`: STATUS_LED_OK / _WARNING / _ERROR.
  uint8_t state = App.get_app_state() & STATUS_LED_MASK;

  bool on;
  StatusColor color;
  if ((state & STATUS_LED_ERROR) != 0u) {
    color = this->error_;
    on = millis() % 250u < 150u;    // fast blink (cf. native status_led)
  } else if ((state & STATUS_LED_WARNING) != 0u) {
    color = this->warning_;
    on = millis() % 1500u < 250u;   // slow blink
  } else {
    color = this->ok_;
    on = true;                      // solid when everything is fine
  }

  // Only push to the LED if the displayed state actually changes.
  if (!this->initialized_ || on != this->last_on_ || color.r != this->last_color_.r ||
      color.g != this->last_color_.g || color.b != this->last_color_.b) {
    this->apply_(color, on);
  }
}

void StatusLEDRGB::apply_(const StatusColor &c, bool on) {
  this->last_on_ = on;
  this->last_color_ = c;
  this->initialized_ = true;
  if (this->light_ == nullptr)
    return;

  auto call = this->light_->make_call();
  call.set_state(on);
  if (on) {
    call.set_color_mode_if_supported(light::ColorMode::RGB);
    call.set_rgb(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
    call.set_brightness(this->brightness_);
  }
  call.set_transition_length(0);
  call.perform();
}

void StatusLEDRGB::dump_config() {
  ESP_LOGCONFIG(TAG, "Status LED RGB:");
  ESP_LOGCONFIG(TAG, "  OK:      #%02X%02X%02X", this->ok_.r, this->ok_.g, this->ok_.b);
  ESP_LOGCONFIG(TAG, "  Warning: #%02X%02X%02X", this->warning_.r, this->warning_.g, this->warning_.b);
  ESP_LOGCONFIG(TAG, "  Error:   #%02X%02X%02X", this->error_.r, this->error_.g, this->error_.b);
}

}  // namespace status_led_rgb
}  // namespace esphome
