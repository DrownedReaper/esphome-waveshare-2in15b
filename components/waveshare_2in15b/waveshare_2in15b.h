#pragma once

#include "esphome/core/component.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace waveshare_2in15b {

static const uint16_t EPD_WIDTH    = 160;
static const uint16_t EPD_HEIGHT   = 296;
static const uint8_t  EPD_Y_OFFSET = 5;   // skip dummy gate lines (red bar)

// SSD1680 command set (completely different from UC8151/UC8253)
static const uint8_t SSD1680_SW_RESET            = 0x12;
static const uint8_t SSD1680_DRIVER_OUTPUT        = 0x01;
static const uint8_t SSD1680_DATA_ENTRY_MODE      = 0x11;
static const uint8_t SSD1680_SET_RAM_X            = 0x44;
static const uint8_t SSD1680_SET_RAM_Y            = 0x45;
static const uint8_t SSD1680_BORDER_WAVEFORM      = 0x3C;
static const uint8_t SSD1680_TEMP_SENSOR          = 0x18;
static const uint8_t SSD1680_DISPLAY_UPDATE_CTRL1 = 0x21;
static const uint8_t SSD1680_DISPLAY_UPDATE_CTRL2 = 0x22;
static const uint8_t SSD1680_MASTER_ACTIVATION    = 0x20;
static const uint8_t SSD1680_WRITE_RAM_BW         = 0x24;
static const uint8_t SSD1680_WRITE_RAM_RED        = 0x26;
static const uint8_t SSD1680_SET_RAM_X_COUNTER    = 0x4E;
static const uint8_t SSD1680_SET_RAM_Y_COUNTER    = 0x4F;
static const uint8_t SSD1680_NOP                  = 0xFF;

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

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  display::DisplayType get_display_type() override {
    return display::DisplayType::DISPLAY_TYPE_COLOR;
  }

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  int get_width_internal()  override { return EPD_WIDTH; }
  int get_height_internal() override { return EPD_HEIGHT; }

  void send_command_(uint8_t cmd);
  void send_data_(uint8_t data);
  // SSD1680: BUSY HIGH = working, BUSY LOW = idle. Wait for LOW.
  void wait_until_idle_();
  void hardware_reset_();
  void initialize_display_();
  void set_ram_area_();
  void set_ram_counter_();

  GPIOPin *dc_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};

  bool initialized_{false};

  optional<std::function<void(WaveshareEPaper2in15B &)>> writer_{};

  // 1 bit per pixel MSB first: 160 × 296 / 8 = 5920 bytes each
  static const uint32_t EPD_BUFFER_SIZE = (EPD_WIDTH * EPD_HEIGHT) / 8;
  uint8_t bw_buffer_[EPD_BUFFER_SIZE];
  uint8_t red_buffer_[EPD_BUFFER_SIZE];
};

}  // namespace waveshare_2in15b
}  // namespace esphome
