#include "light.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hormann_hcp1 {

static const char *const TAG = "hormann_hcp1.light";

void HormannLight::setup() {
  // Register callback for state changes from the door controller
  this->parent_->add_on_state_callback([this]() { this->on_state_change(); });
}

void HormannLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Hörmann HCP1 Light");
}

light::LightTraits HormannLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::ON_OFF});
  return traits;
}

void HormannLight::write_state(light::LightState *state) {
  this->light_state_ = state;
  
  bool target_state;
  state->current_values_as_binary(&target_state);
  
  // Only toggle if the state is different from current
  if (this->parent_->is_data_valid()) {
    DoorState door_state = this->parent_->get_door_state();
    if (target_state != door_state.light) {
      ESP_LOGD(TAG, "Toggling light (target: %s, current: %s)", 
               target_state ? "ON" : "OFF",
               door_state.light ? "ON" : "OFF");
      this->parent_->toggle_light();
    }
  } else {
    // If no valid data, just toggle
    ESP_LOGD(TAG, "Toggling light (no valid state data)");
    this->parent_->toggle_light();
  }
}

void HormannLight::on_state_change() {
  if (!this->parent_->is_data_valid()) {
    return;
  }
  
  DoorState door_state = this->parent_->get_door_state();
  
  // Update light state from door controller feedback
  if (this->light_state_ != nullptr && door_state.light != this->last_state_) {
    this->last_state_ = door_state.light;
    
    // Publish the actual state from the door controller
    auto call = this->light_state_->make_call();
    call.set_state(door_state.light);
    call.perform();
    
    ESP_LOGD(TAG, "Light state updated: %s", door_state.light ? "ON" : "OFF");
  }
}

}  // namespace hormann_hcp1
}  // namespace esphome
