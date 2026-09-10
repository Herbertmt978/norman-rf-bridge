"""Synthetic waveform tests; contains no installation identifiers."""
import runpy
from pathlib import Path
import unittest

import numpy as np

decoder = runpy.run_path(str(Path(__file__).with_name('decode-iq.py')))


class DecoderTests(unittest.TestCase):
    def waveform(self, corrupt=False, polarity=1):
        frame = bytearray([0x18] + list(range(1, 28)))
        frame.extend(decoder['crc16'](frame).to_bytes(2, 'big'))
        if corrupt:
            frame[10] ^= 1
        bits = np.unpackbits(np.frombuffer(b'\xaa' + decoder['ADDRESS'] + frame + b'\xff', dtype='u1'))
        frequency = polarity * (bits.astype(float) * 2 - 1) * 250000 + 25000
        phase = np.cumsum(np.repeat(frequency, 4) * 2 * np.pi / 4000000)
        rng = np.random.default_rng(72)
        iq = np.exp(1j * phase) + 0.04 * (rng.normal(size=len(phase)) + 1j * rng.normal(size=len(phase)))
        return np.concatenate((np.zeros(73), iq, np.zeros(101))), frame.hex()

    def test_crc_reference(self):
        self.assertEqual(decoder['crc16'](b'123456789'), 0xf473)

    def test_valid_frames_both_polarities(self):
        for polarity in (1, -1):
            iq, expected = self.waveform(polarity=polarity)
            found = decoder['decode_block'](iq, 4000000)
            self.assertTrue(any(item['payload'] == expected for item in found))

    def test_corrupt_frame_rejected(self):
        iq, _ = self.waveform(corrupt=True)
        self.assertEqual(decoder['decode_block'](iq, 4000000), [])


if __name__ == '__main__':
    unittest.main()
