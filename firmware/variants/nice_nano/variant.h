/*
  nice!nano v2 / nRF52840 "Pro Micro" (SuperMini) variant for the Adafruit nRF52 Arduino core.

  Pin numbering is an identity map: Arduino pin N is nRF52840 GPIO N, where N = port * 32 + pin.
  So P0.29 is pin 29 and P1.13 is pin 32 + 13 = 45. This mirrors the community definition at
  https://github.com/ICantMakeThings/Nicenano-NRF52-Supermini-PlatformIO-Support and keeps the
  firmware pin macros readable against the schematic's Pxx.yy labels.

  Derived from the Adafruit Feather nRF52840 Express variant (LGPL-2.1; see the Adafruit core).
*/

#ifndef _VARIANT_NICE_NANO_
#define _VARIANT_NICE_NANO_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO  // nice!nano and SuperMini clones fit a 32.768 kHz crystal.

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Number of pins defined in the g_ADigitalPinMap array (see variant.cpp).
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (8)  // P0.02..P0.05 and P0.28..P0.31 (AIN0..AIN7).
#define NUM_ANALOG_OUTPUTS (0)

// Port 0 GPIO (Arduino pin == nRF pin).
#define PIN_P0_02 (2)
#define PIN_P0_03 (3)
#define PIN_P0_04 (4)
#define PIN_P0_05 (5)
#define PIN_P0_06 (6)
#define PIN_P0_07 (7)
#define PIN_P0_08 (8)
#define PIN_P0_09 (9)
#define PIN_P0_10 (10)
#define PIN_P0_11 (11)
#define PIN_P0_12 (12)
#define PIN_P0_13 (13)
#define PIN_P0_14 (14)
#define PIN_P0_15 (15)
#define PIN_P0_16 (16)
#define PIN_P0_17 (17)
#define PIN_P0_18 (18)
#define PIN_P0_19 (19)
#define PIN_P0_20 (20)
#define PIN_P0_21 (21)
#define PIN_P0_22 (22)
#define PIN_P0_23 (23)
#define PIN_P0_24 (24)
#define PIN_P0_25 (25)
#define PIN_P0_26 (26)
#define PIN_P0_27 (27)
#define PIN_P0_28 (28)
#define PIN_P0_29 (29)
#define PIN_P0_30 (30)
#define PIN_P0_31 (31)

// Port 1 GPIO (Arduino pin == 32 + nRF pin).
#define PIN_P1_00 (32)
#define PIN_P1_01 (33)
#define PIN_P1_02 (34)
#define PIN_P1_03 (35)
#define PIN_P1_04 (36)
#define PIN_P1_05 (37)
#define PIN_P1_06 (38)
#define PIN_P1_07 (39)
#define PIN_P1_08 (40)
#define PIN_P1_09 (41)
#define PIN_P1_10 (42)
#define PIN_P1_11 (43)
#define PIN_P1_12 (44)
#define PIN_P1_13 (45)
#define PIN_P1_14 (46)
#define PIN_P1_15 (47)

// LEDs: the single on-board blue LED sits on P0.15 and is active low on the nice!nano.
// Bluefruit requires LED_BLUE / LED_CONN to exist even when unused.
#define PIN_LED1 PIN_P0_15
#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED1
#define LED_RED PIN_LED1
#define LED_BLUE PIN_LED1
#define LED_STATE_ON 0  // State when LED is lit (active low).

/*
 * Analog pins (SAADC inputs AIN0..AIN7).
 */
#define PIN_A0 PIN_P0_02
#define PIN_A1 PIN_P0_03
#define PIN_A2 PIN_P0_04
#define PIN_A3 PIN_P0_05
#define PIN_A4 PIN_P0_28
#define PIN_A5 PIN_P0_29
#define PIN_A6 PIN_P0_30
#define PIN_A7 PIN_P0_31

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

// Battery sense and switched-rail control. These are internal to the module (not on the header).
// Their behaviour depends on the board implementation: the nice!nano documentation, the community
// variant, and the no-name clone phrog uses all disagree. phrog does not currently use either pin;
// test/test_board_power reports what a given board does so nobody has to trust the documentation.
#define PIN_VBAT PIN_P0_04  // Battery divider sense (AIN2). Read ~150 mV with no battery on our clone.
// On our clone, driving P0.13 LOW switches off the module's 3.3 V output pin, which on phrog powers
// the NAU7802 and the I2C pull-ups; HIGH or floating (the power-on state) keeps the rail up. The
// nice!nano documentation claims the opposite polarity. Leave the pin floating.
#define PIN_EXT_VCC_CUTOFF PIN_P0_13

/*
 * Serial interfaces (Pro Micro RX/TX pads).
 */
#define PIN_SERIAL1_RX PIN_P0_08
#define PIN_SERIAL1_TX PIN_P0_06

/*
 * SPI Interfaces. The core requires a definition; phrog does not use SPI, so these sit on
 * otherwise-unused pads.
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO PIN_P1_15
#define PIN_SPI_MOSI PIN_P1_02
#define PIN_SPI_SCK PIN_P1_01

static const uint8_t SS   = PIN_P1_07;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;

/*
 * Wire Interfaces. phrog routes the NAU7802 on P1.04 (SCL) / P1.06 (SDA) per docs/pin-map.md.
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA PIN_P1_06
#define PIN_WIRE_SCL PIN_P1_04

// No QSPI flash on the module; leave EXTERNAL_FLASH_* undefined so the core skips it.

#ifdef __cplusplus
}
#endif

#endif  // _VARIANT_NICE_NANO_
