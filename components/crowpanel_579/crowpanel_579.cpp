#include "crowpanel_579.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"

namespace esphome {
namespace crowpanel_579 {

static const char *const TAG = "crowpanel_579";

// Buffer layout: 99 bytes/row × 272 rows = 26928 bytes total.
// The display uses two SSD1683 driver chips sharing the panel:
//   Slave  (LEFT  half): bytes  0–49 per row (covers columns   0–399)
//   Master (RIGHT half): bytes 49–98 per row (covers columns 392–791)
// The one-byte overlap at index 49 provides the 8-pixel seam alignment.
static const uint32_t FULL_BUF          = 26928;
static const uint32_t BYTES_PER_ROW     = 99;
static const uint32_t BYTES_PER_HALF_ROW = 50;

// Color convention (empirically confirmed):
//   bit = 1 in display RAM → white paper
//   bit = 0 in display RAM → black ink
//
// Lambda / draw_absolute_pixel_internal:
//   color.is_on() → black ink (bit cleared to 0)
//   !color.is_on() → white paper (bit set to 1)
//   Use Color::WHITE for black text, Color::BLACK to clear to white.
//
// LVGL / draw_pixels_at (16-bit RGB565 source):
//   luminance sum (r+g+b) >= 382 → white paper
//   luminance sum (r+g+b) <  382 → black ink

size_t CrowPanel579::get_buffer_length_() { return FULL_BUF; }

void CrowPanel579::setup() {
  if (this->power_pin_ != nullptr) {
    this->power_pin_->setup();
    this->power_pin_->digital_write(false);  // cut display power to clear any stuck state
    delay(500);
    this->power_pin_->digital_write(true);
    delay(100);
  }
  this->dc_pin_->setup();
  this->busy_pin_->setup();
  this->reset_pin_->setup();
  this->spi_setup();

  this->init_internal_(this->get_buffer_length_());
  memset(this->buffer_, 0xFF, this->get_buffer_length_());  // 0xFF = white paper
  this->init_display_();
  ESP_LOGI(TAG, "Setup complete");
}

void CrowPanel579::fill(Color color) {
  // Override ESPHome's fill() — base class may use memset with standard polarity,
  // but this display has inverted convention (is_on=true → black ink = 0x00).
  uint8_t fill_byte = color.is_on() ? 0x00 : 0xFF;
  memset(this->buffer_, fill_byte, this->get_buffer_length_());
}

void CrowPanel579::update() {
  this->do_update_();
  // When using LVGL: do NOT call display() here.
  // Instead, call display() from the lvgl: on_draw_end trigger in YAML.
  // When using a lambda: call display() at the end of the lambda.
}

void CrowPanel579::display() {
  // Slave: left half — bytes 0–49 per row, written in ascending order
  set_ram_slave_();
  send_command_(0xA4);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t y = 0; y < 272; y++) {
    uint32_t row = y * BYTES_PER_ROW;
    for (uint32_t b = 0; b < BYTES_PER_HALF_ROW; b++)
      this->write_byte(this->buffer_[row + b]);
    if (y % 68 == 0) App.feed_wdt();
  }
  this->disable();

  // Master: right half — bytes 49–98 per row, written in ascending order
  set_ram_master_();
  send_command_(0x24);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t y = 0; y < 272; y++) {
    uint32_t row = y * BYTES_PER_ROW;
    for (uint32_t b = BYTES_PER_HALF_ROW - 1; b < BYTES_PER_ROW; b++)
      this->write_byte(this->buffer_[row + b]);
    if (y % 68 == 0) App.feed_wdt();
  }
  this->disable();

  // Trigger full refresh
  send_command_(0x22);
  send_data_(0xF7);
  send_command_(0x20);
  this->wait_busy_();
}

void CrowPanel579::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= 792 || y < 0 || y >= 272)
    return;
  uint32_t pos = (y * 792 + x) / 8;
  uint8_t  bit = 7 - (x % 8);
  if (color.is_on()) {
    this->buffer_[pos] &= ~(1 << bit);  // black ink
  } else {
    this->buffer_[pos] |=  (1 << bit);  // white paper
  }
}

void CrowPanel579::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                                  display::ColorOrder order, display::ColorBitness bitness,
                                  bool big_endian, int x_offset, int y_offset, int x_pad) {
  const uint8_t *src = ptr;
  for (int y = y_start; y < y_start + h; y++) {
    for (int x = x_start; x < x_start + w; x++) {
      uint16_t pix = big_endian ? ((uint16_t)src[0] << 8 | src[1])
                                : ((uint16_t)src[1] << 8 | src[0]);
      src += 2;
      if (x < 0 || x >= 792 || y < 0 || y >= 272)
        continue;
      uint8_t r = (pix >> 11) << 3;
      uint8_t g = ((pix >> 5) & 0x3F) << 2;
      uint8_t b = (pix & 0x1F) << 3;
      uint32_t pos = (uint32_t)(y * 792 + x) / 8;
      uint8_t  bit = 7 - (x % 8);
      if ((int)r + g + b >= 382) {
        this->buffer_[pos] |=  (1 << bit);  // light → white paper
      } else {
        this->buffer_[pos] &= ~(1 << bit);  // dark  → black ink
      }
    }
    src += x_pad * 2;
  }
}

void CrowPanel579::reset_() {
  delay(10);
  this->reset_pin_->digital_write(false);
  delay(10);
  this->reset_pin_->digital_write(true);
  delay(10);
  this->wait_busy_();
}

void CrowPanel579::wait_busy_() {
  uint32_t start = millis();
  while (this->busy_pin_->digital_read()) {
    if (millis() - start > 10000) {
      ESP_LOGE(TAG, "Busy timeout");
      break;
    }
    App.feed_wdt();
    delay(10);
  }
}

void CrowPanel579::send_command_(uint8_t cmd) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(cmd);
  this->disable();
}

void CrowPanel579::send_data_(uint8_t data) {
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_byte(data);
  this->disable();
}

void CrowPanel579::set_ram_master_() {
  send_command_(0x11);
  send_data_(0x02);
  send_command_(0x44);
  send_data_(0x31);
  send_data_(0x00);
  send_command_(0x45);
  send_data_(0x00);
  send_data_(0x00);
  send_data_(0x0F);
  send_data_(0x01);
  send_command_(0x4E);
  send_data_(0x31);
  send_command_(0x4F);
  send_data_(0x00);
  send_data_(0x00);
}

void CrowPanel579::set_ram_slave_() {
  send_command_(0x91);
  send_data_(0x03);
  send_command_(0xC4);
  send_data_(0x00);
  send_data_(0x31);
  send_command_(0xC5);
  send_data_(0x00);
  send_data_(0x00);
  send_data_(0x0F);
  send_data_(0x01);
  send_command_(0xCE);
  send_data_(0x00);
  send_command_(0xCF);
  send_data_(0x00);
  send_data_(0x00);
}

void CrowPanel579::write_ram_(uint8_t cmd, uint8_t fill, uint32_t count) {
  send_command_(cmd);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint32_t i = 0; i < count; i++) {
    this->write_byte(fill);
    if (i % 1000 == 0) App.feed_wdt();
  }
  this->disable();
}

void CrowPanel579::set_window_slave_(int byte_start, int byte_end, int y_start, int y_end) {
  send_command_(0x91);
  send_data_(0x03);
  send_command_(0xC4);
  send_data_(byte_start & 0x3F);
  send_data_(byte_end   & 0x3F);
  send_command_(0xC5);
  send_data_(y_start & 0xFF);
  send_data_((y_start >> 8) & 0x01);
  send_data_(y_end & 0xFF);
  send_data_((y_end >> 8) & 0x01);
  send_command_(0xCE);
  send_data_(byte_start & 0x3F);
  send_command_(0xCF);
  send_data_(y_start & 0xFF);
  send_data_((y_start >> 8) & 0x01);
}

void CrowPanel579::set_window_master_(int byte_start, int byte_end, int y_start, int y_end) {
  // Master X address is inverted: X_addr = 98 - buffer_byte_index
  // Full panel: byte_start=49 → XSA=49, byte_end=98 → XEA=0 (matches set_ram_master_)
  int x_addr_start = 98 - byte_start;
  int x_addr_end   = 98 - byte_end;
  send_command_(0x11);
  send_data_(0x02);  // Y increment, X decrement
  send_command_(0x44);
  send_data_(x_addr_start & 0x3F);
  send_data_(x_addr_end   & 0x3F);
  send_command_(0x45);
  send_data_(y_start & 0xFF);
  send_data_((y_start >> 8) & 0x01);
  send_data_(y_end & 0xFF);
  send_data_((y_end >> 8) & 0x01);
  send_command_(0x4E);
  send_data_(x_addr_start & 0x3F);
  send_command_(0x4F);
  send_data_(y_start & 0xFF);
  send_data_((y_start >> 8) & 0x01);
}

void CrowPanel579::partial_refresh(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;

  int x_end = x + w - 1;
  int y_end = y + h - 1;
  if (x < 0)       x = 0;
  if (y < 0)       y = 0;
  if (x_end > 791) x_end = 791;
  if (y_end > 271) y_end = 271;

  bool touches_slave  = (x < 400);
  bool touches_master = (x_end >= 392);

  if (touches_slave) {
    int bs = x / 8;
    int be = (x_end < 399 ? x_end : 399) / 8;
    // Byte 49 is the slave's natural right boundary (the seam overlap byte).
    // Excluding it causes the OTP full-waveform to black out all non-window
    // pixels on the slave chip. Always extend to byte 49 when the region
    // reaches byte 48.
    if (be >= 48) be = 49;
    set_window_slave_(bs, be, y, y_end);
    send_command_(0xA4);
    this->dc_pin_->digital_write(true);
    this->enable();
    for (int row = y; row <= y_end; row++) {
      for (int b = bs; b <= be; b++)
        this->write_byte(this->buffer_[row * BYTES_PER_ROW + b]);
      App.feed_wdt();
    }
    this->disable();
  }

  if (touches_master) {
    int px_start = (x >= 392 ? x : 392);
    int px_end   = (x_end <= 791 ? x_end : 791);
    int bs = 49 + (px_start - 392) / 8;
    int be = 49 + (px_end   - 392) / 8;
    set_window_master_(bs, be, y, y_end);
    send_command_(0x24);
    this->dc_pin_->digital_write(true);
    this->enable();
    for (int row = y; row <= y_end; row++) {
      for (int b = bs; b <= be; b++)
        this->write_byte(this->buffer_[row * BYTES_PER_ROW + b]);
      App.feed_wdt();
    }
    this->disable();
  }

  // OTP LUT partial refresh — 0xFF uses the full B/W waveform already stored in OTP.
  // For fast mode, write 0x1A before calling partial_refresh: 0x6E=~1.5s, 0x5A=~1.0s
  send_command_(0x22);
  send_data_(0xFF);
  send_command_(0x20);
  this->wait_busy_();
}

void CrowPanel579::init_display_() {
  this->reset_();
  this->wait_busy_();
  send_command_(0x12);  // SW reset
  this->wait_busy_();
  send_command_(0x18);
  send_data_(0x80);     // internal temperature sensor
  send_command_(0x22);
  send_data_(0xB1);     // enable clock, CP, load temp
  send_command_(0x20);
  this->wait_busy_();
  send_command_(0x1A);
  send_data_(0x64);
  send_data_(0x00);
  send_command_(0x22);
  send_data_(0x91);     // enable clock, load LUT from OTP
  send_command_(0x20);
  this->wait_busy_();
  send_command_(0x3C);
  send_data_(0x01);     // border waveform

  // Clear both RAM banks on both chips to white.
  // Old RAM must match New RAM or the anti-ghosting waveform produces static.
  set_ram_master_();
  write_ram_(0x24, 0x00, 13600);  // master new RAM = white
  set_ram_master_();
  write_ram_(0x26, 0x00, 13600);  // master old RAM = white
  set_ram_slave_();
  write_ram_(0xA4, 0x00, 13600);  // slave new RAM = white
  set_ram_slave_();
  write_ram_(0xA6, 0x00, 13600);  // slave old RAM = white

  // Full refresh to bring panel to known-white state
  send_command_(0x22);
  send_data_(0xF7);
  send_command_(0x20);
  this->wait_busy_();

  ESP_LOGI(TAG, "Init complete");
}

}  // namespace crowpanel_579
}  // namespace esphome
