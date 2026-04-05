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

// SSD1680: BUSY HIGH = busy, LOW = idle
void WaveshareEPaper2in15B::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    delay(2000);
    return;
  }
  uint32_t start = millis();
  while (this->busy_pin_->digital_read() == true) {
    if (millis() - start > 20000) {
      ESP_LOGW(TAG, "BUSY timeout 20s");
      break;
    }
    delay(10);
    App.feed_wdt();
  }
  ESP_LOGD(TAG, "BUSY LOW after %ums", millis() - start);
}

void WaveshareEPaper2in15B::hardware_reset_() {
  if (this->reset_pin_ == nullptr) return;
  this->reset_pin_->digital_write(true);  delay(20);
  this->reset_pin_->digital_write(false); delay(2);
  this->reset_pin_->digital_write(true);  delay(20);
}

// RAM window: X byte 0..19 (160px), Y line 0..295
// Using 0x03 data entry (X-inc, Y-inc) so both counters start at 0
void WaveshareEPaper2in15B::set_ram_area_() {
  this->send_command_(SSD1680_SET_RAM_X);
  this->send_data_(0x00);   // X start byte = 0
  this->send_data_(0x13);   // X end byte  = 19  (160/8 - 1)

  this->send_command_(SSD1680_SET_RAM_Y);
  this->send_data_(0x00);   // Y start lo  = 0
  this->send_data_(0x00);   // Y start hi
  this->send_data_(0x27);   // Y end lo    = 295 & 0xFF
  this->send_data_(0x01);   // Y end hi    = 295 >> 8
}

// Reset RAM address counters to (0,0) — matches 0x03 X-inc Y-inc mode
void WaveshareEPaper2in15B::set_ram_counter_() {
  this->send_command_(SSD1680_SET_RAM_X_COUNTER);
  this->send_data_(0x00);

  this->send_command_(SSD1680_SET_RAM_Y_COUNTER);
  this->send_data_(0x00);
  this->send_data_(0x00);
}

void WaveshareEPaper2in15B::initialize_display_() {
  ESP_LOGI(TAG, "======= SSD1680 INIT =======");

  this->hardware_reset_();
  this->wait_until_idle_();

  // Software reset
  this->send_command_(SSD1680_SW_RESET);
  this->wait_until_idle_();

  // Driver Output Control: 296 gate lines (MUX=295=0x127)
  this->send_command_(SSD1680_DRIVER_OUTPUT);
  this->send_data_(0x27);   // MUX[7:0]
  this->send_data_(0x01);   // MUX[8]
  this->send_data_(0x00);   // GD=0, SM=0, TB=0

  // Data Entry Mode: 0x03 = X-increment, Y-increment (portrait)
  // Rotation correction is handled in YAML with rotation: 90°
  this->send_command_(SSD1680_DATA_ENTRY_MODE);
  this->send_data_(0x03);

  // RAM window
  this->set_ram_area_();

  // Border waveform: follow LUT (white border)
  this->send_command_(SSD1680_BORDER_WAVEFORM);
  this->send_data_(0x05);

  // Internal temperature sensor
  this->send_command_(SSD1680_TEMP_SENSOR);
  this->send_data_(0x80);

  // Display Update Control 1: BW from 0x24, Red from 0x26, no inversion
  this->send_command_(SSD1680_DISPLAY_UPDATE_CTRL1);
  this->send_data_(0x00);
  this->send_data_(0x80);

  // Set counters to (0,0)
  this->set_ram_counter_();
  this->wait_until_idle_();

  this->initialized_ = true;
  ESP_LOGI(TAG, "======= SSD1680 INIT DONE =======");
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
  LOG_DISPLAY("", "Waveshare 2.15\" B E-Paper (SSD1680)", this);
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
    this->red_buffer_[byte_idx] &= ~bit_mask;  // RED RAM 0 = red
    this->bw_buffer_[byte_idx]  |=  bit_mask;  // BW RAM  1 = white (let red show)
  } else if (is_black) {
    this->bw_buffer_[byte_idx]  &= ~bit_mask;  // BW RAM  0 = black
    this->red_buffer_[byte_idx] |=  bit_mask;  // RED RAM 1 = white (no red here)
  } else {
    this->bw_buffer_[byte_idx]  |=  bit_mask;  // white
    this->red_buffer_[byte_idx] |=  bit_mask;
  }
}

void WaveshareEPaper2in15B::update() {
  if (!this->initialized_) {
    ESP_LOGI(TAG, "Initialising SSD1680...");
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

  // --- Write BW RAM ---
  this->set_ram_counter_();
  this->send_command_(SSD1680_WRITE_RAM_BW);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->bw_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // --- Write RED RAM ---
  this->set_ram_counter_();  // reset to (0,0) before RED write
  this->send_command_(SSD1680_WRITE_RAM_RED);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->red_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // --- Trigger full refresh ---
  this->send_command_(SSD1680_DISPLAY_UPDATE_CTRL2);
  this->send_data_(0xF7);  // full update sequence
  this->send_command_(SSD1680_MASTER_ACTIVATION);

  this->wait_until_idle_();
  ESP_LOGI(TAG, "Refresh complete.");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
