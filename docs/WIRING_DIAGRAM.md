# IonOS — Hardware Wiring Reference

> **Interactive diagram**: open `docs/wiring_diagram.html` in any browser
> for the full colour-coded visual schematic.

---

## Pin Summary

### ST7789V Display — SPI2

| Display Pin | ESP32-S3 GPIO | Notes |
|-------------|---------------|-------|
| SCL | 12 | SPI clock |
| SDA | 11 | SPI MOSI |
| RES | 8 | Reset (active LOW) |
| DC | 9 | Data / Command select |
| CS | 10 | Chip select (active LOW) |
| BLK | 13 | Backlight — LEDC PWM channel 0 |
| VCC | 3.3 V | |
| GND | GND | |

MADCTL = `0x60` → landscape 320×240. Try `0xA0` if image is flipped.

---

### PCM5102A Audio DAC — I2S0

| DAC Pin | ESP32-S3 GPIO | Notes |
|---------|---------------|-------|
| BCK | 4 | Bit clock |
| LRCK / WS | 5 | Word select / frame sync |
| DIN | 6 | Serial data |
| VDD | 3.3 V | |
| GND | GND | |
| XSMT | 3.3 V | **Must be HIGH to unmute** |
| FLT | GND | Normal latency |
| DEMP | GND | De-emphasis off |

---

### WS2812B RGB LEDs — RMT

| LED Pin | Connection | Notes |
|---------|-----------|-------|
| VCC | **5 V external** | NOT 3.3 V |
| GND | GND | Add 100 µF across VCC/GND |
| DIN | GPIO 48 | **300 Ω series resistor required** |
| DOUT | → LED 2 DIN | Chain 7 LEDs |

---

### microSD Card — SPI3

| SD Pin | ESP32-S3 GPIO | Notes |
|--------|---------------|-------|
| MOSI | 35 | |
| SCK | 36 | |
| MISO | 37 | **10 KΩ pull-up to 3.3 V** |
| CS | 38 | |
| VCC | 3.3 V | |
| GND | GND | |

Format card as FAT32 with 32 KB allocation units.

---

### nRF24L01+ Radio — shared SPI

| nRF24 Pin | ESP32-S3 GPIO | Notes |
|-----------|---------------|-------|
| MOSI | 2 | Long trace — route below chip |
| MISO | 3 | Long trace |
| SCK | 7 | Long trace |
| CSN | 39 | |
| CE | 40 | |
| IRQ | 41 | |
| VCC | 3.3 V | **100 µF + 100 nF bypass caps essential** |
| GND | GND | |

---

### 9 Buttons — active LOW

All buttons: one pin to GPIO, other pin to GND.
Internal pull-ups enabled in firmware (`GPIO_PULLUP_ENABLE`).

| Button | GPIO | IonOS key | Game Boy |
|--------|------|-----------|---------|
| UP | 14 | ION_KEY_UP | D-pad Up |
| DOWN | 15 | ION_KEY_DOWN | D-pad Down |
| LEFT | 16 | ION_KEY_LEFT | D-pad Left |
| RIGHT | 17 | ION_KEY_RIGHT | D-pad Right |
| **X ★** | **20** | **ION_KEY_X — Primary select** | **GB A** |
| A | 18 | ION_KEY_A — Context | GB B |
| B | 19 | ION_KEY_B — Back/cancel | Exit emu |
| START | 21 | ION_KEY_START | GB Start |
| MENU | 47 | ION_KEY_MENU — System menu | GB Select |

**X = confirm / select** (maps to `LV_KEY_ENTER`).
**B = back / cancel** (maps to `LV_KEY_ESC`).
Long-press MENU = power off.

---

### Battery Management

| Signal | GPIO | Notes |
|--------|------|-------|
| CHG_STATUS | 0 | TP4056 CHRG pin; LOW = charging |
| BATT_ADC | 1 | ADC1_CH0 via 100 KΩ + 100 KΩ voltage divider |

Voltage divider: BAT+ → 100 KΩ → GPIO1 → 100 KΩ → GND.
GPIO1 reads half the battery voltage (0–2.1 V for 0–4.2 V battery).

---

## Power Architecture

```
USB Micro-B
    │
  TP4056 (charge controller)
    ├── CHRG pin → GPIO0 (LOW = charging)
    └── BAT+ → LiPo cell
              │
           3.3V LDO
              │
         3.3V rail ──┬── ESP32-S3 VDD
                     ├── ST7789V VCC
                     ├── PCM5102A VDD
                     ├── microSD VCC
                     ├── nRF24L01+ VCC (+ 100µF decoupling)
                     └── Button pull-ups (internal)

5V external ──── WS2812B VCC (separate supply required)
```

## Estimated Current Draw

| State | Current |
|-------|---------|
| Full active (WiFi TX + audio + LEDs 100% + display 100%) | ~280 mA |
| Normal use (display 80%, LEDs off, no WiFi TX) | ~90 mA |
| Auto-dim idle (60 s, display 20%) | ~45 mA |
| Deep sleep | ~12 µA |

On a 1000 mAh LiPo: ~4 h normal use, >80 h deep-sleep standby.
