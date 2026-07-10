#!/usr/bin/env python3
"""CLI for managing wire mappings and pins on an esp-harness device.

Pin-to-GPIO mappings are baked into the firmware at compile time (per board)
and are read-only at runtime.

Wire mappings are stored in NVS and are the dynamic part — they describe
which harness host signals are physically wired to which DUT pads.

Usage:
    # List all wire mappings (DUT-side label ↔ host-side label)
    python3 harness_cli.py wire list

    # Add a wire mapping
    python3 harness_cli.py wire add LED HOST_IO5

    # Delete a wire mapping by DUT label
    python3 harness_cli.py wire delete LED

    # List all board pins (from the compiled-in pin table)
    python3 harness_cli.py pin list

    # Read a pin value by DUT label or GPIO number
    python3 harness_cli.py pin read USB_POWER_ON
    python3 harness_cli.py pin read 27

    # Write a pin value
    python3 harness_cli.py pin write USB_POWER_ON 1

    # Toggle a pin
    python3 harness_cli.py pin toggle USB_POWER_ON

    # Set/get pin direction
    python3 harness_cli.py pin dir USB_POWER_ON OUT
    python3 harness_cli.py pin dir USB_POWER_ON

    # Show full DUT status (identity, name, note, wires)
    python3 harness_cli.py status

    # Use a different serial port
    python3 harness_cli.py --port /dev/ttyACM1 wire list

    # Interactive SCPI REPL
    python3 harness_cli.py repl
"""

from __future__ import annotations

import argparse
import sys
from typing import NoReturn

from harness import HarnessSerial


def die(msg: str) -> NoReturn:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Wire subcommands
# ---------------------------------------------------------------------------

def cmd_wire_list(h: HarnessSerial, args: argparse.Namespace) -> None:
    wires = h.dut_wire_list()
    if not wires:
        print("No wire mappings configured.")
        return
    print(f"{'DUT Label':<32} {'Host Label':<32}")
    print("-" * 66)
    for dut_label, host_label in sorted(wires):
        print(f"{dut_label:<32} {host_label:<32}")


def cmd_wire_add(h: HarnessSerial, args: argparse.Namespace) -> None:
    h.dut_wire(args.dut_label, args.host_label)
    print(f"Wire mapping added: {args.dut_label} ↔ {args.host_label}")


def cmd_wire_delete(h: HarnessSerial, args: argparse.Namespace) -> None:
    wires = h.dut_wire_list()
    labels = [w[0] for w in wires]
    if args.dut_label not in labels:
        die(f"Wire with DUT label '{args.dut_label}' not found")
    h.dut_wire_del(args.dut_label)
    print(f"Wire mapping deleted: {args.dut_label}")


# ---------------------------------------------------------------------------
# Pin subcommands
# ---------------------------------------------------------------------------

def cmd_pin_list(h: HarnessSerial, args: argparse.Namespace) -> None:
    """List all pins in the compiled-in board profile."""
    board = h.dut_board()
    print(f"Board: {board}")
    print()
    pins = h.dut_pin_list()
    if not pins:
        print("No pins defined for this board profile.")
        return
    print(f"{'Label':<32} {'GPIO':<6}")
    print("-" * 40)
    for label, gpio in sorted(pins, key=lambda x: (x[1], x[0])):
        print(f"{label:<32} {gpio:<6}")


def cmd_pin_read(h: HarnessSerial, args: argparse.Namespace) -> None:
    """Read the current value of a pin."""
    val = h.gpio_read(args.pin)
    print(f"{args.pin} = {val}")


def cmd_pin_write(h: HarnessSerial, args: argparse.Namespace) -> None:
    """Set a pin to a specific level (0 or 1)."""
    h.gpio_write(args.pin, args.level)
    print(f"{args.pin} = {args.level}")


def cmd_pin_toggle(h: HarnessSerial, args: argparse.Namespace) -> None:
    """Toggle a pin (invert its current output level)."""
    h.gpio_toggle(args.pin)
    # Read back to show the new value
    val = h.gpio_read(args.pin)
    print(f"{args.pin} toggled → {val}")


def cmd_pin_dir(h: HarnessSerial, args: argparse.Namespace) -> None:
    """Get or set pin direction."""
    if args.direction is None:
        # Query
        d = h.gpio_dir_query(args.pin)
        print(f"{args.pin} direction: {d}")
    else:
        # Set
        h.gpio_dir(args.pin, args.direction)
        print(f"{args.pin} direction set to {args.direction}")


# ---------------------------------------------------------------------------
# Status / info
# ---------------------------------------------------------------------------

def cmd_status(h: HarnessSerial, args: argparse.Namespace) -> None:
    """Show full DUT status: identity, name, note, wires."""
    try:
        ident = h.idn()
    except Exception:
        ident = "(no response)"
    print(f"Device:  {ident}")

    chip = h.chip()
    serial = h.serial()
    board = h.dut_board()
    print(f"Chip:    {chip}")
    print(f"Serial:  {serial}")
    print(f"Board:   {board}  (compiled-in pin profile)")
    print()

    name = h.dut_name()
    note = h.dut_note()
    print(f"DUT name: {name or '(not set)'}")
    print(f"DUT note: {note or '(not set)'}")
    print()

    # Errors
    err_cnt = h.error_count()
    if err_cnt:
        print(f"Errors pending: {err_cnt}")
        while err_cnt:
            code, msg = h.error_next()
            print(f"  [{code}] {msg}")
            err_cnt -= 1
        print()

    print("Wire mappings (NVS-backed):")
    wires = h.dut_wire_list()
    if wires:
        for dut_label, host_label in sorted(wires):
            print(f"  {dut_label:<32} ↔ {host_label}")
    else:
        print("  (none)")
    print()

    print(f"Board profile: {board} — pin-to-GPIO mappings are compiled-in.")


# ---------------------------------------------------------------------------
# Repl / interactive mode
# ---------------------------------------------------------------------------

def cmd_repl(h: HarnessSerial, args: argparse.Namespace) -> None:
    """Enter an interactive SCPI REPL."""
    import readline  # noqa: F401 — enables line-editing in input()
    print("esp-harness interactive SCPI REPL. Type 'exit' or Ctrl-D to quit.")
    print("(commands are sent as-is; replies are printed raw)")
    print()
    while True:
        try:
            line = input("SCPI> ").strip()
        except EOFError:
            print()
            break
        if not line:
            continue
        if line.lower() in ("exit", "quit"):
            break
        # Send and print response if it's a query
        if line.endswith("?"):
            try:
                reply = h.query(line)
                print(reply)
            except Exception as e:
                print(f"! {e}")
        else:
            try:
                h.write(line)
            except Exception as e:
                print(f"! {e}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Manage wires and pins on an esp-harness device.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--port", "-p",
        default="/dev/ttyACM0",
        help="Serial port (default: /dev/ttyACM0)",
    )
    parser.add_argument(
        "--baud", "-b",
        type=int,
        default=115200,
        help="Baud rate (default: 115200, ignored for USB CDC)",
    )
    parser.add_argument(
        "--timeout", "-t",
        type=float,
        default=2.0,
        help="Serial timeout in seconds (default: 2.0)",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # --- wire ---
    wire = sub.add_parser("wire", help="Manage wire mappings (DUT ↔ host)")
    wire_sub = wire.add_subparsers(dest="subcommand", required=True)

    wire_list = wire_sub.add_parser("list", help="List all wire mappings")
    wire_list.set_defaults(func=cmd_wire_list)

    wire_add = wire_sub.add_parser("add", help="Add a wire mapping")
    wire_add.add_argument("dut_label", help="DUT-side pin label")
    wire_add.add_argument("host_label", help="Host-side signal label")
    wire_add.set_defaults(func=cmd_wire_add)

    wire_del = wire_sub.add_parser("delete", aliases=["del", "remove"],
                                   help="Delete a wire mapping")
    wire_del.add_argument("dut_label", help="DUT-side label of wire to remove")
    wire_del.set_defaults(func=cmd_wire_delete)

    # --- pin ---
    pin = sub.add_parser("pin", help="Manage board pins (read, write, toggle, dir)")
    pin_sub = pin.add_subparsers(dest="subcommand", required=True)

    pin_list = pin_sub.add_parser("list", help="List all board pins with GPIO numbers")
    pin_list.set_defaults(func=cmd_pin_list)

    pin_read = pin_sub.add_parser("read", help="Read a pin value")
    pin_read.add_argument("pin", help="Pin label (e.g. USB_POWER_ON) or GPIO number")
    pin_read.set_defaults(func=cmd_pin_read)

    pin_write = pin_sub.add_parser("write", help="Set a pin to 0 or 1")
    pin_write.add_argument("pin", help="Pin label or GPIO number")
    pin_write.add_argument("level", type=int, choices=[0, 1],
                           help="Output level (0 or 1)")
    pin_write.set_defaults(func=cmd_pin_write)

    pin_toggle = pin_sub.add_parser("toggle", help="Toggle a pin (invert output)")
    pin_toggle.add_argument("pin", help="Pin label or GPIO number")
    pin_toggle.set_defaults(func=cmd_pin_toggle)

    pin_dir = pin_sub.add_parser("dir", help="Get or set pin direction")
    pin_dir.add_argument("pin", help="Pin label or GPIO number")
    pin_dir.add_argument("direction", nargs="?",
                         choices=["IN", "OUT", "INOUT", "OFF"],
                         help="Direction to set (omit to query)")
    pin_dir.set_defaults(func=cmd_pin_dir)

    # --- status ---
    status = sub.add_parser("status", help="Show full DUT status (identity, name, wires)")
    status.set_defaults(func=cmd_status)

    # --- repl ---
    repl = sub.add_parser("repl", help="Interactive SCPI REPL for ad-hoc commands")
    repl.set_defaults(func=cmd_repl)

    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    try:
        h = HarnessSerial(port=args.port, baud_rate=args.baud,
                          timeout_s=args.timeout)
    except Exception as e:
        die(f"cannot open {args.port}: {e}")

    try:
        args.func(h, args)
    except Exception as e:
        die(str(e))
    finally:
        h.close()


if __name__ == "__main__":
    main()
