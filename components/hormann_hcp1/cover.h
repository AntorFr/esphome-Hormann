#pragma once

#include "esphome/core/component.h"
#include "esphome/components/cover/cover.h"
#include "hormann_hcp1.h"

namespace esphome {
namespace hormann_hcp1 {

class HormannCover : public cover::Cover, public Component {
 public:
  void setup() override;
  void dump_config() override;
  
  void set_parent(HormannHCP1Component *parent) { this->parent_ = parent; }
  
  cover::CoverTraits get_traits() override;

 protected:
  void control(const cover::CoverCall &call) override;
  void on_state_change();
  
  HormannHCP1Component *parent_{nullptr};
};

}  // namespace hormann_hcp1
}  // namespace esphome
