#include "hormann_hcp1.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#ifdef USE_ESP_IDF
#include "esp_rom_sys.h"
#include "esp_timer.h"
#endif

namespace esphome {
namespace hormann_hcp1 {

static const char *const TAG = "hormann_hcp1";

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

uint8_t HormannHCP1Component::calculate_crc(const uint8_t *data, uint8_t length) {
  uint8_t crc = CRC8_INITIAL_VALUE;
  for (uint8_t i = 0; i < length; i++) {
    crc = crc_table_[data[i] ^ crc];
  }
  return crc;
}

void HormannHCP1Component::setup() {
  ESP_LOGI(TAG, "Setting up Hörmann HCP1 (ESP-IDF native UART%u)...", this->uart_num_);

  if (this->de_pin_ != nullptr) {
    this->de_pin_->setup();
    this->de_pin_->digital_write(this->de_invert_);  // start in RX mode
  }
  if (this->re_pin_ != nullptr) {
    this->re_pin_->setup();
    this->re_pin_->digital_write(false);  // active low: enable receiver
  }

#ifdef USE_ESP_IDF
  uart_config_t uart_cfg = {};
  uart_cfg.baud_rate = 19200;
  uart_cfg.data_bits = UART_DATA_8_BITS;
  uart_cfg.parity = UART_PARITY_DISABLE;
  uart_cfg.stop_bits = UART_STOP_BITS_1;
  uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_cfg.source_clk = UART_SCLK_DEFAULT;

  uart_port_t port = static_cast<uart_port_t>(this->uart_num_);

  // RX buffer 256, TX buffer 256, queue depth 20
  esp_err_t err = uart_driver_install(port, 512, 256, 40, &this->uart_queue_, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "uart_driver_install failed: %d", err);
    this->mark_failed();
    return;
  }
  uart_param_config(port, &uart_cfg);
  // DE is controlled MANUALLY (see send_frame). Do NOT wire it as the UART RTS pin:
  // without UART_MODE_RS485_HALF_DUPLEX the driver holds RTS deasserted (HIGH), which
  // forces the transceiver into TX mode at idle and CLAMPS the whole bus (symptom: 0 RX
  // on every node, ours and the witness). Manual DE idles LOW (= RX mode) instead.
  uart_set_pin(port, this->tx_pin_, this->rx_pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  // --- A/B line polarity -----------------------------------------------
  // A physical A/B swap inverts RX and TX together, so we treat them as one.
  // Forced modes apply now; 'auto' starts non-inverted and decides in loop()
  // after listening to the bus for POLARITY_WINDOW_MS.
  this->inversion_active_ = (this->ab_inverted_mode_ == AB_INV_ON);
  this->polarity_state_ = (this->ab_inverted_mode_ == AB_INV_AUTO) ? POLARITY_LISTENING : POLARITY_DONE;
  this->apply_line_inversion_();
  this->polarity_window_start_ = millis();
  // ---------------------------------------------------------------------

  // Read in batches; rely on RX timeout (~3 char times = ~1.6 ms gap) to mark end-of-frame.
  // Hörmann frames are back-to-back internally and separated by >5 ms idle.
  uart_set_rx_full_threshold(port, 1);   // notify per byte for low-latency
  uart_set_rx_timeout(port, 1);          // ~0.5 ms gap = end of frame

  xTaskCreatePinnedToCore(&HormannHCP1Component::bus_task_trampoline, "hcp1_bus",
                          8192, this, 23, &this->bus_task_, 1);

  ESP_LOGI(TAG, "Hörmann HCP1 ready (TX=%d RX=%d)", this->tx_pin_, this->rx_pin_);
#else
  ESP_LOGE(TAG, "hormann_hcp1 requires the esp-idf framework");
  this->mark_failed();
#endif
}

void HormannHCP1Component::dump_config() {
  ESP_LOGCONFIG(TAG, "Hörmann HCP1:");
  ESP_LOGCONFIG(TAG, "  UART num: %u", this->uart_num_);
  ESP_LOGCONFIG(TAG, "  TX pin: %d", this->tx_pin_);
  ESP_LOGCONFIG(TAG, "  RX pin: %d", this->rx_pin_);
  if (this->de_pin_ != nullptr) LOG_PIN("  DE pin: ", this->de_pin_);
  if (this->re_pin_ != nullptr) LOG_PIN("  RE pin: ", this->re_pin_);
  const char *ab_mode = this->ab_inverted_mode_ == AB_INV_AUTO ? "auto"
                        : (this->ab_inverted_mode_ == AB_INV_ON ? "true" : "false");
  ESP_LOGCONFIG(TAG, "  A/B inverted (RX+TX): %s (active=%s)", ab_mode,
                this->inversion_active_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Sniffer: %s", this->sniffer_ ? "ON (valid frames at INFO)" : "off");
  ESP_LOGCONFIG(TAG, "  Reply delay: %u us", (unsigned) this->reply_delay_us_);
}

// Combinations to try when auto_scan is enabled. Order = most likely first.
struct ComboCandidate { uint8_t slave; uint8_t master; uint8_t type; };
static const ComboCandidate AUTO_SCAN_TABLE[] = {
  {0x28, 0x80, 0x14},  // default UAP1
  {0x29, 0x80, 0x14},
  {0x82, 0x80, 0x14},  // intelligent controller range
  {0x83, 0x80, 0x14},
  {0x81, 0x80, 0x14},
  {0x28, 0x8D, 0x14},  // master_device variant
  {0x82, 0x8D, 0x14},
  {0x28, 0x80, 0x10},  // alternate type codes
  {0x28, 0x80, 0x12},
  {0x28, 0x80, 0x15},
  {0x28, 0x80, 0x16},
  {0x28, 0x80, 0x20},
};
static constexpr uint8_t AUTO_SCAN_COUNT = sizeof(AUTO_SCAN_TABLE) / sizeof(AUTO_SCAN_TABLE[0]);
static constexpr uint32_t AUTO_SCAN_INTERVAL_MS = 15000;  // 15s per combo (~3 master polls)

// rx_inverted: auto — listen this long, then decide from breaks/errors vs valid frames.
static constexpr uint32_t POLARITY_WINDOW_MS = 5000;
static constexpr uint32_t POLARITY_ERROR_THRESHOLD = 20;  // breaks+frame-errs that look "inverted"

// Sniffer mode: periodic stats interval, and how often identical frames re-log.
static constexpr uint32_t SNIFF_STATS_MS = 10000;
static constexpr uint32_t SNIFF_REFRESH_MS = 5000;
static constexpr uint32_t SNIFF_JUNK_MIN_MS = 500;  // rate-limit for junk-byte logging

void HormannHCP1Component::loop() {
  if (this->state_changed_) {
    this->state_changed_ = false;
    this->state_callback_.call();
  }

  // Boot-time RX polarity auto-detection (rx_inverted: auto).
  if (this->polarity_state_ != POLARITY_DONE) {
    uint32_t now = millis();
    if (now - this->polarity_window_start_ >= POLARITY_WINDOW_MS) {
      uint32_t frames = this->valid_frame_count_;
      uint32_t errors = this->rx_error_count_;
      if (frames > 0) {
        ESP_LOGI(TAG, "A/B polarity OK (inverted=%s): %u valid frames in %ums",
                 this->inversion_active_ ? "true" : "false",
                 (unsigned) frames, (unsigned) POLARITY_WINDOW_MS);
        this->polarity_state_ = POLARITY_DONE;
      } else if (errors > POLARITY_ERROR_THRESHOLD) {
        if (this->polarity_state_ == POLARITY_LISTENING && !this->inversion_active_) {
          this->inversion_active_ = true;
          this->apply_line_inversion_();
          ESP_LOGW(TAG, "Bus looks A/B-INVERTED (%u breaks/errors, 0 frames) -> inverting RX+TX, confirming...",
                   (unsigned) errors);
          this->valid_frame_count_ = 0;
          this->rx_error_count_ = 0;
          this->polarity_window_start_ = now;
          this->polarity_state_ = POLARITY_CONFIRMING;
        } else {
          ESP_LOGW(TAG, "Still no valid frames after inverting RX+TX (%u breaks/errors). Not a simple "
                        "A/B inversion -> check wiring & fail-safe bias (INVESTIGATION 4quater).",
                   (unsigned) errors);
          this->polarity_state_ = POLARITY_DONE;
        }
      } else {
        // Little/no traffic: can't decide. Keep listening (handles late bus power-up).
        if (!this->no_traffic_warned_) {
          ESP_LOGW(TAG, "No bus traffic yet (%u breaks/errors, 0 frames) -> waiting to auto-detect polarity",
                   (unsigned) errors);
          this->no_traffic_warned_ = true;
        }
        this->valid_frame_count_ = 0;
        this->rx_error_count_ = 0;
        this->polarity_window_start_ = now;
      }
    }
  }

  if (this->auto_scan_ && !this->combo_locked_) {
    uint32_t now = millis();
    if (this->auto_scan_last_change_ == 0 ||
        now - this->auto_scan_last_change_ > AUTO_SCAN_INTERVAL_MS) {
      this->auto_scan_last_change_ = now;
      const auto &c = AUTO_SCAN_TABLE[this->auto_scan_idx_];
      this->slave_addr_ = c.slave;
      this->master_addr_ = c.master;
      this->slave_type_ = c.type;
      ESP_LOGW(TAG, "AUTO-SCAN [%u/%u]: slave=0x%02X master=0x%02X type=0x%02X",
               this->auto_scan_idx_ + 1, AUTO_SCAN_COUNT,
               this->slave_addr_, this->master_addr_, this->slave_type_);
      this->auto_scan_idx_ = (this->auto_scan_idx_ + 1) % AUTO_SCAN_COUNT;
    }
  }

  if (this->tx_test_) {
    uint32_t now = millis();
    if (this->tx_test_last_ == 0 || now - this->tx_test_last_ > 2000) {
      this->tx_test_last_ = now;
      this->tx_diag();
    }
  }

  // Sniffer heartbeat: periodic counts so a wrong-polarity bus (frames arriving
  // but unparseable = junk) is distinguishable from a dead bus (all zero).
  if (this->sniffer_) {
    uint32_t now = millis();
    if (this->sniff_stats_last_ == 0) {
      this->sniff_stats_last_ = now;
    } else if (now - this->sniff_stats_last_ >= SNIFF_STATS_MS) {
      uint32_t v = this->sniff_valid_ - this->sniff_valid_snap_;
      uint32_t j = this->sniff_junk_ - this->sniff_junk_snap_;
      uint32_t e = this->rx_error_count_ >= this->sniff_err_snap_
                   ? this->rx_error_count_ - this->sniff_err_snap_ : this->rx_error_count_;
      this->sniff_valid_snap_ = this->sniff_valid_;
      this->sniff_junk_snap_ = this->sniff_junk_;
      this->sniff_err_snap_ = this->rx_error_count_;
      ESP_LOGI(TAG, "sniffer: %u valid, %u junk, %u breaks/errs in %us (ab_inv=%s)",
               (unsigned) v, (unsigned) j, (unsigned) e, (unsigned) (SNIFF_STATS_MS / 1000),
               this->inversion_active_ ? "on" : "off");
      this->sniff_stats_last_ = now;
    }
  }
}

// (Re)apply the UART inversion mask (RXD+TXD together — a single A/B swap
// affects both directions) and flush stale RX captured with the previous
// polarity. Defined outside the USE_ESP_IDF block (with an inner guard)
// because loop() may call it on any build.
void HormannHCP1Component::apply_line_inversion_() {
#ifdef USE_ESP_IDF
  uart_port_t port = static_cast<uart_port_t>(this->uart_num_);
  uint32_t mask = this->inversion_active_ ? (UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV) : 0;
  uart_set_line_inverse(port, mask);
  uart_flush_input(port);
  this->rx_counter_ = 0;
#endif
}

#ifdef USE_ESP_IDF

void HormannHCP1Component::bus_task_trampoline(void *arg) {
  static_cast<HormannHCP1Component *>(arg)->bus_task();
}

void HormannHCP1Component::bus_task() {
  uart_port_t port = static_cast<uart_port_t>(this->uart_num_);
  uart_event_t event;
  uint8_t buf[64];

  for (;;) {
    // Test A/B (drive vs timing): fire the EXACT scan-reply frame from THIS task on a
    // 2s WALL-CLOCK timer — async to the master's traffic — via the same send_frame()
    // path. With the master OFF it lands in the clear (drive proof); with the master ON
    // it lands at random offsets: if NONE reach the witness clean -> drive/loading
    // problem; if some do -> pure timing/collision. Checked each iteration so it fires
    // even when the queue is busy with master frames.
    if (this->bustask_tx_test_ && this->de_pin_ != nullptr) {
      uint32_t now = millis();
      if (this->bustask_tx_last_ == 0 || now - this->bustask_tx_last_ >= 2000) {
        this->bustask_tx_last_ = now;
        this->tx_buffer_[0] = this->master_addr_;
        this->tx_buffer_[1] = 0x02 | 0x10;
        this->tx_buffer_[2] = this->slave_type_;
        this->tx_buffer_[3] = this->slave_addr_;
        this->tx_buffer_[4] = calculate_crc(this->tx_buffer_, 4);
        ESP_LOGW(TAG, "BUSTASK-TX-TEST: firing scan-reply (80:%02X:%02X:%02X) from bus_task",
                 this->tx_buffer_[1], this->slave_type_, this->slave_addr_);
        send_frame(5);
      }
    }
    TickType_t wait = this->bustask_tx_test_ ? pdMS_TO_TICKS(200) : portMAX_DELAY;
    if (xQueueReceive(this->uart_queue_, &event, wait) != pdTRUE) {
      continue;
    }
    switch (event.type) {
      case UART_BREAK:
        // Mark frame boundary: try to parse what we have so far, then reset.
        this->rx_error_count_++;  // polarity auto-detect: breaks dominate when inverted
        try_parse_buffered();
        this->rx_counter_ = 0;
        break;
      case UART_DATA: {
        int len = uart_read_bytes(port, buf, std::min<int>(event.size, sizeof(buf)), 0);
        if (len > 0) this->last_rx_us_ = esp_timer_get_time();  // reply-latency instrumentation
        // Raw byte dump — VERBOSE only. Building this hex string every chunk from a
        // priority-23 task is what flooded the logger and faulted core 1, so it is
        // compiled out below VERBOSE. The sniffer covers normal use.
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
        if (len > 0) {
          char hexbuf[256]; int pos = 0;
          for (int i = 0; i < len && pos < 250; i++)
            pos += snprintf(hexbuf + pos, sizeof(hexbuf) - pos, "%02X:", buf[i]);
          if (pos > 0) hexbuf[pos - 1] = '\0';
          ESP_LOGV(TAG, "<<< %s", hexbuf);
        }
#endif
        for (int i = 0; i < len && this->rx_counter_ < sizeof(this->rx_buffer_); i++) {
          this->rx_buffer_[this->rx_counter_++] = buf[i];
        }
        // Eager reply: answer a scan/status addressed to us the instant it is complete,
        // without waiting for the ~0.5 ms inter-frame timeout that pushes our reply out
        // of the master's slot. Listen-only nodes (no de_pin) skip this and just sniff.
        if (this->de_pin_ != nullptr && this->eager_reply_to_us_()) {
          this->rx_counter_ = 0;
        } else if (event.timeout_flag) {
          // Inter-frame gap -> frame complete: full parse (broadcast/state/sniffer).
          try_parse_buffered();
          this->rx_counter_ = 0;
        }
        break;
      }
      case UART_FIFO_OVF:
      case UART_BUFFER_FULL:
        ESP_LOGW(TAG, "UART overflow, flushing");
        uart_flush_input(port);
        xQueueReset(this->uart_queue_);
        this->rx_counter_ = 0;
        break;
      case UART_FRAME_ERR:
      case UART_PARITY_ERR:
        this->rx_error_count_++;  // polarity auto-detect: framing errors when inverted/garbled
        try_parse_buffered();
        this->rx_counter_ = 0;
        break;
      default:
        break;
    }
  }
}

// Eager path: as soon as a complete, valid frame addressed to us (scan or status
// request) is in the buffer, reply immediately — don't wait for the inter-frame gap.
// Returns true if it replied (caller then clears the buffer).
bool HormannHCP1Component::eager_reply_to_us_() {
  for (uint8_t off = 0; off + 4 <= this->rx_counter_; off++) {
    if (this->rx_buffer_[off] != this->slave_addr_) continue;
    uint8_t total = 3 + (this->rx_buffer_[off + 1] & 0x0F);
    if (off + total > this->rx_counter_) continue;          // not fully received yet
    if (calculate_crc(this->rx_buffer_ + off, total) != 0x00) continue;
    uint8_t len = this->rx_buffer_[off + 1] & 0x0F;
    uint8_t cmd = this->rx_buffer_[off + 2];
    uint8_t counter = (this->rx_buffer_[off + 1] & 0xF0) + 0x10;
    if (off > 0) memmove(this->rx_buffer_, this->rx_buffer_ + off, total);
    if (len == 0x02 && cmd == CMD_SLAVE_SCAN) { this->process_slave_scan(counter); return true; }
    if (len == 0x01 && cmd == CMD_SLAVE_STATUS_REQUEST) { this->process_status_request(counter); return true; }
    return false;  // a frame to us but not scan/status -> let the full parse handle it
  }
  return false;
}

// Scan the rolling RX buffer for a valid Hörmann frame.
// A valid frame: [addr ∈ {0x00, 0x28, 0x80}][cnt|len][payload...][crc]
// where total length = 3 + (cnt|len & 0x0F) and CRC over the whole frame == 0x00.
void HormannHCP1Component::try_parse_buffered() {
  if (this->rx_counter_ < 4) return;
  // Raw RX dump — VERBOSE only (compiled out otherwise; see bus_task note above).
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  char hexbuf[128]; int pos = 0;
  for (uint8_t i = 0; i < this->rx_counter_ && pos < 120; i++)
    pos += snprintf(hexbuf + pos, sizeof(hexbuf) - pos, "%02X:", this->rx_buffer_[i]);
  if (pos > 0) hexbuf[pos - 1] = '\0';
  ESP_LOGV(TAG, "RX[%u]: %s", this->rx_counter_, hexbuf);
#endif
  if (this->sniffer_) this->sniff_scan_();
  for (uint8_t off = 0; off + 4 <= this->rx_counter_; off++) {
    uint8_t addr = this->rx_buffer_[off];
    if (addr != BROADCAST_ADDR && addr != this->slave_addr_ && addr != this->master_addr_) continue;
    uint8_t total = 3 + (this->rx_buffer_[off + 1] & 0x0F);
    if (off + total > this->rx_counter_) continue;
    if (calculate_crc(this->rx_buffer_ + off, total) != 0x00) continue;
    this->valid_frame_count_++;  // polarity auto-detect: a good frame means polarity is correct
    if (off > 0) memmove(this->rx_buffer_, this->rx_buffer_ + off, total);
    parse_message();
    return;
  }
}

// Sniffer: read-only pass over the RX buffer. Counts every valid-CRC frame
// (any address — so the master's address sweep counts as real traffic, not junk)
// and logs only the ones interesting for us (broadcast / our slave / master).
void HormannHCP1Component::sniff_scan_() {
  bool any = false;
  for (uint8_t off = 0; off + 4 <= this->rx_counter_; off++) {
    uint8_t total = 3 + (this->rx_buffer_[off + 1] & 0x0F);
    if (off + total > this->rx_counter_) continue;
    if (calculate_crc(this->rx_buffer_ + off, total) != 0x00) continue;
    any = true;
    this->sniff_valid_++;
    uint8_t addr = this->rx_buffer_[off];
    if (addr == BROADCAST_ADDR || addr == this->slave_addr_ || addr == this->master_addr_)
      this->sniff_log_frame_(this->rx_buffer_ + off, total);
    off += total - 1;  // skip past this frame
  }
  if (!any) {
    this->sniff_junk_++;  // data present but nothing valid: garbled TX / wrong polarity
    // Show the unparseable bytes (rate-limited) — this is how we SEE garage-3's reply
    // landing garbled on the bus: truncated (DE), fragmented (collision) or bad CRC (format).
    uint32_t now = millis();
    if (now - this->sniff_junk_log_ms_ >= SNIFF_JUNK_MIN_MS) {
      this->sniff_junk_log_ms_ = now;
      char hex[72];
      int p = 0;
      for (uint8_t i = 0; i < this->rx_counter_ && p < 68; i++)
        p += snprintf(hex + p, sizeof(hex) - p, "%02X:", this->rx_buffer_[i]);
      if (p > 0) hex[p - 1] = '\0'; else hex[0] = '\0';
      ESP_LOGI(TAG, "SNIFF JUNK[%u]        %s", (unsigned) this->rx_counter_, hex);
    }
  }
}

// Log one valid frame, categorized and de-duplicated. Identical frames (ignoring
// the rolling counter nibble) are collapsed and re-logged at most every SNIFF_REFRESH_MS.
void HormannHCP1Component::sniff_log_frame_(const uint8_t *frame, uint8_t length) {
  uint8_t plen = frame[1] & 0x0F;  // payload length; high nibble of frame[1] = counter (ignored)

  uint8_t key[18];
  uint8_t kl = 0;
  key[kl++] = frame[0];
  key[kl++] = plen;
  for (uint8_t i = 0; i < plen && kl < sizeof(key); i++) key[kl++] = frame[2 + i];

  uint32_t now = millis();
  bool same = (kl == this->sniff_last_len_) && (memcmp(key, this->sniff_last_key_, kl) == 0);
  if (same && (now - this->sniff_last_log_ms_) < SNIFF_REFRESH_MS) {
    this->sniff_suppressed_++;
    return;
  }

  char catbuf[24];
  const char *cat;
  uint8_t addr = frame[0];
  uint8_t cmd = plen >= 1 ? frame[2] : 0xFF;
  if (addr == BROADCAST_ADDR) {
    cat = "BCAST";
  } else if (addr == this->slave_addr_) {
    if (cmd == CMD_SLAVE_SCAN) { cat = "SCAN->us"; this->sniff_req_ms_ = now; }
    else if (cmd == CMD_SLAVE_STATUS_REQUEST) { cat = "STATUS_REQ->us ***"; this->sniff_req_ms_ = now; }
    else { snprintf(catbuf, sizeof(catbuf), "->us cmd=0x%02X", cmd); cat = catbuf; }
  } else if (addr == this->master_addr_) {
    // Is this OUR reply (the device-under-test answering the master)? A real UAP1
    // scan-response is [type 0x14][slave 0x28]; status-response carries cmd 0x29.
    // Flag it distinctly + the passive latency since the matching request, so the
    // witness CONFIRMS our reply reaches the bus clean and at ~3.84 ms like a real UAP1.
    bool our_scanresp = (plen >= 2 && frame[2] == this->slave_type_ && frame[3] == this->slave_addr_);
    bool our_statusresp = (cmd == CMD_SLAVE_STATUS_RESPONSE);
    if (our_scanresp || our_statusresp) {
      uint32_t lat = this->sniff_req_ms_ ? (now - this->sniff_req_ms_) : 0;
      snprintf(catbuf, sizeof(catbuf), "%s +%ums", our_scanresp ? "OUR-SCANRESP<<<" : "OUR-STATUSRESP<<<",
               (unsigned) lat);
      cat = catbuf;
    } else {
      cat = "->master";
    }
  } else {
    cat = "?";
  }

  char hex[64];
  int p = 0;
  for (uint8_t i = 0; i < length && p < 58; i++)
    p += snprintf(hex + p, sizeof(hex) - p, "%02X:", frame[i]);
  if (p > 0) hex[p - 1] = '\0'; else hex[0] = '\0';

  int64_t nowus = esp_timer_get_time();
  long long dus = this->sniff_prev_log_us_ ? (nowus - this->sniff_prev_log_us_) : 0;
  this->sniff_prev_log_us_ = nowus;
  if (this->sniff_suppressed_ > 0)
    ESP_LOGI(TAG, "SNIFF d=%6lldus %-18s %s  (+%u identical)", dus, cat, hex, (unsigned) this->sniff_suppressed_);
  else
    ESP_LOGI(TAG, "SNIFF d=%6lldus %-18s %s", dus, cat, hex);

  memcpy(this->sniff_last_key_, key, kl);
  this->sniff_last_len_ = kl;
  this->sniff_suppressed_ = 0;
  this->sniff_last_log_ms_ = now;
}

void HormannHCP1Component::parse_message() {
  uint8_t address = this->rx_buffer_[0];
  uint8_t length = this->rx_buffer_[1] & 0x0F;
  uint8_t counter = (this->rx_buffer_[1] & 0xF0) + 0x10;

  if (address == BROADCAST_ADDR) {
    if (length == 0x02) process_broadcast(length);
  } else if (address == this->slave_addr_) {
    if (length == 0x02 && this->rx_buffer_[2] == CMD_SLAVE_SCAN) {
      process_slave_scan(counter);
    } else if (length == 0x01 && this->rx_buffer_[2] == CMD_SLAVE_STATUS_REQUEST) {
      process_status_request(counter);
    }
  }
}

void HormannHCP1Component::process_broadcast(uint8_t length) {
  this->broadcast_status_ = this->rx_buffer_[2] | ((uint16_t) this->rx_buffer_[3] << 8);
  uint8_t d0 = this->rx_buffer_[2];
  uint8_t d1 = this->rx_buffer_[3];

  DoorState prev = this->door_state_;

  if (d0 & 0x01) this->door_state_.cover = COVER_OPEN;
  else if (d0 & 0x02) this->door_state_.cover = COVER_CLOSED;
  else if ((d0 & 0x60) == 0x40) this->door_state_.cover = COVER_OPENING;
  else if ((d0 & 0x60) == 0x60) this->door_state_.cover = COVER_CLOSING;
  else this->door_state_.cover = COVER_STOPPED;

  this->door_state_.option_relay = (d0 & 0x04) != 0;
  this->door_state_.light = (d0 & 0x08) != 0;
  this->door_state_.error = (d0 & 0x10) != 0;
  this->door_state_.venting = (d0 & 0x80) != 0;
  this->door_state_.prewarn = (d1 & 0x01) != 0;
  this->door_state_.data_valid = true;

  bool changed = prev.cover != this->door_state_.cover ||
                 prev.light != this->door_state_.light ||
                 prev.error != this->door_state_.error ||
                 prev.venting != this->door_state_.venting ||
                 prev.prewarn != this->door_state_.prewarn ||
                 prev.option_relay != this->door_state_.option_relay ||
                 !prev.data_valid;
  if (changed) {
    ESP_LOGI(TAG, "Door state: cover=%d light=%d err=%d vent=%d",
             (int) this->door_state_.cover, this->door_state_.light,
             this->door_state_.error, this->door_state_.venting);
    this->state_changed_ = true;
  }
}

void HormannHCP1Component::process_slave_scan(uint8_t counter) {
  this->tx_buffer_[0] = this->master_addr_;
  this->tx_buffer_[1] = 0x02 | counter;
  this->tx_buffer_[2] = this->slave_type_;
  this->tx_buffer_[3] = this->slave_addr_;
  this->tx_buffer_[4] = calculate_crc(this->tx_buffer_, 4);
  send_frame(5);
}

void HormannHCP1Component::process_status_request(uint8_t counter) {
  if (this->auto_scan_ && !this->combo_locked_) {
    this->combo_locked_ = true;
    ESP_LOGW(TAG, "*** WORKING COMBO: slave=0x%02X master=0x%02X type=0x%02X ***",
             this->slave_addr_, this->master_addr_, this->slave_type_);
  }
  this->tx_buffer_[0] = this->master_addr_;
  this->tx_buffer_[1] = 0x03 | counter;
  this->tx_buffer_[2] = CMD_SLAVE_STATUS_RESPONSE;
  this->tx_buffer_[3] = (uint8_t) this->slave_response_data_;
  this->tx_buffer_[4] = (uint8_t) (this->slave_response_data_ >> 8);
  this->slave_response_data_ = RESPONSE_DEFAULT;
  this->tx_buffer_[5] = calculate_crc(this->tx_buffer_, 5);
  send_frame(6);
}

void HormannHCP1Component::send_frame(uint8_t length) {
#ifdef USE_ESP_IDF
  // Listen-only node (no DE pin, e.g. the witness): never drive the bus, and never
  // block this task on a TX that goes nowhere — otherwise the ~4 ms uart_wait_tx_done
  // makes it miss/concatenate incoming frames and report false "junk". Pure observer.
  if (this->de_pin_ == nullptr)
    return;

  // Configurable micro-delay to slide our reply inside the master's expected window.
  if (this->reply_delay_us_ > 0)
    esp_rom_delay_us(this->reply_delay_us_);

  uart_port_t port = static_cast<uart_port_t>(this->uart_num_);

  int64_t t0 = esp_timer_get_time();

  // Manual DE control — raise BEFORE writing.
  if (this->re_pin_ != nullptr) this->re_pin_->digital_write(true);
  if (this->de_pin_ != nullptr) this->de_pin_->digital_write(!this->de_invert_);
  esp_rom_delay_us(20);  // allow the line to settle

  // Fast leading "break": prepend a 0x00 byte at 19200 8N1 = 1 start + 8 data low + 1 stop
  // = ~470 µs low which most HCP masters accept as sync break (~12 bit-times).
  // No baud-switch (which would cost ~40 ms via uart_set_baudrate).
  uint8_t framed[20];
  framed[0] = 0x00;
  for (uint8_t i = 0; i < length; i++) framed[i + 1] = this->tx_buffer_[i];
  uart_write_bytes(port, framed, length + 1);
  uart_wait_tx_done(port, pdMS_TO_TICKS(20));
  // wait_tx_done returns when the FIFO is empty, but the shift register may still
  // be clocking out the last byte. Hold DE for an extra full byte time (~520 µs)
  // before releasing, otherwise the last bits get truncated on the wire.
  esp_rom_delay_us(600);

  if (this->de_pin_ != nullptr) this->de_pin_->digital_write(this->de_invert_);
  if (this->re_pin_ != nullptr) this->re_pin_->digital_write(false);
  int64_t t1 = esp_timer_get_time();
  ESP_LOGW(TAG, "TX took %lldus (reply lat %lldus since last RX byte)",
           (long long)(t1 - t0), (long long)(t0 - this->last_rx_us_));
#endif
}

void HormannHCP1Component::trigger_action(HormannAction action) {
  switch (action) {
    case ACTION_STOP:
      if (this->door_state_.cover == COVER_OPENING || this->door_state_.cover == COVER_CLOSING)
        this->slave_response_data_ = RESPONSE_IMPULSE;
      break;
    case ACTION_OPEN: this->slave_response_data_ = RESPONSE_OPEN; break;
    case ACTION_CLOSE: this->slave_response_data_ = RESPONSE_CLOSE; break;
    case ACTION_VENTING: this->slave_response_data_ = RESPONSE_VENTING; break;
    case ACTION_TOGGLE_LIGHT: this->slave_response_data_ = RESPONSE_TOGGLE_LIGHT; break;
    case ACTION_EMERGENCY_STOP: this->slave_response_data_ = RESPONSE_EMERGENCY_STOP; break;
    case ACTION_IMPULSE: this->slave_response_data_ = RESPONSE_IMPULSE; break;
    default: break;
  }
}

void HormannHCP1Component::tx_diag() {
#ifdef USE_ESP_IDF
  uart_port_t port = static_cast<uart_port_t>(this->uart_num_);
  // Recognizable marker pattern: 0xDE 0xAD 0xBE 0xEF repeated.
  uint8_t buf[16];
  for (int i = 0; i < 16; i += 4) { buf[i] = 0xDE; buf[i+1] = 0xAD; buf[i+2] = 0xBE; buf[i+3] = 0xEF; }

  // Raise DE BEFORE writing (manual control — same as send_frame).
  if (this->re_pin_ != nullptr) this->re_pin_->digital_write(true);
  if (this->de_pin_ != nullptr) this->de_pin_->digital_write(!this->de_invert_);
  esp_rom_delay_us(20);

  uart_write_bytes(port, buf, 16);
  uart_wait_tx_done(port, pdMS_TO_TICKS(100));
  esp_rom_delay_us(600);  // hold DE for the last byte's shift-out

  if (this->de_pin_ != nullptr) this->de_pin_->digital_write(this->de_invert_);
  if (this->re_pin_ != nullptr) this->re_pin_->digital_write(false);
  ESP_LOGW(TAG, "TX DIAG: sent 16 bytes (DE/RE toggled)");
#endif
}

#endif  // USE_ESP_IDF

}  // namespace hormann_hcp1
}  // namespace esphome
