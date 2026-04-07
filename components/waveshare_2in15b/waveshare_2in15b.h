#pragma once

#include "esphome/core/component.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace waveshare_2in15b {

// Physical panel dimensions — matches EPD_2IN15B_WIDTH / HEIGHT in official source
static const uint16_t EPD_WIDTH  = 160;
static const uint16_t EPD_HEIGHT = 296;

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
  int get_width_internal()  override { return EPD_WIDTH;  }
  int get_height_internal() override { return EPD_HEIGHT; }

  void send_command_(uint8_t cmd);
  void send_data_(uint8_t data);
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

  static const uint32_t EPD_BUFFER_SIZE = (EPD_WIDTH * EPD_HEIGHT) / 8;
  uint8_t bw_buffer_[EPD_BUFFER_SIZE];
  uint8_t red_buffer_[EPD_BUFFER_SIZE];
};

}  // namespace waveshare_2in15b
}  // namespace esphome
