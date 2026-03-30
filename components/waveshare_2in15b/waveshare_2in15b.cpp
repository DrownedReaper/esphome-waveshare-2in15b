#include "waveshare_2in15b.h"

namespace esphome {
namespace waveshare {

static const char *const TAG = "waveshare_2in15b";

// =====================
// Waveshare commands
// =====================
static const uint8_t CMD_PANEL_SETTING                 = 0x00;
static const uint8_t CMD_POWER_SETTING                 = 0x01;
static const uint8_t CMD_POWER_ON                      = 0x04;
static const uint8_t CMD_BOOSTER_SOFT_START             = 0x06;
static const uint8_t CMD_DEEP_SLEEP                    = 0x07;
static const uint8_t CMD_DATA_START_TRANSMISSION_1     = 0x10; // black
static const uint8_t CMD_DATA_START_TRANSMISSION_2     = 0x13; // red
static const uint8_t CMD_DISPLAY_REFRESH               = 0x12;
static const uint8_t CMD_PLL_CONTROL                   = 0x30;
static const uint8_t CMD_RESOLUTION_SETTING            = 0x61;
static const uint8_t CMD_VCOM_AND_DATA_INTERVAL        = 0x50;
static const uint8_t CMD_VCM_DC_SETTING                = 0x82;

// =====================
// LUTs (full refresh)
// =====================
static const uint8_t LUT_VCOM[] = {
  0x0E,0x14,0x01,0x0A,0x06,0x04,0x0A,0x0A,
  0x0F,0x03,0x03,0x0C,0x06,0x0A,0x0A,0x04
};

static const uint8_t LUT_WW[] = {
  0x0E,0x14,0x01,0x0A,0x46,0x04,0x8A,0x4A,
  0x0F,0x83,0x43,0x0C,0x06,0x4A,0x4A,0x04
};

static const uint8_t LUT_BB[] = {
  0x0E,0x14,0x01,0x0A,0x46,0x04,0x8A,0x4A,
  0x0F,0x83,0x43,0x0C,0x06,0x4A,0x4A,0x04
};

static const uint8_t LUT_WB[] = {
  0x0E,0x14,0x01,0x8A,0x06,0x04,0x8A,0x4A,
  0x0F,0x83,0x43,0x0C,0x06,0x0A,0x0A,0x04
};

static const uint8_t LUT_BW[] = {
  0x0E,0x14,0x01,0x8A,0x06,0x04,0x8A,0x4A,
  0x0F,0x83,0x43,0x0C,0x06,0x0A,0x0A,0x04
};

// =====================
// SPI helpers (SPIDevice)
// =====================
void Waveshare2in15B::send_command(uint8_t cmd) {
  if (dc_pin_)
    dc_pin_->digital_write(false);  // command

  this->enable();
  this->write_byte(cmd);
  this->disable();
}

void Waveshare2in15B::send_data(uint8_t data) {
  if (dc_pin_)
    dc_pin_->digital_write(true);   // data

  this->enable();
  this->write_byte(data);
  this->disable();
}

void Waveshare2in15B::wait_until_idle_() {
  if (!busy_pin_)
    return;

  ESP_LOGV(TAG, "Waiting for BUSY...");
  while (busy_pin_->digital_read()) {
    delay(10);
  }
}

void Waveshare2in15B::load_lut_() {
  send_command(0x20); for (uint8_t v : LUT_VCOM) send_data(v);
  send_command(0x21); for (uint8_t v : LUT_WW)   send_data(v);
  send_command(0x22); for (uint8_t v : LUT_BB)   send_data(v);
  send_command(0x23); for (uint8_t v : LUT_WB)   send_data(v);
  send_command(0x24); for (uint8_t v : LUT_BW)   send_data(v);
}

// =====================
// ESPHome lifecycle
// =====================
void Waveshare2in15B::setup() {
  ESP_LOGI(TAG, "Initializing Waveshare 2.15\" B e-paper");

  if (dc_pin_)    dc_pin_->setup();
  if (reset_pin_) reset_pin_->setup();
  if (busy_pin_)  busy_pin_->setup();

  // Hardware reset
  if (reset_pin_) {
    reset_pin_->digital_write(false);
    delay(10);
    reset_pin_->digital_write(true);
    delay(10);
  }

  memset(buffer_black_, 0xFF, sizeof(buffer_black_));
  memset(buffer_red_,   0xFF, sizeof(buffer_red_));

  send_command(CMD_POWER_SETTING);
  send_data(0x03);
  send_data(0x00);
  send_data(0x2B);
  send_data(0x2B);

  send_command(CMD_BOOSTER_SOFT_START);
  send_data(0x17);
  send_data(0x17);
  send_data(0x17);

  send_command(CMD_POWER_ON);
  wait_until_idle_();

  send_command(CMD_PANEL_SETTING);
  send_data(0x0F);

  send_command(CMD_PLL_CONTROL);
  send_data(0x3A);

  send_command(CMD_RESOLUTION_SETTING);
  send_data(0x01);  // 296 width high
  send_data(0x28);  // 296 width low
  send_data(0xA0);  // 160 height

  send_command(CMD_VCOM_AND_DATA_INTERVAL);
  send_data(0x77);

  send_command(CMD_VCM_DC_SETTING);
  send_data(0x0A);

  load_lut_();

  ESP_LOGI(TAG, "Waveshare display ready");
}

void Waveshare2in15B::update() {
  ESP_LOGD(TAG, "Updating display");

  this->do_update_();

  send_command(CMD_DATA_START_TRANSMISSION_1);
  for (uint8_t b : buffer_black_)
    send_data(b);

  send_command(CMD_DATA_START_TRANSMISSION_2);
  for (uint8_t b : buffer_red_)
    send_data(b);

  send_command(CMD_DISPLAY_REFRESH);
  wait_until_idle_();

  ESP_LOGI(TAG, "Display refresh complete");
}

// =====================
// Drawing helpers
// =====================
void Waveshare2in15B::fill(Color color) {
  uint8_t v = color.is_on() ? 0x00 : 0xFF;
  memset(buffer_black_, v, sizeof(buffer_black_));
  memset(buffer_red_, 0xFF, sizeof(buffer_red_));
}

display::DisplayType Waveshare2in15B::get_display_type() {
  return display::DisplayType::DISPLAY_TYPE_BINARY;
}

void Waveshare2in15B::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= 296 || y >= 160)
    return;

  size_t index = (x + y * 296) / 8;
  uint8_t bit = 0x80 >> (x & 7);

  if (color.red > 128)
    buffer_red_[index] &= ~bit;
  else if (color.is_on())
    buffer_black_[index] &= ~bit;
}

}  // namespace waveshare
}  // namespace esphome
