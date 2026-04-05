#include "waveshare_2in15b.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace waveshare_2in15b {

static const char *const TAG = "waveshare_2in15b";

// ---------------------------------------------------------------------------
// SPI helpers — use write_byte (no MISO needed for e-paper)
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::send_command_(uint8_t cmd) {
  this->dc_pin_->digital_write(false);  // DC low = command
  this->enable();
  this->write_byte(cmd);               // write_byte, not transfer_byte
  this->disable();
}

void WaveshareEPaper2in15B::send_data_(uint8_t data) {
  this->dc_pin_->digital_write(true);   // DC high = data
  this->enable();
  this->write_byte(data);              // write_byte, not transfer_byte
  this->disable();
}

// ---------------------------------------------------------------------------
// BUSY pin handling
// BUSY is active-LOW on Waveshare 2.15" B: LOW = busy, HIGH = idle
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    ESP_LOGD(TAG, "No BUSY pin — waiting 2000ms");
    delay(2000);
    return;
  }
  uint32_t start = millis();
  while (this->busy_pin_->digital_read() == false) {
    if (millis() - start > 15000) {
      ESP_LOGW(TAG, "BUSY timeout after 15s — continuing anyway");
      break;
    }
    delay(20);
    App.feed_wdt();
  }
  ESP_LOGD(TAG, "BUSY cleared after %ums", millis() - start);
}

void WaveshareEPaper2in15B::hardware_reset_() {
  if (this->reset_pin_ == nullptr)
    return;
  this->reset_pin_->digital_write(true);
  delay(20);
  this->reset_pin_->digital_write(false);
  delay(2);
  this->reset_pin_->digital_write(true);
  delay(20);
}

// ---------------------------------------------------------------------------
// Init sequence (Waveshare EPD_2in15b reference demo)
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::initialize_display_() {
  this->hardware_reset_();
  this->wait_until_idle_();

  this->send_command_(CMD_BOOSTER_SOFT_START);
  this->send_data_(0x17);
  this->send_data_(0x17);
  this->send_data_(0x17);

  this->send_command_(CMD_POWER_ON);
  this->wait_until_idle_();

  this->send_command_(CMD_PANEL_SETTING);
  this->send_data_(0x0F);

  // Resolution: width=160 (0x00A0), height=296 (0x0128)
  this->send_command_(CMD_RESOLUTION_SETTING);
  this->send_data_(0x00);
  this->send_data_(0xA0);
  this->send_data_(0x01);
  this->send_data_(0x28);

  this->send_command_(CMD_VCOM_DATA_INTERVAL);
  this->send_data_(0x11);

  this->send_command_(CMD_TCON_SETTING);
  this->send_data_(0x22);
}

// ---------------------------------------------------------------------------
// Component lifecycle
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::setup() {
  ESP_LOGD(TAG, "Setting up Waveshare 2.15\" B (160x296 R/B/W)");

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

  this->initialize_display_();
}

void WaveshareEPaper2in15B::dump_config() {
  LOG_DISPLAY("", "Waveshare 2.15\" B E-Paper", this);
  LOG_PIN("  DC Pin:    ", this->dc_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin:  ", this->busy_pin_);
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", EPD_WIDTH, EPD_HEIGHT);
  LOG_UPDATE_INTERVAL(this);
}

// ---------------------------------------------------------------------------
// Pixel writing
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
// Full refresh
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::update() {
  memset(this->bw_buffer_,  0xFF, EPD_BUFFER_SIZE);
  memset(this->red_buffer_, 0xFF, EPD_BUFFER_SIZE);

  if (this->writer_.has_value())
    (*this->writer_)(*this);

  ESP_LOGD(TAG, "Sending frame buffers...");

  // Black/White plane
  this->send_command_(CMD_DATA_START_TX_BW);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->bw_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // Red plane
  this->send_command_(CMD_DATA_START_TX_RED);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->red_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // Trigger refresh
  this->send_command_(CMD_DISPLAY_REFRESH);
  delay(100);
  this->wait_until_idle_();

  ESP_LOGD(TAG, "Display refresh complete.");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
