#include "twi.h"
#include "twi_internal.h"

#include <stddef.h>

#include <avr/io.h>

// Master transmitter status codes from the datasheet.
#define TWI_MT_START        0x08u
#define TWI_MT_REP_START    0x10u
#define TWI_MT_SLA_W_ACK    0x18u
#define TWI_MT_SLA_W_NACK   0x20u
#define TWI_MT_DATA_ACK     0x28u
#define TWI_MT_DATA_NACK    0x30u
#define TWI_MT_ARB_LOST     0x38u

// Master receiver status codes from the datasheet.
#define TWI_MR_SLA_R_ACK    0x40u
#define TWI_MR_SLA_R_NACK   0x48u
#define TWI_MR_DATA_ACK     0x50u
#define TWI_MR_DATA_NACK    0x58u

// Bus error from an illegal START/STOP condition.
#define TWI_BUS_ERROR       0x00u

// Build the SLA+W / SLA+R byte from a 7-bit device address.
#define TWI_SLA_W(addr) ((uint8_t)((addr) << 1))
#define TWI_SLA_R(addr) ((uint8_t)(((addr) << 1) | 1u))

// Test seam: when compiled with -DUNIT_TEST the TWINT wait goes through a
// function pointer that tests can replace via twi_set_wait_function(). In the
// production AVR build the pointer and setter are not compiled at all, so the
// seam costs zero flash and zero RAM.
#ifdef UNIT_TEST
static twi_wait_function wait_fn = &twi_wait_for_twint;

void twi_set_wait_function(twi_wait_function fn)
{
    wait_fn = (fn != NULL) ? fn : &twi_wait_for_twint;
}
#define TWI_WAIT_FN() (wait_fn())
#else
#define TWI_WAIT_FN() (twi_wait_for_twint())
#endif

uint8_t twi_wait_for_twint(void)
{
#if TWI_TIMEOUT_LOOPS == 0UL
    while (!(TWCR & _BV(TWINT))) {
    }
#else
    uint32_t spins = TWI_TIMEOUT_LOOPS;
    while (!(TWCR & _BV(TWINT))) {
        if (--spins == 0UL) {
            return TWI_STATUS_TIMEOUT_SENTINEL;
        }
    }
#endif
    return (uint8_t)(TWSR & 0xF8u);
}

// Centralised status check: wait for TWINT, map timeouts and errors.
static twi_status wait_and_check(uint8_t expected_status)
{
    uint8_t status = TWI_WAIT_FN();
    if (status == TWI_STATUS_TIMEOUT_SENTINEL) {
        return TWI_ERROR_TIMEOUT;
    }
    if (status == expected_status) {
        return TWI_OK;
    }
    switch (status) {
        case TWI_MT_SLA_W_NACK:
        case TWI_MR_SLA_R_NACK:
            return TWI_ERROR_NACK_ADDRESS;
        case TWI_MT_DATA_NACK:
            return TWI_ERROR_NACK_DATA;
        case TWI_MT_ARB_LOST:
            return TWI_ERROR_ARBITRATION_LOST;
        case TWI_BUS_ERROR:
        default:
            return TWI_ERROR_BUS;
    }
}

// Enable the internal pull-ups on the SDA/SCL pins. Both ATmega16/32 (PC1/PC0)
// and ATmega328P (PC4/PC5) route TWI through port C, so we only need the port
// C mask. Pins stay as inputs — the TWI module drives them when active.
static void twi_enable_pullups(void)
{
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__) || defined(__AVR_ATmega168__) || defined(__AVR_ATmega88__)
    PORTC |= (uint8_t)(_BV(PC4) | _BV(PC5));
#elif defined(__AVR_ATmega16__) || defined(__AVR_ATmega32__) || defined(__AVR_ATmega164P__) || defined(__AVR_ATmega324P__) || defined(__AVR_ATmega644__) || defined(__AVR_ATmega1284__)
    PORTC |= (uint8_t)(_BV(PC0) | _BV(PC1));
#else
    // Unknown part. Skip pull-ups; the user must enable external ones.
#endif
}

void twi_init(void)
{
    twi_enable_pullups();

    // Prescaler = 1, then set the bit rate for the configured SCL frequency.
    TWSR = 0u;
    TWBR = (uint8_t)TWI_COMPUTED_TWBR;

    // Enable the TWI module. No interrupt, no slave ACK — master mode polls.
    TWCR = _BV(TWEN);
}

twi_status twi_start(void)
{
    TWCR = (uint8_t)(_BV(TWINT) | _BV(TWSTA) | _BV(TWEN));
    uint8_t status = TWI_WAIT_FN();
    if (status == TWI_STATUS_TIMEOUT_SENTINEL) {
        return TWI_ERROR_TIMEOUT;
    }
    if (status == TWI_MT_START || status == TWI_MT_REP_START) {
        return TWI_OK;
    }
    if (status == TWI_MT_ARB_LOST) {
        return TWI_ERROR_ARBITRATION_LOST;
    }
    return TWI_ERROR_BUS;
}

void twi_stop(void)
{
    // Master mode only: no interrupt-enable, no slave-ACK. The v1 library set
    // TWIE | TWEA here, which could jump to the reset vector after sei() when
    // no TWI ISR was defined.
    TWCR = (uint8_t)(_BV(TWINT) | _BV(TWEN) | _BV(TWSTO));
    // TWSTO auto-clears when the stop condition reaches the bus; the TWINT
    // flag is not set by STOP, so we spin on TWSTO instead. Bounded by the
    // same timeout as the TWINT waits so a hung bus cannot wedge the caller.
#if TWI_TIMEOUT_LOOPS == 0UL
    while (TWCR & _BV(TWSTO)) {
    }
#else
    uint32_t spins = TWI_TIMEOUT_LOOPS;
    while (TWCR & _BV(TWSTO)) {
        if (--spins == 0UL) {
            break;
        }
    }
#endif
}

twi_status twi_write_byte(uint8_t byte)
{
    TWDR = byte;
    TWCR = (uint8_t)(_BV(TWINT) | _BV(TWEN));
    // The caller knows whether this byte is an address or payload, so the
    // "expected" value for wait_and_check is ambiguous. Resolve both cases
    // here and return the specific error.
    uint8_t status = TWI_WAIT_FN();
    if (status == TWI_STATUS_TIMEOUT_SENTINEL) {
        return TWI_ERROR_TIMEOUT;
    }
    // Accepts both master-transmit (0x18/0x28) and master-receive (0x40)
    // ack codes because twi_read uses this function to send the SLA+R byte.
    switch (status) {
        case TWI_MT_SLA_W_ACK:
        case TWI_MT_DATA_ACK:
        case TWI_MR_SLA_R_ACK:
            return TWI_OK;
        case TWI_MT_SLA_W_NACK:
        case TWI_MR_SLA_R_NACK:
            return TWI_ERROR_NACK_ADDRESS;
        case TWI_MT_DATA_NACK:
            return TWI_ERROR_NACK_DATA;
        case TWI_MT_ARB_LOST:
            return TWI_ERROR_ARBITRATION_LOST;
        default:
            return TWI_ERROR_BUS;
    }
}

twi_status twi_read_byte(bool send_ack, uint8_t *out)
{
    if (out == NULL) {
        return TWI_ERROR_BUS;
    }
    if (send_ack) {
        TWCR = (uint8_t)(_BV(TWINT) | _BV(TWEA) | _BV(TWEN));
    } else {
        TWCR = (uint8_t)(_BV(TWINT) | _BV(TWEN));
    }
    uint8_t expected = send_ack ? TWI_MR_DATA_ACK : TWI_MR_DATA_NACK;
    twi_status rc = wait_and_check(expected);
    if (rc == TWI_OK) {
        *out = TWDR;
    }
    return rc;
}

// Address-phase write: START, SLA+W, register address. Shared by twi_write and
// twi_read. On any failure the caller must still issue a STOP to release the
// bus.
static twi_status twi_address_phase_write(uint8_t device_address, uint8_t register_address)
{
    twi_status rc = twi_start();
    if (rc != TWI_OK) {
        return rc;
    }
    rc = twi_write_byte(TWI_SLA_W(device_address));
    if (rc != TWI_OK) {
        return rc;
    }
    return twi_write_byte(register_address);
}

twi_status twi_write(uint8_t device_address, uint8_t register_address, const uint8_t *data, uint8_t data_length)
{
    if (data_length > 0u && data == NULL) {
        twi_stop();
        return TWI_ERROR_BUS;
    }

    twi_status rc = twi_address_phase_write(device_address, register_address);
    if (rc != TWI_OK) {
        twi_stop();
        return rc;
    }

    // Return immediately on the first NACK so errors are not swallowed by a
    // trailing success assignment (the X3 bug in the v1 library).
    for (uint8_t i = 0; i < data_length; i++) {
        rc = twi_write_byte(data[i]);
        if (rc != TWI_OK) {
            twi_stop();
            return rc;
        }
    }

    twi_stop();
    return TWI_OK;
}

twi_status twi_read(uint8_t device_address, uint8_t register_address, uint8_t *data, uint8_t data_length)
{
    // Zero-length reads are a no-op; the v1 library wrote data[0] in this
    // case, which was undefined behaviour.
    if (data_length == 0u) {
        return TWI_OK;
    }
    if (data == NULL) {
        twi_stop();
        return TWI_ERROR_BUS;
    }

    twi_status rc = twi_address_phase_write(device_address, register_address);
    if (rc != TWI_OK) {
        twi_stop();
        return rc;
    }

    // Repeated START, then SLA+R.
    rc = twi_start();
    if (rc != TWI_OK) {
        twi_stop();
        return rc;
    }
    rc = twi_write_byte(TWI_SLA_R(device_address));
    if (rc != TWI_OK) {
        twi_stop();
        return rc;
    }

    // Read every byte with ACK except the last, which gets NACK so the slave
    // releases the bus for the STOP that follows.
    for (uint8_t i = 0; i < data_length; i++) {
        bool is_last = (i == (uint8_t)(data_length - 1u));
        rc = twi_read_byte(!is_last, &data[i]);
        if (rc != TWI_OK) {
            twi_stop();
            return rc;
        }
    }

    twi_stop();
    return TWI_OK;
}
