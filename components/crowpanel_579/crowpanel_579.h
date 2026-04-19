#pragma once
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/display/display_buffer.h"

namespace esphome {
namespace crowpanel_579 {

class CrowPanel579 : public display::DisplayBuffer,
                     public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                                          spi::CLOCK_POLARITY_LOW,
                                          spi::CLOCK_PHASE_LEADING,
                                          spi::DATA_RATE_2MHZ> {
 public:
  void set_dc_pin(GPIOPin *pin) { dc_pin_ = pin; }
  void set_busy_pin(GPIOPin *pin) { busy_pin_ = pin; }
  void set_reset_pin(GPIOPin *pin) { reset_pin_ = pin; }
  void set_power_pin(GPIOPin *pin) { power_pin_ = pin; }

  void setup() override;
  void update() override;
  void fill(Color color) override;
  void display();
  void partial_refresh(int x, int y, int w, int h);

  int get_width_internal() override { return 792; }
  int get_height_internal() override { return 272; }
  display::DisplayType get_display_type() override {
    return display::DisplayType::DISPLAY_TYPE_BINARY;
  }
  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                      display::ColorOrder order, display::ColorBitness bitness,
                      bool big_endian, int x_offset, int y_offset, int x_pad) override;

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  size_t get_buffer_length_();
  void reset_();
  void wait_busy_();
  void send_command_(uint8_t cmd);
  void send_data_(uint8_t data);
  void init_display_();
  void set_ram_master_();
  void set_ram_slave_();
  void set_window_slave_(int byte_start, int byte_end, int y_start, int y_end);
  void set_window_master_(int byte_start, int byte_end, int y_start, int y_end);
  void write_ram_(uint8_t cmd, uint8_t fill, uint32_t count);

  GPIOPin *dc_pin_;
  GPIOPin *busy_pin_;
  GPIOPin *reset_pin_;
  GPIOPin *power_pin_{nullptr};
};

}  // namespace crowpanel_579
}  // namespace esphome