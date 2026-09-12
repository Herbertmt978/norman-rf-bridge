"""Record bounded, passive ESPHome logs; never call a movement or radio action.

Logs may contain installation-specific radio identifiers. Store them privately,
outside the public repository. ESPHOME_NOISE_PSK optionally supplies encryption.
"""

import argparse
import asyncio
import json
import os
from pathlib import Path
import time

import aioesphomeapi


async def record(args):
    client = aioesphomeapi.APIClient(
        args.host, 6053, "", expected_name=args.expected_name,
        noise_psk=os.environ.get("ESPHOME_NOISE_PSK"),
    )
    count = 0
    size = 0
    limit = asyncio.Event()
    with args.output.open("x", encoding="utf-8", newline="\n") as output:
        def on_log(message):
            nonlocal count, size
            line = json.dumps({
                "host_time": time.time(),
                "message": message.message.decode(errors="replace"),
            }) + "\n"
            length = len(line.encode("utf-8"))
            if size + length > args.max_bytes:
                limit.set()
                return
            output.write(line)
            output.flush()
            size += length
            count += 1

        try:
            await client.connect(login=True)
            client.subscribe_logs(on_log, log_level=aioesphomeapi.LogLevel.LOG_LEVEL_INFO,
                                  dump_config=False)
            print(json.dumps({"event": "LOG_SUBSCRIBED", "host_time": time.time()}), flush=True)
            try:
                await asyncio.wait_for(limit.wait(), timeout=args.seconds)
            except TimeoutError:
                pass
        finally:
            await client.disconnect()
    print(json.dumps({"event": "LOG_STOPPED", "messages": count, "bytes": size,
                      "limit_reached": limit.is_set()}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--expected-name", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, choices=range(1, 3601), default=60)
    parser.add_argument("--max-bytes", type=int, default=16 * 1024 * 1024)
    args = parser.parse_args()
    if not 1 <= args.max_bytes <= 64 * 1024 * 1024:
        parser.error("Log limit must be between 1 byte and 64 MiB")
    if args.output.exists() or not args.output.parent.is_dir():
        parser.error("Choose a new log file in an existing private directory")
    asyncio.run(record(args))


if __name__ == "__main__":
    main()
