#include "waveshare_2in15b.h"

namespace esphome {
namespace waveshare {

static const char *const TAG = "waveshare_2in15b";

// =====================
// Waveshare Commands
// =====================
static const uint8_t CMD_PANEL_SETTING             = 0x00;
static const uint8_t CMD_POWER_SETTING             = 0x01;
static const uint8_t CMD_POWER_ON                  = 0x04;
static const uint8_t CMD_BOOSTER_SOFT_START         = 0x06;
static const uint8_t CMD_DATA_START_TRANSMISSION_1 = 0x10; // black
static const uint8_t CMD_DATA_START_TRANSMISSION_2 = 0x13; // red
static const uint8_t CMD_DISPLAY_REFRESH           = 0x12;
static const uint8_t CMD_PLL_CONTROL               = 0x30;
static const uint8_t CMD_RESOLUTION_SETTING        = 0x61;
static const uint8_t CMD_VCOM_AND_DATA_INTERVAL    = 0x50;
static const uint8_t CMD_VCM_DC_SETTING             = 0x82;

// =====================
// LUTs (2.15" B panel)
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
// Low‑level helpers
// =====================
void Waveshare2in15B::send_command(uint8_t cmd) {
  if (dc_pin_) dc_pin_->digital_write(false);
  this->write_byte(cmd);
}

void Waveshare2in15B::send_data(uint8_t data) {
  if (dc_pin_) dc_pin_->digital_write(true);
  this->write_byte(data);
}

// BUSY: HIGH = busy, LOW = idle (HAT+ B)
void Waveshare2in15B::wait_until_idle_() {
  if (!busy_pin_) return;

  ESP_LOGV(TAG, "Waiting for BUSY...");
  while (busy_pin_->digital_read()) {
    yield();
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
  ESP_LOGI(TAG, "Scheduling Waveshare display init");
}

void Waveshare2in15B::init_display_step_() {
  switch (init_step_) {
    case 0:
      ESP_LOGI(TAG, "Initializing Waveshare 2.15\" B e-paper");
      this->spi_setup();
      ESP_LOGI(TAG, "[INIT 0] spi_setup()");
      init_step_++;
      return;

    case 1:
      ESP_LOGI(TAG, "[INIT 1] Power pin enable");
      if (power_pin_) {
        power_pin_->setup();
        power_pin_->digital_write(true);
      }
      init_step_++;
      return;

    case 2:
      ESP_LOGI(TAG, "[INIT 2] GPIO setup");
      if (dc_pin_) dc_pin_->setup();
      if (reset_pin_) reset_pin_->setup();
      if (busy_pin_) busy_pin_->setup();
      init_step_++;
      return;

    case 3:
      ESP_LOGI(TAG, "[INIT 3] Reset LOW");
      if (reset_pin_) reset_pin_->digital_write(false);
      init_step_++;
      return;

    case 4:
      ESP_LOGI(TAG, "[INIT 4] Reset HIGH");
      if (reset_pin_) reset_pin_->digital_write(true);
      init_step_++;
      return;

    case 5:
      ESP_LOGI(TAG, "[INIT 5] POWER ON command");
      send_command(CMD_POWER_ON);
      init_step_++;
      return;

    case 6:
      ESP_LOGI(TAG, "[INIT 6] Clear frame buffers");
      memset(buffer_black_, 0xFF, sizeof(buffer_black_));
      memset(buffer_red_,   0xFF, sizeof(buffer_red_));
      init_step_++;
      return;

    case 7:
      ESP_LOGI(TAG, "[INIT 7] Power settings");
      send_command(CMD_POWER_SETTING);
      send_data(0x03); send_data(0x00);
      send_data(0x2B); send_data(0x2B);
      init_step_++;
      return;

    case 8:
      ESP_LOGI(TAG, "[INIT 8] Booster soft start");
      send_command(CMD_BOOSTER_SOFT_START);
      send_data(0x17); send_data(0x17); send_data(0x17);
      init_step_++;
      return;

    case 9: {
      static bool was_busy = false;

      if (busy_pin_ && busy_pin_->digital_read()) {
        if (!was_busy) {
          ESP_LOGI(TAG, "[INIT 9] BUSY asserted, waiting");
          was_busy = true;
        }
        return;  // still busy, come back next loop iteration
      }

      if (was_busy) {
        ESP_LOGI(TAG, "[INIT 9] BUSY cleared");
        was_busy = false;
      }

      init_step_++;
      return;
    }

    case 10:
      ESP_LOGI(TAG, "[INIT 10] Panel config, resolution & timings");
      send_command(CMD_PANEL_SETTING);
      send_data(0x0F);
      send_command(CMD_PLL_CONTROL);
      send_data(0x3A);
      send_command(CMD_RESOLUTION_SETTING);
      send_data(0x01); send_data(0x28);
      send_data(0x00); send_data(0xA0);
      init_step_++;
      return;

    case 11:
      load_lut_();
      initialized_ = true;
      ESP_LOGI(TAG, "Display initialized");
      return;
  }
}

void Waveshare2in15B::update() {
  if (!initialized_) {
    ESP_LOGV(TAG, "Display not initialized yet, skipping update");
    return;
  }

  ESP_LOGD(TAG, "Updating display");
  this->do_update_();

  // -------- Black layer --------
  this->enable();
  if (dc_pin_) dc_pin_->digital_write(false);
  this->write_byte(CMD_DATA_START_TRANSMISSION_1);
  if (dc_pin_) dc_pin_->digital_write(true);

  for (size_t i = 0; i < sizeof(buffer_black_); i++) {
    this->write_byte(buffer_black_[i]);
    if ((i % 64) == 0) yield();
  }
  this->disable();
  yield();

  // -------- Red layer --------
  this->enable();
  if (dc_pin_) dc_pin_->digital_write(false);
  this->write_byte(CMD_DATA_START_TRANSMISSION_2);
  if (dc_pin_) dc_pin_->digital_write(true);

  for (size_t i = 0; i < sizeof(buffer_red_); i++) {
    this->write_byte(buffer_red_[i]);
    if ((i % 64) == 0) yield();
  }
  this->disable();
  yield();

  send_command(CMD_DISPLAY_REFRESH);
  wait_until_idle_();

  ESP_LOGI(TAG, "Display refresh complete");
}

void Waveshare2in15B::loop() {
  if (!initialized_) {
    init_display_step_();
  }
}

// =====================
// Drawing helpers
// =====================
void Waveshare2in15B::fill(Color color) {
  uint8_t v = color.is_on() ? 0x00 : 0xFF;
  memset(buffer_black_, v, sizeof(buffer_black_));
  memset(buffer_red_, 0xFF, sizeof(buffer_red_));
}

void Waveshare2in15B::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= 296 || y >= 160) return;

  size_t index = (x + y * 296) / 8;
  uint8_t bit = 0x80 >> (x & 7);

  if (color.red > 128)
    buffer_red_[index] &= ~bit;
  else if (color.is_on())
    buffer_black_[index] &= ~bit;
}

display::DisplayType Waveshare2in15B::get_display_type() {
  return display::DISPLAY_TYPE_BINARY;
}

}  // namespace waveshare
}  // namespace esphome
