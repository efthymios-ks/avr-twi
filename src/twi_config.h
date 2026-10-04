#ifndef TWI_CONFIG_H
#define TWI_CONFIG_H

// SCL clock frequency in Hz. 100 kHz is standard-mode I2C, 400 kHz is
// fast-mode. The library targets master mode only, so these are the master's
// output rates.
#ifndef TWI_SCL_HZ
#define TWI_SCL_HZ 100000UL
#endif

// Upper bound on the busy-wait loop that polls TWINT. Each iteration is a few
// AVR cycles, so 10000 is around 2.5 ms at 16 MHz. Set to 0 to disable the
// timeout entirely (loop forever, matching avr-libc's TWI examples).
#ifndef TWI_TIMEOUT_LOOPS
#define TWI_TIMEOUT_LOOPS 10000UL
#endif

#endif
