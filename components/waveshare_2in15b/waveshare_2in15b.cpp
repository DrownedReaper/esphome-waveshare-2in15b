#include "waveshare_2in15b.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace waveshare_2in15b {

static const char *const TAG = "waveshare_2in15b";

// ---------------------------------------------------------------------------
// SPI helpers
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::send_command_(uint8_t cmd) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(cmd);
  this->disable();
}

void WaveshareEPaper2in15B::send_data_(uint8_t data) {
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_byte(data);
  this->disable();
}

// ---------------------------------------------------------------------------
// BUSY
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    delay(3000);
    return;
  }
  uint32_t start = millis();
  while (!this->busy_pin_->digital_read()) {
    if (millis() - start > 30000) {
      ESP_LOGW(TAG, "BUSY timeout 30s");
      break;
    }
    delay(50);
    App.feed_wdt();
  }
  ESP_LOGD(TAG, "BUSY released after %ums", millis() - start);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::hardware_reset_() {
  if (this->reset_pin_ == nullptr) return;
  this->reset_pin_->digital_write(true);  delay(20);
  this->reset_pin_->digital_write(false); delay(5);
  this->reset_pin_->digital_write(true);  delay(20);
}

// ---------------------------------------------------------------------------
// Pin state diagnostic — logs every pin's logical level
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::log_pin_states_(const char *context) {
  ESP_LOGI(TAG, "--- Pin states @ %s ---", context);
  if (this->busy_pin_ != nullptr)
    ESP_LOGI(TAG, "  BUSY = %s", this->busy_pin_->digital_read() ? "HIGH" : "LOW");
  else
    ESP_LOGI(TAG, "  BUSY = not configured");
  // DC and RST are output-only — we log what we last set them to
  ESP_LOGI(TAG, "  (DC and RST are outputs — see last set values in surrounding logs)");
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::initialize_display_() {
  ESP_LOGI(TAG, "=== EPD init start ===");

  // --- RST pin test: pulse it and log ---
  ESP_LOGI(TAG, "RST: going LOW");
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(false);
    delay(5);
    ESP_LOGI(TAG, "RST: going HIGH");
    this->reset_pin_->digital_write(true);
    delay(20);
  }

  // --- DC pin test ---
  ESP_LOGI(TAG, "DC: going LOW (command mode)");
  this->dc_pin_->digital_write(false);
  delay(5);
  ESP_LOGI(TAG, "DC: going HIGH (data mode)");
  this->dc_pin_->digital_write(true);
  delay(5);
  ESP_LOGI(TAG, "DC: returning LOW");
  this->dc_pin_->digital_write(false);

  // --- CS pin test ---
  ESP_LOGI(TAG, "CS: asserting (LOW)");
  this->enable();
  delay(5);
  ESP_LOGI(TAG, "CS: releasing (HIGH)");
  this->disable();

  delay(100);
  this->log_pin_states_("after pin tests");

  // --- Full hardware reset ---
  this->hardware_reset_();
  delay(100);
  this->log_pin_states_("after hardware reset");

  ESP_LOGI(TAG, "Sending CMD_BOOSTER_SOFT_START (0x06)...");
  this->send_command_(CMD_BOOSTER_SOFT_START);
  this->send_data_(0x17);
  this->send_data_(0x17);
  this->send_data_(0x17);
  this->log_pin_states_("after booster soft start");

  ESP_LOGI(TAG, "Sending CMD_POWER_ON (0x04)...");
  this->send_command_(CMD_POWER_ON);
  delay(200);
  this->log_pin_states_("after power on (200ms delay)");

  ESP_LOGI(TAG, "Sending CMD_PANEL_SETTING (0x00)...");
  this->send_command_(CMD_PANEL_SETTING);
  this->send_data_(0x0F);

  ESP_LOGI(TAG, "Sending CMD_RESOLUTION_SETTING (0x61)...");
  this->send_command_(CMD_RESOLUTION_SETTING);
  this->send_data_(0x00);
  this->send_data_(0xA0);
  this->send_data_(0x01);
  this->send_data_(0x28);

  this->send_command_(CMD_VCOM_DATA_INTERVAL);
  this->send_data_(0x11);

  this->send_command_(CMD_TCON_SETTING);
  this->send_data_(0x22);

  this->initialized_ = true;
  ESP_LOGI(TAG, "=== EPD init complete ===");
  this->log_pin_states_("after full init");
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::setup() {
  ESP_LOGI(TAG, "*** setup() called ***");

  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
    ESP_LOGI(TAG, "RST pin configured");
  } else {
    ESP_LOGW(TAG, "RST pin NOT configured");
  }

  this->dc_pin_->setup();
  ESP_LOGI(TAG, "DC pin configured");

  if (this->busy_pin_ != nullptr) {
    this->busy_pin_->setup();
    ESP_LOGI(TAG, "BUSY pin configured, logical state: %s",
             this->busy_pin_->digital_read() ? "HIGH" : "LOW");
  } else {
    ESP_LOGW(TAG, "BUSY pin NOT configured");
  }

  this->spi_setup();
  ESP_LOGI(TAG, "SPI configured");

  memset(this->bw_buffer_,  0xFF, EPD_BUFFER_SIZE);
  memset(this->red_buffer_, 0xFF, EPD_BUFFER_SIZE);

  this->initialize_display_();
}

// ---------------------------------------------------------------------------
// dump_config()
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::dump_config() {
  LOG_DISPLAY("", "Waveshare 2.15\" B E-Paper", this);
  LOG_PIN("  DC Pin:    ", this->dc_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin:  ", this->busy_pin_);
  ESP_LOGCONFIG(TAG, "  Initialized: %s", this->initialized_ ? "YES" : "NO");
  LOG_UPDATE_INTERVAL(this);
}

// ---------------------------------------------------------------------------
// draw_absolute_pixel_internal()
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT)
    return;

  uint32_t byte_idx = (x + y * EPD_WIDTH) / 8;
  uint8_t  bit_mask = 0x80 >> (x % 8);

  bool is_red   = (color.r > 200 && color.g < 100 && color.b < 100);
  bool is_black = (!is_red && color.r < 64 && color.g < 64 && color.b < 64);

  if (is_red) {
    this->red_buffer_[byte_idx] &= ~bit_mask;
    this->bw_buffer_[byte_idx]  |=  bit_mask;
  } else if (is_black) {
    this->bw_buffer_[byte_idx]  &= ~bit_mask;
    this->red_buffer_[byte_idx] |=  bit_mask;
  } else {
    this->bw_buffer_[byte_idx]  |=  bit_mask;
    this->red_buffer_[byte_idx] |=  bit_mask;
  }
}

// ---------------------------------------------------------------------------
// update()
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::update() {
  if (!this->initialized_) {
    ESP_LOGW(TAG, "Not initialized — running init from update()");
    this->initialize_display_();
  }

  memset(this->bw_buffer_,  0xFF, EPD_BUFFER_SIZE);
  memset(this->red_buffer_, 0xFF, EPD_BUFFER_SIZE);

  if (this->writer_.has_value())
    (*this->writer_)(*this);

  uint32_t black_px = 0, red_px = 0;
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    black_px += __builtin_popcount(~this->bw_buffer_[i]  & 0xFF);
    red_px   += __builtin_popcount(~this->red_buffer_[i] & 0xFF);
  }
  ESP_LOGI(TAG, "Frame: %u black, %u red pixels", black_px, red_px);

  this->send_command_(CMD_DATA_START_TX_BW);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->bw_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  this->send_command_(CMD_DATA_START_TX_RED);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->red_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  ESP_LOGI(TAG, "Triggering refresh...");
  this->log_pin_states_("before refresh command");
  this->send_command_(CMD_DISPLAY_REFRESH);
  delay(100);
  this->log_pin_states_("after refresh command");
  this->wait_until_idle_();

  ESP_LOGI(TAG, "Refresh complete.");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
