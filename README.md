# Music_Spectrum_oled – Real‑Time Audio Spectrum Analyzer for ESP32

![Spectrum Demo](https://via.placeholder.com/800x200?text=Spectrum+Display+on+OLED)

A real‑time, bar‑graph audio spectrum analyzer that uses an **ESP32**, an **I2S digital microphone**, and a **128×64 OLED display**.  
The FFT (Fast Fourier Transform) splits the audio into 8 logarithmic frequency bands, visualised as smooth, responsive bars with falling peak dots.



## Features

- 8 frequency bands spread logarithmically from 200 Hz to 20 kHz
- Fast attack, slow decay for smooth visual response
- Per‑band sensitivity adjustment (low frequencies can be dampened to avoid mic noise)
- DC offset removal and silence detection
- Peaks with falling dots
- Works with any I2S MEMS microphone (e.g., INMP441, SPH0645, ICS‑43434)

## Hardware Requirements

| Component          | Example / Suggested                     |
|--------------------|------------------------------------------|
| Microcontroller    | ESP32 (DevKit, NodeMCU, WROOM, etc.)     |
| I2S Microphone     | INMP441 (or any 3.3V I2S MEMS mic)       |
| OLED Display       | 128×64, SSD1306, I2C                     |
| Breadboard & wires | –                                        |
| Power              | USB (5V) or 3.3V from ESP32              |

> **Important** – The INMP441 is 3.3V only. Do not connect to 5V.  
> Some other I2S microphones might require different pin mapping – check its datasheet.

## Wiring Table

Connect components to the ESP32 as follows:

### I2S Microphone (e.g., INMP441)

| INMP441 Pin | ESP32 Pin       | Notes                            |
|-------------|----------------|----------------------------------|
| VDD         | 3.3V           |                                  |
| GND         | GND            |                                  |
| SD (DOUT)   | GPIO15 (I2S_SD)| Data line                        |
| WS (LRCLK)  | GPIO17 (I2S_WS)| Word select (left/right clock)   |
| SCK (BCLK)  | GPIO18 (I2S_SCK)| Bit clock                        |

**For other I2S microphones** (e.g., SPH0645):  
- SPH0645 uses `data_in_num = I2S_SD` but may need different I2S format (Philips standard works). The code uses `I2S_COMM_FORMAT_STAND_I2S` – compatible with most modern MEMS mics.

### OLED Display (I2C, SSD1306)

| OLED Pin | ESP32 Pin      |
|----------|----------------|
| VCC      | 3.3V (or 5V depending on your OLED) |
| GND      | GND            |
| SCL      | GPIO22 (I2C SCL) |
| SDA      | GPIO21 (I2C SDA) |

> Some OLED boards have built‑in level shifters and accept 5V; check your display’s specification. If unsure, use 3.3V.

## Software & Libraries

Install the following libraries using the Arduino Library Manager (**Sketch → Include Library → Manage Libraries**):

1. **Adafruit GFX Library** – by Adafruit (version ≥ 1.11)
2. **Adafruit SSD1306** – by Adafruit (version ≥ 2.5)
3. **arduinoFFT** – by Enrique Condes (version ≥ 1.5.6)

Also make sure your **ESP32 board package** is installed (Espressif’s `esp32` via Boards Manager).

## Setup Instructions

1. **Connect hardware** as per the wiring table.
2. **Open the `.ino` file** in Arduino IDE (or PlatformIO).
3. **Select board**: Tools → Board → ESP32 Dev Module (or your specific ESP32 variant).
4. **Set port** (Tools → Port) to the COM / ttyUSB where your ESP32 is connected.
5. **Adjust pins** (optional):  
   - If you change I2S pins, modify `I2S_WS`, `I2S_SD`, `I2S_SCK` at the top of the code.  
   - If your OLED uses a different I2C address (e.g., 0x3D), change `OLED_ADDRESS`.
6. **Upload** the sketch.

If everything works, you should see a “Spectrum Ready!” message for 1.5 seconds, then the live audio spectrum.

## How the Code Works (Brief)

- **I2S Sampling**: The ESP32 reads 32‑bit audio samples at 40 kHz (configurable). The microphone data is shifted (`>>8`) to convert to 24‑bit range and then scaled to float in the range [-1, 1].
- **DC Offset Removal**: Running average of the signal removes any constant bias.
- **FFT**: 512‑point real FFT (Hamming window).
- **Band Mapping**: Instead of using fixed FFT bins, the code groups bins into 8 logarithmically spaced frequency bands. Per‑band gain allows boosting or attenuating certain ranges (e.g., suppressing low‑frequency rumble).
- **Smoothing & Peaks**: Fast attack, slower decay for the bar height; peaks fall at a constant speed.
- **Display**: Bars are drawn with `fillRect()`; peaks are horizontal lines.

## Calibration & Tuning

The following parameters (in the `.ino` file) let you adjust the visual response:

| Parameter         | What it does                                 |
|-------------------|----------------------------------------------|
| `bandGain[]`      | Multiplier per band (0 = off, >1 = boost). Currently low bands are damped to avoid microphone noise. |
| `NOISE_FLOOR`     | RMS threshold below which signal is treated as silence (bars fall quickly). |
| `SMOOTHING`       | Decay factor for falling bars (0…1). Lower = faster fall. |
| `ATTACK_SPEED`    | Attack factor for rising bars.               |
| `SILENCE_DECAY`   | How fast bars drop when silence is detected. |

### Suggested fine‑tuning steps

1. **If low frequencies over‑respond** – decrease `bandGain[0]` and `bandGain[1]` (e.g., 0.2, 0.4).
2. **If high frequencies are too weak** – increase `bandGain[5]`, `bandGain[6]`.
3. **If bars are too “jumpy”** – increase `SMOOTHING` (e.g., 0.8) and/or decrease `ATTACK_SPEED` (e.g., 0.2).
4. **If bars never fall to zero** – lower `SILENCE_DECAY` or increase `NOISE_FLOOR`.
5. **If you hear distortion** – reduce microphone gain (not adjustable in code) by moving mic farther from speaker; or adjust `bandGain` overall scaling factor (the `300.0f` inside `mapToBands()`).

## Customisation Ideas

- **Change number of bands** – Modify `NUM_BANDS` and `bandFreqs[]`. Also adjust `BAR_WIDTH` and `BAR_GAP` to fit screen width.
- **Different FFT size** – Change `SAMPLES` (power of two, e.g., 256 or 1024) and adjust `SAMPLING_FREQ` accordingly.
- **Different OLED size** – Modify `SCREEN_WIDTH` and `SCREEN_HEIGHT`. The drawing code will automatically re‑center bars.
- **Add colour** – Use a dual‑colour OLED (e.g., SSD1306 with yellow/blue) – only white is supported, but you can substitute with `SSD1306_BLACK` for effects.

## Troubleshooting

| Problem                     | Likely Fix                                                                   |
|-----------------------------|------------------------------------------------------------------------------|
| No display or I2C error     | Check wiring (SCL/SDA). Try changing `OLED_ADDRESS` to 0x3D.                 |
| No microphone data / FFT is constant | Verify I2S pins: SCK→GPIO18, WS→GPIO17, SD→GPIO15. Ensure microphone is 3.3V powered. |
| Very low bars / no response | Increase `bandGain[]` or the global multiplier (`300.0f`). Check if `NOISE_FLOOR` is too high. |
| Only low frequencies show   | Adjust `bandGain` – lower bands too sensitive? Or your microphone might have a high‑pass filter. |
| Bars constantly “breathing” | Increase `SILENCE_DECAY` or lower `SMOOTHING`.                               |

## License

This project is open‑source under the **MIT License**. You are free to use, modify, and distribute it.

## Acknowledgements

- [arduinoFFT](https://github.com/kosme/arduinoFFT) library
- Adafruit for GFX & SSD1306 drivers
- Espressif for I2S driver support

---
**Happy music visualizing!** 🎵📊
