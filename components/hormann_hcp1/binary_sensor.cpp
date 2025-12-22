#include "binary_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hormann_hcp1 {

static const char *const TAG = "hormann_hcp1.binary_sensor";

void HormannBinarySensor::setup() {
  // Register callback for state changes
  this->parent_->add_on_state_callback([this]() { this->on_state_change(); });
}

void HormannBinarySensor::dump_config() {
  LOG_BINARY_SENSOR("", "Hörmann HCP1 Binary Sensor", this);
  const char *type_str;
  switch (this->sensor_type_) {
    case SENSOR_LIGHT: type_str = "Light"; break;
    case SENSOR_ERROR: type_str = "Error"; break;
    case SENSOR_VENTING: type_str = "Venting"; break;
    case SENSOR_PREWARN: type_str = "Prewarn"; break;
    default: type_str = "Unknown"; break;
  }
  ESP_LOGCONFIG(TAG, "  Type: %s", type_str);
}

void HormannBinarySensor::on_state_change() {
  if (!this->parent_->is_data_valid()) {
    return;
  }
  
  DoorState state = this->parent_->get_door_state();
  
  bool value = false;
  switch (this->sensor_type_) {
    case SENSOR_LIGHT:
      value = state.light;
      break;
    case SENSOR_ERROR:
      value = state.error;
      break;
    case SENSOR_VENTING:
      value = state.venting;
      break;
    case SENSOR_PREWARN:
      value = state.prewarn;
      break;
  }
  
  this->publish_state(value);
}

}  // namespace hormann_hcp1
}  // namespace esphome
