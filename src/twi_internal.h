#ifndef TWI_INTERNAL_H
#define TWI_INTERNAL_H

// Internal API. Not for application code — subject to change without notice.
// Included only by src/twi.c and tests/test_twi.c.

#include <stdint.h>

#include "twi.h"

// Sentinel status returned by the TWINT-wait helper when the poll loop hits
// TWI_TIMEOUT_LOOPS without seeing TWINT go high. 0xFF is not a valid TWSR
// status code on any AVR device, so it never collides with real traffic.
#define TWI_STATUS_TIMEOUT_SENTINEL 0xFFu

// Default TWINT-wait implementation that polls the real hardware register.
uint8_t twi_wait_for_twint(void);

// Install an alternative wait function. Tests use this to script status codes
// without a real TWI peripheral; application code never calls it. Passing NULL
// restores the default hardware-polling implementation.
typedef uint8_t (*twi_wait_function)(void);
void twi_set_wait_function(twi_wait_function fn);

#endif
