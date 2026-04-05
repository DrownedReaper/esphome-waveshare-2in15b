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
  this->dc_pin_->digital_write(false);  // DC LOW = command
  this->enable();
  this->write_byte(cmd);
  this->disable();
}

void WaveshareEPaper2in15B::send_data_(uint8_t data) {
  this->dc_pin_->digital_write(true);   // DC HIGH = data
  this->enable();
  this->write_byte(data);
  this->disable();
}

// ---------------------------------------------------------------------------
// SSD1680 BUSY: HIGH = busy/working, LOW = idle/ready
// We wait for the pin to go LOW (idle).
// NOTE: use busy_pin with NO inversion in YAML.
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    ESP_LOGW(TAG, "No BUSY pin — 2s blind delay");
    delay(2000);
    return;
  }
  // SSD1680: busy = HIGH, idle = LOW
  // Wait for LOW (idle)
  ESP_LOGD(TAG, "Waiting for BUSY LOW (idle)... currently %s",
           this->busy_pin_->digital_read() ? "HIGH(busy)" : "LOW(idle)");
  uint32_t start = millis();
  while (this->busy_pin_->digital_read() == true) {
    if (millis() - start > 10000) {
      ESP_LOGW(TAG, "BUSY timeout 10s — still HIGH");
      break;
    }
    delay(10);
    App.feed_wdt();
  }
  ESP_LOGD(TAG, "BUSY LOW after %ums", millis() - start);
}

// ---------------------------------------------------------------------------
// Hardware reset
// SSD1680 needs a clean RST pulse — keep it short (2ms) to avoid
// triggering the HAT+'s power-off switch circuit.
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::hardware_reset_() {
  if (this->reset_pin_ == nullptr) return;
  this->reset_pin_->digital_write(true);  delay(20);
  this->reset_pin_->digital_write(false); delay(2);
  this->reset_pin_->digital_write(true);  delay(20);
  ESP_LOGD(TAG, "RST done, BUSY=%s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH(busy)" : "LOW(idle)") : "no pin");
}

// ---------------------------------------------------------------------------
// Set RAM window: X = 0..19 (160px/8=20 bytes), Y = 0..295
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::set_ram_area_() {
  // RAM X: byte 0 = start (0x00), byte 1 = end (19 = 0x13)
  this->send_command_(SSD1680_SET_RAM_X);
  this->send_data_(0x00);
  this->send_data_(0x13);  // (160/8) - 1 = 19 = 0x13

  // RAM Y: 4 bytes — start_lo, start_hi, end_lo, end_hi
  // 296-1 = 295 = 0x0127
  this->send_command_(SSD1680_SET_RAM_Y);
  this->send_data_(0x00);  // Y start lo
  this->send_data_(0x00);  // Y start hi
  this->send_data_(0x27);  // Y end lo  (295 = 0x127 → lo=0x27)
  this->send_data_(0x01);  // Y end hi
}

// ---------------------------------------------------------------------------
// Reset RAM address counters to (0,0) before writing pixel data
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::set_ram_counter_() {
  this->send_command_(SSD1680_SET_RAM_X_COUNTER);
  this->send_data_(0x00);

  this->send_command_(SSD1680_SET_RAM_Y_COUNTER);
  this->send_data_(0x00);
  this->send_data_(0x00);
}

// ---------------------------------------------------------------------------
// SSD1680 init sequence for 160×296 BWR
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::initialize_display_() {
  ESP_LOGI(TAG, "======= SSD1680 INIT START =======");

  this->hardware_reset_();
  this->wait_until_idle_();

  // Software reset — resets all registers to default
  ESP_LOGI(TAG, "SW_RESET (0x12)");
  this->send_command_(SSD1680_SW_RESET);
  this->wait_until_idle_();

  // Driver Output Control: 296 gate lines
  // (295 = 0x0127: lo=0x27, hi=0x01), GD=0, SM=0, TB=0
  ESP_LOGI(TAG, "Driver Output Control (0x01)");
  this->send_command_(SSD1680_DRIVER_OUTPUT);
  this->send_data_(0x27);  // MUX[7:0] = 295 & 0xFF
  this->send_data_(0x01);  // MUX[8] = 1
  this->send_data_(0x00);  // GD=0, SM=0, TB=0

  // Data Entry Mode: X increment, Y increment (portrait)
  ESP_LOGI(TAG, "Data Entry Mode (0x11): 0x03");
  this->send_command_(SSD1680_DATA_ENTRY_MODE);
  this->send_data_(0x03);

  // Set RAM window
  this->set_ram_area_();

  // Border waveform: follow LUT
  this->send_command_(SSD1680_BORDER_WAVEFORM);
  this->send_data_(0x05);

  // Temperature sensor: internal
  this->send_command_(SSD1680_TEMP_SENSOR);
  this->send_data_(0x80);

  // Display Update Control 1: for BWR mode
  // Byte 0 = 0x00: normal BW RAM, Byte 1 = 0x80: RED RAM from 0x26
  this->send_command_(SSD1680_DISPLAY_UPDATE_CTRL1);
  this->send_data_(0x00);
  this->send_data_(0x80);

  // Reset RAM counters
  this->set_ram_counter_();

  this->initialized_ = true;
  ESP_LOGI(TAG, "BUSY after init: %s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH(busy)" : "LOW(idle)") : "no pin");
  ESP_LOGI(TAG, "======= SSD1680 INIT DONE =======");
}

// ---------------------------------------------------------------------------
// setup() — pins + SPI only, init deferred to first update()
// ---------------------------------------------------------------------------

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
  ESP_LOGCONFIG(TAG, "  Controller: SSD1680 (BWR mode)");
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", EPD_WIDTH, EPD_HEIGHT);
  ESP_LOGCONFIG(TAG, "  Initialized: %s", this->initialized_ ? "YES" : "NO");
  LOG_UPDATE_INTERVAL(this);
}

// ---------------------------------------------------------------------------
// Pixel writing
// SSD1680 BW RAM: 0=black, 1=white
// SSD1680 RED RAM: 0=red,   1=white (background)
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT)
    return;

  uint32_t byte_idx = (x + y * EPD_WIDTH) / 8;
  uint8_t  bit_mask = 0x80 >> (x % 8);

  bool is_red   = (color.r > 200 && color.g < 100 && color.b < 100);
  bool is_black = (!is_red && color.r < 64 && color.g < 64 && color.b < 64);

  if (is_red) {
    this->red_buffer_[byte_idx] &= ~bit_mask;  // 0 = red
    this->bw_buffer_[byte_idx]  |=  bit_mask;  // 1 = white (not black here)
  } else if (is_black) {
    this->bw_buffer_[byte_idx]  &= ~bit_mask;  // 0 = black
    this->red_buffer_[byte_idx] |=  bit_mask;  // 1 = white (not red here)
  } else {
    this->bw_buffer_[byte_idx]  |=  bit_mask;  // 1 = white
    this->red_buffer_[byte_idx] |=  bit_mask;  // 1 = white
  }
}

// ---------------------------------------------------------------------------
// Full refresh
// ---------------------------------------------------------------------------

void WaveshareEPaper2in15B::update() {
  // Always re-init so we can see init logs after logger is ready
  ESP_LOGI(TAG, "update() — running SSD1680 init");
  this->initialized_ = false;
  this->initialize_display_();

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

  // Reset RAM counters before writing
  this->set_ram_counter_();

  // Write B/W RAM (0x24)
  ESP_LOGI(TAG, "Writing BW RAM (0x24)...");
  this->send_command_(SSD1680_WRITE_RAM_BW);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->bw_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // Reset counters again before writing red
  this->set_ram_counter_();

  // Write RED RAM (0x26)
  ESP_LOGI(TAG, "Writing RED RAM (0x26)...");
  this->send_command_(SSD1680_WRITE_RAM_RED);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->red_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  // Trigger full refresh
  ESP_LOGI(TAG, "Display Update Control 2 (0x22): 0xF7");
  this->send_command_(SSD1680_DISPLAY_UPDATE_CTRL2);
  this->send_data_(0xF7);  // full update LUT

  ESP_LOGI(TAG, "Master Activation (0x20). BUSY=%s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH" : "LOW") : "no pin");
  this->send_command_(SSD1680_MASTER_ACTIVATION);
  delay(10);
  ESP_LOGI(TAG, "BUSY 10ms after activation: %s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH(busy,good)" : "LOW(idle)") : "no pin");

  this->wait_until_idle_();
  ESP_LOGI(TAG, "======= Refresh complete =======");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
