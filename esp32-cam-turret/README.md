# ESP32-CAM Turret

ESP-IDF firmware for an ESP32-CAM pan/tilt turret. The current firmware hosts a Wi-Fi control page and moves the tilt servo from its directional controls. Base servo control is not active yet.

## Components

- `app`: initializes NVS and the tilt servo, then starts the web control.
- `servo`: generates 50 Hz servo PWM with the LEDC peripheral and tracks the commanded angle.
- `web_ui`: serves the control page and tilt API over the ESP32 SoftAP and, when configured, the local Wi-Fi network.
- `cam`: reserved camera component; initialization currently returns `ESP_ERR_NOT_SUPPORTED`.

## Servo test wiring

- Servo signal: GPIO 14 by default. Change **Turret application > Servo signal GPIO** in `idf.py menuconfig` if needed.
- Servo power: use a regulated supply appropriate for the servo, not the ESP32-CAM 3.3 V output.
- Connect the servo supply ground and ESP32-CAM ground together.
- GPIO 14 is also used by the microSD interface on common ESP32-CAM boards; leave microSD unused or select another exposed output-capable GPIO.

The tilt servo accepts whole-number angles from -90 to +90 degrees. The driver maps this range to 500-2500 microsecond pulses. Check the servo datasheet and calibrate these limits before testing: either limit may be a mechanical end stop on your servo.

## Live tilt control

1. Connect the tilt servo signal wire to the configured GPIO (GPIO 14 by default), and power the servo from a suitable regulated supply. Connect the servo and ESP32 grounds together.
2. In `idf.py menuconfig`, set **Turret web control > Local Wi-Fi network name** and **Local Wi-Fi password** to your 2.4 GHz network credentials. Leave the SSID empty to use only the ESP32 control network. Credentials belong in the local `sdkconfig`; do not commit them. You can also change **Control Wi-Fi password** from its test default.
3. Build, flash, and open the serial monitor with `idf.py -p COMx flash monitor`.
4. After boot, watch the serial monitor for `Connected to local Wi-Fi; open http://...` and browse to that address from a device on the same network. The ESP32-S3 supports 2.4 GHz Wi-Fi; a 5 GHz-only network will not work.
5. The `Turret-Control` Wi-Fi network remains available as a fallback at `http://192.168.4.1`. Connect directly to it using the configured control password if the ESP32 cannot join the local network.
6. The page reads the current commanded tilt angle at startup. Press up/down to move the tilt servo in 5-degree steps; successful commands are printed in the serial monitor as `Tilt servo set to ... degrees`.

The left/right base controls are intentionally inactive until a second servo is installed. They do not send hardware commands.

## Build and flash

Run these commands from this project directory in an ESP-IDF v6.0 environment:

```powershell
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the board's serial port. Keep the servo unloaded and clear of mechanical stops during the first test.
