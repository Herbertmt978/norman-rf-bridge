"""Offline guard tests for the generated-build-only tracker compatibility hook."""
from hashlib import sha256
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'esphome/components/norman_rf_monitor/ble_tracker_compat.py'
spec = importlib.util.spec_from_file_location('ble_tracker_compat', SCRIPT)
compat = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compat)


class CompatibilityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.version = self.root / 'src/esphome/core/version.h'
        self.header = self.root / 'src/esphome/components/esp32_ble_tracker/esp32_ble_tracker.h'
        self.version.parent.mkdir(parents=True)
        self.header.parent.mkdir(parents=True)
        self.version.write_text('#define ESPHOME_VERSION "2026.4.1"\n')
        self.original = 'class Tracker {\n' + compat.ORIGINAL + '\n};\n'
        self.header.write_text(self.original)
        self.patched = self.original.replace(compat.ORIGINAL, compat.REPLACEMENT)
        self.expected = patch.object(compat, 'EXPECTED', sha256(self.original.encode()).hexdigest())
        self.modified = patch.object(compat, 'PATCHED', sha256(self.patched.encode()).hexdigest())
        self.expected.start()
        self.modified.start()
        self.addCleanup(self.expected.stop)
        self.addCleanup(self.modified.stop)

    def test_generated_copy_only_and_idempotence(self):
        unrelated = self.root / 'installed_package.h'
        unrelated.write_text(self.original)
        self.assertTrue(compat.patch_project(self.root))
        self.assertEqual(self.header.read_text(), self.patched)
        self.assertFalse(compat.patch_project(self.root))
        self.assertEqual(unrelated.read_text(), self.original)

    def test_wrong_version_refused_without_mutation(self):
        self.version.write_text('#define ESPHOME_VERSION "2026.4.2"\n')
        with self.assertRaisesRegex(RuntimeError, 'exactly ESPHome'):
            compat.patch_project(self.root)
        self.assertEqual(self.header.read_text(), self.original)

    def test_unexpected_header_refused_without_mutation(self):
        changed = self.original + '// unknown change\n'
        self.header.write_text(changed)
        with self.assertRaisesRegex(RuntimeError, 'Unexpected ESPHome'):
            compat.patch_project(self.root)
        self.assertEqual(self.header.read_text(), changed)

    def test_missing_source_refused(self):
        self.header.unlink()
        with self.assertRaises(FileNotFoundError):
            compat.patch_project(self.root)

    def test_policy_change_is_the_only_wakeup(self):
        self.assertIn('if (this->scan_continuous_ != scan_continuous)', compat.REPLACEMENT)
        self.assertEqual(compat.REPLACEMENT.count('++this->state_version_'), 1)
        self.assertNotIn('start_scan', compat.REPLACEMENT)
        monitor = (SCRIPT.parent / 'norman_rf_monitor.cpp').read_text()
        self.assertNotIn('ble_tracker_->start_scan', monitor)


if __name__ == '__main__':
    unittest.main()
