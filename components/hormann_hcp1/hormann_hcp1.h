#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/hal.h"

#ifdef USE_ESP_IDF
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#endif

namespace esphome {
namespace hormann_hcp1 {

static const uint8_t BROADCAST_ADDR = 0x00;
static const uint8_t MASTER_ADDR = 0x80;
static const uint8_t UAP1_ADDR = 0x28;
static const uint8_t UAP1_TYPE = 0x14;

static const uint8_t CMD_SLAVE_SCAN = 0x01;
static const uint8_t CMD_SLAVE_STATUS_REQUEST = 0x20;
static const uint8_t CMD_SLAVE_STATUS_RESPONSE = 0x29;

static const uint16_t RESPONSE_DEFAULT = 0x1000;
static const uint16_t RESPONSE_EMERGENCY_STOP = 0x0000;
static const uint16_t RESPONSE_OPEN = 0x1001;
static const uint16_t RESPONSE_CLOSE = 0x1002;
static const uint16_t RESPONSE_VENTING = 0x1010;
static const uint16_t RESPONSE_TOGGLE_LIGHT = 0x1008;
static const uint16_t RESPONSE_IMPULSE = 0x1004;

static const uint8_t CRC8_INITIAL_VALUE = 0xF3;

enum CoverState {
  COVER_STOPPED = 0,
  COVER_OPEN,
  COVER_CLOSED,
  COVER_OPENING,
  COVER_CLOSING
};

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

// RX line polarity handling
// A/B differential polarity. A physical A/B swap (or a module whose A/B
// convention is opposite to the bus) inverts BOTH directions of the single
// differential pair, so this applies to RX and TX together.
enum AbInvertMode : uint8_t {
  AB_INV_OFF = 0,   // A/B as marked — no inversion
  AB_INV_ON = 1,    // A/B swapped — invert RX and TX (RXD_INV | TXD_INV)
  AB_INV_AUTO = 2,  // detect at boot: listen, flip RX+TX if the bus looks inverted
};

// State machine of the boot-time polarity auto-detection
enum PolarityState : uint8_t {
  POLARITY_LISTENING = 0,   // first window, current (non-inverted) polarity
  POLARITY_CONFIRMING = 1,  // after a flip, verifying frames now decode
  POLARITY_DONE = 2,        // decision made (or a forced mode is in use)
};

struct DoorState {
  CoverState cover{COVER_STOPPED};
  bool venting{false};
  bool error{false};
  bool prewarn{false};
  bool light{false};
  bool option_relay{false};
  bool data_valid{false};
};

class HormannHCP1Component : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  void set_uart_num(uint8_t num) { this->uart_num_ = num; }
  void set_tx_pin(int pin) { this->tx_pin_ = pin; }
  void set_rx_pin(int pin) { this->rx_pin_ = pin; }
  void set_de_pin(GPIOPin *pin) { this->de_pin_ = pin; }
  void set_re_pin(GPIOPin *pin) { this->re_pin_ = pin; }
  void set_slave_addr(uint8_t addr) { this->slave_addr_ = addr; }
  void set_master_addr(uint8_t addr) { this->master_addr_ = addr; }
  void set_slave_type(uint8_t type) { this->slave_type_ = type; }
  void set_auto_scan(bool enable) { this->auto_scan_ = enable; }
  void set_de_invert(bool inv) { this->de_invert_ = inv; }
  void set_tx_test(bool enable) { this->tx_test_ = enable; }
  void set_bustask_tx_test(bool enable) { this->bustask_tx_test_ = enable; }
  void set_ab_inverted_mode(uint8_t mode) { this->ab_inverted_mode_ = mode; }
  void set_sniffer(bool enable) { this->sniffer_ = enable; }
  void set_reply_delay_us(uint32_t us) { this->reply_delay_us_ = us; }

  DoorState get_door_state() const { return this->door_state_; }
  bool is_data_valid() const { return this->door_state_.data_valid; }

  void trigger_action(HormannAction action);
  void open_door() { trigger_action(ACTION_OPEN); }
  void close_door() { trigger_action(ACTION_CLOSE); }
  void stop_door() { trigger_action(ACTION_STOP); }
  void toggle_venting() { trigger_action(ACTION_VENTING); }
  void toggle_light() { trigger_action(ACTION_TOGGLE_LIGHT); }
  void impulse() { trigger_action(ACTION_IMPULSE); }
  void emergency_stop() { trigger_action(ACTION_EMERGENCY_STOP); }
  void tx_diag();  // Diagnostic: forces DE high and sends 0xAA repeatedly

  void add_on_state_callback(std::function<void()> callback) {
    this->state_callback_.add(std::move(callback));
  }

 protected:
  uint8_t uart_num_{1};
  int tx_pin_{17};
  int rx_pin_{16};
  GPIOPin *de_pin_{nullptr};
  GPIOPin *re_pin_{nullptr};
  uint8_t slave_addr_{UAP1_ADDR};
  uint8_t master_addr_{MASTER_ADDR};
  uint8_t slave_type_{UAP1_TYPE};
  bool auto_scan_{false};
  bool de_invert_{false};
  bool tx_test_{false};
  uint32_t tx_test_last_{0};
  // Test A/B: fire the real scan-reply from the bus_task context (not loop()) on a
  // 2s wall-clock timer — async to master traffic — to discriminate drive vs timing.
  bool bustask_tx_test_{false};
  uint32_t bustask_tx_last_{0};
  uint32_t reply_delay_us_{0};       // micro-delay before TX (eager-reply window tuning)
  volatile int64_t last_rx_us_{0};   // timestamp of last received byte (reply-latency instr.)

  // Line polarity / boot-time auto-detection
  uint8_t ab_inverted_mode_{AB_INV_AUTO};   // AbInvertMode
  bool inversion_active_{false};            // current actual RX+TX inversion state
  uint8_t polarity_state_{POLARITY_DONE};   // PolarityState
  uint32_t polarity_window_start_{0};
  bool no_traffic_warned_{false};
  volatile uint32_t rx_error_count_{0};     // breaks + frame errors in current window
  volatile uint32_t valid_frame_count_{0};  // valid-CRC frames in current window
  uint8_t auto_scan_idx_{0};
  uint32_t auto_scan_last_change_{0};
  bool combo_locked_{false};

  // Sniffer / bus witness: log only valid, categorized, de-duplicated frames.
  bool sniffer_{false};
  uint8_t sniff_last_key_[18]{};      // last logged frame's dedup key (addr+len+payload)
  uint8_t sniff_last_len_{0};
  uint32_t sniff_suppressed_{0};      // identical frames collapsed since last log
  uint32_t sniff_last_log_ms_{0};
  volatile uint32_t sniff_valid_{0};  // valid operational frames seen (sniffer stats)
  volatile uint32_t sniff_junk_{0};   // buffers with data but no valid frame for us
  uint32_t sniff_valid_snap_{0};      // loop-owned snapshots for per-window deltas
  uint32_t sniff_junk_snap_{0};
  uint32_t sniff_err_snap_{0};
  uint32_t sniff_stats_last_{0};
  uint32_t sniff_junk_log_ms_{0};     // rate-limit for logging unparseable (junk) buffers
  uint32_t sniff_req_ms_{0};          // witness: ms of last SCAN/STATUS_REQ->us (passive reply-latency)

#ifdef USE_ESP_IDF
  QueueHandle_t uart_queue_{nullptr};
  TaskHandle_t bus_task_{nullptr};
  static void bus_task_trampoline(void *arg);
  void bus_task();
#endif

  uint8_t rx_buffer_[20]{};
  uint8_t rx_counter_{0};
  uint8_t rx_length_{0};
  bool frame_started_{false};

  uint8_t tx_buffer_[20]{};
  uint8_t tx_length_{0};

  DoorState door_state_;
  uint16_t broadcast_status_{0};
  volatile uint16_t slave_response_data_{RESPONSE_DEFAULT};
  volatile bool state_changed_{false};

  static const uint8_t crc_table_[256];

  uint8_t calculate_crc(const uint8_t *data, uint8_t length);
  void try_parse_buffered();
  bool eager_reply_to_us_();  // reply to a complete scan/status addressed to us, ASAP
  void parse_message();
  void process_broadcast(uint8_t length);
  void process_slave_scan(uint8_t counter);
  void process_status_request(uint8_t counter);
  void send_frame(uint8_t length);
  void apply_line_inversion_();  // (re)apply RXD/TXD mask (TX follows RX when auto) + flush RX
  void sniff_scan_();                                           // read-only: count + log valid frames
  void sniff_log_frame_(const uint8_t *frame, uint8_t length);  // categorized, de-duped INFO log

  CallbackManager<void()> state_callback_;
};

}  // namespace hormann_hcp1
}  // namespace esphome
