import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
(ROOT / "build").mkdir(exist_ok=True)
sys.path.insert(0, str(ROOT / "tools"))
import can_to_inc as replay
import generate_profile as profile


class ConverterTests(unittest.TestCase):
    def read(self, content, suffix=".csv", **kwargs):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            path = Path(directory) / ("input" + suffix)
            path.write_text(content, encoding="utf-8")
            return replay.read_records(path, **kwargs)

    def test_csv_timing_dlc_extended_and_rtr(self):
        records = self.read(
            'timestamp_ms,can_id,dlc,data,extended,rtr\n'
            '100,0x301,7,00 01 02 03 04 05 06,false,false\n'
            '110,0x123,0,,true,false\n'
            '110,0x1234567,8,,true,true\n')
        result = replay.render(records, 25)
        self.assertIn('{0U, {0x301U, 7,', result)
        self.assertIn('{10U, {0x123U, 0,', result)
        self.assertIn('true, true', result)
        self.assertIn('loopGapMs = 25U', result)

    def test_txt_actual_excerpt_preserves_bytes_and_lengths(self):
        records = replay.read_records(ROOT / "examples/motor_excerpt.txt")
        self.assertEqual(len(records), 49)
        self.assertEqual(records[-1].timestamp_ms - records[0].timestamp_ms, 1805)
        self.assertTrue(any(r.can_id == 0x301 and r.dlc == 7 for r in records))
        expected = replay.render(records, 190)
        self.assertEqual(expected, (ROOT / "include/recording.inc").read_text())
        source_lines = (ROOT / "CAN SIM/CAN SIM/Some CAN DATA FROM MOTOR to use as mock data.txt").read_text().splitlines()
        excerpt = (ROOT / "examples/motor_excerpt.txt").read_text().splitlines()[1:]
        self.assertEqual(excerpt, source_lines[125:174])

    def test_invalid_csv_rows_rejected(self):
        bad_rows = [
            '0,0x800,1,00,false,false', '0,0x20000000,1,00,true,false',
            '0,0x301,9,00,false,false', '0,0x301,2,00,false,false',
            '0,0x301,1,GG,false,false', '0,0x301,1,100,false,false',
            '0,0x301,1,0,false,false', '-1,0x301,1,00,false,false',
            '0,0x301,1,00,maybe,false', '0,0x301,1,00,false,true',
            'NaN,0x301,1,00,false,false', '0,0x301,1,00,false,false,extra',
        ]
        header = 'timestamp_ms,can_id,dlc,data,extended,rtr\n'
        for row in bad_rows:
            with self.subTest(row=row), self.assertRaises(ValueError):
                self.read(header + row + '\n')

    def test_order_and_duration(self):
        for times in [(10, 9), (0, 4294967296)]:
            with self.subTest(times=times), self.assertRaises(ValueError):
                self.read('timestamp_ms,can_id,dlc,data\n' +
                          f'{times[0]},0x1,0,\n{times[1]},0x1,0,\n')
        records = self.read('timestamp_ms,can_id,dlc,data\n9999999999,0x1,0,\n10000000000,0x1,0,\n')
        self.assertIn('{1U,', replay.render(records))

    def test_metadata_and_missing_emcy_payload_are_explicit(self):
        text = 'Connected to CAN adapter\n0x301 100 PDO { 0x01 }\n'
        text += '0x081 101 EMCY Error Reset. Sevcon code: 0x4681\n'
        with self.assertRaises(ValueError):
            self.read(text, '.txt')
        self.assertEqual(len(self.read(text, '.txt', skip_decoded_events=True)), 1)
        with self.assertRaises(ValueError):
            self.read('0x301 0 PDO { 0xGG }\n', '.txt', ignore_nonframes=True)

    def test_headers_and_empty_input(self):
        for text in ['', 'timestamp_ms,can_id,dlc,data\n',
                     'timestamp_ms,can_id,dlc,data,data\n',
                     'timestamp_ms,can_id,dlc,data,typo\n']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.read(text)

    def test_cli_failure_leaves_previous_output(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            src, dst = Path(directory) / 'bad.csv', Path(directory) / 'out.inc'
            src.write_text('timestamp_ms,can_id,dlc,data\n0,0x301,8,00\n')
            dst.write_text('keep previous recording')
            process = subprocess.run([sys.executable, str(ROOT / 'tools/can_to_inc.py'),
                                      str(src), str(dst)], capture_output=True)
            self.assertEqual(process.returncode, 1)
            self.assertEqual(dst.read_text(), 'keep previous recording')


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.vehicle = {"messages": [{
            "id": "0x100", "dlc": 1, "interval_ms": 1000,
            "default_data": [5], "checksum": "none", "source": "Synthetic test fixture",
            "signals": [{"name": "TEST_STATE", "byte": 0, "width": 1,
                         "endian": "big", "signed": False, "scale": 1,
                         "offset": 0, "min": 0, "max": 255, "default": 5}]
        }]}

    def test_reproducible_profile(self):
        vehicle = json.loads((ROOT / 'config/vehicle.json').read_text())
        self.assertEqual(vehicle['messages'], [])
        self.assertEqual(profile.generate(vehicle), (ROOT / 'include/vehicle_profile.h').read_text())

    def test_invalid_profile_rejected(self):
        cases = []
        for field, value in [('dlc', 9), ('interval_ms', 0), ('default_data', []),
                             ('checksum', 'guess'), ('id', '0x800')]:
            p = copy.deepcopy(self.vehicle); p['messages'][0][field] = value; cases.append(p)
        for field, value in [('scale', 0), ('scale', float('nan')), ('width', 2),
                             ('default', 256), ('endian', 'guess'), ('signed', 1),
                             ('name', 'BAD"NAME'), ('min', -1), ('rate_per_second', float('inf'))]:
            p = copy.deepcopy(self.vehicle); p['messages'][0]['signals'][0][field] = value; cases.append(p)
        p = copy.deepcopy(self.vehicle); p['messages'] *= 2; cases.append(p)
        p = copy.deepcopy(self.vehicle); p['messages'][0]['signals'] *= 2; cases.append(p)
        p = copy.deepcopy(self.vehicle); p['messages'][0]['signals'][0]['rate_per_sec'] = 5; cases.append(p)
        for p in cases:
            with self.subTest(p=p), self.assertRaises(ValueError):
                profile.generate(p)

    def test_checksum_requires_verification(self):
        p = copy.deepcopy(self.vehicle)
        m = p['messages'][0]; m['dlc'] = 8; m['default_data'] = [0] * 8
        m['checksum'] = 'sum_id_dlc'
        with self.assertRaises(ValueError):
            profile.generate(p)
        m['checksum_source'] = 'Synthetic test vector, not vehicle protocol'
        self.assertIn('Checksum::SumIdDlc', profile.generate(p))


if __name__ == '__main__':
    unittest.main()
