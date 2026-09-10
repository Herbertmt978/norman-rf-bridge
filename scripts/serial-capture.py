"""Bounded ESP32 UART log capture without asserting the reset/boot lines."""

import argparse
import time
import sys
import re
from datetime import datetime, timezone
from pathlib import Path

import serial

sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("destination", type=Path)
parser.add_argument("--port", default="COM4")
parser.add_argument("--seconds", type=float, default=30)
parser.add_argument("--timestamp-lines", action="store_true")
args = parser.parse_args()
if not 0 < args.seconds <= 180:
    parser.error("Duration must be 0-180 seconds")
port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
port.dtr = False
port.rts = False
port.port = args.port
port.open()
try:
    with args.destination.open("xb") as output:
        print("UART_CAPTURE_READY", flush=True)
        until = time.monotonic() + args.seconds
        while time.monotonic() < until:
            data = port.readline(4096) if args.timestamp_lines else port.read(65536)
            if data:
                if args.timestamp_lines:
                    data = (datetime.now(timezone.utc).isoformat() + " ").encode() + data
                output.write(data)
                output.flush()
                text = re.sub(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", "[redacted]",
                              data.decode(errors="replace"))
                print(text, end="", flush=True)
finally:
    port.close()
