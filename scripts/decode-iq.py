"""Offline 1 Mbps Norman GFSK decoder for Pluto little-endian int16 IQ.

Searches all integer sample/bit phases and both discriminator polarities.
Only 30-byte frames passing the independent application CRC are accepted.
No radio transmission, payload guessing, or correction of corrupt bits.
"""

import argparse
import json
from pathlib import Path

import numpy as np

ADDRESS = bytes.fromhex('dc5c9c1c05')


def crc16(data):
    value = 0xacc8
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ (0x83 if value & 0x8000 else 0)) & 0xffff
    return value


def decode_block(iq, rate, sample_offset=0):
    sps = rate // 1000000
    discriminator = np.angle(iq[1:] * np.conj(iq[:-1]))
    found = []
    for phase in range(sps):
        usable = (len(discriminator) - phase) // sps * sps
        symbols = discriminator[phase:phase + usable].reshape(-1, sps).mean(axis=1)
        for polarity in (1, -1):
            bits = (symbols * polarity) > 0
            for shift in range(8):
                packed = np.packbits(bits[shift:], bitorder='big').tobytes()
                position = packed.find(ADDRESS)
                while position >= 0:
                    frame = packed[position + 5:position + 35]
                    if (len(frame) == 30 and frame[0] == 0x18 and
                            crc16(frame[:28]) == int.from_bytes(frame[28:], 'big')):
                        sample = sample_offset + phase + (shift + position * 8) * sps
                        start = sample - sample_offset
                        segment = iq[max(0, start):start + 280 * sps]
                        observed = discriminator[max(0, start):start + 280 * sps]
                        q10, q90 = np.percentile(observed, [10, 90]) * rate / (2 * np.pi)
                        found.append({
                            'sample': sample, 'time_s': sample / rate,
                            'payload': frame.hex(), 'polarity': polarity,
                            'rms_adc': float(np.sqrt(np.mean(abs(segment) ** 2))),
                            'frequency_q10_hz': float(q10), 'frequency_q90_hz': float(q90),
                        })
                    position = packed.find(ADDRESS, position + 1)
    return found


def decode_file(path, rate):
    if path.stat().st_size % 4:
        raise ValueError('IQ file does not contain complete int16 I/Q pairs')
    raw = np.memmap(path, dtype='<i2', mode='r').reshape(-1, 2)
    found = []
    block = 1000000
    overlap = 4096
    for offset in range(0, len(raw), block):
        pair = raw[offset:offset + block + overlap].astype(np.float32)
        iq = pair[:, 0] + 1j * pair[:, 1]
        found.extend(decode_block(iq, rate, offset))
    found.sort(key=lambda item: item['sample'])
    packets = []
    for item in found:
        if any(item['payload'] == old['payload'] and abs(item['sample'] - old['sample']) < 16
               for old in packets[-10:]):
            continue
        packets.append(item)
    return packets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--rate', type=int, default=4000000, choices=[2000000, 4000000, 8000000])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    packets = decode_file(args.source, args.rate)
    report = {'source': str(args.source), 'sample_rate': args.rate,
              'duration_s': args.source.stat().st_size / 4 / args.rate,
              'crc_valid_packets': len(packets), 'packets': packets}
    with args.output.open('x') as output:
        json.dump(report, output, indent=2)
    unique = {}
    for packet in packets:
        unique[packet['payload']] = unique.get(packet['payload'], 0) + 1
    print(json.dumps({'crc_valid_packets': len(packets), 'distinct_frames': unique,
                      'report': str(args.output)}, indent=2))


if __name__ == '__main__':
    main()
