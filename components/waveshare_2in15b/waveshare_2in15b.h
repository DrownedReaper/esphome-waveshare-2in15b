#pragma once

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/gpio.h"
#include "esphome/core/log.h"

namespace esphome {
namespace waveshare {

class Waveshare2in15B
  : public display::DisplayBuffer,
    public spi::SPIDevice<
        spi::SPIBitOrder::BIT_ORDER_MSB_FIRST,
        spi::SPIClockPolarity::CLOCK_POLARITY_LOW,
        spi::SPIClockPhase::CLOCK_PHASE_LEADING,
        spi::SPIDataRate::DATA_RATE_4MHZ> {

 public:
  void setup() override;
  void update() override;
  void loop() override;
  void fill(Color color) override;

  display::DisplayType get_display_type() override;

  void set_dc_pin(GPIOPin *pin) { dc_pin_ = pin; }
  void set_reset_pin(GPIOPin *pin) { reset_pin_ = pin; }
  void set_busy_pin(GPIOPin *pin) { busy_pin_ = pin; }
  void set_power_pin(GPIOPin *pin) { power_pin_ = pin; }

  void request_refresh();

 protected:
  bool initialized_{false};
  bool refresh_requested_{false};
  bool refresh_in_progress_{false};
  uint8_t init_step_{0};

  void init_display_step_();

  int get_width_internal() override { return 296; }
  int get_height_internal() override { return 160; }

  void draw_absolute_pixel_internal(int x, int y, Color color) override;

  // --- internal helpers (must be declared here) ---
  void send_command(uint8_t cmd);
  void send_data(uint8_t data);
  void wait_until_idle_();
  void load_lut_();

  // GPIOs
  GPIOPin *dc_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};
  GPIOPin *power_pin_{nullptr};

  // Framebuffers
  uint8_t buffer_black_[296 * 160 / 8];
  uint8_t buffer_red_[296 * 160 / 8];
};

}  // namespace waveshare
}  // namespace esphome
