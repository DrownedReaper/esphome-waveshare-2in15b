# Waveshare 2.15" HAT+ (B) — ESPHome External Component

Custom ESPHome driver for the **Waveshare 2.15" HAT+ (B)**  
Resolution: **160×296** | Colors: **Red / Black / White** | Controller: **SSD1680** | Interface: **SPI Mode 0**

Tested on: **ESP32-C3 SuperMini** with ESPHome 2024+

---

## Quick Start (Git-based)

Add this to your YAML — no file copying needed:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/DrownedReaper/esphome-waveshare-2in15b
      ref: main
    refresh: 0s
    components: [waveshare_2in15b]
```

---

## Wiring — ESP32-C3 SuperMini → Waveshare 2.15" HAT+ (B)

The HAT+ uses a 9-pin JST connector. Tested pin assignments:

| HAT+ Pin | Signal | ESP32-C3 GPIO | Notes                    |
|----------|--------|---------------|--------------------------|
| VCC/PWR  | PWR    | GPIO10 (HIGH) | Drive HIGH via GPIO output |
| GND      | GND    | GND           |                          |
| DIN      | MOSI   | GPIO6         | SPI data                 |
| CLK      | SCK    | GPIO4         | SPI clock                |
| CS       | CS     | GPIO7         | Chip select              |
| DC       | DC     | GPIO0         | Data/Command select      |
| RST      | RESET  | GPIO1         | Hardware reset           |
| BUSY     | BUSY   | GPIO9         | HIGH = busy, LOW = idle  |

> **Note:** The PWR pin must be driven HIGH for the display to operate.
> Wire it to 3.3V directly or control it via a GPIO output (recommended
> for power management).

---

## Minimal YAML

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/DrownedReaper/esphome-waveshare-2in15b
      ref: main
    refresh: 0s
    components: [waveshare_2in15b]

spi:
  clk_pin: GPIO4
  mosi_pin: GPIO6

output:
  - platform: gpio
    pin: GPIO10
    id: epaper_pwr

esphome:
  on_boot:
    priority: 600
    then:
      - output.turn_on: epaper_pwr

color:
  - id: color_black
    red: 0%;   green: 0%;   blue: 0%
  - id: color_red
    red: 100%; green: 0%;   blue: 0%
  - id: color_white
    red: 100%; green: 100%; blue: 100%

font:
  - file: "gfonts://Roboto"
    id: font_medium
    size: 16

display:
  - platform: waveshare_2in15b
    id: epaper_display
    cs_pin: GPIO7
    dc_pin: GPIO0
    reset_pin: GPIO1
    busy_pin: GPIO9
    rotation: 90
    update_interval: 300s
    lambda: |-
      it.fill(id(color_white));
      it.print(10, 10, id(font_medium), id(color_black), "Hello World!");
      it.print(10, 40, id(font_medium), id(color_red), "Red text");

button:
  - platform: template
    name: "Refresh Display"
    on_press:
      then:
        - component.update: epaper_display
```

---

## Key Display Facts

- **Controller:** SSD1680 (confirmed via testing)
- **Tri-color:** black, red, and white only
- **No partial refresh:** BWR e-paper physically cannot do partial refresh
- **Refresh time:** ~23 seconds for a full tri-color refresh
- **Minimum refresh interval:** 180 seconds (Waveshare recommendation)
- **BUSY pin:** HIGH = busy, LOW = idle (SSD1680 polarity — no `inverted: true`)
- **Rotation:** Use `rotation: 90` for landscape orientation
- **Init sequence:** Matches the official Waveshare `EPD_2IN15b.c` source exactly

---

## Color Usage

```yaml
color:
  - id: color_black
    red: 0%;   green: 0%;   blue: 0%
  - id: color_red
    red: 100%; green: 0%;   blue: 0%
  - id: color_white
    red: 100%; green: 100%; blue: 100%
```

Colors outside black/red/white snap to white.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Blank screen after flash | PWR pin not driven HIGH | Add `output.turn_on: epaper_pwr` on boot |
| BUSY stuck HIGH forever | Wrong BUSY polarity | Do NOT use `inverted: true` on busy_pin |
| BUSY timeout in logs | RST not wired | Connect RST pin |
| All red screen | Missing RAM counter reset | Update to latest version |
| Wrong colours | Verify color definitions match above | Ensure red is 100% red, 0% green, 0% blue |

