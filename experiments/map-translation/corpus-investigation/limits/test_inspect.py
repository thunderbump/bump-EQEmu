"""Small executable fixtures, run only inside the disposable worker."""
import hashlib
import struct
import tempfile
import unittest
from pathlib import Path
from inspect_mesh import inspect


class InspectorTests(unittest.TestCase):
    def run_mesh(self, vertices=None, indices=(0, 1, 2), mutate=None, pin=True):
        vertices = vertices or [(0., 1., 2.), (-3., 4., 5.), (6., -7., 8.), (9., 9., 9.)]
        raw = struct.pack('<8sIII64s', b'EQMESH01', 1, len(vertices), len(indices), b'a' * 64)
        raw += b''.join(struct.pack('<3f', *v) for v in vertices)
        raw += b''.join(struct.pack('<I', i) for i in indices)
        case = {'input': {'path': 'base/fixture.map', 'sha256': 'a' * 64},
                'geometry': {'vertices': len(vertices), 'indices': len(indices),
                             'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}}
        if mutate:
            raw = mutate(raw)
            if pin:
                case['geometry'].update(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'fixture.mesh'
            p.write_bytes(raw)
            return inspect(p, case)

    def test_valid(self):
        r = self.run_mesh()
        self.assertEqual(r['bounds_min'], [-3., -7., 2.])
        self.assertEqual(r['bounds_max'], [9., 9., 9.])
        self.assertEqual((r['referenced_vertices'], r['unreferenced_vertices']), (3, 1))
        self.assertEqual((r['min_index'], r['max_index']), (0, 2))
        self.assertEqual(r['nonfinite_components'], 0)

    def test_repeated_indices(self):
        self.assertEqual(self.run_mesh(indices=(0, 0, 2))['repeated_index_triangles'], 1)

    def test_range_and_nonfinite_are_reported(self):
        r = self.run_mesh(vertices=[(float('nan'), 2e8, 0.), (0., 0., 0.), (1., 1., 1.)])
        self.assertEqual(r['nonfinite_components'], 1)
        self.assertEqual(r['components_outside_core_range'], 1)

    def test_bad_index(self):
        with self.assertRaisesRegex(ValueError, 'index out of bounds'):
            self.run_mesh(indices=(0, 1, 4))

    def test_identity_hash(self):
        with self.assertRaisesRegex(ValueError, 'identity or size'):
            self.run_mesh(mutate=lambda b: b[:-1] + bytes([b[-1] ^ 1]), pin=False)

    def test_header_schema(self):
        with self.assertRaisesRegex(ValueError, 'header/count/identity'):
            self.run_mesh(mutate=lambda b: b'BADMAGIC' + b[8:])

    def test_header_input_identity(self):
        with self.assertRaisesRegex(ValueError, 'header/count/identity'):
            self.run_mesh(mutate=lambda b: b[:20] + b'b' * 64 + b[84:])

    def test_non_triangular(self):
        with self.assertRaisesRegex(ValueError, 'header/count/identity'):
            self.run_mesh(indices=(0, 1))

    def test_truncated(self):
        with self.assertRaisesRegex(ValueError, 'header/count/identity'):
            self.run_mesh(mutate=lambda b: b[:-1])

    def test_trailing(self):
        with self.assertRaisesRegex(ValueError, 'header/count/identity'):
            self.run_mesh(mutate=lambda b: b + b'x')


if __name__ == '__main__':
    unittest.main(verbosity=2)
