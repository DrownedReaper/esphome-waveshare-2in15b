#pragma once

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/log.h"

namespace esphome {
namespace waveshare {

class Waveshare2in15B
  : public display::DisplayBuffer,
    public spi::SPIDevice {

 public:
  void setup() override;
  void update() override;
  void fill(Color color) override;

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;

  int get_width_internal() override { return 296; }
  int get_height_internal() override { return 160; }

 private:
  uint8_t buffer_black_[296 * 160 / 8];
  uint8_t buffer_red_[296 * 160 / 8];

  void send_command(uint8_t cmd);
  void send_data(uint8_t data);
};

}  // namespace waveshare
}  // namespace esphome