// 24LC256 EEPROM round-trip demo. Writes a known pattern to EEPROM address 0,
// reads it back, lights the PB5 LED on success and leaves it dark on failure.
// Default wiring: ATmega328P DIP-28, 24LC256 at I2C address 0x50, SDA on PC4,
// SCL on PC5. See README for the full wiring table.
//
// The 24LC256 uses a 16-bit internal address, so the demo bypasses the
// single-register-byte helpers (twi_write/twi_read) and drives the low-level
// primitives directly. That is the recommended pattern for any slave with a
// multi-byte internal address pointer (24LC512, FM24C64 FRAM, etc.).

#include <avr/io.h>
#include <util/delay.h>

#include "io_macros.h"
#include "twi.h"

#define LED B, 5

// 24LC256 I2C address with all three A0/A1/A2 strap pins tied to GND.
#define EEPROM_ADDRESS 0x50u

// Build the SLA+W / SLA+R bytes from a 7-bit device address.
#define SLA_W(addr) ((uint8_t)((addr) << 1))
#define SLA_R(addr) ((uint8_t)(((addr) << 1) | 1u))

static twi_status eeprom_write(uint16_t address, const uint8_t *data, uint8_t length)
{
    twi_status rc = twi_start();
    if (rc != TWI_OK) { twi_stop(); return rc; }
    rc = twi_write_byte(SLA_W(EEPROM_ADDRESS));
    if (rc != TWI_OK) { twi_stop(); return rc; }
    rc = twi_write_byte((uint8_t)(address >> 8));
    if (rc != TWI_OK) { twi_stop(); return rc; }
    rc = twi_write_byte((uint8_t)(address & 0xFFu));
    if (rc != TWI_OK) { twi_stop(); return rc; }
    for (uint8_t i = 0; i < length; i++) {
        rc = twi_write_byte(data[i]);
        if (rc != TWI_OK) { twi_stop(); return rc; }
    }
    twi_stop();
    return TWI_OK;
}

static twi_status eeprom_read(uint16_t address, uint8_t *out, uint8_t length)
{
    if (length == 0u) { return TWI_OK; }

    // Address-set phase: write the two-byte internal address, no payload.
    twi_status rc = twi_start();
    if (rc != TWI_OK) { twi_stop(); return rc; }
    rc = twi_write_byte(SLA_W(EEPROM_ADDRESS));
    if (rc != TWI_OK) { twi_stop(); return rc; }
    rc = twi_write_byte((uint8_t)(address >> 8));
    if (rc != TWI_OK) { twi_stop(); return rc; }
    rc = twi_write_byte((uint8_t)(address & 0xFFu));
    if (rc != TWI_OK) { twi_stop(); return rc; }

    // Read phase: repeated START, SLA+R, length-1 ACKed bytes, final NACKed.
    rc = twi_start();
    if (rc != TWI_OK) { twi_stop(); return rc; }
    rc = twi_write_byte(SLA_R(EEPROM_ADDRESS));
    if (rc != TWI_OK) { twi_stop(); return rc; }
    for (uint8_t i = 0; i < length; i++) {
        bool is_last = (i == (uint8_t)(length - 1u));
        rc = twi_read_byte(!is_last, &out[i]);
        if (rc != TWI_OK) { twi_stop(); return rc; }
    }
    twi_stop();
    return TWI_OK;
}

int main(void)
{
    IO_MODE(LED, IO_OUTPUT);
    IO_WRITE(LED, IO_LOW);

    twi_init();

    uint8_t pattern[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t readback[4] = { 0, 0, 0, 0 };

    bool ok = (eeprom_write(0x0000u, pattern, (uint8_t)sizeof(pattern)) == TWI_OK);
    // 24LC256 self-timed write cycle is up to 5 ms.
    _delay_ms(10);
    if (ok) {
        ok = (eeprom_read(0x0000u, readback, (uint8_t)sizeof(readback)) == TWI_OK);
    }
    if (ok) {
        for (uint8_t i = 0; i < sizeof(pattern); i++) {
            if (readback[i] != pattern[i]) {
                ok = false;
                break;
            }
        }
    }

    IO_WRITE(LED, ok ? IO_HIGH : IO_LOW);

    while (1) {
    }
}
