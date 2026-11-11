"""Host-only tooling controls; no native code execution."""
import json, tempfile, unittest
from pathlib import Path
from runner import census, cleanup, order, write
class Tooling(unittest.TestCase):
    def test_path_identity_and_complete_inventory(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t)
            names=['base/griegsend.map','base/qeynos2.map','base/poknowledge.map','base/arena.map','legacy/base/arena.map']
            for name in names:
                p=root/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(b'\0\0\0\2example')
            rows=census(root)
            self.assertEqual(len(rows),5); self.assertEqual(len({r['path'] for r in rows}),5)
            self.assertEqual([x['path'] for x in order(rows)][:3],names[:3])
            self.assertNotEqual(rows[0]['path'],rows[-1]['path'])
    def test_cleanup_preserves_inputs_reports_and_unowned(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t)
            for n in ('geometry.mesh','queries.bin','input.map','outcome.json','other.mesh'): (root/n).write_bytes(b'preserve')
            receipt=cleanup(root)
            self.assertEqual({r['name'] for r in receipt},{'geometry.mesh','queries.bin'})
            for n in ('input.map','outcome.json','other.mesh'): self.assertEqual((root/n).read_bytes(),b'preserve')
    def test_atomic_checkpoint_and_identity_sensitive_resume(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'identity.json'; expected={'source':'abc','inputs':[{'path':'base/a.map','sha256':'x'}]}
            write(p,expected); self.assertEqual(json.loads(p.read_text()),expected)
            for key in ('source','inputs'):
                changed=expected.copy(); changed[key]='changed'; self.assertNotEqual(json.loads(p.read_text()),changed)
            self.assertFalse(p.with_suffix('.tmp').exists())
if __name__=='__main__': unittest.main()
