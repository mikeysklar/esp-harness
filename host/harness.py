"""Python client for the esp-harness SCPI firmware.

Two transports are supported:

* :class:`Harness` (default) wraps a ``pyvisa`` ``ASRL`` resource and gives you
  the standard PyVISA semantics (``query``, ``write``, locks, etc.).
* :class:`HarnessSerial` is a tiny ``pyserial`` fallback for users who don't
  want the PyVISA dependency.

Both classes expose the same high-level methods (``idn``, ``gpio_write``,
``i2c_cont_read`` ...). Methods return decoded Python values; byte payloads
are returned as :class:`bytes`.
"""

from __future__ import annotations

import re
import time
from dataclasses import dataclass
from typing import Iterable, Optional, Sequence


# ---------------------------------------------------------------------------
# IEEE 488.2 arbitrary-block helpers (used by both transports).
# ---------------------------------------------------------------------------

def encode_block(data: bytes) -> bytes:
    """Encode ``data`` as an IEEE 488.2 definite-length arbitrary block."""
    n = str(len(data))
    return f"#{len(n)}{n}".encode("ascii") + data


def decode_block(reply: bytes) -> bytes:
    """Decode the first definite-length arbitrary block in ``reply``."""
    m = re.match(rb"\s*#(\d)", reply)
    if not m:
        raise ValueError(f"not an arbitrary block: {reply!r}")
    ndigits = int(m.group(1))
    hdr_end = m.end()
    length = int(reply[hdr_end : hdr_end + ndigits])
    start = hdr_end + ndigits
    return bytes(reply[start : start + length])


# ---------------------------------------------------------------------------
# Transport-agnostic command surface (mixin).
# ---------------------------------------------------------------------------

class _HarnessCommands:
    """Mixin: turns raw write/query into typed harness commands.

    Subclasses must supply ``write(cmd: str)``, ``query(cmd: str) -> str``,
    and ``query_block(cmd: str) -> bytes``.
    """

    # ---- IEEE 488.2 mandatory ----
    def idn(self) -> str:
        """Return the 4-field instrument identity string."""
        return self.query("*IDN?")

    def reset(self) -> None:
        """*RST &mdash; tear down all live buses and release claimed GPIOs.

        DUT metadata in NVS is preserved across *RST; use :py:meth:`dut_clear`
        to wipe it.
        """
        self.write("*RST")

    def cls(self) -> None:
        """*CLS &mdash; clear the error queue and event status registers."""
        self.write("*CLS")

    def self_test(self) -> int:
        """*TST? &mdash; always returns 0 on this device (boot success)."""
        return int(self.query("*TST?"))

    def opc(self) -> None:
        """*OPC &mdash; set the operation-complete bit when pending ops settle.

        All harness operations are synchronous, so this returns immediately.
        """
        self.write("*OPC")

    def opc_wait(self) -> int:
        """*OPC? &mdash; barrier; always returns 1 on this device."""
        return int(self.query("*OPC?"))

    # ---- SYSTem (SCPI-required + vendor) ----
    def error_next(self) -> tuple[int, str]:
        """Pop the next error from the queue. Returns ``(0, "No error")`` if empty."""
        reply = self.query("SYST:ERR?")
        code, _, msg = reply.partition(",")
        return int(code), msg.strip().strip('"')

    def error_count(self) -> int:
        """Number of errors still queued."""
        return int(self.query("SYST:ERR:COUN?"))

    def scpi_version(self) -> str:
        """SCPI version string the parser reports (e.g. ``\"1999.0\"``)."""
        return self.query("SYST:VERS?")

    def chip(self) -> str:
        """Chip identifier (``\"ESP32-P4\"``)."""
        return self.query("SYST:CHIP?")

    def serial(self) -> str:
        """MAC-derived serial string (12 hex chars)."""
        return self.query("SYST:SER?")

    def free(self) -> tuple[int, int]:
        """Free heap, as ``(internal_bytes, psram_bytes)``."""
        internal, psram = self.query("SYST:FREE?").split(",")
        return int(internal), int(psram)

    def uptime_ms(self) -> int:
        """Milliseconds since boot."""
        return int(self.query("SYST:UPT?"))

    def reboot(self) -> None:
        """Reboot the device. No reply is sent; the next call will reopen the port."""
        self.write("SYST:REB")

    # ---- DUT ----
    def dut_name(self, name: Optional[str] = None) -> Optional[str]:
        """Get or set the DUT board name (NVS-backed)."""
        if name is None:
            r = self.query("DUT:NAME?")
            return r.strip().strip('"') or None
        self.write(f'DUT:NAME "{name}"')
        return None

    def dut_note(self, text: Optional[str] = None) -> Optional[str]:
        """Get or set the free-form DUT note (NVS-backed)."""
        if text is None:
            r = self.query("DUT:NOTE?")
            return r.strip().strip('"') or None
        self.write(f'DUT:NOTE "{text}"')
        return None

    def dut_pin(self, label: str, gpio: Optional[int] = None) -> Optional[int]:
        """Get or set a DUT pin label → GPIO binding.

        ``gpio=-1`` records a logical pin with no GPIO (VCC, GND, NC).
        """
        if gpio is None:
            return int(self.query(f'DUT:PIN? "{label}"'))
        self.write(f'DUT:PIN "{label}",{gpio}')
        return None

    def dut_pin_del(self, label: str) -> None:
        """Remove a pin label."""
        self.write(f'DUT:PIN:DEL "{label}"')

    def dut_pin_list(self) -> dict[str, int]:
        """Return ``{label: gpio}`` for every recorded DUT pin."""
        r = self.query("DUT:PIN:LIST?").strip()
        if not r:
            return {}
        toks = _split_csv(r)
        out: dict[str, int] = {}
        for label, gpio in zip(toks[0::2], toks[1::2]):
            out[label.strip('"')] = int(gpio)
        return out

    def dut_wire(self, dut_label: str, host_label: str) -> None:
        """Record (or update) a wire entry mapping ``dut_label`` ↔ ``host_label``."""
        self.write(f'DUT:WIRE "{dut_label}","{host_label}"')

    def dut_wire_del(self, dut_label: str) -> None:
        """Remove a wire entry by its DUT-side label."""
        self.write(f'DUT:WIRE:DEL "{dut_label}"')

    def dut_wire_list(self) -> list[tuple[str, str]]:
        """Return ``[(dut_label, host_label), ...]`` for every recorded wire."""
        r = self.query("DUT:WIRE:LIST?").strip()
        if not r:
            return []
        toks = _split_csv(r)
        return [(toks[i].strip('"'), toks[i + 1].strip('"'))
                for i in range(0, len(toks), 2)]

    def dut_clear(self) -> None:
        """Wipe all DUT metadata from NVS."""
        self.write("DUT:CLE")

    # ---- GPIO ----
    def gpio_dir(self, pin, direction: str) -> None:
        """Set pin direction: ``IN`` / ``OUT`` / ``INOUT`` / ``OFF``.

        ``pin`` may be an integer GPIO number or a quoted DUT label.
        """
        self.write(f"GPIO:DIR {_pin(pin)},{direction}")

    def gpio_dir_query(self, pin) -> str:
        """Return ``\"OWNED\"`` if the harness configured this pin, else ``\"OFF\"``."""
        return self.query(f"GPIO:DIR? {_pin(pin)}").strip()

    def gpio_pull(self, pin, pull: str) -> None:
        """Set pull resistors: ``NONE`` / ``UP`` / ``DOWN`` / ``UPDOWN``."""
        self.write(f"GPIO:PULL {_pin(pin)},{pull}")

    def gpio_write(self, pin, level: int) -> None:
        self.write(f"GPIO:WRIT {_pin(pin)},{int(bool(level))}")

    def gpio_read(self, pin) -> int:
        return int(self.query(f"GPIO:READ? {_pin(pin)}"))

    def gpio_toggle(self, pin) -> None:
        self.write(f"GPIO:TOGG {_pin(pin)}")

    def gpio_pulse(self, pin, microseconds: int) -> None:
        self.write(f"GPIO:PUL {_pin(pin)},{int(microseconds)}")

    # ---- I2C controller ----
    def i2c_cont_init(self, sda: int, scl: int, hz: int) -> None:
        self.write(f"BUS:I2C:CONT:INIT {sda},{scl},{hz}")

    def i2c_cont_deinit(self) -> None:
        self.write("BUS:I2C:CONT:DEIN")

    def i2c_cont_scan(self) -> list[int]:
        r = self.query("BUS:I2C:CONT:SCAN?").strip()
        return [int(x) for x in r.split(",")] if r else []

    def i2c_cont_write(self, addr: int, data: bytes) -> None:
        block = encode_block(bytes(data)).decode("latin-1")
        self.write(f"BUS:I2C:CONT:WRIT {addr},{block}")

    def i2c_cont_read(self, addr: int, n: int) -> bytes:
        return self.query_block(f"BUS:I2C:CONT:READ? {addr},{n}")

    def i2c_cont_xfer(self, addr: int, write: bytes, read_n: int) -> bytes:
        block = encode_block(bytes(write)).decode("latin-1")
        return self.query_block(f"BUS:I2C:CONT:XFER? {addr},{block},{read_n}")

    # ---- I2C target ----
    def i2c_targ_init(self, sda: int, scl: int, addr: int) -> None:
        self.write(f"BUS:I2C:TARG:INIT {sda},{scl},{addr}")

    def i2c_targ_deinit(self) -> None:
        self.write("BUS:I2C:TARG:DEIN")

    def i2c_targ_write(self, data: bytes) -> None:
        block = encode_block(bytes(data)).decode("latin-1")
        self.write(f"BUS:I2C:TARG:WRIT {block}")

    def i2c_targ_read(self, n: int, timeout_ms: int = 100) -> bytes:
        return self.query_block(f"BUS:I2C:TARG:READ? {n},{timeout_ms}")

    def i2c_targ_state(self) -> tuple[int, int]:
        tx, rx = self.query("BUS:I2C:TARG:STAT?").split(",")
        return int(tx), int(rx)

    # ---- SPI controller ----
    def spi_cont_init(self, sck: int, mosi: int, miso: int, cs: int,
                      hz: int, mode: int) -> None:
        self.write(f"BUS:SPI:CONT:INIT {sck},{mosi},{miso},{cs},{hz},{mode}")

    def spi_cont_deinit(self) -> None:
        self.write("BUS:SPI:CONT:DEIN")

    def spi_cont_xfer(self, data: bytes) -> bytes:
        block = encode_block(bytes(data)).decode("latin-1")
        return self.query_block(f"BUS:SPI:CONT:XFER? {block}")

    def spi_cont_write(self, data: bytes) -> None:
        block = encode_block(bytes(data)).decode("latin-1")
        self.write(f"BUS:SPI:CONT:WRIT {block}")

    def spi_cont_cs(self, level: int) -> None:
        self.write(f"BUS:SPI:CONT:CS {int(bool(level))}")

    # ---- SPI target ----
    def spi_targ_init(self, sck: int, mosi: int, miso: int, cs: int,
                      mode: int) -> None:
        self.write(f"BUS:SPI:TARG:INIT {sck},{mosi},{miso},{cs},{mode}")

    def spi_targ_deinit(self) -> None:
        self.write("BUS:SPI:TARG:DEIN")

    def spi_targ_xfer(self, data: bytes, timeout_ms: int = 1000) -> bytes:
        block = encode_block(bytes(data)).decode("latin-1")
        return self.query_block(f"BUS:SPI:TARG:XFER? {block},{timeout_ms}")

    # ---- UART ----
    def uart_init(self, tx: int, rx: int, baud: int,
                  databits: int = 8, parity: str = "NONE", stop: int = 1) -> None:
        self.write(f"BUS:UART:INIT {tx},{rx},{baud},{databits},{parity},{stop}")

    def uart_deinit(self) -> None:
        self.write("BUS:UART:DEIN")

    def uart_write(self, data: bytes) -> None:
        block = encode_block(bytes(data)).decode("latin-1")
        self.write(f"BUS:UART:WRIT {block}")

    def uart_read(self, n: int, timeout_ms: int = 100) -> bytes:
        return self.query_block(f"BUS:UART:READ? {n},{timeout_ms}")

    def uart_drain(self) -> None:
        self.write("BUS:UART:DRA")


# ---------------------------------------------------------------------------
# pyvisa-backed transport (preferred).
# ---------------------------------------------------------------------------

class Harness(_HarnessCommands):
    """SCPI client over PyVISA. Open the harness by VISA resource string.

    Example::

        >>> with Harness("ASRL/dev/ttyACM0::INSTR") as h:
        ...     print(h.idn())
        ...     h.gpio_write(12, 1)
    """

    def __init__(self, resource: str = "ASRL/dev/ttyACM0::INSTR",
                 *, timeout_ms: int = 2000, baud_rate: int = 115200):
        import pyvisa  # local import so the file is usable w/o pyvisa
        rm = pyvisa.ResourceManager("@py")
        self._inst = rm.open_resource(
            resource,
            baud_rate=baud_rate,
            read_termination="\n",
            write_termination="\n",
        )
        self._inst.timeout = timeout_ms

    def write(self, cmd: str) -> None:
        self._inst.write(cmd)

    def query(self, cmd: str) -> str:
        return self._inst.query(cmd)

    def query_block(self, cmd: str) -> bytes:
        # pyvisa has query_binary_values, but it requires you to specify the
        # datatype. We do the block parse ourselves so the wire format stays
        # transparent and easy to debug.
        return decode_block(self._inst.query(cmd).encode("latin-1"))

    def close(self) -> None:
        self._inst.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


# ---------------------------------------------------------------------------
# pyserial fallback (no PyVISA dependency).
# ---------------------------------------------------------------------------

class HarnessSerial(_HarnessCommands):
    """SCPI client over a raw pyserial port. Useful if you don't have pyvisa."""

    def __init__(self, port: str = "/dev/ttyACM0",
                 *, baud_rate: int = 115200, timeout_s: float = 2.0):
        import serial  # type: ignore
        self._ser = serial.Serial(port, baudrate=baud_rate, timeout=timeout_s)

    def write(self, cmd: str) -> None:
        self._ser.write(cmd.encode("ascii") + b"\n")

    def query(self, cmd: str) -> str:
        self.write(cmd)
        line = self._ser.readline()
        return line.rstrip(b"\r\n").decode("latin-1")

    def query_block(self, cmd: str) -> bytes:
        # The block contains a literal length followed by raw bytes. We can't
        # rely on '\n' as a terminator inside the binary, so parse the header
        # then read exactly the announced number of bytes, then the trailing
        # newline.
        self.write(cmd)
        header = self._ser.read(2)
        if not header.startswith(b"#"):
            extra = self._ser.read_until(b"\n")
            raise ValueError(f"not a block reply: {(header + extra)!r}")
        ndigits = int(chr(header[1]))
        length = int(self._ser.read(ndigits))
        payload = self._ser.read(length)
        # consume the trailing terminator
        self._ser.read_until(b"\n")
        return payload

    def close(self) -> None:
        self._ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


# ---------------------------------------------------------------------------
# Helpers.
# ---------------------------------------------------------------------------

def _pin(p) -> str:
    """Render a pin argument as either an int or a quoted DUT label."""
    if isinstance(p, int):
        return str(p)
    return f'"{p}"'


def _split_csv(s: str) -> list[str]:
    """Split a comma-separated list, respecting "quoted,strings,with,commas"."""
    out: list[str] = []
    buf = []
    in_q = False
    for ch in s:
        if ch == '"':
            in_q = not in_q
            buf.append(ch)
        elif ch == "," and not in_q:
            out.append("".join(buf).strip())
            buf = []
        else:
            buf.append(ch)
    if buf:
        out.append("".join(buf).strip())
    return out


if __name__ == "__main__":
    # Trivial smoke test: print *IDN? and free heap, then bail.
    import sys
    try:
        with Harness() as h:
            print(h.idn())
            print("free internal/psram:", h.free())
    except Exception:
        with HarnessSerial() as h:
            print(h.idn())
            print("free internal/psram:", h.free())
