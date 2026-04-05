#include "waveshare_2in15b.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace waveshare_2in15b {

static const char *const TAG = "waveshare_2in15b";

// ---------------------------------------------------------------------------
// Low-level SPI helpers
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::send_command_(uint8_t cmd) {
  this->dc_pin_->digital_write(false);  // DC low = command
  this->enable();
  this->transfer_byte(cmd);
  this->disable();
}

void WaveshareEPaper2in15B::send_data_(uint8_t data) {
  this->dc_pin_->digital_write(true);   // DC high = data
  this->enable();
  this->transfer_byte(data);
  this->disable();
}

void WaveshareEPaper2in15B::wait_until_idle_() {
  // BUSY pin is active LOW on this display (0 = busy)
  if (this->busy_pin_ == nullptr) {
    delay(500);
    return;
  }
  uint32_t start = millis();
  while (this->busy_pin_->digital_read() == false) {
    if (millis() - start > 10000) {
      ESP_LOGE(TAG, "Timeout waiting for BUSY pin to go HIGH");
      break;
    }
    delay(10);
    App.feed_wdt();
  }
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
// Initialisation sequence
// Based on Waveshare EPD_2in15b C/Python demo
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::initialize_display_() {
  this->hardware_reset_();
  this->wait_until_idle_();

  // Booster soft-start
  this->send_command_(CMD_BOOSTER_SOFT_START);
  this->send_data_(0x17);
  this->send_data_(0x17);
  this->send_data_(0x17);

  // Power on, then wait for display to be ready
  this->send_command_(CMD_POWER_ON);
  this->wait_until_idle_();

  // Panel setting: KWR mode, scan up, shift right
  this->send_command_(CMD_PANEL_SETTING);
  this->send_data_(0x0F);

  // Resolution: width=160 (0x00A0), height=296 (0x0128)
  this->send_command_(CMD_RESOLUTION_SETTING);
  this->send_data_(0x00);  // HRES high byte
  this->send_data_(0xA0);  // HRES low byte  = 160
  this->send_data_(0x01);  // VRES high byte
  this->send_data_(0x28);  // VRES low byte  = 40 → 256+40 = 296

  // VCOM and data interval
  this->send_command_(CMD_VCOM_DATA_INTERVAL);
  this->send_data_(0x11);

  // TCON timing
  this->send_command_(CMD_TCON_SETTING);
  this->send_data_(0x22);
}

// ---------------------------------------------------------------------------
// Component lifecycle
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::setup() {
  ESP_LOGD(TAG, "Setting up Waveshare 2.15\" B (160×296, R/B/W)");

  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
  }
  this->dc_pin_->setup();
  if (this->busy_pin_ != nullptr) {
    this->busy_pin_->setup();
  }

  this->spi_setup();

  // Both buffers start as all-white (0xFF = all bits 1 = white)
  memset(this->bw_buffer_,  0xFF, BUFFER_SIZE);
  memset(this->red_buffer_, 0xFF, BUFFER_SIZE);

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
// Pixel writing — called by ESPHome's rendering engine
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT)
    return;

  uint32_t byte_idx = (x + y * EPD_WIDTH) / 8;
  uint8_t  bit_mask = 0x80 >> (x % 8);

  // Classify pixel: anything strongly red → red, strongly dark → black, else white
  bool is_red   = (color.r > 200 && color.g < 100 && color.b < 100);
  bool is_black = (!is_red && color.r < 64 && color.g < 64 && color.b < 64);

  if (is_red) {
    this->red_buffer_[byte_idx] &= ~bit_mask;  // red plane:  0 = red
    this->bw_buffer_[byte_idx]  |=  bit_mask;  // bw plane:   1 = white (no black here)
  } else if (is_black) {
    this->bw_buffer_[byte_idx]  &= ~bit_mask;  // bw plane:   0 = black
    this->red_buffer_[byte_idx] |=  bit_mask;  // red plane:  1 = white (no red here)
  } else {
    // White (or any other colour): both planes = 1
    this->bw_buffer_[byte_idx]  |=  bit_mask;
    this->red_buffer_[byte_idx] |=  bit_mask;
  }
}

// ---------------------------------------------------------------------------
// Full display refresh — called at update_interval
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::update() {
  // Clear buffers to white before each render pass
  memset(this->bw_buffer_,  0xFF, BUFFER_SIZE);
  memset(this->red_buffer_, 0xFF, BUFFER_SIZE);

  // Run the ESPHome drawing lambda (populates buffers via draw_absolute_pixel_internal)
  if (this->writer_.has_value())
    (*this->writer_)(*this);

  ESP_LOGD(TAG, "Sending frame to display...");

  // Transmit Black/White frame
  this->send_command_(CMD_DATA_START_TX_BW);
  for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
    this->send_data_(this->bw_buffer_[i]);
    if (i % 256 == 0) App.feed_wdt();
  }

  // Transmit Red frame
  this->send_command_(CMD_DATA_START_TX_RED);
  for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
    this->send_data_(this->red_buffer_[i]);
    if (i % 256 == 0) App.feed_wdt();
  }

  // Trigger hardware refresh
  this->send_command_(CMD_DISPLAY_REFRESH);
  delay(100);
  this->wait_until_idle_();

  ESP_LOGD(TAG, "Display refresh complete.");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
