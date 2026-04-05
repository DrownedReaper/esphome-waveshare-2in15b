#pragma once

#include "esphome/core/component.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace waveshare_2in15b {

// Display resolution
static const uint16_t EPD_WIDTH  = 160;
static const uint16_t EPD_HEIGHT = 296;

// Command registers (Waveshare EPD_2in15b datasheet)
static const uint8_t CMD_BOOSTER_SOFT_START  = 0x06;
static const uint8_t CMD_POWER_ON            = 0x04;
static const uint8_t CMD_POWER_OFF           = 0x02;
static const uint8_t CMD_PANEL_SETTING       = 0x00;
static const uint8_t CMD_RESOLUTION_SETTING  = 0x61;
static const uint8_t CMD_VCOM_DATA_INTERVAL  = 0x50;
static const uint8_t CMD_TCON_SETTING        = 0x60;
static const uint8_t CMD_DATA_START_TX_BW    = 0x10;  // Black/White plane
static const uint8_t CMD_DATA_START_TX_RED   = 0x13;  // Red plane
static const uint8_t CMD_DISPLAY_REFRESH     = 0x12;
static const uint8_t CMD_DEEP_SLEEP          = 0x07;

// Inherit DisplayBuffer (which gives us draw_absolute_pixel_internal as a
// pure virtual + provides draw_pixel_at and PollingComponent correctly).
class WaveshareEPaper2in15B
    : public display::DisplayBuffer,
      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                            spi::CLOCK_POLARITY_LOW,
                            spi::CLOCK_PHASE_LEADING,
                            spi::DATA_RATE_2MHZ> {
 public:
  void set_dc_pin(GPIOPin *pin)    { dc_pin_    = pin; }
  void set_reset_pin(GPIOPin *pin) { reset_pin_ = pin; }
  void set_busy_pin(GPIOPin *pin)  { busy_pin_  = pin; }

  void set_writer(std::function<void(WaveshareEPaper2in15B &)> writer) {
    writer_ = writer;
  }

  // Component
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  // Display
  display::DisplayType get_display_type() override {
    return display::DisplayType::DISPLAY_TYPE_COLOR;
  }

 protected:
  // Pure virtual from DisplayBuffer — our per-pixel write
  void draw_absolute_pixel_internal(int x, int y, Color color) override;

  int get_width_internal()  override { return EPD_WIDTH; }
  int get_height_internal() override { return EPD_HEIGHT; }

  void send_command_(uint8_t cmd);
  void send_data_(uint8_t data);
  void wait_until_idle_();
  void hardware_reset_();
  void initialize_display_();

  GPIOPin *dc_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};

  optional<std::function<void(WaveshareEPaper2in15B &)>> writer_{};

  // 1 bit per pixel, MSB first. Size = (160 × 296) / 8 = 5920 bytes
  static const uint32_t EPD_BUFFER_SIZE = (EPD_WIDTH * EPD_HEIGHT) / 8;
  uint8_t bw_buffer_[EPD_BUFFER_SIZE];   // 0=black, 1=white
  uint8_t red_buffer_[EPD_BUFFER_SIZE];  // 0=red,   1=white
};

}  // namespace waveshare_2in15b
}  // namespace esphome
