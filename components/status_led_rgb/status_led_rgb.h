#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_state.h"

namespace esphome {
namespace status_led_rgb {

struct StatusColor {
  uint8_t r, g, b;
};

/// Réplique le comportement du `status_led` natif (lecture de App.get_app_state())
/// mais pilote une light ADRESSABLE en couleur au lieu d'un GPIO on/off.
class StatusLEDRGB : public Component {
 public:
  void set_light(light::LightState *light) { this->light_ = light; }
  void set_ok_color(uint8_t r, uint8_t g, uint8_t b) { this->ok_ = {r, g, b}; }
  void set_warning_color(uint8_t r, uint8_t g, uint8_t b) { this->warning_ = {r, g, b}; }
  void set_error_color(uint8_t r, uint8_t g, uint8_t b) { this->error_ = {r, g, b}; }
  void set_brightness(float brightness) { this->brightness_ = brightness; }

  void loop() override;
  void dump_config() override;
  // Tourne après l'init de la light pour que make_call() soit valide.
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void apply_(const StatusColor &c, bool on);

  light::LightState *light_{nullptr};
  StatusColor ok_{0, 255, 0};
  StatusColor warning_{255, 130, 0};
  StatusColor error_{255, 0, 0};
  float brightness_{0.3f};

  // Mémorise ce qui est affiché pour ne repousser à la LED que sur changement.
  bool last_on_{false};
  StatusColor last_color_{0, 0, 0};
  bool initialized_{false};
};

}  // namespace status_led_rgb
}  // namespace esphome
