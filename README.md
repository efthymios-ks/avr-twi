# avr-twi

Polled-mode I2C (TWI) master driver for AVR microcontrollers.

[![ci](https://github.com/efthymios-ks/avr-twi/actions/workflows/ci.yml/badge.svg)](https://github.com/efthymios-ks/avr-twi/actions/workflows/ci.yml)

## Features

- Master mode only, polled (no ISR, no `sei()` needed).
- Status-return API: every call returns `twi_status`,  
  no global error getter.
- Compile-time TWBR computation with a datasheet-minimum guard (`TWBR >= 10`).
- Bounded TWINT and TWSTO waits — a hung bus returns `TWI_ERROR_TIMEOUT` instead of locking the firmware.
- NACKs on address and data are reported distinctly (`TWI_ERROR_NACK_ADDRESS` vs `TWI_ERROR_NACK_DATA`).
- Zero-length read is a safe no-op (v1 wrote `data[0]`).
- Internal pull-ups enabled on the SDA/SCL pins so breadboard prototypes do not need external resistors.  
  External 2.2-10 kOhm pull-ups are still recommended for anything serious.
- Host-side Unity tests with scripted TWINT status codes — the full happy and sad paths run on CI without a real AVR.

## Supported MCUs and toolchain

Verified on ATmega328P (default demo, DIP-28, SDA/SCL on PC4/PC5) and ATmega32 (DIP-40, SDA/SCL on PC1/PC0).  
Should work on any AVR with the datasheet-standard TWI module.  
Toolchain is avr-gcc with `-std=gnu99`.

## Wiring (default demo, ATmega328P DIP-28, 24LC256 EEPROM)

| Signal | Device pin             | AVR pin | DIP-28 | Config                      | Description |
|--------|------------------------|---------|--------|-----------------------------|-------------|
| SDA    | 24LC256 pin 5          | PC4     | 27     | hardware-fixed              | I2C data; 4.7 kOhm pull-up to +5 V |
| SCL    | 24LC256 pin 6          | PC5     | 28     | hardware-fixed              | I2C clock; 4.7 kOhm pull-up to +5 V |
| VCC    | 24LC256 pin 8          | +5 V    | —      | —                           | Supply |
| GND    | 24LC256 pin 4          | GND     | 8      | —                           | Ground |
| A0..A2 | 24LC256 pins 1, 2, 3   | GND     | —      | —                           | Address straps all low → 7-bit addr `0x50` |
| WP     | 24LC256 pin 7          | GND     | —      | —                           | Write-protect disabled |
| LED    | LED anode (via 330 Ohm)| PB5     | 19     | `#define LED B, 5` (demo)   | Lights on PASS |
| GND    | LED cathode            | GND     | 22     | —                           | LED return |

For ATmega32,  
SDA is PC1 (pin 23) and SCL is PC0 (pin 22);  
everything else is unchanged.

## Quick start

```c
#include "twi.h"

int main(void)
{
    twi_init();

    uint8_t device_address = 0x50;
    uint8_t register_address = 0x00;
    uint8_t buffer[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    twi_status status = twi_write(device_address, register_address, buffer, sizeof(buffer));
    if (status != TWI_OK) {
        // Handle the error.
    }

    while (1) {
    }
}
```

The 7-bit device address is passed unshifted.  
`twi_write` and `twi_read` build the `SLA+W` / `SLA+R` bytes themselves.

## API

### twi_init
- `void twi_init(void)`
- Does: enables the TWI peripheral in master mode,  
  sets the bit rate from `F_CPU` and `TWI_SCL_HZ`,  
  and turns on the SDA/SCL internal pull-ups.
- Notes: SDA/SCL pins are hardware-fixed per MCU (PC1/PC0 on ATmega16/32, PC4/PC5 on ATmega328P).

### twi_start
- `twi_status twi_start(void)`
- Does: transmits a START (or repeated START) condition on the bus.
- Returns: `TWI_OK` on success, otherwise a `TWI_ERROR_*` status.

### twi_stop
- `void twi_stop(void)`
- Does: transmits a STOP condition and releases the bus.
- Notes: the TWSTO clear-wait is bounded by `TWI_TIMEOUT_LOOPS` to avoid hanging on a wedged bus.

### twi_write_byte
- `twi_status twi_write_byte(uint8_t byte)`
- Does: shifts one byte out on the bus.
- Returns: `TWI_OK`, `TWI_ERROR_NACK_ADDRESS` on a NACKed SLA+W, `TWI_ERROR_NACK_DATA` on a NACKed payload byte, or another `TWI_ERROR_*` status.

### twi_read_byte
- `twi_status twi_read_byte(bool send_ack, uint8_t *out)`
- Does: shifts one byte in from the bus.
- Params: `send_ack` is `true` for every byte except the last byte of a transfer.
- Returns: `TWI_OK` on success, otherwise a `TWI_ERROR_*` status.

### twi_write
- `twi_status twi_write(uint8_t device_address, uint8_t register_address, const uint8_t *data, uint8_t data_length)`
- Does: performs START, SLA+W, register address, `data_length` payload bytes, STOP.
- Params: `device_address` is the 7-bit slave address (not shifted).
- Returns: `TWI_OK` on success, otherwise a `TWI_ERROR_*` status.

### twi_read
- `twi_status twi_read(uint8_t device_address, uint8_t register_address, uint8_t *data, uint8_t data_length)`
- Does: performs START, SLA+W, register address, repeated START, SLA+R, `data_length` bytes (ACK/ACK/.../NACK), STOP.
- Params: `device_address` is the 7-bit slave address (not shifted).
- Returns: `TWI_OK` on success (including when `data_length == 0`, which touches nothing), otherwise a `TWI_ERROR_*` status.

### twi_status

Return code enumeration shared by every public call.  
`TWI_OK` is zero so callers can write `if (twi_write(...) != TWI_OK) { ... }`.

- `TWI_OK` — success.
- `TWI_ERROR_NACK_ADDRESS` — slave did not acknowledge SLA+W or SLA+R (wrong address, no device present).
- `TWI_ERROR_NACK_DATA` — slave NACKed a payload byte (buffer full, write-protect, etc.).
- `TWI_ERROR_TIMEOUT` — TWINT or TWSTO did not clear within `TWI_TIMEOUT_LOOPS` iterations (SCL stuck, bus wedged).
- `TWI_ERROR_BUS` — illegal START/STOP, null pointer in a non-zero-length call, or unexpected status code.
- `TWI_ERROR_ARBITRATION_LOST` — lost the bus to another master.  
  Multi-master is not supported, but the status is surfaced for completeness.

## Configuration

### TWI_SCL_HZ
- `#define TWI_SCL_HZ 100000UL`
- Does: sets the master SCL frequency in Hz.
- Notes: 100 kHz is standard-mode I2C,  
  400 kHz is fast-mode and works on short,  
  well-pulled-up buses.  
  Set `-DTWI_SCL_HZ=400000` on the compiler command line (or edit the header) to change the bit rate.  
  The driver asserts at compile time that the resulting `TWBR` is at least 10,  
  as required by the ATmega datasheets.

### TWI_TIMEOUT_LOOPS
- `#define TWI_TIMEOUT_LOOPS 10000UL`
- Does: upper bound on the busy-wait loop that polls TWINT and TWSTO.
- Notes: each iteration is a few AVR cycles,  
  so 10000 is around 2.5 ms at 16 MHz.  
  Set to `0` to disable the timeout entirely (loop forever, matching avr-libc's TWI examples).

## Memory usage

Flash and RAM sizes for the demo are produced by `Build.ps1` and written to `build/size.txt`.  
Before/after comparison on ATmega32 (v1's historical target),  

| Build | Flash / RAM |
|-------|------------:|
| v1 (ATmega32, -Os) | did not build |
| v2 (ATmega32, -Os) | 774 B / 0 B |

## Build, test, simulate

```powershell
.\Build.ps1
.\Build.ps1 -AllMcus -DebugBuild
.\Build.ps1 -Test
.\Build.ps1 -Clean
.\Simulate.ps1          # launches SimulIDE
.\Simulate.ps1 -NoLaunch # smoke-test without GUI
```

`Build.ps1` and `Simulate.ps1` install the AVR toolchain,  
host gcc for `-Test`,  
and SimulIDE on first run,  
into a shared per-user cache folder — no admin rights,  
no system-wide `PATH` changes.  
Add `-RemoveTools` to uninstall what the script installed when the run ends.  
Pass `-NoInstall` to fail loudly instead of installing.

## Limitations

- Master mode only. Slave mode (`TWI_SetAddress`) was removed in v2.0.0.
- Single-master only.  
  `TWI_ERROR_ARBITRATION_LOST` is reported but the driver does not retry.
- No DMA/interrupt backend. The driver spins on TWINT.
- `twi_write` / `twi_read` take an 8-bit length,  
  so transfers are capped at 255 bytes per call.  
  Loop at the application layer for larger blocks.
- The register-address byte is 8-bit.  
  Devices with 16-bit internal addresses (24LC256, 24LC512, large FRAMs) need two calls or a manual `twi_start` / `twi_write_byte` sequence — the demo shows the pattern.

## Changelog and license

See [CHANGELOG.md](CHANGELOG.md).  
MIT — see [LICENSE](LICENSE).
