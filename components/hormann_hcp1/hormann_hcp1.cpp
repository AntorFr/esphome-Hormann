#include "hormann_hcp1.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace hormann_hcp1 {

static const char *const TAG = "hormann_hcp1";

// CRC table for polynomial 0x07 with initial value 0xF3
const uint8_t HormannHCP1Component::crc_table_[256] = {
  0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
  0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
  0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
  0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
  0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
  0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
  0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
  0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
  0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
  0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
  0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
  0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
  0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
  0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
  0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
  0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

void HormannHCP1Component::setup() {
  ESP_LOGI(TAG, "Setting up Hörmann HCP1...");
  this->setup_done_ = true;
  
  // Setup RS485 direction control pins
  if (this->de_pin_ != nullptr) {
    this->de_pin_->setup();
    this->de_pin_->digital_write(false);  // Disable driver by default
    ESP_LOGI(TAG, "DE pin configured");
  }
  if (this->re_pin_ != nullptr) {
    this->re_pin_->setup();
    this->re_pin_->digital_write(false);  // Enable receiver by default (active low)
    ESP_LOGI(TAG, "RE pin configured");
  }
  
  // Start in listening mode
  start_listening();
  
  ESP_LOGI(TAG, "Hörmann HCP1 setup complete, listening for data...");
}

void HormannHCP1Component::dump_config() {
  ESP_LOGCONFIG(TAG, "Hörmann HCP1:");
  ESP_LOGCONFIG(TAG, "  Setup was called: %s", this->setup_done_ ? "YES" : "NO");
  if (this->de_pin_ != nullptr) {
    LOG_PIN("  DE Pin: ", this->de_pin_);
  }
  if (this->re_pin_ != nullptr) {
    LOG_PIN("  RE Pin: ", this->re_pin_);
  }
}

void HormannHCP1Component::loop() {
  const uint32_t now = millis();
  
  // Read available data from UART
  while (this->available() > 0) {
    uint8_t data;
    if (!this->read_byte(&data)) {
      break;
    }
    
    // Debug: log every received byte
    ESP_LOGV(TAG, "RX byte: 0x%02X (counter=%d)", data, this->rx_counter_);
    
    // Check for framing error (sync break detection)
    // In ESP32, we detect sync break by checking for 0x00 after a pause
    if (this->rx_counter_ == -1) {
      // Waiting for sync break - look for address byte after break
      if (data == BROADCAST_ADDR || data == UAP1_ADDR) {
        ESP_LOGD(TAG, "Start of frame detected: 0x%02X", data);
        this->rx_buffer_[0] = data;
        this->rx_counter_ = 1;
        this->rx_length_ = 0;
      }
    } else if (this->rx_counter_ >= 0) {
      this->rx_buffer_[this->rx_counter_] = data;
      this->rx_counter_++;
      
      // Second byte contains counter (4 bits) and length (4 bits)
      if (this->rx_counter_ == 2) {
        this->rx_length_ = (data & 0x0F);
        if (this->rx_length_ > 15) {
          // Invalid length, reset
          this->rx_counter_ = -1;
        }
      }
      
      // Check if we have received the complete message
      // Message format: [addr][counter+len][data...][crc]
      // Total length = 1 (addr) + 1 (counter+len) + length (data) + 1 (crc) = length + 3
      if (this->rx_counter_ > 2 && this->rx_counter_ == (this->rx_length_ + 3)) {
        // Verify CRC
        uint8_t calculated_crc = calculate_crc(this->rx_buffer_, this->rx_length_ + 2);
        if (calculated_crc == this->rx_buffer_[this->rx_length_ + 2]) {
          this->rx_message_ready_ = true;
          this->last_message_time_ = now;
        }
        this->rx_counter_ = -1;
      }
    }
  }
  
  // Process received message
  if (this->rx_message_ready_) {
    parse_message();
    this->rx_message_ready_ = false;
  }
  
  // Handle response delay (3ms after receiving a message addressed to us)
  if (this->delay_counter_ > 0) {
    this->delay_counter_--;
    if (this->delay_counter_ == 0 && this->tx_message_ready_) {
      send_response();
    }
  }
  
  // Timeout for partial messages
  if (this->rx_counter_ >= 0 && (now - this->last_message_time_ > 100)) {
    this->rx_counter_ = -1;
  }
}

uint8_t HormannHCP1Component::calculate_crc(const uint8_t *data, uint8_t length) {
  uint8_t crc = CRC8_INITIAL_VALUE;
  for (uint8_t i = 0; i < length; i++) {
    crc = crc_table_[data[i] ^ crc];
  }
  return crc;
}

void HormannHCP1Component::parse_message() {
  uint8_t address = this->rx_buffer_[0];
  uint8_t length = this->rx_buffer_[1] & 0x0F;
  uint8_t counter = (this->rx_buffer_[1] & 0xF0) + 0x10;  // Increment counter for response
  
  if (address == BROADCAST_ADDR) {
    // Broadcast message from master
    process_broadcast(length);
  } else if (address == UAP1_ADDR) {
    // Message addressed to us (UAP1)
    if (length == 0x02 && this->rx_buffer_[2] == CMD_SLAVE_SCAN) {
      // Slave scan command
      process_slave_scan(counter);
    } else if (length == 0x01 && this->rx_buffer_[2] == CMD_SLAVE_STATUS_REQUEST) {
      // Status request command
      process_status_request(counter);
    }
  }
}

void HormannHCP1Component::process_broadcast(uint8_t length) {
  if (length == 0x02) {
    // Store broadcast status
    this->broadcast_status_ = this->rx_buffer_[2];
    this->broadcast_status_ |= (uint16_t)this->rx_buffer_[3] << 8;
    
    DoorState old_state = this->door_state_;
    
    // Parse door state from broadcast
    uint8_t d0 = this->rx_buffer_[2];
    uint8_t d1 = this->rx_buffer_[3];
    
    // Determine cover state
    if (d0 & 0x01) {
      this->door_state_.cover = COVER_OPEN;
    } else if (d0 & 0x02) {
      this->door_state_.cover = COVER_CLOSED;
    } else if ((d0 & 0x60) == 0x40) {
      this->door_state_.cover = COVER_OPENING;
    } else if ((d0 & 0x60) == 0x60) {
      this->door_state_.cover = COVER_CLOSING;
    } else {
      this->door_state_.cover = COVER_STOPPED;
    }
    
    // Other states
    this->door_state_.option_relay = (d0 & 0x04) != 0;
    this->door_state_.light = (d0 & 0x08) != 0;
    this->door_state_.error = (d0 & 0x10) != 0;
    this->door_state_.venting = (d0 & 0x80) != 0;
    this->door_state_.prewarn = (d1 & 0x01) != 0;
    this->door_state_.data_valid = true;
    
    // Log state changes
    if (old_state.cover != this->door_state_.cover || !old_state.data_valid) {
      const char *state_str;
      switch (this->door_state_.cover) {
        case COVER_OPEN: state_str = "OPEN"; break;
        case COVER_CLOSED: state_str = "CLOSED"; break;
        case COVER_OPENING: state_str = "OPENING"; break;
        case COVER_CLOSING: state_str = "CLOSING"; break;
        default: state_str = "STOPPED"; break;
      }
      ESP_LOGI(TAG, "Door state: %s", state_str);
    }
    
    // Notify callbacks
    this->state_callback_.call();
  }
}

void HormannHCP1Component::process_slave_scan(uint8_t counter) {
  // Check if scan is from master (0x80)
  if (this->rx_buffer_[3] != MASTER_ADDR) {
    return;
  }
  
  ESP_LOGD(TAG, "Received slave scan, responding...");
  
  // Prepare scan response
  this->tx_buffer_[0] = MASTER_ADDR;
  this->tx_buffer_[1] = 0x02 | counter;
  this->tx_buffer_[2] = UAP1_TYPE;
  this->tx_buffer_[3] = UAP1_ADDR;
  this->tx_buffer_[4] = calculate_crc(this->tx_buffer_, 4);
  this->tx_length_ = 5;
  this->tx_message_ready_ = true;
  this->delay_counter_ = 3;  // 3ms delay before responding
}

void HormannHCP1Component::process_status_request(uint8_t counter) {
  ESP_LOGD(TAG, "Received status request, responding with 0x%04X", this->slave_response_data_);
  
  // Prepare status response
  this->tx_buffer_[0] = MASTER_ADDR;
  this->tx_buffer_[1] = 0x03 | counter;
  this->tx_buffer_[2] = CMD_SLAVE_STATUS_RESPONSE;
  this->tx_buffer_[3] = (uint8_t)this->slave_response_data_;
  this->tx_buffer_[4] = (uint8_t)(this->slave_response_data_ >> 8);
  
  // Reset response to default after sending
  this->slave_response_data_ = RESPONSE_DEFAULT;
  
  this->tx_buffer_[5] = calculate_crc(this->tx_buffer_, 5);
  this->tx_length_ = 6;
  this->tx_message_ready_ = true;
  this->delay_counter_ = 3;  // 3ms delay before responding
}

void HormannHCP1Component::send_response() {
  if (!this->tx_message_ready_) {
    return;
  }
  
  stop_listening();
  start_sending();
  
  // Send sync break (all zeros for ~400µs at 19200 baud = ~7-8 bits)
  // We approximate this by sending 0x00 with a break condition
  // For ESP32, we can use a short delay and send the data
  
  // Write all data to UART
  for (uint8_t i = 0; i < this->tx_length_; i++) {
    this->write_byte(this->tx_buffer_[i]);
  }
  this->flush();
  
  // Small delay to ensure transmission is complete
  delayMicroseconds(600);  // ~1 byte time at 19200 baud
  
  stop_sending();
  start_listening();
  
  this->tx_message_ready_ = false;
}

void HormannHCP1Component::start_listening() {
  if (this->de_pin_ != nullptr) {
    this->de_pin_->digital_write(false);  // Disable driver
  }
  if (this->re_pin_ != nullptr) {
    this->re_pin_->digital_write(false);  // Enable receiver (active low)
  }
}

void HormannHCP1Component::stop_listening() {
  if (this->re_pin_ != nullptr) {
    this->re_pin_->digital_write(true);  // Disable receiver (active low)
  }
}

void HormannHCP1Component::start_sending() {
  if (this->de_pin_ != nullptr) {
    this->de_pin_->digital_write(true);  // Enable driver
  }
}

void HormannHCP1Component::stop_sending() {
  if (this->de_pin_ != nullptr) {
    this->de_pin_->digital_write(false);  // Disable driver
  }
}

void HormannHCP1Component::trigger_action(HormannAction action) {
  switch (action) {
    case ACTION_STOP:
      // Only stop if door is moving
      if (this->door_state_.cover == COVER_OPENING || this->door_state_.cover == COVER_CLOSING) {
        this->slave_response_data_ = RESPONSE_IMPULSE;
        ESP_LOGI(TAG, "Action: STOP");
      }
      break;
    case ACTION_OPEN:
      this->slave_response_data_ = RESPONSE_OPEN;
      ESP_LOGI(TAG, "Action: OPEN");
      break;
    case ACTION_CLOSE:
      this->slave_response_data_ = RESPONSE_CLOSE;
      ESP_LOGI(TAG, "Action: CLOSE");
      break;
    case ACTION_VENTING:
      this->slave_response_data_ = RESPONSE_VENTING;
      ESP_LOGI(TAG, "Action: VENTING");
      break;
    case ACTION_TOGGLE_LIGHT:
      this->slave_response_data_ = RESPONSE_TOGGLE_LIGHT;
      ESP_LOGI(TAG, "Action: TOGGLE_LIGHT");
      break;
    case ACTION_EMERGENCY_STOP:
      this->slave_response_data_ = RESPONSE_EMERGENCY_STOP;
      ESP_LOGI(TAG, "Action: EMERGENCY_STOP");
      break;
    case ACTION_IMPULSE:
      this->slave_response_data_ = RESPONSE_IMPULSE;
      ESP_LOGI(TAG, "Action: IMPULSE");
      break;
    default:
      break;
  }
}

}  // namespace hormann_hcp1
}  // namespace esphome
