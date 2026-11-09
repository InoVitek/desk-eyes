# Desk Eyes 👀
<img width="800" alt="Desk Eyes animated robot eyes" src="https://github.com/user-attachments/assets/5948ff7a-cb59-423e-837a-6fc9476323b5" />

Animated robot eyes for your desk, running on an ESP32-C6 display board in a 3D printed case.
They blink, look around and change moods on their own.

- No soldering
- ~45 min build (most of it is print time 🙂)
- ~$18 in parts

## Build guide, print files & one-click flashing
Everything you need is on Printpal:
👉 **https://printpal.io/projects/slab-eyes**

## Watch the build
https://www.youtube.com/watch?v=fKnaSUIqE-E

## Hardware
- Waveshare ESP32-C6-LCD-1.47 (display only, no touch)
- 3D printed case (files on Printpal)
- USB-C cable

## Flash the code
**Easiest:** one click from your browser on [Printpal](https://printpal.io/projects/slab-eyes), no install needed.

**Arduino IDE**
1. Open `slab-eyes/slab-eyes.ino`
2. Board: **ESP32C6 Dev Module**, USB CDC On Boot: **Enabled**
3. Install the libraries **GFX Library for Arduino** and **Adafruit NeoPixel**
4. Upload

**PlatformIO**
pio run -t upload

## Files
| File | What it is |
|---|---|
| `slab-eyes/slab-eyes.ino` | Main program |
| `slab-eyes/c6_slab_pins.h` | Pin definitions |
| `platformio.ini` | PlatformIO config |

## License
Code: MIT. Print files: CC BY-SA 4.0 (on [Printpal](https://printpal.io/projects/slab-eyes)).

Made by Vitek Kubal ([@inovitek_3d](https://www.instagram.com/inovitek_3d/))
