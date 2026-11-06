# ESP32-CAM Turret

ESP-IDF firmware for an ESP32-CAM turret. The current firmware hosts a Wi-Fi control page and uses its single servo output for base rotation; tilt control is disabled.

## Components

- `app`: initializes NVS and the base servo, then starts the web control. Camera support is optional and disabled by default.
- `servo`: generates 50 Hz servo PWM with the LEDC peripheral and tracks the commanded speed.
- `web_ui`: serves the control page and base API over the ESP32 SoftAP and, when configured, the local Wi-Fi network.
- `cam`: initializes the OV5640 and provides JPEG frames for the MJPEG stream.

## Servo test wiring

- Servo signal: GPIO 14 by default. Change **Turret application > Servo signal GPIO** in `idf.py menuconfig` if needed.
- Servo power: use a regulated supply appropriate for the servo, not the ESP32-CAM 3.3 V output.
- Connect the servo supply ground and ESP32-CAM ground together.
- GPIO 14 is also used by the microSD interface on common ESP32-CAM boards; leave microSD unused or select another exposed output-capable GPIO.

The base output is configured for a continuous-rotation servo: speed commands range from -100% to +100%, with 0% mapped to a nominal neutral pulse of 1500 microseconds. The web controls use a conservative 25% drive command and send neutral when released. Neutral pulse widths vary between servos; if the base creeps while stopped, calibrate the neutral value for the specific servo before use.

## Base control

1. Connect the base servo signal wire to the configured GPIO (GPIO 14 by default), and power the servo from a suitable regulated supply. Connect the servo and ESP32 grounds together.
2. In `idf.py menuconfig`, set **Turret web control > Local Wi-Fi network name** and **Local Wi-Fi password** to your 2.4 GHz network credentials. Leave the SSID empty to use only the ESP32 control network. Credentials belong in the local `sdkconfig`; do not commit them. You can also change **Control Wi-Fi password** from its test default.
3. Build, flash, and open the serial monitor with `idf.py -p COMx flash monitor`.
4. After boot, watch the serial monitor for `Connected to local Wi-Fi; open http://...` and browse to that address from a device on the same network. The ESP32-S3 supports 2.4 GHz Wi-Fi; a 5 GHz-only network will not work.
5. The `Turret-Control` Wi-Fi network remains available as a fallback at `http://192.168.4.1`. Connect directly to it using the configured control password if the ESP32 cannot join the local network.
6. Hold left or right to rotate the base; release to stop. The initial drive commands are -5% left and +10% right to account for possible neutral asymmetry in the continuous-rotation servo. These are speed commands, not target angles; how far the base turns depends on how long the button is held. Adjust the two values in `components/web_ui/index.html` to calibrate your servo. If the browser loses its connection while moving, the firmware stops the servo after 700 ms without a refreshed command. The up/down tilt buttons remain visible but disabled because no tilt servo is connected.

## Camera streaming (optional)

Camera support is disabled by default. To enable it, select **Turret camera > Enable OV5640 camera and MJPEG stream** in `idf.py menuconfig`. When enabled, camera initialization is deferred until the separate stream is requested. The webpage does not start the video stream until you click **Start feed**. The MJPEG stream uses a separate HTTP server on port 81, while the control page and servo API remain on port 80. A camera initialization or stream failure does not stop the control page or servo. The stream accepts one client, validates JPEG frames, and caps output to about 10 frames per second. The camera pin defaults in **Turret camera** match this board pin map: XCLK 15, PCLK 13, VSYNC 6, HREF 7, D0-D7 11/9/8/10/12/18/17/16, SIOD 4, SIOC 5, with PWDN and RESET unused. Change them if your board wiring differs.

To isolate camera capture from the browser and Wi-Fi, select **Turret camera > Run camera-only capture diagnostic** as well as enabling the camera. This diagnostic initializes the camera, attempts three captures, logs each frame's dimensions and byte count on success, and skips servo/Wi-Fi/web-server startup. Disable the diagnostic option afterward to return to normal turret operation.

The control page source is `components/web_ui/index.html`. Its checked-in `index.html.gz` is the gzip-compressed asset embedded in firmware; regenerate it after editing the HTML with `python -c "import gzip,pathlib; p=pathlib.Path('components/web_ui/index.html'); pathlib.Path('components/web_ui/index.html.gz').write_bytes(gzip.compress(p.read_bytes(), compresslevel=9, mtime=0))"`.

This board is marked N16R8, so the firmware needs 16 MB flash and octal PSRAM enabled. `sdkconfig.defaults` contains those defaults for a fresh configuration. For an existing `sdkconfig`, verify **Serial flasher config > Flash size** is 16 MB and **Component config > ESP PSRAM** is enabled in octal mode at 80 MHz, with PSRAM available to the heap. OV5640 JPEG streaming needs PSRAM. The current diagnostic camera settings use VGA JPEG, one frame buffer, and 10 MHz XCLK. The page limits the displayed preview width to 640 CSS pixels; this affects only its on-page display size, not the camera capture resolution or stream data.

After building and flashing, open the control page at the ESP32's LAN address. The page and base API use port 80; the separate MJPEG stream uses port 81. The stream is unauthenticated and available to devices that can reach the ESP32 on the local network; do not expose it to the public internet.

## Build and flash

Run these commands from this project directory in an ESP-IDF v6.0 environment:

```powershell
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the board's serial port. Keep the servo unloaded and clear of mechanical stops during the first test.
