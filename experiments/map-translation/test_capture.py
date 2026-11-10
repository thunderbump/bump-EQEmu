import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

from mesh import HEADER, compare, read


def floats(*values):
    return struct.pack('<' + 'f' * len(values), *values)


def words(*values):
    return struct.pack('<' + 'I' * len(values), *values)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.binary = os.environ['MAP_CAPTURE_BINARY']

    def tearDown(self):
        self.tmp.cleanup()

    def capture(self, payload):
        source = self.root / 'analytic.map'
        packed = zlib.compress(payload)
        source.write_bytes(words(0x02000000, len(packed), len(payload)) + packed)
        output = self.root / 'analytic.mesh'
        subprocess.run([self.binary, str(source), str(output), hashlib.sha256(source.read_bytes()).hexdigest()], check=True)
        return output

    def fixture(self):
        # One asymmetric base face; noncollidable geometry must disappear.
        base = floats(1, 2, 3, 4, 6, 8, 9, 12, 15)
        header = words(3, 3, 3, 3, 1, 2, 0, 0, 0) + floats(1)
        # Name length and 13-byte polygons force unaligned packed scalars.
        model = b'mm\0' + words(3, 2) + base
        model += words(2, 0, 1) + b'\1' + words(0, 1, 2) + b'\0'
        identity = b'mm\0' + floats(0, 0, 0, 0, 0, 0, 1, 1, 1)
        placed = b'mm\0' + floats(10, 20, 30, 0, 0, 0, 2, 3, 4)
        return header + base + words(2, 1, 0) + base + words(0, 1, 2) + model + identity + placed

    def test_analytic_base_placement_filtering(self):
        _, vertices, indices = read(self.capture(self.fixture()))
        # Hand computed in polygon order 2,0,1, with final x/y swap.
        expected = floats(1, 2, 3, 4, 6, 8, 9, 12, 15,
                          12, 9, 15, 2, 1, 3, 6, 4, 8,
                          56, 28, 90, 26, 12, 42, 38, 18, 62)
        self.assertEqual(vertices, struct.unpack('<27I', expected))
        self.assertEqual(indices, (2, 1, 0, 3, 4, 5, 6, 7, 8))

    def test_comparator_controls(self):
        ref = self.capture(self.fixture())
        original = ref.read_bytes()
        candidate = self.root / 'mutated.mesh'
        candidate.write_bytes(original)
        self.assertEqual(len(compare(ref, candidate)), 64)
        mutations = {}
        for label, offset in [('vertex', HEADER), ('index', HEADER + 9 * 12)]:
            altered = bytearray(original)
            altered[offset] ^= 1
            mutations[label] = altered
        altered = bytearray(original)
        offset = HEADER + 9 * 12
        altered[offset:offset + 24] = original[offset + 12:offset + 24] + original[offset:offset + 12]
        mutations['triangle order'] = altered
        mutations['truncated'] = original[:-1]
        altered = bytearray(original); altered[8] = 2
        mutations['schema'] = altered
        altered = bytearray(original); altered[20] = ord('0') if original[20] != ord('0') else ord('1')
        mutations['input identity'] = altered
        altered = bytearray(original); altered[offset:offset + 4] = words(99)
        mutations['bounds'] = altered
        for label, data in mutations.items():
            with self.subTest(label=label):
                candidate.write_bytes(data)
                with self.assertRaises(ValueError): compare(ref, candidate)
                result = subprocess.run(['python3', str(Path(__file__).with_name('mesh.py')), str(ref), str(candidate)], capture_output=True)
                self.assertEqual(result.returncode, 1, result.stderr)
                print(label, result.stderr.decode().strip())

    def test_basic_driver_errors(self):
        for data in (b'', words(123), words(0x02000000)):
            source = self.root / 'bad.map'; source.write_bytes(data)
            self.assertEqual(subprocess.run([self.binary, str(source), str(self.root / 'bad.mesh'), '0' * 64]).returncode, 1)
        self.assertNotEqual(subprocess.run([self.binary]).returncode, 0)
        self.assertNotEqual(subprocess.run([self.binary, '/missing', '/missing', '0' * 64]).returncode, 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
