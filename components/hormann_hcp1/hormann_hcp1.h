#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace hormann_hcp1 {

// Protocol constants
static const uint8_t BROADCAST_ADDR = 0x00;
static const uint8_t MASTER_ADDR = 0x80;
static const uint8_t UAP1_ADDR = 0x28;
static const uint8_t UAP1_TYPE = 0x14;

// Commands
static const uint8_t CMD_SLAVE_SCAN = 0x01;
static const uint8_t CMD_SLAVE_STATUS_REQUEST = 0x20;
static const uint8_t CMD_SLAVE_STATUS_RESPONSE = 0x29;

// Response codes
static const uint16_t RESPONSE_DEFAULT = 0x1000;
static const uint16_t RESPONSE_EMERGENCY_STOP = 0x0000;
static const uint16_t RESPONSE_OPEN = 0x1001;
static const uint16_t RESPONSE_CLOSE = 0x1002;
static const uint16_t RESPONSE_VENTING = 0x1010;
static const uint16_t RESPONSE_TOGGLE_LIGHT = 0x1008;
static const uint16_t RESPONSE_IMPULSE = 0x1004;

// CRC
static const uint8_t CRC8_INITIAL_VALUE = 0xF3;

// Cover states
enum CoverState {
  COVER_STOPPED = 0,
  COVER_OPEN,
  COVER_CLOSED,
  COVER_OPENING,
  COVER_CLOSING
};

// Actions
enum HormannAction {
  ACTION_NONE = 0,
  ACTION_STOP,
  ACTION_OPEN,
  ACTION_CLOSE,
  ACTION_VENTING,
  ACTION_TOGGLE_LIGHT,
  ACTION_EMERGENCY_STOP,
  ACTION_IMPULSE
};

// Door state structure
struct DoorState {
  CoverState cover{COVER_STOPPED};
  bool venting{false};
  bool error{false};
  bool prewarn{false};
  bool light{false};
  bool option_relay{false};
  bool data_valid{false};
};

class HormannHCP1Component : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Pin configuration for RS485 direction control
  void set_de_pin(GPIOPin *pin) { this->de_pin_ = pin; }
  void set_re_pin(GPIOPin *pin) { this->re_pin_ = pin; }

  // Get current door state
  DoorState get_door_state() const { return this->door_state_; }
  
  // Check if we have valid data
  bool is_data_valid() const { return this->door_state_.data_valid; }

  // Trigger actions
  void trigger_action(HormannAction action);
  void open_door() { trigger_action(ACTION_OPEN); }
  void close_door() { trigger_action(ACTION_CLOSE); }
  void stop_door() { trigger_action(ACTION_STOP); }
  void toggle_venting() { trigger_action(ACTION_VENTING); }
  void toggle_light() { trigger_action(ACTION_TOGGLE_LIGHT); }
  void impulse() { trigger_action(ACTION_IMPULSE); }
  void emergency_stop() { trigger_action(ACTION_EMERGENCY_STOP); }

  // Callbacks for state changes
  void add_on_state_callback(std::function<void()> callback) { 
    this->state_callback_.add(std::move(callback)); 
  }

 protected:
  // RS485 direction control pins
  GPIOPin *de_pin_{nullptr};  // Driver Enable
  GPIOPin *re_pin_{nullptr};  // Receiver Enable (active low)

  // RX/TX buffers
  uint8_t rx_buffer_[20];
  uint8_t tx_buffer_[20];
  int8_t rx_counter_{-1};
  uint8_t rx_length_{0};
  bool rx_message_ready_{false};
  bool tx_message_ready_{false};
  uint8_t tx_length_{0};
  uint8_t tx_counter_{0};

  // Protocol state
  DoorState door_state_;
  uint16_t broadcast_status_{0};
  uint16_t slave_response_data_{RESPONSE_DEFAULT};
  uint8_t message_counter_{0};
  uint32_t last_message_time_{0};
  uint8_t delay_counter_{0};

  // Pending action
  HormannAction pending_action_{ACTION_NONE};

  // CRC table for polynomial 0x07
  static const uint8_t crc_table_[256];

  // Internal methods
  uint8_t calculate_crc(const uint8_t *data, uint8_t length);
  void parse_message();
  void start_listening();
  void stop_listening();
  void start_sending();
  void stop_sending();
  void send_response();
  void process_broadcast(uint8_t length);
  void process_slave_scan(uint8_t counter);
  void process_status_request(uint8_t counter);

  // Callback
  CallbackManager<void()> state_callback_;
};

}  // namespace hormann_hcp1
}  // namespace esphome
