#include "waveshare_2in15b.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

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
  // BUSY pin is active LOW on this display (busy = 0)
  uint32_t start = millis();
  while (this->busy_pin_->digital_read() == false) {
    if (millis() - start > 10000) {
      ESP_LOGE(TAG, "Timeout waiting for display BUSY pin");
      break;
    }
    delay(10);
    App.feed_wdt();
  }
}

void WaveshareEPaper2in15B::hardware_reset_() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(true);
    delay(20);
    this->reset_pin_->digital_write(false);
    delay(2);
    this->reset_pin_->digital_write(true);
    delay(20);
  }
}

// ---------------------------------------------------------------------------
// Display initialisation sequence
// Derived from Waveshare EPD_2in15b C demo (RaspberryPi_JetsonNano/c/lib)
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::initialize_display_() {
  this->hardware_reset_();
  this->wait_until_idle_();

  // Booster soft-start
  this->send_command_(CMD_BOOSTER_SOFT_START);
  this->send_data_(0x17);
  this->send_data_(0x17);
  this->send_data_(0x17);

  // Power on
  this->send_command_(CMD_POWER_ON);
  this->wait_until_idle_();

  // Panel setting: KW/R mode, LUT from register, 160x296
  this->send_command_(CMD_PANEL_SETTING);
  this->send_data_(0x0F);  // KWR, scan up, shift right, DC/DC on

  // Resolution: width=160 (0x00,0xA0), height=296 (0x01,0x28)
  this->send_command_(CMD_RESOLUTION_SETTING);
  this->send_data_(0x00);  // HRES[8] = 0
  this->send_data_(0xA0);  // HRES[7:0] = 160
  this->send_data_(0x01);  // VRES[8] = 1
  this->send_data_(0x28);  // VRES[7:0] = 40 → total 296

  // VCOM and data interval: CDI
  this->send_command_(CMD_VCOM_DATA_INTERVAL);
  this->send_data_(0x11);  // Border output: white; CDI = 17

  // TCON (line gate and source non-overlap timing)
  this->send_command_(CMD_TCON_SETTING);
  this->send_data_(0x22);
}

// ---------------------------------------------------------------------------
// Component lifecycle
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::setup() {
  ESP_LOGD(TAG, "Setting up Waveshare 2.15\" B (160x296, R/B/W)");

  // Configure pin directions
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
  }
  this->dc_pin_->setup();
  if (this->busy_pin_ != nullptr) {
    this->busy_pin_->setup();
  }

  this->spi_setup();

  // Clear both frame buffers to white (0xFF = all 1s = all white)
  memset(this->bw_buffer_,  0xFF, BUFFER_SIZE);
  memset(this->red_buffer_, 0xFF, BUFFER_SIZE);

  this->initialize_display_();
}

void WaveshareEPaper2in15B::dump_config() {
  LOG_DISPLAY("", "Waveshare 2.15\" B E-Paper", this);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", EPD_WIDTH, EPD_HEIGHT);
}

// ---------------------------------------------------------------------------
// Pixel writing
// Called by the ESPHome rendering engine for every pixel in the lambda.
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT)
    return;

  // Byte index and bit position within that byte
  // Pixels are stored MSB-first: pixel 0 is bit 7 of byte 0.
  uint32_t byte_idx = (x + y * EPD_WIDTH) / 8;
  uint8_t  bit_mask = 0x80 >> (x % 8);

  // Determine pixel colour: black, red, or white.
  // ESPHome Color: red/green/blue each 0–255.
  bool is_red   = (color.r > 200 && color.g < 100 && color.b < 100);
  bool is_black = (color.r < 64  && color.g < 64  && color.b < 64);
  // Everything else (including white) is treated as white.

  if (is_red) {
    // Red plane: 0 = red pixel
    this->red_buffer_[byte_idx] &= ~bit_mask;
    // Black/white plane: keep white (1) for this pixel position
    this->bw_buffer_[byte_idx]  |=  bit_mask;
  } else if (is_black) {
    // Black/white plane: 0 = black pixel
    this->bw_buffer_[byte_idx]  &= ~bit_mask;
    // Red plane: keep white (1)
    this->red_buffer_[byte_idx] |=  bit_mask;
  } else {
    // White: both planes = 1
    this->bw_buffer_[byte_idx]  |=  bit_mask;
    this->red_buffer_[byte_idx] |=  bit_mask;
  }
}

// ---------------------------------------------------------------------------
// Full display refresh – called by ESPHome at update_interval
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::update() {
  // Run the ESPHome rendering engine (populates bw_buffer_ / red_buffer_
  // via draw_absolute_pixel_internal calls).
  this->do_update_();

  ESP_LOGD(TAG, "Refreshing display...");

  // --- Transmit Black/White frame ---
  this->send_command_(CMD_DATA_START_TX_BW);
  for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
    this->send_data_(this->bw_buffer_[i]);
  }

  // --- Transmit Red frame ---
  this->send_command_(CMD_DATA_START_TX_RED);
  for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
    this->send_data_(this->red_buffer_[i]);
  }

  // --- Trigger refresh ---
  this->send_command_(CMD_DISPLAY_REFRESH);
  delay(100);  // brief pause before polling BUSY
  this->wait_until_idle_();

  ESP_LOGD(TAG, "Display refresh complete.");
}

// ---------------------------------------------------------------------------
// Deep sleep (call when done to protect the display)
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::deep_sleep_() {
  this->send_command_(CMD_POWER_OFF);
  this->wait_until_idle_();
  this->send_command_(CMD_DEEP_SLEEP);
  this->send_data_(0xA5);  // check code
}

}  // namespace waveshare_2in15b
}  // namespace esphome
