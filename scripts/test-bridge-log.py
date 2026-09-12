"""Offline tests for passive logging; no hardware or network access."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('bridge_log', Path(__file__).with_name('bridge-log.py'))
logger = importlib.util.module_from_spec(spec)
spec.loader.exec_module(logger)


class FakeClient:
    # Deliberately provides no execute_service/movement API.
    def __init__(self, *args, **kwargs):
        self.disconnected = False
        self.fail = False

    async def connect(self, **kwargs):
        if self.fail:
            raise RuntimeError('connection failure')

    async def disconnect(self):
        self.disconnected = True

    def subscribe_logs(self, callback, **kwargs):
        if kwargs.get('dump_config') is not False:
            raise AssertionError('Full configuration must not be requested')
        callback(SimpleNamespace(message=b'RX synthetic'))
        callback(SimpleNamespace(message=b'RX second'))


class LoggerTests(unittest.IsolatedAsyncioTestCase):
    async def run_record(self, cap=4096, fail=False):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'record.jsonl'
            client = FakeClient()
            client.fail = fail
            args = SimpleNamespace(host='example.invalid', expected_name='test',
                                   output=path, max_bytes=cap, seconds=0.01)
            output = io.StringIO()
            with patch.object(logger.aioesphomeapi, 'APIClient', return_value=client), contextlib.redirect_stdout(output):
                if fail:
                    with self.assertRaisesRegex(RuntimeError, 'connection failure'):
                        await logger.record(args)
                else:
                    await logger.record(args)
            self.assertTrue(client.disconnected)
            if not fail:
                reported = json.loads(output.getvalue().splitlines()[-1])
                self.assertEqual(path.stat().st_size, reported['bytes'])
            return path.read_text(encoding='utf-8'), output.getvalue()

    async def test_passive_timestamped_records(self):
        data, console = await self.run_record()
        records = [json.loads(line) for line in data.splitlines()]
        self.assertEqual([r['message'] for r in records], ['RX synthetic', 'RX second'])
        self.assertTrue(all(isinstance(r['host_time'], float) for r in records))
        self.assertNotIn('RX synthetic', console)

    async def test_byte_cap(self):
        data, console = await self.run_record(cap=1)
        self.assertEqual(data, '')
        self.assertTrue(json.loads(console.splitlines()[-1])['limit_reached'])

    async def test_connection_failure_disconnects(self):
        await self.run_record(fail=True)

    async def test_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'existing'
            path.write_text('original', encoding='utf-8')
            args = SimpleNamespace(host='example.invalid', expected_name='test', output=path)
            with patch.object(logger.aioesphomeapi, 'APIClient', FakeClient):
                with self.assertRaises(FileExistsError):
                    await logger.record(args)
            self.assertEqual(path.read_text(encoding='utf-8'), 'original')


if __name__ == '__main__':
    unittest.main()
