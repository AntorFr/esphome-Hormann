#include "button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hormann_hcp1 {

static const char *const TAG = "hormann_hcp1.button";

void HormannButton::dump_config() {
  LOG_BUTTON("", "Hörmann HCP1 Button", this);
  const char *action_str;
  switch (this->action_) {
    case ACTION_IMPULSE: action_str = "Impulse"; break;
    case ACTION_VENTING: action_str = "Venting"; break;
    case ACTION_EMERGENCY_STOP: action_str = "Emergency Stop"; break;
    default: action_str = "Unknown"; break;
  }
  ESP_LOGCONFIG(TAG, "  Action: %s", action_str);
}

void HormannButton::press_action() {
  if (this->parent_ != nullptr && this->action_ != ACTION_NONE) {
    ESP_LOGD(TAG, "Button pressed, triggering action");
    this->parent_->trigger_action(this->action_);
  }
}

}  // namespace hormann_hcp1
}  // namespace esphome
