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
    ESP_LOGW(TAG, "No BUSY pin — 20s blind delay");
    delay(20000);
    return;
  }
  ESP_LOGI(TAG, "BUSY pin state: %s", this->busy_pin_->digital_read() ? "HIGH(idle)" : "LOW(busy)");
  uint32_t start = millis();
  while (!this->busy_pin_->digital_read()) {
    if (millis() - start > 30000) {
      ESP_LOGW(TAG, "BUSY timeout 30s — stuck LOW");
      break;
    }
    delay(50);
    App.feed_wdt();
  }
  ESP_LOGI(TAG, "BUSY wait done: %ums, pin now %s",
           millis() - start,
           this->busy_pin_->digital_read() ? "HIGH" : "LOW");
}

void WaveshareEPaper2in15B::hardware_reset_() {
  ESP_LOGI(TAG, "Hardware reset...");
  if (this->reset_pin_ == nullptr) {
    ESP_LOGW(TAG, "No RST pin");
    return;
  }
  this->reset_pin_->digital_write(true);  delay(20);
  this->reset_pin_->digital_write(false); delay(5);
  this->reset_pin_->digital_write(true);  delay(20);
  ESP_LOGI(TAG, "Reset done");
}

void WaveshareEPaper2in15B::initialize_display_() {
  ESP_LOGI(TAG, "======= EPD INIT START =======");
  ESP_LOGI(TAG, "BUSY before reset: %s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH" : "LOW") : "no pin");

  this->hardware_reset_();
  delay(200);

  ESP_LOGI(TAG, "BUSY after reset+200ms: %s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH" : "LOW") : "no pin");

  ESP_LOGI(TAG, "Sending booster soft start 0x06...");
  this->send_command_(CMD_BOOSTER_SOFT_START);
  this->send_data_(0x17);
  this->send_data_(0x17);
  this->send_data_(0x17);

  ESP_LOGI(TAG, "Sending power on 0x04...");
  this->send_command_(CMD_POWER_ON);
  delay(300);
  ESP_LOGI(TAG, "BUSY after power on+300ms: %s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH" : "LOW") : "no pin");

  ESP_LOGI(TAG, "Panel setting 0x00...");
  this->send_command_(CMD_PANEL_SETTING);
  this->send_data_(0x0F);

  ESP_LOGI(TAG, "Resolution 160x296...");
  this->send_command_(CMD_RESOLUTION_SETTING);
  this->send_data_(0x00);
  this->send_data_(0xA0);
  this->send_data_(0x01);
  this->send_data_(0x28);

  this->send_command_(CMD_VCOM_DATA_INTERVAL);
  this->send_data_(0x11);

  this->send_command_(CMD_TCON_SETTING);
  this->send_data_(0x22);

  ESP_LOGI(TAG, "BUSY after full init: %s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH" : "LOW") : "no pin");
  ESP_LOGI(TAG, "======= EPD INIT DONE =======");

  this->initialized_ = true;
}

// setup() only configures pins and SPI — NO display init here.
// Init is deferred to the first update() so it runs after logger is ready.
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

  // Do NOT call initialize_display_() here — logger isn't ready yet.
  // It will be called at the start of the first update().
}

void WaveshareEPaper2in15B::dump_config() {
  LOG_DISPLAY("", "Waveshare 2.15\" B E-Paper", this);
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

void WaveshareEPaper2in15B::update() {
  // Initialise on first update() — logger is guaranteed ready by now
  if (!this->initialized_) {
    ESP_LOGI(TAG, "First update() — running deferred init");
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

  ESP_LOGI(TAG, "Sending B/W buffer...");
  this->send_command_(CMD_DATA_START_TX_BW);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->bw_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  ESP_LOGI(TAG, "Sending RED buffer...");
  this->send_command_(CMD_DATA_START_TX_RED);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < EPD_BUFFER_SIZE; i++) {
    this->write_byte(this->red_buffer_[i]);
    if (i % 512 == 0) App.feed_wdt();
  }
  this->disable();

  ESP_LOGI(TAG, "BUSY before refresh cmd: %s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH" : "LOW") : "no pin");
  this->send_command_(CMD_DISPLAY_REFRESH);
  delay(100);
  ESP_LOGI(TAG, "BUSY after refresh cmd: %s",
           this->busy_pin_ ? (this->busy_pin_->digital_read() ? "HIGH" : "LOW") : "no pin");

  this->wait_until_idle_();
  ESP_LOGI(TAG, "======= Refresh complete =======");
}

}  // namespace waveshare_2in15b
}  // namespace esphome
