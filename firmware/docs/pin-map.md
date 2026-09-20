# phrog Pin Map

The phrog mainboard uses an nRF52840 Pro Micro / nice!nano module. Firmware pin macros are
defined in `include/device_setup.hpp`; the board variant in `variants/nice_nano/` uses an
identity map, so Arduino pin `N` is nRF GPIO `N` (`P1.13` is `32 + 13 = 45`).

| Function                 | nRF pin | Arduino pin | Macro                        |
|--------------------------|---------|-------------|------------------------------|
| Thermistor signal (SAADC)| P0.29   | 29          | `PIN_THERMISTOR_SIGNAL`      |
| Thermistor drive         | P0.02   | 2           | `PIN_THERMISTOR_DRIVE`       |
| Green LED drive          | P1.13   | 45          | `PIN_LED_GREEN_DRIVE`        |
| Blue LED drive           | P1.11   | 43          | `PIN_LED_BLUE_DRIVE`         |
| I2C SCL (NAU7802)        | P1.04   | 36          | `PIN_WIRE_SCL` (variant)     |
| I2C SDA (NAU7802)        | P1.06   | 38          | `PIN_WIRE_SDA` (variant)     |
| On-board LED (active low)| P0.15   | 15          | `LED_BUILTIN` (variant)      |

Source of truth for the routing is the KiCad project in `phrog-mainboard/`; `firmware/pins.txt`
is the original hand note.

## Board definition

PlatformIO does not ship a nice!nano / Pro Micro board, so the project carries its own:

- `boards/nice_nano.json` — Adafruit nRF52 core, S140 v6.1.1 SoftDevice, Adafruit UF2 bootloader
  (same as the nice!nano ships with).
- `variants/nice_nano/variant.{h,cpp}` — pin map and peripheral defaults.

Both are selected from `platformio.ini` via `boards_dir`, `board = nice_nano`, and
`board_build.variants_dir`. The layout follows the community definition at
<https://github.com/ICantMakeThings/Nicenano-NRF52-Supermini-PlatformIO-Support>, kept inside
the repo instead of the global `.platformio` folder so every checkout builds the same way.
