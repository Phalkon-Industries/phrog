#ifndef PIO_UNIT_TESTING  // Allows device_setup.cpp to be shared with tests via test_build_src.

#include "cli.hpp"
#include "device_setup.hpp"
#include <Arduino.h>

#ifndef PHROG_STARTUP_WARMUP_DELAY_MS
#define PHROG_STARTUP_WARMUP_DELAY_MS 0u
#endif

#ifndef PHROG_DEVICE_SETUP_RETRY_DELAY_MS
#define PHROG_DEVICE_SETUP_RETRY_DELAY_MS 500u
#endif

#ifndef PHROG_DEVICE_SETUP_RETRY_LIMIT
#define PHROG_DEVICE_SETUP_RETRY_LIMIT 0u
#endif

void setup() {
  // Step 1: Open the USB serial link before anything can report an error.
  Serial.begin(115200);
  delay(100);  // Short delay to allow serial to set up.

  // Step 2: Configure the instrument before accepting CLI commands.
  uint32_t setup_attempt_count = 0u;
  while (true) {
    const int return_code = device_setup_initialize();
    if (return_code == PHX_OK) {
      break;
    }

    Serial.print("error\tdevice_setup_initialize\t");
    Serial.println(return_code);

    ++setup_attempt_count;
    if ((PHROG_DEVICE_SETUP_RETRY_LIMIT > 0u) && (setup_attempt_count >= PHROG_DEVICE_SETUP_RETRY_LIMIT)) {
      return;
    }

    delay(PHROG_DEVICE_SETUP_RETRY_DELAY_MS);
  }

  // Step 3: Optionally allow LEDs and sensors to warm up before measurements.
  if (PHROG_STARTUP_WARMUP_DELAY_MS > 0u) {
    delay(PHROG_STARTUP_WARMUP_DELAY_MS);
  }

  // Step 4: Bring up the CLI so baseline/sample commands can be received.
  cli_initialize();
}

void loop() {
  // Step 1: Service CLI commands.
  cli_poll();
}

#endif
