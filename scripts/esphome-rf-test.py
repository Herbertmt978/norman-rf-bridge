"""Historical firmware0.4 bench tool; current firmware has no rf_transmit_test.

Use bridge-control.py for the current learned-command API. This script is kept
only to reproduce the earlier explicitly armed experiment with its old image.

Does not call the Norman hub. CRC and channel/copy bounds are rechecked by the
ESP32 firmware. Physical shutter movement must be observed independently.
"""
import argparse
import asyncio
from pathlib import Path
import runpy
import time

import aioesphomeapi


async def main(args):
    client = aioesphomeapi.APIClient(args.host, 6053, '', expected_name=args.expected_name)
    await client.connect(login=True)
    try:
        _, services = await client.list_entities_services()
        service = next(item for item in services if item.name == 'rf_transmit_test')
        with args.log.open('x') as output:
            def on_log(message):
                line = message.message.decode(errors='replace')
                output.write(f'{time.time():.6f} {line}\n')
                output.flush()
                print(line, flush=True)
            client.subscribe_logs(on_log, log_level=aioesphomeapi.LogLevel.LOG_LEVEL_INFO,
                                  dump_config=False)
            frame = bytearray.fromhex(args.frame)
            if args.rolling_index is not None:
                index = args.rolling_index
                frame[24] = index
                frame[25] = 0xdd ^ int(f'{index:08b}'[::-1], 2)
                frame[28:] = crc16(frame[:28]).to_bytes(2, 'big')
            print(f'Explicit TX: ch={args.channel} copies={args.copies} frame={frame.hex()}', flush=True)
            output.write(f'{time.time():.6f} REQUEST {frame.hex()} ch={args.channel} copies={args.copies}\n')
            await client.execute_service(service, {'frame': list(frame), 'channel': args.channel,
                                                   'copies': args.copies})
            await asyncio.sleep(8)
    finally:
        await client.disconnect()


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', required=True)
parser.add_argument('--expected-name', required=True)
parser.add_argument('--frame', required=True)
parser.add_argument('--channel', type=int, choices=[15, 39, 59], default=15)
parser.add_argument('--copies', type=int, choices=range(1, 101), default=100)
parser.add_argument('--rolling-index', type=int, choices=range(256))
parser.add_argument('--log', type=Path, required=True)
args = parser.parse_args()
crc16 = runpy.run_path(str(Path(__file__).with_name('decode-iq.py')))['crc16']
try:
    frame = bytes.fromhex(args.frame)
except ValueError:
    parser.error('Frame must be hexadecimal')
if len(frame) != 30 or frame[0] != 24 or crc16(frame[:28]) != int.from_bytes(frame[28:], 'big'):
    parser.error('Template must be a CRC-valid 30-byte Norman frame')
if args.log.exists():
    parser.error('Refusing to replace an existing test log')
asyncio.run(main(args))
