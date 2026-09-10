"""Bounded receive captures using an already logged-in Pluto USB console.

Uses the device's native iio_readdev. No network driver, radio TX, or device
firmware change is needed. IQ files remain under /tmp and transfer with SHA256
verification. The USB CDC channel does not use a physical 115200-baud UART.
"""

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import time
import uuid

import serial


class Console:
    def __init__(self, port):
        self.serial = serial.Serial(port, 115200, timeout=0.2, write_timeout=5)
        self.serial.write(b'\x03\r\n')
        greeting = self.prompt()
        if b'Password:' in greeting:
            self.serial.write(b'\x03\r\n')
            greeting = self.prompt()
        if b'login:' in greeting:
            password = os.environ.get('PLUTO_CONSOLE_PASSWORD')
            if password is None:
                raise RuntimeError('Set PLUTO_CONSOLE_PASSWORD for console login')
            self.serial.write(b'root\n')
            if b'Password:' not in self.prompt():
                raise RuntimeError('Console did not request login password')
            self.serial.write(password.encode() + b'\n')
            if b'# ' not in self.prompt():
                raise RuntimeError('Console authentication failed')
        self.serial.write(b'stty sane -echo clocal -hupcl\n')
        self.prompt()

    def prompt(self):
        data = bytearray()
        until = time.monotonic() + 6
        while time.monotonic() < until:
            data.extend(self.serial.read(4096))
            if any(value in data for value in (b'login:', b'Password:', b'# ')):
                return bytes(data)
        raise TimeoutError('Console did not produce a login or shell prompt')

    def close(self):
        self.serial.close()

    def command(self, command, timeout=10):
        marker = "END_" + uuid.uuid4().hex
        self.serial.reset_input_buffer()
        self.serial.write((command + "; printf '\\n" + marker + ":%s\\n' $?\n").encode())
        data = bytearray()
        until = time.monotonic() + timeout
        pattern = re.compile(rb"\r?\n" + marker.encode() + rb":(\d+)\r*\n")
        while time.monotonic() < until:
            data.extend(self.serial.read(65536))
            match = pattern.search(data)
            if match:
                output = data[:match.start()].decode(errors="replace").strip()
                if int(match[1]):
                    raise RuntimeError(output)
                return output
        raise TimeoutError("Pluto console command timed out: " + repr(bytes(data[-1000:])))

    def download(self, remote, destination, size):
        # The console's raw binary path loses USB blocks on this Pluto/Windows
        # combination. POSIX uuencode -m provides an ASCII envelope; validate
        # framing, decoded length and the device's SHA256 before accepting IQ.
        end = ("END_" + uuid.uuid4().hex).encode()
        self.serial.reset_input_buffer()
        self.serial.write((f"uuencode -m {remote} capture; printf '\\n"
                           + end.decode() + "\\n'\n").encode())
        count = 0
        digest = hashlib.sha256()
        started = last_progress = time.monotonic()
        until = started + 20
        pending = b""
        header_seen = finished = False
        with destination.open("xb") as output:
            while not finished:
                chunk = self.serial.read(65536)
                if chunk:
                    until = time.monotonic() + 20
                    lines = (pending + chunk).split(b'\n')
                    pending = lines.pop()
                    for line in lines:
                        line = line.strip()
                        if not line:
                            continue
                        if line.startswith(b'begin-base64 '):
                            header_seen = True
                        elif line == b'====':
                            pass
                        elif line == end:
                            finished = True
                            break
                        elif header_seen:
                            payload = base64.b64decode(line, validate=True)
                            output.write(payload)
                            digest.update(payload)
                            count += len(payload)
                            if count > size:
                                raise RuntimeError('Transfer exceeds expected capture size')
                if time.monotonic() > until:
                    raise TimeoutError(f"Transfer stalled at {count}/{size}")
                if time.monotonic() - last_progress > 5:
                    print(json.dumps({"bytes": count, "total": size}), flush=True)
                    last_progress = time.monotonic()
        if count != size:
            raise RuntimeError(f'Transfer length mismatch: {count}/{size}')
        return digest.hexdigest(), time.monotonic() - started


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("info")
    capture = sub.add_parser("capture")
    capture.add_argument("label")
    capture.add_argument("--frequency", type=int, default=2415000000)
    capture.add_argument("--rate", type=int, default=4000000)
    capture.add_argument("--seconds", type=float, default=5)
    capture.add_argument("--gain", type=int, default=30)
    capture.add_argument("--download", type=Path)
    download = sub.add_parser("download")
    download.add_argument("label")
    download.add_argument("destination", type=Path)
    args = parser.parse_args()
    if args.action != "info" and not re.fullmatch(r"[a-zA-Z0-9_-]{1,60}", args.label):
        parser.error("Label must be 1-60 letters, digits, underscores or hyphens")
    console = Console(args.port)
    try:
        if args.action == "info":
            print(console.command("cat /sys/bus/iio/devices/iio:device0/name; "
                  "iio_attr -q -i -c ad9361-phy voltage0 sampling_frequency; "
                  "iio_attr -q -i -c ad9361-phy voltage0 rf_bandwidth; "
                  "iio_attr -q -i -c ad9361-phy voltage0 gain_control_mode; "
                  "iio_attr -q -i -c ad9361-phy voltage0 hardwaregain; "
                  "iio_attr -q -c ad9361-phy altvoltage0 frequency; free -m"))
        elif args.action == "capture":
            if not (2400000000 <= args.frequency <= 2483500000 and
                    2000000 <= args.rate <= 8000000 and 0 < args.seconds <= 15 and
                    0 <= args.gain <= 70):
                parser.error("Capture settings outside the bounded 2.4 GHz RX range")
            count = int(args.rate * args.seconds)
            if count * 4 > 240000000:
                parser.error("Capture exceeds the 240 MB device memory budget")
            base = "/tmp/norman-" + args.label
            settings = [
                f"test ! -e {base}.iq",
                "task_free=$(df -k /tmp | tail -1 | awk '{print $4}'); "
                f"test \"$task_free\" -gt {(count * 4 + 8388608) // 1024}",
                f"iio_attr -q -i -c ad9361-phy voltage0 rf_port_select A_BALANCED",
                f"iio_attr -q -i -c ad9361-phy voltage0 gain_control_mode manual",
                f"iio_attr -q -i -c ad9361-phy voltage0 hardwaregain {args.gain}",
                f"iio_attr -q -i -c ad9361-phy voltage0 sampling_frequency {args.rate}",
                f"iio_attr -q -i -c ad9361-phy voltage0 rf_bandwidth {int(args.rate * .8)}",
                f"iio_attr -q -c ad9361-phy altvoltage0 frequency {args.frequency}",
            ]
            print(console.command(" && ".join(settings)))
            print(json.dumps({"label": args.label, "frequency": args.frequency,
                              "sample_rate": args.rate, "samples": count,
                              "gain_db": args.gain, "host_time": time.time(),
                              "format": "ci16_le; signed 12-bit I/Q in 16-bit containers"}), flush=True)
            job = (f"iio_readdev -u local: -b 262144 -s {count} cf-ad9361-lpc voltage0 voltage1 "
                   f">{base}.iq 2>{base}.err; task_rc=$?; echo $task_rc >{base}.status; "
                   f"test $task_rc -eq 0")
            print(console.command(job, timeout=args.seconds + 20), flush=True)
            if args.download:
                args.destination = args.download
        if args.action == "download" or (args.action == "capture" and args.download):
            remote = "/tmp/norman-" + args.label + ".iq"
            base = remote[:-3]
            print(console.command(f"test \"$(cat {base}.status)\" = 0 && cat {base}.err"))
            listing = console.command(f"wc -c <{remote}; sha256sum {remote}", timeout=60)
            numbers = re.findall(r"(?m)^\s*(\d+)\s*\r?$", listing)
            hashes = re.findall(r"\b[a-f0-9]{64}\b", listing)
            if len(numbers) != 1 or len(hashes) != 1:
                raise RuntimeError("Could not parse capture size/hash")
            size = int(numbers[0])
            if not 0 < size <= 240000000:
                raise RuntimeError("Invalid capture size")
            if args.action == 'capture' and size != count * 4:
                raise RuntimeError('Captured size differs from requested sample count')
            args.destination.parent.mkdir(parents=True, exist_ok=True)
            actual, elapsed = console.download(remote, args.destination, size)
            if actual != hashes[0]:
                raise RuntimeError("Capture SHA256 does not match the Pluto file")
            print(json.dumps({"path": str(args.destination), "bytes": size,
                              "sha256": actual, "transfer_seconds": elapsed}))
    finally:
        console.close()


if __name__ == "__main__":
    main()
