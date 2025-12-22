#pragma once

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "hormann_hcp1.h"

namespace esphome {
namespace hormann_hcp1 {

class HormannButton : public button::Button, public Component {
 public:
  void dump_config() override;
  
  void set_parent(HormannHCP1Component *parent) { this->parent_ = parent; }
  void set_action(HormannAction action) { this->action_ = action; }

 protected:
  void press_action() override;
  
  HormannHCP1Component *parent_{nullptr};
  HormannAction action_{ACTION_NONE};
};

}  // namespace hormann_hcp1
}  // namespace esphome
