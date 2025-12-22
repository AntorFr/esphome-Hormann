#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_output.h"
#include "hormann_hcp1.h"

namespace esphome {
namespace hormann_hcp1 {

class HormannLight : public light::LightOutput, public Component {
 public:
  void setup() override;
  void dump_config() override;
  
  void set_parent(HormannHCP1Component *parent) { this->parent_ = parent; }
  
  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

 protected:
  void on_state_change();
  
  HormannHCP1Component *parent_{nullptr};
  light::LightState *light_state_{nullptr};
  bool last_state_{false};
};

}  // namespace hormann_hcp1
}  // namespace esphome
