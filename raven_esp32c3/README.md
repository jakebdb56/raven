# ESP32-C3 Motion Raven

Ready-to-flash PlatformIO starter project for:

- Tenstar Robot ESP32-C3 Super Mini
- MPU6050
- MAX98357A I2S amp
- 1 W / 8 ohm speaker
- Two red analog LEDs driven together through one transistor
- Audio stored in onboard flash with LittleFS

## Pin map

| Function | GPIO |
|---|---:|
| MPU6050 SDA | 3 |
| MPU6050 SCL | 4 |
| MAX98357A LRC / WS | 5 |
| MAX98357A BCLK | 6 |
| MAX98357A DIN | 7 |
| Both eye LEDs | 10 |

## Power

Use one regulated 5 V supply and tie all grounds together.

- ESP32-C3: 5 V input
- MAX98357A VIN: 5 V
- MPU6050: 3.3 V is preferred for a typical breakout
- Speaker: connect only to SPK+ and SPK- on the MAX98357A

For the eyes, use one 220 ohm resistor per LED and switch both with a transistor. A 2N2222 with a 1k base resistor is fine.

## Audio

Put your audio clip at:

    data/raven.wav

Recommended:
- PCM WAV
- mono
- 16-bit
- 16 kHz or 22.05 kHz

## PlatformIO commands

    pio run
    pio run -t upload
    pio run -t uploadfs
    pio device monitor

Upload the firmware, then upload the filesystem. You only need to re-run `uploadfs` when the audio file changes.

## Motion tuning

In `src/main.cpp`:

    float MOTION_THRESHOLD = 2.2f;

Raise it if the raven triggers too easily. Lower it if you have to move it too hard.

To inspect live motion values, uncomment:

    // Serial.printf("motion=%.3f\n", score);

The firmware requires motion for about 60 ms, plays the effect, then uses a 4-second cooldown to reduce self-triggering from speaker vibration.

## Board target

PlatformIO's `esp32-c3-devkitm-1` target is used as a practical generic ESP32-C3 definition. PlatformIO documents that target as ESP32-C3, 160 MHz, 4 MB flash.
