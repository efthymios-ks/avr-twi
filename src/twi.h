#ifndef TWI_H
#define TWI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "twi_config.h"

// Compile-time TWBR computation. The master-mode SCL period is
//     F_SCL = F_CPU / (16 + 2 * TWBR * prescaler)
// With prescaler fixed at 1 that gives
//     TWBR = (F_CPU / F_SCL - 16) / 2
// Exposed as a macro so tests can verify the boundary values without linking.
#ifdef F_CPU
#define TWI_COMPUTED_TWBR (((F_CPU / TWI_SCL_HZ) - 16UL) / 2UL)

// The ATmega16/32 and ATmega328P datasheets both require TWBR >= 10 in master
// mode. Below that the master cannot guarantee the setup/hold timings.
#if TWI_COMPUTED_TWBR < 10UL
#error "TWI_SCL_HZ too high for F_CPU; raise F_CPU or lower TWI_SCL_HZ (TWBR must be >= 10 in master mode)"
#endif
#endif

// Return codes for every public TWI call. TWI_OK is zero so callers can write
//     if (twi_write(...) != TWI_OK) { ... }
typedef enum {
    TWI_OK = 0,
    TWI_ERROR_NACK_ADDRESS,
    TWI_ERROR_NACK_DATA,
    TWI_ERROR_TIMEOUT,
    TWI_ERROR_BUS,
    TWI_ERROR_ARBITRATION_LOST,
} twi_status;

// Initialise the TWI hardware in master mode. Enables the internal pull-ups on
// the SDA/SCL pins, which are fixed by the hardware (PC1/PC0 on ATmega16/32,
// PC4/PC5 on ATmega328P).
void twi_init(void);

// Low-level primitives. Most callers want twi_write()/twi_read() instead.
twi_status twi_start(void);
void twi_stop(void);
twi_status twi_write_byte(uint8_t byte);
twi_status twi_read_byte(bool send_ack, uint8_t *out);

// High-level register access, matching the layout of most I2C slave chips:
// START, SLA+W, register address, data..., STOP for a write;
// START, SLA+W, register address, repeated-START, SLA+R, data..., NACK, STOP
// for a read. device_address is the 7-bit slave address (not shifted).
twi_status twi_write(uint8_t device_address, uint8_t register_address, const uint8_t *data, uint8_t data_length);
twi_status twi_read(uint8_t device_address, uint8_t register_address, uint8_t *data, uint8_t data_length);

#ifdef __cplusplus
}
#endif

#endif
