"""Bounded RX-only native libiio capture directly to the Windows data drive."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import shutil


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('destination', type=Path)
parser.add_argument('--tools', type=Path, required=True)
parser.add_argument('--uri', required=True)
parser.add_argument('--frequency', type=int, default=2415000000)
parser.add_argument('--rate', type=int, default=4000000)
parser.add_argument('--seconds', type=float, default=8)
parser.add_argument('--gain', type=int, default=30)
parser.add_argument('--max-bytes', type=int, default=512 * 1024 * 1024,
                    help='Per-recording limit; raise explicitly for a longer owner-coordinated capture (maximum 2 GiB)')
args = parser.parse_args()
if not (2400000000 <= args.frequency <= 2483500000 and args.rate in (2000000, 4000000, 8000000)
        and 0 < args.seconds <= 120 and 0 <= args.gain <= 70
        and 0 < args.max_bytes <= 2 * 1024 * 1024 * 1024):
    parser.error('Unsupported bounded 2.4 GHz receive parameters')
if args.destination.exists():
    parser.error('Refusing to replace an existing recording')
samples = int(args.rate * args.seconds)
if samples * 4 > args.max_bytes:
    parser.error('Recording exceeds --max-bytes; review storage budget before increasing it')
if not args.destination.parent.is_dir():
    parser.error('Capture directory must already exist')
if shutil.disk_usage(args.destination.parent).free < samples * 4 + 1024 * 1024 * 1024:
    parser.error('Insufficient free space for recording plus 1 GiB reserve')
attr = str(args.tools / 'iio_attr.exe')
for name, value in [('rf_port_select', 'A_BALANCED'), ('gain_control_mode', 'manual'),
                    ('hardwaregain', args.gain), ('sampling_frequency', args.rate),
                    ('rf_bandwidth', int(args.rate * 0.8))]:
    subprocess.run([attr, '-u', args.uri, '-q', '-i', '-c', 'ad9361-phy', 'voltage0', name, str(value)],
                   check=True, capture_output=True)
subprocess.run([attr, '-u', args.uri, '-q', '-c', 'ad9361-phy', 'altvoltage0',
                'frequency', str(args.frequency)], check=True, capture_output=True)
started = time.time()
with args.destination.open('xb') as output:
    process = subprocess.Popen([str(args.tools / 'iio_readdev.exe'), '-u', args.uri,
                                '-b', '262144', '-s', str(samples),
                                'cf-ad9361-lpc', 'voltage0', 'voltage1'],
                               stdout=output, stderr=subprocess.PIPE)
    print(json.dumps({'event': 'RX_CAPTURE_STARTED', 'host_time': started,
                      'frequency': args.frequency, 'samples': samples}), flush=True)
    try:
        _, error = process.communicate(timeout=args.seconds + 30)
    except BaseException:
        process.kill()
        process.wait()
        raise
if process.returncode:
    raise RuntimeError(error.decode(errors='replace'))
size = args.destination.stat().st_size
if size != samples * 4:
    raise RuntimeError(f'Truncated recording: expected {samples * 4}, got {size}')
with args.destination.open('rb') as source:
    digest = hashlib.file_digest(source, 'sha256').hexdigest()
metadata = {'source': str(args.destination), 'host_start_time': started,
            'sample_rate': args.rate, 'frequency': args.frequency, 'gain_db': args.gain,
            'duration_s': args.seconds, 'samples': samples, 'bytes': size,
            'format': 'ci16_le; signed 12-bit IQ in int16 containers', 'sha256': digest,
            'rx_port': 'R1/A_BALANCED', 'transmit_enabled': False}
with args.destination.with_suffix('.metadata.json').open('x') as output:
    json.dump(metadata, output, indent=2)
print(json.dumps(metadata), flush=True)
