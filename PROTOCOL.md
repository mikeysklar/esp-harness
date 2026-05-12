# esp-harness SCPI Command Protocol

esp-harness exposes a USB CDC ACM port speaking **SCPI-1999** (IEEE 488.2
syntax). It is intended to be driven from Python via `pyvisa` / `pyvisa-py`
addressing the resource `ASRL/dev/ttyACM0::INSTR`, but any line-oriented
serial terminal works.

The grammar follows the published SCPI-1999 standard so existing tooling
(PyVISA, easy-scpi, lxi-tools, every test-equipment driver in the wild)
applies unmodified. The subsystems below (`:GPIO`, `:BUS`, `:DUT`) are
vendor-defined extensions, which SCPI-1999 explicitly permits.

## Transport

| Field                 | Value                          |
|-----------------------|--------------------------------|
| USB class             | CDC ACM                        |
| VISA resource         | `ASRL/dev/ttyACM0::INSTR` (Linux), `ASRL<n>::INSTR` (Windows) |
| `write_termination`   | `\n`                           |
| `read_termination`    | `\n`                           |
| Baud rate             | Ignored (CDC), declare 115200  |
| Max command length    | 256 bytes                      |
| Max reply length      | 4096 bytes (binary blocks excepted) |

## Syntax (SCPI-1999)

```
[*]COMMAND[:NODE...][?] [arg[,arg...]]
```

- Common commands begin with `*` (e.g. `*IDN?`).
- Subsystem commands start at the SCPI root (e.g. `:GPIO:WRITE`). The leading
  colon is optional in single-command lines.
- Mnemonics accept **short form** (uppercase letters shown here) and **long
  form**. `:GPIO:WRIT 12,1` ≡ `:GPIO:WRITE 12,1` ≡ `:gpio:write 12,1`.
- Multiple commands may be chained with `;`. After `;` the parser stays in
  the current node.
- Queries end with `?` and always emit a reply line. Action commands emit no
  reply unless they fail.
- Errors are pushed onto the SCPI error queue and retrieved via
  `SYST:ERR?`. The error LED is the response: clients should poll
  `SYST:ERR?` after a batch, or use `*OPC?` as a barrier and then read errors.

### Argument types

| Type        | Form                                              |
|-------------|---------------------------------------------------|
| Integer     | `12`, `0x0C`, `#H0C`, `#B1100`                    |
| Boolean     | `0` / `1` / `OFF` / `ON`                          |
| Enum        | Short or long form mnemonic (e.g. `OUT`, `MODE0`) |
| String      | `"text"` (IEEE 488.2 §7.7.5)                      |
| Block (in)  | `#<n><len><bytes>` definite-length arbitrary block (IEEE 488.2 §7.7.6) |
| Block (out) | Same form for byte replies                        |

### Block payload encoding

For byte-oriented payloads (I2C/SPI/UART transfers), commands and queries
use the standard IEEE 488.2 **definite-length arbitrary block**:

```
#<n><len_digits><raw bytes>
```

`n` is one ASCII digit giving the number of digits in `len_digits`, and
`len_digits` is the byte count. Examples:

- 4 raw bytes `0xDE 0xAD 0xBE 0xEF` ⇒ `#14<de><ad><be><ef>`
- Zero bytes ⇒ `#10`

PyVISA encodes/decodes these natively via `write_binary_values()` and
`query_binary_values(..., datatype='B')`.

## Mandatory IEEE 488.2 / SCPI commands

These come from `scpi-parser` and are exposed unchanged:

| Command          | Behavior                                                   |
|------------------|------------------------------------------------------------|
| `*IDN?`          | `Chickadee,esp-harness,<serial>,<fw_version>`              |
| `*RST`           | Resets all volatile state (buses deinit, GPIO to inputs)   |
| `*CLS`           | Clears the error queue and status registers                |
| `*TST?`          | Returns `0` (self-test passes if firmware booted)          |
| `*OPC` / `*OPC?` | Operation-complete barrier (`*OPC?` always returns `1`)    |
| `*ESE` / `*ESE?` | Event status enable                                        |
| `*ESR?`          | Event status register                                      |
| `*SRE` / `*SRE?` | Service request enable                                     |
| `*STB?`          | Status byte                                                |
| `*WAI`           | Wait for pending ops (no-op; all ops are synchronous)      |
| `SYST:ERR?`      | Pops next `<code>,"<message>"` from error queue            |
| `SYST:ERR:COUN?` | Number of errors still in the queue                        |
| `SYST:VERS?`     | SCPI version (`1999.0`)                                    |

### SCPI status registers (scpi-parser defaults)

The library implements the standard SCPI Questionable/Operation register
trees. The harness does not currently raise any events on them, so all
reads return `0` and writes have no observable effect. They exist for
strict SCPI compliance and are safe to ignore.

| Command                              | Behavior                                  |
|--------------------------------------|-------------------------------------------|
| `STATus:QUEStionable[:EVENt]?`       | Reads (and clears) the event register     |
| `STATus:QUEStionable:ENABle <mask>`  | Sets the enable mask                      |
| `STATus:QUEStionable:ENABle?`        | Reads the enable mask                     |
| `STATus:PRESet`                      | Restores enable mask to its default       |

## Vendor subsystems

### `:SYST` — system info (in addition to SCPI-required)

| Command            | Args            | Reply / effect                                         |
|--------------------|-----------------|--------------------------------------------------------|
| `:SYST:CHIP?`      | —               | `ESP32-P4`                                             |
| `:SYST:SERial?`    | —               | MAC-derived serial string                              |
| `:SYST:FREE?`      | —               | `<internal>,<psram>` free heap bytes                   |
| `:SYST:UPTime?`    | —               | uptime in milliseconds                                 |
| `:SYST:REBoot`     | —               | Reboot the device. No reply.                           |

### `:GPIO` — manual pin control

| Command            | Args               | Reply / effect                                          |
|--------------------|--------------------|---------------------------------------------------------|
| `:GPIO:DIR`        | `pin,dir`          | `dir` ∈ `IN`, `OUT`, `INOUT`, `OFF`                     |
| `:GPIO:DIR?`       | `pin`              | Current dir as mnemonic                                 |
| `:GPIO:PULL`       | `pin,pull`         | `pull` ∈ `NONE`, `UP`, `DOWN`, `UPDOWN`                 |
| `:GPIO:WRITe`      | `pin,level`        | Drive `pin` to `level` (0 or 1)                         |
| `:GPIO:READ?`      | `pin`              | `0` or `1`                                              |
| `:GPIO:TOGGle`     | `pin`              | Invert current output level                             |
| `:GPIO:PULse`      | `pin,us`           | Drive pin high then low for `us` microseconds           |

### Pin arguments

Anywhere a command takes a pin &mdash; the `pin` slot in every `:GPIO:*` op,
and the `sda`/`scl`/`sck`/`mosi`/`miso`/`cs`/`tx`/`rx` slots in
`:BUS:*:INIT` &mdash; the argument may be **either** a numeric GPIO
(`12`, `0x0C`) **or** a quoted DUT pin label set previously via `:DUT:PIN`
(e.g. `"SDA"`).

For `:GPIO:*` ops the resolved GPIO must lie in `0..54` (ESP32-P4 range).
For `:BUS:*:INIT` the value `-1` is additionally accepted to mean "unused"
(e.g. SPI MISO on a write-only device, or `INOUT` flow-control pins).

### `:BUS` — target buses

I²C and SPI come in two flavors: **controller** (the harness initiates
transactions; `:CONT` for short) and **target** (the harness is addressed by
an external controller; `:TARG` for short). UART is symmetric and has no
controller/target distinction.

The two roles share one peripheral instance per bus, so a given bus must
`:DEINit` before switching roles.

#### `:BUS:I2C:CONT` — I²C controller

| Command                  | Args                        | Reply / effect                                |
|--------------------------|-----------------------------|-----------------------------------------------|
| `:BUS:I2C:CONT:INIT`     | `sda,scl,hz`                | Bring up bus as controller                    |
| `:BUS:I2C:CONT:DEINit`   | —                           | Stop the bus                                  |
| `:BUS:I2C:CONT:SCAN?`    | —                           | Comma-separated 7-bit addresses that ACK      |
| `:BUS:I2C:CONT:WRITe`    | `addr,<block>`              | Write block bytes to `addr`                   |
| `:BUS:I2C:CONT:READ?`    | `addr,n`                    | Returns a `<block>` of `n` bytes              |
| `:BUS:I2C:CONT:XFER?`    | `addr,<block>,n`            | Write block then repeated-start read `n`      |

#### `:BUS:I2C:TARG` — I²C target

| Command                  | Args                                  | Reply / effect                                              |
|--------------------------|---------------------------------------|-------------------------------------------------------------|
| `:BUS:I2C:TARG:INIT`     | `sda,scl,addr[,hz]`                   | Bring up bus as target with 7-bit address                   |
| `:BUS:I2C:TARG:DEINit`   | —                                     | Stop the bus                                                |
| `:BUS:I2C:TARG:WRITe`    | `<block>`                             | Queue bytes for the next controller read                    |
| `:BUS:I2C:TARG:READ?`    | `n,timeout_ms`                        | Pop up to `n` received bytes; returns `<block>` (may be 0)  |
| `:BUS:I2C:TARG:STATe?`   | —                                     | `<tx_queued>,<rx_queued>` byte counts                       |

#### `:BUS:SPI:CONT` — SPI controller

| Command                  | Args                                  | Reply / effect                          |
|--------------------------|---------------------------------------|-----------------------------------------|
| `:BUS:SPI:CONT:INIT`     | `sck,mosi,miso,cs,hz,mode`            | `mode` ∈ `0..3`. `-1` for unused pins.  |
| `:BUS:SPI:CONT:DEINit`   | —                                     | Free SPI bus                            |
| `:BUS:SPI:CONT:XFER?`    | `<block>`                             | Full-duplex; returns RX `<block>`       |
| `:BUS:SPI:CONT:WRITe`    | `<block>`                             | Write-only; RX discarded                |
| `:BUS:SPI:CONT:CS`       | `level`                               | Manually drive CS pin                   |

#### `:BUS:SPI:TARG` — SPI target

| Command                  | Args                                  | Reply / effect                                          |
|--------------------------|---------------------------------------|---------------------------------------------------------|
| `:BUS:SPI:TARG:INIT`     | `sck,mosi,miso,cs,mode`               | Bring up SPI as a target                                |
| `:BUS:SPI:TARG:DEINit`   | —                                     | Free SPI bus                                            |
| `:BUS:SPI:TARG:XFER?`    | `<block>,timeout_ms`                  | Queue TX `<block>`, block until controller clocks       |
|                          |                                       | a transaction, return same-length RX `<block>`          |

#### `:BUS:UART`

This is the *target-side* UART exposed to the DUT, distinct from the console
the host uses to talk to the harness.

| Command               | Args                                  | Reply / effect                                  |
|-----------------------|---------------------------------------|-------------------------------------------------|
| `:BUS:UART:INIT`      | `tx,rx,baud[,databits,parity,stop]`   | Defaults: 8N1. `parity` ∈ `NONE`, `EVEN`, `ODD` |
| `:BUS:UART:DEINit`    | —                                     | Free UART                                       |
| `:BUS:UART:WRITe`     | `<block>`                             | Write raw bytes                                 |
| `:BUS:UART:READ?`     | `n,timeout_ms`                        | Up to `n` bytes as `<block>`                    |
| `:BUS:UART:DRAin`     | —                                     | Flush TX, discard RX                            |

### `:DUT` — device-under-test metadata (NVS-backed)

`:DUT` records what *board* is connected, how its pins are labeled, and which
host signals reach which DUT pads. State persists across reboots in the NVS
namespace `harness_dut`.

| Command               | Args                              | Reply / effect                                       |
|-----------------------|-----------------------------------|------------------------------------------------------|
| `:DUT:NAME?`          | —                                 | Board name string (`""` if unset)                    |
| `:DUT:NAME`           | `"name"`                          | Set board name                                       |
| `:DUT:NOTE?`          | —                                 | Free-form note                                       |
| `:DUT:NOTE`           | `"text"`                          | Set note                                             |
| `:DUT:PIN?`           | `"label"`                         | GPIO bound to that DUT pin label                     |
| `:DUT:PIN`            | `"label",gpio`                    | Map label → GPIO (`-1` for power/ground/NC pads)     |
| `:DUT:PIN:DELete`     | `"label"`                         | Remove a pin mapping                                 |
| `:DUT:PIN:LIST?`      | —                                 | `"label1",gpio1,"label2",gpio2,...`                  |
| `:DUT:WIRE`           | `"dut_label","host_label"`        | Record a host↔DUT wire (free-form)                   |
| `:DUT:WIRE:DELete`    | `"dut_label"`                     | Remove a wire entry                                  |
| `:DUT:WIRE:LIST?`     | —                                 | `"dut1","host1","dut2","host2",...`                  |
| `:DUT:CLEar`          | —                                 | Wipe all DUT metadata from NVS                       |

## Error queue

Errors follow the SCPI convention (negative codes are SCPI-defined, positive
are vendor-defined):

| Code  | Meaning                                                         |
|-------|-----------------------------------------------------------------|
| `0`   | No error                                                        |
| `-100`| Command error (parser)                                          |
| `-200`| Execution error (generic)                                       |
| `-220`| Parameter error (count, range)                                  |
| `-221`| Settings conflict (e.g. pin already in use)                     |
| `-241`| Hardware missing (e.g. bus referenced before `INIT`)            |
| `-300`| Device-specific error                                           |
| `100` | Bad pin (out of GPIO range for chip)                            |
| `101` | NVS error                                                       |
| `102` | DUT label not found                                             |

Read with `SYST:ERR?` which returns e.g. `0,"No error"` or
`-220,"Parameter error: pin out of range"`.

## Example session

```text
*IDN?
Chickadee,esp-harness,3c8427a0b1c2,0.1.0
*RST
SYST:CHIP?
ESP32-P4
SYST:FREE?
401528,33554432

DUT:NAME "widget-rev-b"
DUT:PIN "LED",12
DUT:WIRE "LED","HOST_IO5"
DUT:PIN:LIST?
"LED",12

GPIO:DIR "LED",OUT
GPIO:WRITE "LED",1
GPIO:READ? "LED"
1

DUT:PIN "SDA",8
DUT:PIN "SCL",9
BUS:I2C:CONT:INIT "SDA","SCL",400000
BUS:I2C:CONT:SCAN?
80,104
BUS:I2C:CONT:WRITE 0x50,#10
BUS:I2C:CONT:READ? 0x50,4
#14<DE><AD><BE><EF>

BUS:SPI:CONT:INIT 36,35,37,34,1000000,0
BUS:SPI:CONT:XFER? #14<9F><00><00><00>
#14<00><EF><40><18>

# Target-mode example: pretend to be an I²C EEPROM at 0x50
BUS:I2C:TARG:INIT 8,9,0x50
BUS:I2C:TARG:WRITE #14<DE><AD><BE><EF>      # queue 4 bytes for controller to read
BUS:I2C:TARG:READ? 16,1000                  # pop up to 16 bytes the controller wrote

SYST:ERR?
0,"No error"
```

(Hex angle brackets denote raw bytes in the arbitrary block; on the wire
they are literal 0xDE etc.)

## References

- SCPI-1999: https://www.ivifoundation.org/downloads/SCPI/scpi-99.pdf
- IEEE 488.2-1992: §7.7 message exchange, §10 common commands
- scpi-parser (C library on device): https://github.com/j123b567/scpi-parser
- pyvisa: https://pyvisa.readthedocs.io/
- pyvisa-py (pure-Python backend): https://pyvisa.readthedocs.io/projects/pyvisa-py/
