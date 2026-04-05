#include "waveshare_2in15b.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace waveshare_2in15b {

static const char *const TAG = "waveshare_2in15b";

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
// Wait for BUSY HIGH (idle). No timeout version for critical steps.
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::wait_busy_high_(uint32_t timeout_ms, const char *label) {
  if (this->busy_pin_ == nullptr) {
    delay(timeout_ms);
    return;
  }
  uint32_t start = millis();
  while (!this->busy_pin_->digital_read()) {
    if (millis() - start > timeout_ms) {
      ESP_LOGW(TAG, "[%s] BUSY still LOW after %ums", label, timeout_ms);
      return;
    }
    delay(10);
    App.feed_wdt();
  }
  ESP_LOGI(TAG, "[%s] BUSY HIGH after %ums", label, millis() - start);
}

// ---------------------------------------------------------------------------
// After refresh: wait for BUSY to go LOW (busy) then HIGH (done)
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    delay(20000);
    return;
  }
  // Step 1: wait for LOW (display starts refreshing) — up to 2s
  uint32_t start = millis();
  while (this->busy_pin_->digital_read()) {
    if (millis() - start > 2000) {
      ESP_LOGW(TAG, "BUSY never went LOW after refresh cmd — display may not have accepted it");
      return;
    }
    delay(10);
    App.feed_wdt();
  }
  ESP_LOGI(TAG, "BUSY went LOW (refreshing) after %ums", millis() - start);

  // Step 2: wait for HIGH (refresh complete) — up to 30s
  start = millis();
  while (!this->busy_pin_->digital_read()) {
    if (millis() - start > 30000) {
      ESP_LOGW(TAG, "BUSY stuck LOW 30s — refresh may have failed");
      return;
    }
    delay(50);
    App.feed_wdt();
  }
  ESP_LOGI(TAG, "BUSY HIGH (refresh done) after %ums", millis() - start);
}

// ---------------------------------------------------------------------------
// Hardware reset — hold LOW longer to fully reset the controller
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::hardware_reset_() {
  if (this->reset_pin_ == nullptr) return;

  this->reset_pin_->digital_write(true);  delay(20);
  this->reset_pin_->digital_write(false); delay(20);   // hold LOW longer
  this->reset_pin_->digital_write(true);

  // Now wait up to 10s for BUSY to go HIGH (display ready after reset)
  ESP_LOGI(TAG, "Waiting for BUSY HIGH after reset...");
  this->wait_busy_high_(10000, "post-reset");
}

// ---------------------------------------------------------------------------
// Init sequence
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::initialize_display_() {
  ESP_LOGI(TAG, "======= INIT START =======");
  ESP_LOGI(TAG, "BUSY at start: %s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH" : "LOW") : "no pin");

  this->hardware_reset_();

  ESP_LOGI(TAG, "CMD 0x06 booster soft start");
  this->send_command_(CMD_BOOSTER_SOFT_START);
  this->send_data_(0x17);
  this->send_data_(0x17);
  this->send_data_(0x17);

  ESP_LOGI(TAG, "CMD 0x04 power on");
  this->send_command_(CMD_POWER_ON);
  // Wait up to 5s for power on to complete
  this->wait_busy_high_(5000, "post-power-on");

  ESP_LOGI(TAG, "CMD 0x00 panel setting");
  this->send_command_(CMD_PANEL_SETTING);
  this->send_data_(0x0F);

  ESP_LOGI(TAG, "CMD 0x61 resolution 160x296");
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
  ESP_LOGI(TAG, "BUSY at end of init: %s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH(idle)" : "LOW(busy)") : "no pin");
  ESP_LOGI(TAG, "======= INIT DONE =======");
}

void WaveshareEPaper2in15B::setup() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
  }
  this->dc_pin_->setup();
  if (this->busy_pin_ != nullptr)
    this->busy_pin_->setup();
  this->spi_setup();
  memset(this->bw_buffer_,  0xFF, EPD_BUFFER_SIZE);
  memset(this->red_buffer_, 0xFF, EPD_BUFFER_SIZE);
}

void WaveshareEPaper2in15B::dump_config() {
  LOG_DISPLAY("", "Waveshare 2.15\" B E-Paper", this);
  LOG_PIN("  DC Pin:    ", this->dc_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin:  ", this->busy_pin_);
  ESP_LOGCONFIG(TAG, "  Initialized: %s", this->initialized_ ? "YES" : "NO");
  LOG_UPDATE_INTERVAL(this);
}

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

void WaveshareEPaper2in15B::update() {
  ESP_LOGI(TAG, "update() — re-initialising display");
  this->initialized_ = false;
  this->initialize_display_();

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

  ESP_LOGI(TAG, "Sending B/W buffer...");
  this->send_command_(CMD_DATA_START_TX_BW);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->bw_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  ESP_LOGI(TAG, "Sending RED buffer...");
  this->send_command_(CMD_DATA_START_TX_RED);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->red_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  ESP_LOGI(TAG, "CMD 0x12 display refresh");
  this->send_command_(CMD_DISPLAY_REFRESH);
  delay(100);

  this->wait_until_idle_();
  ESP_LOGI(TAG, "======= Refresh complete =======");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
