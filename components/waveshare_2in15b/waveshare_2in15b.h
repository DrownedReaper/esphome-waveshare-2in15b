#pragma once

#include "esphome/core/component.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace waveshare_2in15b {

// Display resolution
static const uint16_t EPD_WIDTH  = 160;
static const uint16_t EPD_HEIGHT = 296;

// Command register addresses (from Waveshare EPD_2in15b datasheet)
static const uint8_t CMD_PANEL_SETTING          = 0x00;
static const uint8_t CMD_POWER_SETTING          = 0x01;
static const uint8_t CMD_POWER_OFF             = 0x02;
static const uint8_t CMD_POWER_ON              = 0x04;
static const uint8_t CMD_BOOSTER_SOFT_START    = 0x06;
static const uint8_t CMD_DISPLAY_REFRESH       = 0x12;
static const uint8_t CMD_DATA_START_TX_BW      = 0x10;  // Black/White channel
static const uint8_t CMD_DATA_START_TX_RED     = 0x13;  // Red channel
static const uint8_t CMD_VCOM_DATA_INTERVAL    = 0x50;
static const uint8_t CMD_TCON_SETTING          = 0x60;
static const uint8_t CMD_RESOLUTION_SETTING    = 0x61;
static const uint8_t CMD_FLASH_MODE            = 0xE3;
static const uint8_t CMD_DEEP_SLEEP            = 0x07;

class WaveshareEPaper2in15B : public display::DisplayBuffer,
                               public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                                                     spi::CLOCK_POLARITY_LOW,
                                                     spi::CLOCK_PHASE_LEADING,
                                                     spi::DATA_RATE_2MHZ> {
 public:
  void set_dc_pin(GPIOPin *dc_pin) { dc_pin_ = dc_pin; }
  void set_reset_pin(GPIOPin *reset_pin) { reset_pin_ = reset_pin; }
  void set_busy_pin(GPIOPin *busy_pin) { busy_pin_ = busy_pin; }

  void setup() override;
  void dump_config() override;
  void update() override;

  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }

  int get_width_internal() override { return EPD_WIDTH; }
  int get_height_internal() override { return EPD_HEIGHT; }

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;

  void initialize_display_();
  void send_command_(uint8_t cmd);
  void send_data_(uint8_t data);
  void wait_until_idle_();
  void hardware_reset_();
  void deep_sleep_();

  GPIOPin *dc_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};

  // Two buffers: black/white and red/white
  // Each bit represents one pixel. 1 = white, 0 = black or red.
  // Buffer size = (WIDTH * HEIGHT) / 8 bytes
  static const uint32_t BUFFER_SIZE = (EPD_WIDTH * EPD_HEIGHT) / 8;
  uint8_t bw_buffer_[BUFFER_SIZE];   // Black/White plane
  uint8_t red_buffer_[BUFFER_SIZE];  // Red plane
};

}  // namespace waveshare_2in15b
}  // namespace esphome
