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
    delay(25000);
    return;
  }
  delay(50);
  uint32_t start = millis();
  while (this->busy_pin_->digital_read() == true) {
    if (millis() - start > 40000) {
      ESP_LOGW(TAG, "BUSY timeout 40s");
      break;
    }
    delay(10);
    App.feed_wdt();
  }
  delay(50);
  ESP_LOGI(TAG, "BUSY cleared after %ums", millis() - start);
}

void WaveshareEPaper2in15B::hardware_reset_() {
  if (this->reset_pin_ == nullptr) return;
  this->reset_pin_->digital_write(true);  delay(200);
  this->reset_pin_->digital_write(false); delay(2);
  this->reset_pin_->digital_write(true);  delay(200);
}

// Matches official EPD_2IN15B_SetWindows exactly
void WaveshareEPaper2in15B::set_ram_area_() {
  this->send_command_(0x44);
  this->send_data_((0 >> 3) & 0x1F);                    // Xstart = 0
  this->send_data_(((EPD_WIDTH - 1) >> 3) & 0x1F);      // Xend = 159 → 0x13

  this->send_command_(0x45);
  this->send_data_(0x00);                                // Ystart lo
  this->send_data_(0x00);                                // Ystart hi
  this->send_data_((EPD_HEIGHT - 1) & 0xFF);             // Yend lo = 0x27
  this->send_data_(((EPD_HEIGHT - 1) >> 8) & 0x01);     // Yend hi = 0x01
}

// Matches official EPD_2IN15B_SetCursor exactly
void WaveshareEPaper2in15B::set_ram_counter_() {
  this->send_command_(0x4E);
  this->send_data_(0x00);

  this->send_command_(0x4F);
  this->send_data_(0x00);
  this->send_data_(0x00);
}

void WaveshareEPaper2in15B::initialize_display_() {
  ESP_LOGI(TAG, "======= SSD1680 INIT (official sequence) =======");

  // Matches EPD_2IN15B_Init exactly
  this->hardware_reset_();
  this->wait_until_idle_();

  this->send_command_(0x12);  // SW_RESET
  this->wait_until_idle_();

  this->send_command_(0x11);  // Data Entry Mode
  this->send_data_(0x03);     // X-inc, Y-inc

  this->set_ram_area_();      // SetWindows(0, 0, W-1, H-1)

  this->send_command_(0x3C);  // Border Waveform
  this->send_data_(0x05);

  this->set_ram_counter_();   // SetCursor(0, 0)
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

  // BW RAM (0x24):  1=white, 0=black — sent as-is
  // Red buffer stored as: 0=red, 1=white — inverted before sending to 0x26
  // (matches official EPD_2IN15B_Display which sends ~ImageRed to 0x26)
  if (is_red) {
    this->bw_buffer_[byte_idx]  |=  bit_mask;  // BW = white (let red show)
    this->red_buffer_[byte_idx] &= ~bit_mask;  // red_buf 0 = red pixel
  } else if (is_black) {
    this->bw_buffer_[byte_idx]  &= ~bit_mask;  // BW = black
    this->red_buffer_[byte_idx] |=  bit_mask;  // red_buf 1 = no red
  } else {
    this->bw_buffer_[byte_idx]  |=  bit_mask;  // BW = white
    this->red_buffer_[byte_idx] |=  bit_mask;  // red_buf 1 = no red
  }
}

void WaveshareEPaper2in15B::update() {
  if (!this->initialized_) {
    ESP_LOGI(TAG, "Initialising...");
    this->initialize_display_();
  }

  // Reset to white (official Clear uses 0xFF for BW, 0x00 for Red)
  memset(this->bw_buffer_,  0xFF, EPD_BUFFER_SIZE);
  memset(this->red_buffer_, 0xFF, EPD_BUFFER_SIZE);  // 1 = no red in our buffer

  if (this->writer_.has_value())
    (*this->writer_)(*this);

  uint32_t black_px = 0, red_px = 0;
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    black_px += __builtin_popcount(~this->bw_buffer_[i]  & 0xFF);
    red_px   += __builtin_popcount(~this->red_buffer_[i] & 0xFF);
  }
  ESP_LOGI(TAG, "Frame: %u black, %u red pixels", black_px, red_px);

  // Write BW RAM (0x24) — sent as-is, 1=white 0=black
  this->set_ram_counter_();
  this->send_command_(0x24);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->bw_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // Write RED RAM (0x26) — sent INVERTED (~red_buffer_), matching official ~ImageRed
  // red_buffer_ stores: 0=red pixel, 1=no red → ~red_buffer_: 1=red in RAM, 0=no red
  this->set_ram_counter_();
  this->send_command_(0x26);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(~this->red_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // TurnOnDisplay
  this->send_command_(0x20);
  this->wait_until_idle_();
  ESP_LOGI(TAG, "Refresh complete.");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
