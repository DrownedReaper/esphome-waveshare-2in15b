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
// BUSY pin
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    ESP_LOGW(TAG, "No BUSY pin — blind 3000ms delay");
    delay(3000);
    return;
  }

  bool initial = this->busy_pin_->digital_read();
  ESP_LOGD(TAG, "wait_until_idle: pin is %s", initial ? "HIGH(idle)" : "LOW(busy)");

  uint32_t start = millis();
  // Wait for HIGH (idle). If pin is inverted: true in YAML, ESPHome handles
  // the inversion transparently, so we always wait for logical HIGH here.
  while (!this->busy_pin_->digital_read()) {
    if (millis() - start > 15000) {
      ESP_LOGW(TAG, "BUSY timeout after 15s — pin still LOW");
      break;
    }
    delay(20);
    App.feed_wdt();
  }
  ESP_LOGD(TAG, "BUSY released after %ums", millis() - start);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::hardware_reset_() {
  if (this->reset_pin_ == nullptr) {
    ESP_LOGW(TAG, "No RESET pin — skipping HW reset");
    return;
  }
  ESP_LOGD(TAG, "HW reset pulse...");
  this->reset_pin_->digital_write(true);  delay(20);
  this->reset_pin_->digital_write(false); delay(5);
  this->reset_pin_->digital_write(true);  delay(20);
  ESP_LOGD(TAG, "HW reset done");
}

// ---------------------------------------------------------------------------
// Init sequence
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::initialize_display_() {
  ESP_LOGI(TAG, "=== Initialising display ===");

  this->hardware_reset_();
  this->wait_until_idle_();

  ESP_LOGD(TAG, "CMD: Booster soft start");
  this->send_command_(CMD_BOOSTER_SOFT_START);
  this->send_data_(0x17);
  this->send_data_(0x17);
  this->send_data_(0x17);

  ESP_LOGD(TAG, "CMD: Power ON");
  this->send_command_(CMD_POWER_ON);
  delay(100);
  this->wait_until_idle_();

  ESP_LOGD(TAG, "CMD: Panel setting");
  this->send_command_(CMD_PANEL_SETTING);
  this->send_data_(0x0F);

  ESP_LOGD(TAG, "CMD: Resolution 160x296");
  this->send_command_(CMD_RESOLUTION_SETTING);
  this->send_data_(0x00);
  this->send_data_(0xA0);
  this->send_data_(0x01);
  this->send_data_(0x28);

  ESP_LOGD(TAG, "CMD: VCOM data interval");
  this->send_command_(CMD_VCOM_DATA_INTERVAL);
  this->send_data_(0x11);

  ESP_LOGD(TAG, "CMD: TCON setting");
  this->send_command_(CMD_TCON_SETTING);
  this->send_data_(0x22);

  this->initialized_ = true;
  ESP_LOGI(TAG, "=== Init complete ===");
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::setup() {
  ESP_LOGI(TAG, "setup() called");

  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
  }
  this->dc_pin_->setup();
  if (this->busy_pin_ != nullptr) {
    this->busy_pin_->setup();
    ESP_LOGI(TAG, "BUSY pin initial logical state: %s",
             this->busy_pin_->digital_read() ? "HIGH" : "LOW");
  }

  this->spi_setup();
  ESP_LOGI(TAG, "SPI setup complete");

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
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", EPD_WIDTH, EPD_HEIGHT);
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
  // Safety net: re-init if setup() was skipped for any reason
  if (!this->initialized_) {
    ESP_LOGW(TAG, "update() called before init — running init now");
    this->initialize_display_();
  }

  memset(this->bw_buffer_,  0xFF, EPD_BUFFER_SIZE);
  memset(this->red_buffer_, 0xFF, EPD_BUFFER_SIZE);

  if (this->writer_.has_value())
    (*this->writer_)(*this);

  // Count pixels to confirm rendering is working
  uint32_t black_px = 0, red_px = 0;
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    black_px += __builtin_popcount(~this->bw_buffer_[i]  & 0xFF);
    red_px   += __builtin_popcount(~this->red_buffer_[i] & 0xFF);
  }
  ESP_LOGI(TAG, "Frame: %u black pixels, %u red pixels (total %u pixels in frame)",
           black_px, red_px, EPD_WIDTH * EPD_HEIGHT);

  ESP_LOGD(TAG, "Sending B/W buffer...");
  this->send_command_(CMD_DATA_START_TX_BW);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->bw_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  ESP_LOGD(TAG, "Sending RED buffer...");
  this->send_command_(CMD_DATA_START_TX_RED);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->red_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  ESP_LOGD(TAG, "CMD: Display refresh");
  this->send_command_(CMD_DISPLAY_REFRESH);
  delay(100);
  this->wait_until_idle_();

  ESP_LOGI(TAG, "Refresh complete.");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
