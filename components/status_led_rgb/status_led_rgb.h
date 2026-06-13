#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_state.h"

namespace esphome {
namespace status_led_rgb {

struct StatusColor {
  uint8_t r, g, b;
};

/// Replicates the native `status_led` behavior (reading App.get_app_state())
/// but drives an ADDRESSABLE light in color instead of a GPIO on/off.
class StatusLEDRGB : public Component {
 public:
  void set_light(light::LightState *light) { this->light_ = light; }
  void set_ok_color(uint8_t r, uint8_t g, uint8_t b) { this->ok_ = {r, g, b}; }
  void set_warning_color(uint8_t r, uint8_t g, uint8_t b) { this->warning_ = {r, g, b}; }
  void set_error_color(uint8_t r, uint8_t g, uint8_t b) { this->error_ = {r, g, b}; }
  void set_brightness(float brightness) { this->brightness_ = brightness; }

  void loop() override;
  void dump_config() override;
  // Run after the light is set up so make_call() is valid.
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void apply_(const StatusColor &c, bool on);

  light::LightState *light_{nullptr};
  StatusColor ok_{0, 255, 0};
  StatusColor warning_{255, 130, 0};
  StatusColor error_{255, 0, 0};
  float brightness_{0.3f};

  // Remember what is displayed so we only push to the LED on change.
  bool last_on_{false};
  StatusColor last_color_{0, 0, 0};
  bool initialized_{false};
};

}  // namespace status_led_rgb
}  // namespace esphome
