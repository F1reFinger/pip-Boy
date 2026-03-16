

# SPI Host Driver Example

(See the README.md file in the upper level 'examples' directory for more information about examples.)

This example aims to show how to use SPI Host driver API, like `spi_transaction_t` and spi_device_queue.

If you are looking for code to drive LCDs in general, rather than code that uses the SPI master, that may be a better example to look at as it uses ESP-IDFs built-in LCD support rather than doing all the low-level work itself, which can be found at `examples/peripherals/lcd/tjpgd/`

## How to Use Example

### Hardware Required

* An ESP32S3 if use another model please adapt on code.
* st7789v lcd screen 2" uses SPI(this model uses some bgr instead of rgb pay attention to that)
* rotary encoder module ky-040

**Connection** :

Depends on boards. The GPIO number used by this example can be changed in `spi_master_example_main.c` No wiring is required on ESP32S3

Especially, please pay attention to the level used to turn on the LCD backlight, some LCD module needs a low level to turn it on, while others take a high level. You can change the backlight level macro LCD_BK_LIGHT_ON_LEVEL in `spi_master_example_main.c`.

### Build and Flash

Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.
At the meantime `ESP32` will be displayed on the connected LCD screen.

## Troubleshooting

For any technical queries, please open an [issue] (https://github.com/espressif/esp-idf/issues) on GitHub. We will get back to you soon.
