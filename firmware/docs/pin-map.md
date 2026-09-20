# phrog Pin Map

The phrog mainboard uses a no-name nRF52840 "Pro Micro" clone on the nice!nano footprint. Firmware
pin macros are defined in `include/device_setup.hpp`; the board variant in `variants/nice_nano/`
uses an identity map, so Arduino pin `N` is nRF GPIO `N` (`P1.13` is `32 + 13 = 45`).

Source of truth for the routing is the KiCad project in `phrog-mainboard/`. The table below was
checked against the PCB netlist.

## Header pins used by phrog

| Function                 | nRF pin | Arduino pin | Macro                        |
|--------------------------|---------|-------------|------------------------------|
| Thermistor signal (SAADC)| P0.29   | 29          | `PIN_THERMISTOR_SIGNAL`      |
| Thermistor drive         | P0.02   | 2           | `PIN_THERMISTOR_DRIVE`       |
| Green LED drive          | P1.13   | 45          | `PIN_LED_GREEN_DRIVE`        |
| Blue LED drive           | P1.11   | 43          | `PIN_LED_BLUE_DRIVE`         |
| NAU7802 DRDY             | P1.00   | 32          | (not yet defined)            |
| I2C SCL (NAU7802)        | P1.04   | 36          | `PIN_WIRE_SCL` (variant)     |
| I2C SDA (NAU7802)        | P1.06   | 38          | `PIN_WIRE_SDA` (variant)     |

The thermistor divider is drive pin -> 10 k series resistor -> signal pin -> thermistor -> GND.
Each LED is an NPN current sink (10 k / 5.6 k base divider, 27 R emitter), GPIO active high.
The NAU7802 has blue on channel 1 (VIN1P) and green on channel 2 (VIN2P), both single-ended to
ground, with REFP tied to the internal LDO output.

## Module-internal pins

These are not on the header, and phrog does not currently use them. Their behaviour depends on
the board implementation, so `test/test_board_power` reports what it observes without failing
on the outcome. The values below are what our no-name clone did on the bench.

| Function                 | nRF pin | Arduino pin | Macro                | Observed on our clone |
|--------------------------|---------|-------------|----------------------|--------------------|
| Battery sense (SAADC)    | P0.04   | 4           | `PIN_VBAT` (variant) | ~150 mV at the pin with no battery attached; divider ratio not yet confirmed. |
| Switched 3.3 V cutoff    | P0.13   | 13          | `PIN_EXT_VCC_CUTOFF` (variant) | LOW switches off the module's 3.3 V output, which powers the NAU7802 and I2C pull-ups. HIGH or floating keeps it on. The nice!nano docs claim the opposite polarity. Leave it floating. |
| On-board LED (active low)| P0.15   | 15          | `LED_BUILTIN` (variant) | Blinks in `test_board_hello`. |

## Board definition

PlatformIO does not ship a nice!nano / Pro Micro board, so the project carries its own:

- `boards/nice_nano.json` — Adafruit nRF52 core, S140 v6.1.1 SoftDevice, Adafruit UF2 bootloader
  (same as the nice!nano ships with).
- `variants/nice_nano/variant.{h,cpp}` — pin map and peripheral defaults.

Both are selected from `platformio.ini` via `boards_dir`, `board = nice_nano`, and
`board_build.variants_dir`. The layout follows the community definition at
<https://github.com/ICantMakeThings/Nicenano-NRF52-Supermini-PlatformIO-Support>, kept inside
the repo instead of the global `.platformio` folder so every checkout builds the same way.
