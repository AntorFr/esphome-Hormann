#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "hormann_hcp1.h"

namespace esphome {
namespace hormann_hcp1 {

enum SensorType {
  SENSOR_LIGHT = 0,
  SENSOR_ERROR = 1,
  SENSOR_VENTING = 2,
  SENSOR_PREWARN = 3,
};

class HormannBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override;
  void dump_config() override;
  
  void set_parent(HormannHCP1Component *parent) { this->parent_ = parent; }
  void set_sensor_type(uint8_t type) { this->sensor_type_ = (SensorType)type; }

 protected:
  void on_state_change();
  
  HormannHCP1Component *parent_{nullptr};
  SensorType sensor_type_{SENSOR_LIGHT};
};

}  // namespace hormann_hcp1
}  // namespace esphome
