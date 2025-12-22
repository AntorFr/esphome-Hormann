#include "cover.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hormann_hcp1 {

static const char *const TAG = "hormann_hcp1.cover";

void HormannCover::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Hörmann Cover...");
  
  // Register callback for state changes
  this->parent_->add_on_state_callback([this]() { this->on_state_change(); });
}

void HormannCover::dump_config() {
  LOG_COVER("", "Hörmann HCP1 Cover", this);
}

cover::CoverTraits HormannCover::get_traits() {
  auto traits = cover::CoverTraits();
  traits.set_is_assumed_state(false);
  traits.set_supports_position(true);
  traits.set_supports_tilt(false);
  traits.set_supports_stop(true);
  return traits;
}

void HormannCover::control(const cover::CoverCall &call) {
  if (call.get_stop()) {
    ESP_LOGD(TAG, "Cover STOP command");
    this->parent_->stop_door();
    return;
  }
  
  if (call.get_position().has_value()) {
    float pos = *call.get_position();
    
    if (pos == cover::COVER_OPEN) {
      ESP_LOGD(TAG, "Cover OPEN command");
      this->parent_->open_door();
    } else if (pos == cover::COVER_CLOSED) {
      ESP_LOGD(TAG, "Cover CLOSE command");
      this->parent_->close_door();
    } else {
      // For intermediate positions, we can use venting (partial open)
      // Hörmann venting position is typically around 10-20% open
      if (pos > 0.05 && pos < 0.5) {
        ESP_LOGD(TAG, "Cover VENTING command (position: %.0f%%)", pos * 100);
        this->parent_->toggle_venting();
      } else if (pos >= 0.5) {
        ESP_LOGD(TAG, "Cover OPEN command (position: %.0f%%)", pos * 100);
        this->parent_->open_door();
      } else {
        ESP_LOGD(TAG, "Cover CLOSE command (position: %.0f%%)", pos * 100);
        this->parent_->close_door();
      }
    }
  }
}

void HormannCover::on_state_change() {
  if (!this->parent_->is_data_valid()) {
    return;
  }
  
  DoorState state = this->parent_->get_door_state();
  
  // Update cover state
  switch (state.cover) {
    case COVER_OPEN:
      this->position = cover::COVER_OPEN;
      this->current_operation = cover::COVER_OPERATION_IDLE;
      break;
    case COVER_CLOSED:
      this->position = cover::COVER_CLOSED;
      this->current_operation = cover::COVER_OPERATION_IDLE;
      break;
    case COVER_OPENING:
      this->current_operation = cover::COVER_OPERATION_OPENING;
      // Keep position as intermediate while moving
      if (this->position == cover::COVER_CLOSED) {
        this->position = 0.5f;  // Set to middle while opening
      }
      break;
    case COVER_CLOSING:
      this->current_operation = cover::COVER_OPERATION_CLOSING;
      // Keep position as intermediate while moving
      if (this->position == cover::COVER_OPEN) {
        this->position = 0.5f;  // Set to middle while closing
      }
      break;
    case COVER_STOPPED:
    default:
      this->current_operation = cover::COVER_OPERATION_IDLE;
      // If venting, show partial position
      if (state.venting) {
        this->position = 0.1f;  // Venting position ~10%
      }
      break;
  }
  
  this->publish_state();
}

}  // namespace hormann_hcp1
}  // namespace esphome
