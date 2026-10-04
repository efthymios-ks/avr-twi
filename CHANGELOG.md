# Changelog

## v2.0.0 - 2026-10-04

Complete rewrite.  
**Every public symbol is renamed** — this release is a hard break with v1.  
Portfolio project,  
no backward-compatibility shims.

### Renames

| v1                                                        | v2                                                                            |
|-----------------------------------------------------------|-------------------------------------------------------------------------------|
| `TWI_Setup()`                                             | `twi_init()`                                                                  |
| `TWI_BeginTransmission()` / `TWI_EndTransmission()`       | `twi_start()` / `twi_stop()`                                                  |
| `TWI_Transmit()`, `TWI_ReceiveACK()`, `TWI_ReceiveNACK()` | `twi_write_byte()`, `twi_read_byte(bool send_ack, uint8_t *out)`              |
| `TWI_PacketTransmit()` / `TWI_PacketReceive()`            | `twi_write(dev, reg, data, len)` / `twi_read(dev, reg, data, len)` -> `twi_status` |
| `TWI_SetAddress()`                                        | REMOVED (slave mode not supported in v2)                                      |
| `TWI_Status()` getter                                     | REMOVED (every call returns `twi_status` directly)                            |
| `TWI_Ok` / `TWI_Error`                                    | `TWI_OK` / `TWI_ERROR_NACK_ADDRESS` / `TWI_ERROR_NACK_DATA` / `TWI_ERROR_TIMEOUT` / `TWI_ERROR_BUS` / `TWI_ERROR_ARBITRATION_LOST` |
| `F_SCL`, `TWI_SCL`, `TWI_SDA` configuration macros        | `TWI_SCL_HZ`; SDA/SCL pins are fixed by hardware per MCU and no longer configurable |

### Fixes

- **Build** - the v1 library did not build:  
  `IO_Macros.h` was missing,  
  the demo called non-existent `TWI_ReceivePacket`,  
  and the demo compared the function pointer `TWI_Status` against `TWI_Ok` instead of calling it.  
  All three defects are gone in the rewrite.
- **NACK on address treated as success** (plan bug X4) - the v1 `TWI_PacketTransmit` accepted both `MT_SLA_W_TRANSMITTED_ACK` and `MT_SLA_W_TRANSMITTED_NACK` as "success",  
  so a missing or wrong-address device went undetected.  
  v2 returns `TWI_ERROR_NACK_ADDRESS` on the NACK code.
- **NACK on data swallowed by `break`** (plan bug X3) - inside the data transmit loop,  
  `break` only exited the `for`;  
  the trailing `status = TWI_Ok` then overwrote the error.  
  v2 returns the error status immediately from the loop.
- **`TWI_EndTransmission` enabled TWIE and TWEA** - master mode with no ISR installed would jump to the reset vector after `sei()`.  
  v2 writes only `TWINT | TWEN | TWSTO`.
- **Zero-length read wrote `Packet[0]`** - undefined behaviour on a null or short buffer.  
  v2 returns `TWI_OK` immediately when `data_length == 0`.
- **No timeout on TWINT** - a stuck SCL line hung the firmware forever.  
  v2 bounds every TWINT and TWSTO poll with `TWI_TIMEOUT_LOOPS` (default 10000) and returns `TWI_ERROR_TIMEOUT`.  
  Setting the macro to 0 restores the previous unbounded behaviour.
- **TWBR below datasheet minimum** - at F_CPU=8 MHz and SCL=400 kHz the formula gave `TWBR = 2`,  
  below the ATmega16/32/328P required minimum of 10 for master mode.  
  v2 asserts at compile time that `TWI_COMPUTED_TWBR >= 10` and emits a clear `#error` when the combination is out of range.
- **SDA/SCL pin configuration macros removed** - the TWI pins are hardware fixed per MCU (PC1/PC0 on ATmega16/32, PC4/PC5 on ATmega328P).  
  v2 enables the internal pull-ups on the correct pins automatically based on `__AVR_<part>__`.
- **twi_write_byte accepts SLA+R ack** - the switch now matches `MR_SLA_R_ACK` (0x40) and `MR_SLA_R_NACK` (0x48) in addition to the master-transmit codes,  
  so `twi_read` can use the same byte-shifter helper for the SLA+R byte.

### Project changes

- Repo renamed `AVR-TWI` -> `avr-twi`.
- Restructured to `src/ examples/ tests/ sim/ docs/ scripts/`.
- Shared `scripts/Common.psm1`,  
  `Build.ps1`,  
  `Simulate.ps1`,  
  CI workflow,  
  and `tests/fake_avr` / `tests/unity` scaffolding copied byte-identical from `avr-io-macros` (the base library of the suite).
- Added host-side Unity tests covering NACK propagation,  
  zero-length guards,  
  timeout and bus-error paths,  
  the TWBR compile-time formula,  
  and the fact that `twi_init` leaves `TWIE` and `TWEA` clear.
- Added `examples/demo.c` for a 24LC256 EEPROM round-trip (write pattern, read back, LED on PASS).
- Added README documenting the public API with flat section-per-function layout; no API tables.

## v1 - initial release

Original `TWI_Setup` / `TWI_PacketTransmit` / `TWI_PacketReceive` API.
