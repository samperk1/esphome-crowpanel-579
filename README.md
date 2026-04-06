# ESPHome: Elecrow CrowPanel 5.79" E-Paper Display Driver

A custom ESPHome external component for the **Elecrow CrowPanel 5.79" e-paper display** (model DIS08792E). Supports both the ESPHome lambda drawing API and LVGL.

## Hardware

| Spec | Value |
|------|-------|
| Display | Elecrow CrowPanel 5.79" E-Paper (DIS08792E) |
| Resolution | 792 × 272 pixels, black & white |
| Driver ICs | Dual SSD1683 (one per half of the panel) |
| MCU | ESP32-S3 (tested on ESP32-S3-WROOM-1-N8R8) |

### Pin Connections (CrowPanel default wiring)

| Signal | GPIO |
|--------|------|
| SPI CLK | GPIO12 |
| SPI MOSI | GPIO11 |
| CS | GPIO45 |
| DC | GPIO46 |
| RST | GPIO47 |
| BUSY | GPIO48 |
| PWR (optional) | GPIO7 |

## Installation

Copy the `crowpanel_579` folder into your ESPHome `config/components/` directory, then reference it in your YAML:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [crowpanel_579]
```

## Basic YAML (lambda mode)

```yaml
spi:
  clk_pin: GPIO12
  mosi_pin: GPIO11

display:
  - platform: crowpanel_579
    id: my_display
    cs_pin: GPIO45
    dc_pin: GPIO46
    reset_pin: GPIO47
    busy_pin:
      number: GPIO48
      mode:
        input: true
        pulldown: true
    rotation: 90°
    update_interval: never
    lambda: |-
      it.fill(Color::BLACK);  // clears screen to white (see color note below)
      it.print(10, 10, id(my_font), Color::WHITE, "Hello World");
```

> **Color convention:** This display uses an inverted convention in lambda mode.
> `Color::WHITE` draws **black ink**. `Color::BLACK` clears to **white paper**.
> Use `it.fill(Color::BLACK)` for a white background.

## LVGL

```yaml
spi:
  clk_pin: GPIO12
  mosi_pin: GPIO11

display:
  - platform: crowpanel_579
    id: my_display
    cs_pin: GPIO45
    dc_pin: GPIO46
    reset_pin: GPIO47
    busy_pin:
      number: GPIO48
      mode:
        input: true
        pulldown: true
    rotation: 90
    auto_clear_enabled: false

lvgl:
  displays:
    - my_display
  color_depth: 16
  bg_color: 0xFFFFFF
  on_draw_end:
    - lambda: "id(my_display).display();"
  pages:
    - id: main_page
      widgets:
        - label:
            text: "Hello from LVGL"
            align: CENTER
            text_color: 0x000000
```

> **LVGL notes:**
> - `color_depth: 16` is required — 1-bit mode is not supported.
> - `auto_clear_enabled: false` is required — prevents the buffer being wiped before LVGL writes.
> - Call `display()` from `on_draw_end`, not from `update()`.
> - Use `bg_color: 0xFFFFFF` (white) and `text_color: 0x000000` (black).

## Optional: Power Pin

If your board has a power control pin for the display (GPIO7 on CrowPanel):

```yaml
display:
  - platform: crowpanel_579
    ...
    power_pin: GPIO7
```

## How It Works

The 5.79" panel uses **two SSD1683 driver chips** wired to the same SPI bus — one chip drives the left half, the other drives the right half. They are addressed by different commands:

- **Slave** (left, columns 0–399): commands `0x91`, `0xA4`, `0xA6`, `0xC4/C5/CE/CF`
- **Master** (right, columns 392–791): commands `0x11`, `0x24`, `0x26`, `0x44/45/4E/4F`

The single framebuffer is 99 bytes × 272 rows = 26,928 bytes. On each `display()` call, the left half (bytes 0–49 per row) is sent to the slave chip and the right half (bytes 49–98 per row) is sent to the master chip.

A full e-paper refresh takes approximately 3 seconds.

## Known Limitations

- Full refresh only — no partial refresh support.
- The `display()` call blocks the main loop for ~3 seconds while waiting for the panel to finish refreshing.
- Tested on ESPHome 2026.3.x with ESP-IDF framework only (not Arduino).
