# IonOS Hardware Schematic Reference

## Minimum Viable Build

For development/testing, you need:
- ESP32-S3 DevKit (any variant with ≥8MB PSRAM)
- ST7789V 240×320 SPI display module
- PCM5102A DAC module (or breakout)
- 7× WS2812B LEDs (strip or ring)
- 9× momentary tactile buttons
- microSD card breakout
- nRF24L01+ module (optional)
- 3.3V LDO + LiPo battery circuit (optional for bench testing)

## Power Supply

```
LiPo (3.7V) ──── TP4056 charger ──── 3.3V LDO ──── ESP32-S3 VDD
                                   └──────────── ST7789V VDD
                                   └──────────── PCM5102A VDD
                                   └──────────── nRF24L01+ VDD
Battery+ ──── Voltage Divider (R1=100K, R2=100K) ──── GPIO1 (ADC)
                                                   ← 0–2.1V range maps to 0–4.2V bat
TP4056 CHRG ──────────────────────────────────────── GPIO0 (LOW=charging)
WS2812B VDD ──── 5V (separate supply or boost converter)
```

## Display Wiring (ST7789V)

```
ESP32-S3        ST7789V
GPIO11 MOSI ────── SDA/MOSI
GPIO12 SCLK ────── SCL/SCLK
GPIO10 CS   ────── CS
GPIO 9 DC   ────── DC/RS
GPIO 8 RST  ────── RST
GPIO13 BLK  ────── BLK (via 33Ω) ← LEDC PWM backlight
3.3V        ────── VCC
GND         ────── GND
```

## Audio Wiring (PCM5102A)

```
ESP32-S3        PCM5102A
GPIO4  BCK ─────── BCK
GPIO5  LRCK ────── LRCK/WS
GPIO6  DATA ────── DIN
3.3V         ────── VDD, FLT=GND, DEMP=GND, XSMT=3.3V (unmute)
GND          ────── GND
             ────── L/R out → 3.5mm jack or amplifier
```

## Button Wiring (all active LOW)

```
ESP32-S3               3.3V
GPIO14 UP  ──┬── [BTN] ──┤
GPIO15 DN  ──┤           │
GPIO16 LT  ──┤           │  (Internal pull-ups enabled in firmware)
GPIO17 RT  ──┤           │
GPIO18 A   ──┤           │
GPIO19 B   ──┤           │
GPIO20 X   ──┤           │
GPIO21 ST  ──┤           │
GPIO47 MENU─┘           │
All ──────────── GND through button
```

## WS2812B LED Chain

```
3.3V GPIO48 ──── 300Ω ──── DIN (LED 1) ──── DOUT → DIN (LED 2) → ... → DIN (LED 7)
5V ──────────────────────── VDD (all LEDs, shared)
GND ─────────────────────── GND (all LEDs, shared)
Add 100µF cap between 5V and GND near first LED.
```

## SD Card (SPI3)

```
ESP32-S3        SD Card
GPIO35 MOSI ────── DI (CMD)
GPIO37 MISO ────── DO (DAT0)
GPIO36 SCLK ────── CLK
GPIO38 CS   ────── CS (DAT3)
3.3V        ────── VDD
GND         ────── GND, DAT1, DAT2
Add 10K pull-up on MISO line.
```

## nRF24L01+ (Optional, SPI2 shared)

```
ESP32-S3        nRF24L01+
GPIO2  MOSI ────── MOSI
GPIO3  MISO ────── MISO
GPIO7  SCLK ────── SCK
GPIO39 CS   ────── CSN
GPIO40 CE   ────── CE
GPIO41 IRQ  ────── IRQ
3.3V        ────── VCC (use 100µF + 100nF bypass caps!)
GND         ────── GND
Note: nRF24L01+ is sensitive to power supply noise.
Decoupling caps are essential.
```
