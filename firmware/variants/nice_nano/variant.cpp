/*
  nice!nano v2 / nRF52840 "Pro Micro" (SuperMini) variant for the Adafruit nRF52 Arduino core.

  Derived from the Adafruit Feather nRF52840 Express variant (LGPL-2.1; see the Adafruit core).
  Pin numbering follows the community nicenano definition (identity map, 48 pins).
*/

#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include "nrf.h"

// Arduino index -> nRF52840 pin number. Identity map: index N is port (N / 32), pin (N % 32).
// P0.00 / P0.01 are the 32 kHz crystal and P0.18 is RESET; they are listed so the indices line
// up, but must not be driven as GPIO.
const uint32_t g_ADigitalPinMap[] = {
    // Port 0
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    // Port 1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
};

void initVariant() {
  // Park the on-board LED off (active low) so boot state is unambiguous.
  pinMode(PIN_LED1, OUTPUT);
  ledOff(PIN_LED1);
}
