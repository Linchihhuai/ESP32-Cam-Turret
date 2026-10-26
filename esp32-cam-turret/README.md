# ESP32-CAM Turret

ESP-IDF firmware for an ESP32-CAM pan/tilt turret. The initial firmware provides a repeating 45-degree servo movement test to verify the board, signal pin, and servo power before camera support is added.

## Components

- `app`: starts the servo test task and owns the test sweep.
- `servo`: generates 50 Hz servo PWM with the LEDC peripheral.
- `cam`: reserved camera component; initialization currently returns `ESP_ERR_NOT_SUPPORTED`.

## Servo test wiring

- Servo signal: GPIO 14 by default. Change **Turret application > Servo signal GPIO** in `idf.py menuconfig` if needed.
- Servo power: use a regulated supply appropriate for the servo, not the ESP32-CAM 3.3 V output.
- Connect the servo supply ground and ESP32-CAM ground together.
- GPIO 14 is also used by the microSD interface on common ESP32-CAM boards; leave microSD unused or select another exposed output-capable GPIO.

The firmware repeatedly moves through 0, 45, 90, 45, and 0 degrees, holding each position for 700 ms. The 45-degree steps move out twice and back twice; which direction is physically right depends on servo mounting. The driver maps 0-180 degrees to 500-2500 microsecond pulses. Check the servo datasheet and calibrate these limits before testing: 0 degrees may be a mechanical end stop on your servo.

## Build and flash

Run these commands from this project directory in an ESP-IDF v6.0 environment:

```powershell
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the board's serial port. Keep the servo unloaded and clear of mechanical stops during the first test.
