# esp-harness

A test-harness firmware for ESP32-P4. Exposes a USB CDC ACM port speaking
[SCPI-1999](https://www.ivifoundation.org/downloads/SCPI/scpi-99.pdf), so any
host &mdash; Python, LabVIEW, a plain serial terminal, an LLM &mdash; can:

- Manually configure and toggle GPIO pins.
- Bring up I²C / SPI / UART buses to a DUT, in **controller** or **target**
  mode for I²C and SPI.
- Store device-under-test (DUT) metadata in NVS: board name, free-form note,
  named-pin → GPIO map, and host↔DUT wiring records.

The full command set is documented in [`PROTOCOL.md`](./PROTOCOL.md). A Python
helper class is in [`host/harness.py`](./host/harness.py).

## Architecture

```
                       USB cable
host (PyVISA / pyserial / terminal)  <===========>  ESP32-P4
                                                       │
                  TinyUSB CDC ACM   ──── main.c ─────▶ harness_io
                                                       │ (stream buffers)
                                                       ▼
                                                   harness_scpi
                                                  (parser + task)
                                                       │
                              ┌────────────┬───────────┼───────────┬────────────┐
                              ▼            ▼           ▼           ▼            ▼
                          harness_gpio  harness_bus  harness_bus  harness_bus  harness_dut
                                          (I²C)        (SPI)        (UART)    (NVS store)
                                                       │
                                                       └── scpi_parser submodule
                                                           (j123b567/scpi-parser)
```

Each component is a standard ESP-IDF component under `components/`:

| Component        | Purpose                                                      |
|------------------|--------------------------------------------------------------|
| `harness_io`     | Bidirectional `StreamBuffer` pair between USB CDC and SCPI parser. |
| `harness_scpi`   | SCPI parser task + master command table (everything plugs in here). |
| `harness_gpio`   | `:GPIO:*` handlers, owns GPIO release on `*RST`.             |
| `harness_bus`    | `:BUS:I2C:CONT/TARG:*`, `:BUS:SPI:CONT/TARG:*`, `:BUS:UART:*`. |
| `harness_dut`    | NVS-backed DUT metadata + the `harness_dut_resolve_pin()` helper that any subsystem can use to accept either a numeric pin or a quoted label. |
| `scpi_parser`    | Vendored as a git submodule from j123b567/scpi-parser.       |

## Setup

This repo uses two git submodules:

- `esp-idf/` &mdash; Adafruit's ESP-IDF fork on the `usbip-bridge` branch.
- `components/scpi_parser/upstream/` &mdash; the j123b567/scpi-parser library.

Clone with `--recursive`, or initialize after cloning:

```sh
git clone --recursive <repo-url>
# or, if already cloned:
git submodule update --init --recursive

# One-time ESP-IDF toolchain install:
./esp-idf/install.sh esp32p4
```

## Build & flash

```sh
. ./esp-idf/export.sh
idf.py set-target esp32p4
idf.py build flash monitor
```

The harness appears as a USB CDC ACM device (e.g. `/dev/ttyACM0` on Linux,
`COMx` on Windows). The default console log uses the built-in USB Serial/JTAG
port; SCPI commands flow over the TinyUSB CDC interface.

## Talk to it

### From Python (PyVISA &mdash; preferred)

```python
import pyvisa
rm = pyvisa.ResourceManager("@py")
dev = rm.open_resource(
    "ASRL/dev/ttyACM0::INSTR",
    baud_rate=115200,
    read_termination="\n",
    write_termination="\n",
)
print(dev.query("*IDN?"))
dev.write('DUT:NAME "widget-rev-b"')
dev.write("GPIO:DIR 12,OUT")
dev.write("GPIO:WRITE 12,1")
print(dev.query("GPIO:READ? 12"))   # -> "1"
```

### Via the bundled helper class

```python
from host.harness import Harness

with Harness() as h:                              # opens ASRL/dev/ttyACM0::INSTR
    print(h.idn())
    h.dut_name("ssd1306-oled")
    h.dut_pin("SDA", 8)
    h.dut_pin("SCL", 9)
    h.dut_pin("RST", 10)
    h.gpio_pulse("RST", 10000)                    # label resolves via DUT table
    h.i2c_cont_init("SDA", "SCL", 400000)         # labels work in bus init too
    print(h.i2c_cont_scan())                      # -> [60]  (0x3C)
```

`HarnessSerial` is a drop-in alternative that uses raw `pyserial` if you
don't want the PyVISA dependency.

### From a serial terminal

Open the CDC port at any baud (CDC ignores the rate) and type one command
per line. Query replies arrive on a single line; binary payloads use the
IEEE 488.2 definite-length block format `#<n><len><raw bytes>`.

## Defining the DUT

A typical first session for a new board:

```text
*RST                                      # back to known state
DUT:CLE                                   # wipe any stale DUT info
DUT:NAME "ssd1306-128x64"
DUT:NOTE "0.96\" mono OLED, I2C 0x3C, 3v3 logic"

DUT:PIN "VCC",-1                          # -1 means "logical pin, no GPIO"
DUT:PIN "GND",-1
DUT:PIN "SDA",8
DUT:PIN "SCL",9
DUT:PIN "RST",10

DUT:WIRE "VCC","HARNESS_3V3"
DUT:WIRE "GND","HARNESS_GND"
DUT:WIRE "SDA","HARNESS_J3.5"
DUT:WIRE "SCL","HARNESS_J3.6"

SYST:ERR?                                 # should be 0,"No error"
```

DUT metadata persists in NVS, so after a reboot every command above can use
labels (`"SDA"`, `"RST"`) directly &mdash; both in `:GPIO:*` ops and in
`:BUS:*:INIT`.

## Directory layout

```
esp-harness/
├── CMakeLists.txt              top-level ESP-IDF project
├── sdkconfig.defaults          ESP32-P4 + PSRAM + TinyUSB CDC
├── PROTOCOL.md                 SCPI command reference
├── README.md                   this file
├── main/                       app entry: TinyUSB CDC + parser wiring
├── components/
│   ├── scpi_parser/            wraps j123b567/scpi-parser submodule
│   │   ├── upstream/           submodule
│   │   ├── user_config/        LF line-ending + full error list overrides
│   │   └── CMakeLists.txt
│   ├── harness_io/             USB-CDC ↔ stream-buffer transport
│   ├── harness_scpi/           parser task + master command table
│   ├── harness_gpio/           :GPIO:* handlers
│   ├── harness_bus/            :BUS:I2C/SPI/UART:* handlers
│   └── harness_dut/            NVS-backed DUT metadata + pin resolver
├── host/harness.py             PyVISA + pyserial Python client
└── esp-idf/                    submodule
```

## License

The vendored `scpi_parser` submodule is BSD-2-Clause. Code original to this
repo: see individual file headers (default to MIT unless noted).
