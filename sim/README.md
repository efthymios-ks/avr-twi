# sim/

SimulIDE circuit for the TWI EEPROM demo.

## Setup (one-time, Windows)

1. Run `.\Build.ps1 -Mcu atmega328p -OutDir build/sim` from the repo root.
2. Open SimulIDE and build the circuit:
   - MCU: ATmega328P
   - 24LC256-style serial EEPROM at I2C address 0x50
     - A0, A1, A2 tied to GND
     - VCC to +5 V, GND to GND, WP to GND
     - SDA to AVR PC4, SCL to AVR PC5
   - 4.7 kOhm pull-up resistors from SDA and SCL to +5 V
   - LED on PB5 via 330 Ohm to GND
3. Load the firmware: right-click the MCU, *Load firmware*, pick `build/sim/demo.hex`.
4. Save the circuit as `demo.sim1` in this folder.

After that, `.\Simulate.ps1` builds and opens the circuit automatically.

## Expected behavior

On reset the demo writes `DE AD BE EF` to EEPROM address 0, waits 10 ms for
the EEPROM's self-timed write cycle, reads the four bytes back, and compares
them. The PB5 LED turns on when every byte matches and stays dark on any
error (bus timeout, NACK, mismatch).
