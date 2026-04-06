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

void WaveshareEPaper2in15B::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    delay(25000);
    return;
  }
  uint32_t start = millis();
  while (this->busy_pin_->digital_read() == true) {
    if (millis() - start > 40000) {
      ESP_LOGW(TAG, "BUSY timeout 40s");
      break;
    }
    delay(50);
    App.feed_wdt();
  }
  ESP_LOGI(TAG, "BUSY cleared after %ums", millis() - start);
}

void WaveshareEPaper2in15B::hardware_reset_() {
  if (this->reset_pin_ == nullptr) return;
  this->reset_pin_->digital_write(true);  delay(20);
  this->reset_pin_->digital_write(false); delay(2);
  this->reset_pin_->digital_write(true);  delay(20);
}

void WaveshareEPaper2in15B::set_ram_area_() {
  this->send_command_(SSD1680_SET_RAM_X);
  this->send_data_(0x00);
  this->send_data_(0x13);  // X: 0 to 19 (160px)

  this->send_command_(SSD1680_SET_RAM_Y);
  this->send_data_(0x00);
  this->send_data_(0x00);
  this->send_data_(0x27);
  this->send_data_(0x01);
}

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

  this->send_command_(SSD1680_SW_RESET);
  this->wait_until_idle_();

  this->send_command_(SSD1680_DRIVER_OUTPUT);
  this->send_data_(0x27);
  this->send_data_(0x01);
  this->send_data_(0x00);

  // Gate Scan Start Position (0x0F):
  // The panel has 5 dummy gate rows before the active pixel area.
  // Starting the scan at line 5 skips them, eliminating the noise bar at top.
  this->send_command_(0x0F);
  this->send_data_(0x05);
  this->send_data_(0x00);

  this->send_command_(SSD1680_DATA_ENTRY_MODE);
  this->send_data_(0x03);

  this->set_ram_area_();

  // Border waveform: LUT mode, clean single-edge behaviour
  this->send_command_(SSD1680_BORDER_WAVEFORM);
  this->send_data_(0x05);

  this->send_command_(SSD1680_TEMP_SENSOR);
  this->send_data_(0x80);

  // Display Update Control 1: no inversion at register level
  this->send_command_(SSD1680_DISPLAY_UPDATE_CTRL1);
  this->send_data_(0x00);
  this->send_data_(0x00);

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
  // BW RAM:  0xFF = all white (1=white, 0=black)
  // RED RAM: 0x00 = all white (0=white, 1=red — inverted polarity on this controller)
  memset(this->bw_buffer_,  0xFF, EPD_BUFFER_SIZE);
  memset(this->red_buffer_, 0x00, EPD_BUFFER_SIZE);
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

  // BW RAM:  1=white, 0=black (standard)
  // RED RAM: 0=white, 1=red   (inverted on this controller)
  //
  // white: BW=1, RED=0  → white BW, transparent RED → white
  // black: BW=0, RED=0  → black BW, transparent RED → black
  // red:   BW=1, RED=1  → white BW, red RED → red (RED takes priority)

  if (is_red) {
    this->bw_buffer_[byte_idx]  |=  bit_mask;  // BW=1 (white, so red shows through)
    this->red_buffer_[byte_idx] |=  bit_mask;  // RED=1 (red)
  } else if (is_black) {
    this->bw_buffer_[byte_idx]  &= ~bit_mask;  // BW=0 (black)
    this->red_buffer_[byte_idx] &= ~bit_mask;  // RED=0 (transparent)
  } else {
    // white
    this->bw_buffer_[byte_idx]  |=  bit_mask;  // BW=1 (white)
    this->red_buffer_[byte_idx] &= ~bit_mask;  // RED=0 (transparent)
  }
}

void WaveshareEPaper2in15B::update() {
  if (!this->initialized_) {
    ESP_LOGI(TAG, "Initialising SSD1680...");
    this->initialize_display_();
  }

  // Reset buffers to white state
  memset(this->bw_buffer_,  0xFF, EPD_BUFFER_SIZE);  // all white
  memset(this->red_buffer_, 0x00, EPD_BUFFER_SIZE);  // all transparent (no red)

  if (this->writer_.has_value())
    (*this->writer_)(*this);

  uint32_t black_px = 0, red_px = 0;
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    black_px += __builtin_popcount(~this->bw_buffer_[i]  & 0xFF);
    red_px   += __builtin_popcount( this->red_buffer_[i] & 0xFF);
  }
  ESP_LOGI(TAG, "Frame: %u black, %u red pixels", black_px, red_px);

  // Write BW RAM (0x24): 1=white, 0=black
  this->set_ram_counter_();
  this->send_command_(SSD1680_WRITE_RAM_BW);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->bw_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // Write RED RAM (0x26): 0=transparent, 1=red
  this->set_ram_counter_();
  this->send_command_(SSD1680_WRITE_RAM_RED);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->red_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // Full refresh
  this->send_command_(SSD1680_DISPLAY_UPDATE_CTRL2);
  this->send_data_(0xF7);
  this->send_command_(SSD1680_MASTER_ACTIVATION);

  this->wait_until_idle_();
  ESP_LOGI(TAG, "Refresh complete.");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
