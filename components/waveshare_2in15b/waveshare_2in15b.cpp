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

// SSD1680: BUSY HIGH = working, LOW = idle. Wait for LOW.
void WaveshareEPaper2in15B::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    delay(2000);
    return;
  }
  uint32_t start = millis();
  while (this->busy_pin_->digital_read() == true) {
    if (millis() - start > 15000) {
      ESP_LOGW(TAG, "BUSY timeout 15s");
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

// RAM window: X=0..19 (20 bytes = 160px), Y=0..295
void WaveshareEPaper2in15B::set_ram_area_() {
  this->send_command_(SSD1680_SET_RAM_X);
  this->send_data_(0x00);
  this->send_data_(0x13);  // (160/8)-1 = 19 = 0x13

  this->send_command_(SSD1680_SET_RAM_Y);
  this->send_data_(0x27);  // Y start lo = 295 & 0xFF
  this->send_data_(0x01);  // Y start hi = 295 >> 8
  this->send_data_(0x00);  // Y end lo
  this->send_data_(0x00);  // Y end hi
}

void WaveshareEPaper2in15B::set_ram_counter_() {
  this->send_command_(SSD1680_SET_RAM_X_COUNTER);
  this->send_data_(0x00);

  this->send_command_(SSD1680_SET_RAM_Y_COUNTER);
  this->send_data_(0x27);  // start at Y=295 (matches Y decrement direction)
  this->send_data_(0x01);
}

void WaveshareEPaper2in15B::initialize_display_() {
  ESP_LOGI(TAG, "======= SSD1680 INIT =======");

  this->hardware_reset_();
  this->wait_until_idle_();

  this->send_command_(SSD1680_SW_RESET);
  this->wait_until_idle_();

  // Driver Output: 296 gate lines (295 = 0x127)
  this->send_command_(SSD1680_DRIVER_OUTPUT);
  this->send_data_(0x27);
  this->send_data_(0x01);
  this->send_data_(0x00);

  // Data Entry Mode:
  // 0x03 = X inc, Y inc  → portrait, normal scan (caused -90° rotation)
  // 0x06 = X inc, Y dec  → portrait, Y scans top-to-bottom from controller view
  // Try 0x06 to correct the -90° rotation issue
  this->send_command_(SSD1680_DATA_ENTRY_MODE);
  this->send_data_(0x06);

  this->set_ram_area_();

  // Border waveform
  this->send_command_(SSD1680_BORDER_WAVEFORM);
  this->send_data_(0x05);

  // Internal temperature sensor
  this->send_command_(SSD1680_TEMP_SENSOR);
  this->send_data_(0x80);

  // Display Update Control 1: normal BW + RED RAM
  this->send_command_(SSD1680_DISPLAY_UPDATE_CTRL1);
  this->send_data_(0x00);
  this->send_data_(0x80);

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
    // RED RAM: 0=red, 1=white
    // BW RAM:  1=white (red pixels show through RED RAM, ignore BW)
    this->red_buffer_[byte_idx] &= ~bit_mask;
    this->bw_buffer_[byte_idx]  |=  bit_mask;
  } else if (is_black) {
    // BW RAM: 0=black, 1=white
    // RED RAM: 1=white (so red doesn't override black)
    this->bw_buffer_[byte_idx]  &= ~bit_mask;
    this->red_buffer_[byte_idx] |=  bit_mask;
  } else {
    // White: both planes = 1
    this->bw_buffer_[byte_idx]  |=  bit_mask;
    this->red_buffer_[byte_idx] |=  bit_mask;
  }
}

void WaveshareEPaper2in15B::update() {
  // Only init on first update — avoids re-flashing every cycle
  if (!this->initialized_) {
    ESP_LOGI(TAG, "First update — initialising SSD1680");
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

  // Reset RAM address counters
  this->set_ram_counter_();

  // Write BW RAM
  this->send_command_(SSD1680_WRITE_RAM_BW);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->bw_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // Reset counters again for RED RAM
  this->set_ram_counter_();

  // Write RED RAM
  this->send_command_(SSD1680_WRITE_RAM_RED);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->red_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // Trigger full refresh
  this->send_command_(SSD1680_DISPLAY_UPDATE_CTRL2);
  this->send_data_(0xF7);
  this->send_command_(SSD1680_MASTER_ACTIVATION);

  this->wait_until_idle_();
  ESP_LOGI(TAG, "Refresh complete.");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
