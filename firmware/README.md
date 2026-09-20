# phrog Firmware

PlatformIO project for the phrog pH instrument mainboard. The layout mirrors the Phoenix
firmware (`phoenix-instrument/platformio/PoenixV1-Firmware`); shared modules are copied
verbatim so they can be kept in sync.

## Hardware

- nRF52840 Pro Micro / nice!nano module (Adafruit nRF52 Arduino core, UF2 bootloader).
- NAU7802 24-bit I2C ADC for the photodiode signal.
- Thermistor divider read on the nRF52840 internal SAADC.
- Green and blue LEDs driven directly from GPIO.

Pin assignments: `docs/pin-map.md` and `include/device_setup.hpp`.

## Layout

| Path | Contents |
|------|----------|
| `boards/`, `variants/` | Project-local nice!nano board definition and pin variant. |
| `include/device_setup.hpp`, `src/device_setup.cpp` | Pin map, board configs, bring-up sequence. |
| `src/main.cpp` | Production entry point (device setup + CLI). |
| `src/examples/mock_main.cpp` | Mock BLE application for phone-app development (ported). |
| `lib/phoenix_common` | Shared `GUARD` macros and `PHX_*` return codes (ported verbatim). |
| `lib/ph_equations` | Spectrophotometric pH maths (ported verbatim). |
| `lib/phoenix_ble` | BLE server facade + Bluefruit backend (ported verbatim). |
| `lib/mocks` | Mock controller and BLE bridge (ported verbatim). |
| `lib/nau7802` | NAU7802 driver (skeleton). |
| `lib/led_driver` | GPIO LED drive (skeleton). |
| `lib/light_readings` | Dark/green/blue sweep helper (skeleton). |
| `lib/thermistor_reader` | SAADC thermistor measurement (skeleton). |
| `lib/cli` | Serial command interface (skeleton). |
| `lib/phrog_settings` | Persistent settings in internal flash (skeleton). |
| `test/` | Unity suites; one per module. |
| `python/` | Host tooling: mock BLE tester and pytest suite (ported). |
| `docs/` | Style guide, TDD and git workflow, pin map. |

## Building

```powershell
pio run -e main            # production firmware
pio run -e mock_main       # mock BLE app
pio test -e main -vv       # full Unity suite on hardware
pio test -e main -vv -f *nau7802*
```

Run `pio` from this directory. See `docs/contributor-checklist.md` before starting work.

## Status

Repository scaffold only. Skeleton modules validate arguments and cache configuration but
return `PHX_ERR_NOT_IMPLEMENTED` from measurement calls. Implementation follows the TDD
workflow in `.github/TDD.md`.
