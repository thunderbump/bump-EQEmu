"""Run inside the worker. Expected rotations use independent f64 matrix math."""
import hashlib
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from test_capture import CaptureTests, floats, words
from mesh import read

class RustTests(CaptureTests):
    def fixture_rotation(self, r):
        base = floats(1, 2, 3, 4, 6, 8, 9, 12, 15)
        header = words(0, 0, 0, 0, 1, 1, 0, 0, 0) + floats(1)
        model = b'\xff\0' + words(3, 1) + base + words(2, 0, 1) + b'\2'
        placement = b'\xff\0' + floats(10, 20, 30, *r, 2, 3, 4)
        return header + model + placement

    def test_rotations(self):
        for r in [(0, 0, 0.5), (0.4, -0.7, 1.1)]:
            with self.subTest(r=r):
                output = self.capture(self.fixture_rotation(r))
                _, bits, indices = read(output)
                actual = struct.unpack('<9f', struct.pack('<9I', *bits))
                # Explicit column-vector matrices: Rz * Ry * Rx, then scale,
                # translation and axis exchange. Inputs are rounded to file f32.
                rx, ry, rz = struct.unpack('<3f', floats(*r))
                cx,sx,cy,sy,cz,sz = math.cos(rx),math.sin(rx),math.cos(ry),math.sin(ry),math.cos(rz),math.sin(rz)
                matrix = [[cz*cy, cz*sy*sx-sz*cx, cz*sy*cx+sz*sx],
                          [sz*cy, sz*sy*sx+cz*cx, sz*sy*cx-cz*sx],
                          [-sy, cy*sx, cy*cx]]
                expected=[]
                for v in [(9,12,15),(1,2,3),(4,6,8)]:
                    x,y,z=[sum(a*b for a,b in zip(row,v)) for row in matrix]
                    expected += [y*3+20,x*2+10,z*4+30]
                for a,b in zip(actual,expected): self.assertLess(abs(a-b), 0.00002)
                self.assertEqual(indices,(0,1,2))
                output.unlink()

    def test_duplicate_missing_and_raw_names(self):
        base=floats(1,2,3,4,6,8,9,12,15)
        model=b'\xff\0'+words(3,1)+base+words(2,0,1)+b'\1'
        # Second definition hides polygon. Missing placement is fully consumed.
        payload=words(0,0,0,0,2,2,0,0,0)+floats(1)+model+model[:-1]+b'\0'
        payload+=b'missing\0'+floats(*([0]*6+[1]*3))+b'\xff\0'+floats(*([0]*6+[1]*3))
        _, v, i=read(self.capture(payload)); self.assertEqual((v,i),((),()))

    def test_parser_errors(self):
        import zlib
        valid=self.fixture_rotation((0,0,0))
        cases=[valid[:n] for n in (0,1,39,40,41,len(valid)-1)]
        for offset,value in [(0,0xffffffff),(4,1),(24,1),(28,1),(40+2,0xffffffff)]:
            b=bytearray(valid); b[offset:offset+4]=words(value); cases.append(bytes(b))
        cases += [valid+b'X', valid[:40]+b'unterminated']
        for n,payload in enumerate(cases):
            packed=zlib.compress(payload)
            source=self.root/'bad.map'; source.write_bytes(words(0x02000000,len(packed),len(payload))+packed)
            out=self.root/f'bad{n}.mesh'
            result=subprocess.run([self.binary,str(source),str(out),hashlib.sha256(source.read_bytes()).hexdigest()],capture_output=True)
            self.assertEqual(result.returncode,1,result.stderr); self.assertFalse(out.exists())
        packed=zlib.compress(valid)
        files=[words(0x01000000),words(0x02000000,len(packed),0xffffffff)+packed,
               words(0x02000000,len(packed),len(valid)+1)+packed,
               words(0x02000000,len(packed),len(valid))+packed[:-1],
               words(0x02000000,len(packed),len(valid))+bytes(len(packed))]
        for data in files:
            source=self.root/'bad.map'; source.write_bytes(data)
            result=subprocess.run([self.binary,str(source),str(self.root/'bad.mesh'),hashlib.sha256(data).hexdigest()],capture_output=True)
            self.assertEqual(result.returncode,1)

if __name__ == '__main__': unittest.main(verbosity=2)
