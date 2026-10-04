// Host-side unit tests for twi.c. We cannot drive real TWI hardware here, but
// we can test the loop logic (NACK propagation, zero-length guards, STOP on
// error) and the TWBR compile-time computation.
//
// The library exposes a function-pointer seam (twi_set_wait_function) so this
// test installs a scripted TWINT-wait stub that returns pre-defined status
// codes in order. Combined with the fake TWBR/TWCR/TWSR/TWDR registers in
// tests/fake_avr/avr/io.h, that covers every branch of the public API without
// needing a real AVR peripheral.

// Pin F_CPU so twi.h's compile-time TWBR check succeeds. The Build.ps1 host
// test runner does not set -DF_CPU — the test file is responsible.
#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include "unity.h"
#include "avr/io.h"
#include "twi.h"
#include "twi_internal.h"

void setUp(void) {}
void tearDown(void) {}


#include <stdint.h>
#include <string.h>

#define SCRIPT_MAX 32
static uint8_t script[SCRIPT_MAX];
static uint8_t script_length;
static uint8_t script_index;
static uint8_t next_twdr_values[SCRIPT_MAX];
static bool next_twdr_set[SCRIPT_MAX];

static uint8_t stub_wait_for_twint(void)
{
    if (script_index >= script_length) {
        // Script ran dry — treat as timeout to make the failure obvious.
        return TWI_STATUS_TIMEOUT_SENTINEL;
    }
    uint8_t idx = script_index++;
    uint8_t status = script[idx];
    TWSR = status;
    if (next_twdr_set[idx]) {
        TWDR = next_twdr_values[idx];
    }
    return status;
}

static void script_reset(void)
{
    memset(script, 0, sizeof(script));
    memset(next_twdr_values, 0, sizeof(next_twdr_values));
    memset(next_twdr_set, 0, sizeof(next_twdr_set));
    script_length = 0;
    script_index = 0;
}

static void script_push(uint8_t status)
{
    if (script_length < SCRIPT_MAX) {
        script[script_length++] = status;
    }
}

static void script_push_with_data(uint8_t status, uint8_t data)
{
    if (script_length < SCRIPT_MAX) {
        next_twdr_values[script_length] = data;
        next_twdr_set[script_length] = true;
        script[script_length++] = status;
    }
}

static void setup(void)
{
    fake_io_reset();
    script_reset();
    twi_set_wait_function(stub_wait_for_twint);
}

// twi_init sets TWBR to the computed value, clears the TWSR prescaler bits,
// and enables the TWI module without raising TWIE or TWEA.
static void twi_init_should_configure_bitrate_and_enable_module(void)
{
    setup();
    twi_init();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)TWI_COMPUTED_TWBR, TWBR);
    TEST_ASSERT_EQUAL_UINT8(0u, TWSR);
    TEST_ASSERT_TRUE((TWCR & _BV(TWEN)) != 0u);
    TEST_ASSERT_FALSE((TWCR & _BV(TWIE)) != 0u);
    TEST_ASSERT_FALSE((TWCR & _BV(TWEA)) != 0u);
}

// twi_write must return the first failure in the data loop. The v1 library's
// X3 bug used `break` to exit the inner for, then reassigned status to OK, so
// in-loop NACKs were silently swallowed.
static void twi_write_should_return_first_data_nack(void)
{
    setup();
    uint8_t data[3] = { 0x11, 0x22, 0x33 };
    script_push(0x08); // start
    script_push(0x18); // sla+w ack
    script_push(0x28); // register byte ack
    script_push(0x28); // data[0] ack
    script_push(0x30); // data[1] NACK
    twi_status rc = twi_write(0x50, 0x00, data, 3);
    TEST_ASSERT_EQUAL_INT(TWI_ERROR_NACK_DATA, rc);
}

// Missing device: SLA+W NACKed → address error (v1 bug X4 accepted it as OK).
static void twi_write_should_return_address_nack_when_sla_w_fails(void)
{
    setup();
    uint8_t data[1] = { 0x55 };
    script_push(0x08); // start
    script_push(0x20); // sla+w NACK
    twi_status rc = twi_write(0x50, 0x00, data, 1);
    TEST_ASSERT_EQUAL_INT(TWI_ERROR_NACK_ADDRESS, rc);
}

// Zero-length read must not touch the buffer (v1 bug: wrote data[0]).
static void twi_read_with_zero_length_should_be_noop(void)
{
    setup();
    uint8_t sentinel = 0xA5;
    twi_status rc = twi_read(0x50, 0x00, &sentinel, 0);
    TEST_ASSERT_EQUAL_INT(TWI_OK, rc);
    TEST_ASSERT_EQUAL_UINT8(0xA5, sentinel);
    TEST_ASSERT_EQUAL_UINT8(0u, script_index);
}

// Happy-path read: START, SLA+W ack, register ack, repeated START, SLA+R ack,
// two ACKed bytes, final NACKed byte. The stub seeds TWDR with the byte the
// slave would have placed on the bus.
static void twi_read_should_round_trip_three_bytes(void)
{
    setup();
    uint8_t buf[3] = { 0, 0, 0 };
    script_push(0x08);                      // start
    script_push(0x18);                      // sla+w ack
    script_push(0x28);                      // register ack
    script_push(0x10);                      // repeated start
    script_push(0x40);                      // sla+r ack
    script_push_with_data(0x50, 0xAA);      // data[0] ack
    script_push_with_data(0x50, 0xBB);      // data[1] ack
    script_push_with_data(0x58, 0xCC);      // data[2] nack (final)
    twi_status rc = twi_read(0x50, 0x00, buf, 3);
    TEST_ASSERT_EQUAL_INT(TWI_OK, rc);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, buf[2]);
}

// Timeout on the START condition propagates as TWI_ERROR_TIMEOUT.
static void twi_start_should_return_timeout_when_twint_never_fires(void)
{
    setup();
    script_push(TWI_STATUS_TIMEOUT_SENTINEL);
    twi_status rc = twi_start();
    TEST_ASSERT_EQUAL_INT(TWI_ERROR_TIMEOUT, rc);
}

// Bus error (status 0x00) maps to TWI_ERROR_BUS.
static void twi_start_should_return_bus_error_on_illegal_condition(void)
{
    setup();
    script_push(0x00);
    twi_status rc = twi_start();
    TEST_ASSERT_EQUAL_INT(TWI_ERROR_BUS, rc);
}

// Arbitration lost during SLA+W transmission.
static void twi_write_byte_should_report_arbitration_lost(void)
{
    setup();
    script_push(0x38); // arbitration lost
    twi_status rc = twi_write_byte(0xA0);
    TEST_ASSERT_EQUAL_INT(TWI_ERROR_ARBITRATION_LOST, rc);
}

// Compile-time TWBR formula sanity check. At F_CPU=8 MHz, TWI_SCL_HZ=100 kHz
// the formula gives TWBR = (80 - 16) / 2 = 32, which is above the datasheet
// minimum of 10. If TWI_SCL_HZ were 400 kHz the guard in twi.h would have
// failed the build, so reaching this test means the guard allowed the config.
static void twi_computed_twbr_should_match_formula(void)
{
    unsigned long expected = ((F_CPU / TWI_SCL_HZ) - 16UL) / 2UL;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)expected, (uint32_t)TWI_COMPUTED_TWBR);
    TEST_ASSERT_TRUE((uint32_t)TWI_COMPUTED_TWBR >= 10u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(twi_init_should_configure_bitrate_and_enable_module);
    RUN_TEST(twi_write_should_return_first_data_nack);
    RUN_TEST(twi_write_should_return_address_nack_when_sla_w_fails);
    RUN_TEST(twi_read_with_zero_length_should_be_noop);
    RUN_TEST(twi_read_should_round_trip_three_bytes);
    RUN_TEST(twi_start_should_return_timeout_when_twint_never_fires);
    RUN_TEST(twi_start_should_return_bus_error_on_illegal_condition);
    RUN_TEST(twi_write_byte_should_report_arbitration_lost);
    RUN_TEST(twi_computed_twbr_should_match_formula);
    return UNITY_END();
}
