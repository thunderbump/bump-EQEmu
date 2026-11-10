"""Separate numeric diagnostics. Structural differences always reject. No acceptance epsilon."""
import math
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from mesh import read, compare

def diagnose(reference,candidate):
    a,b=read(reference),read(candidate)
    if a[0]!=b[0] or len(a[1])!=len(b[1]) or a[2]!=b[2]:
        raise ValueError('structural mismatch: identity/counts/ordered indices')
    try: compare(reference,candidate); exact=True
    except ValueError: exact=False
    def f(w): return struct.unpack('<f',struct.pack('<I',w))[0]
    def order(w): return 0x80000000-(w&0x7fffffff) if w>>31 else 0x80000000+w
    diffs=[]; nonfinite=[]; zeros=0
    for n,(x,y) in enumerate(zip(a[1],b[1])):
        u,v=f(x),f(y)
        if not math.isfinite(u) or not math.isfinite(v):
            nonfinite.append({'coordinate':n,'reference_bits':x,'candidate_bits':y}); continue
        if x==y: continue
        if u==v==0: zeros+=1
        diffs.append({'coordinate':n,'vertex':n//3,'axis':'xyz'[n%3], 'reference':u,'candidate':v,
                      'absolute':abs(v-u),'relative':abs(v-u)/abs(u) if abs(u)>=1e-6 else None,
                      'ulp':abs(order(x)-order(y))})
    def quantile(key,q):
        values=sorted(d[key] for d in diffs if d[key] is not None)
        return values[min(len(values)-1,int(q*(len(values)-1)))] if values else 0
    bounds=[]
    for mesh in (a,b):
        axes=[[f(w) for w in mesh[1][axis::3] if math.isfinite(f(w))] for axis in range(3)]
        bounds.append([[min(values),max(values)] if values else None for values in axes])
    return {'exact':exact,'vertices':len(a[1])//3,'indices':len(a[2]),'ordered_indices_equal':True,
            'differing_coordinates':len(diffs),'coordinate_fraction':len(diffs)/len(a[1]) if a[1] else 0,
            'differing_vertices':len(set(d['vertex'] for d in diffs)),
            'vertex_fraction':len(set(d['vertex'] for d in diffs))/(len(a[1])//3) if a[1] else 0,
            'max_absolute':quantile('absolute',1),'median_absolute':quantile('absolute',.5),'p99_absolute':quantile('absolute',.99),
            'max_relative_above_1e_minus_6':quantile('relative',1),'max_ulp':quantile('ulp',1),
            'signed_zero_changes':zeros,'nonfinite':nonfinite,'bounds':bounds,'bounds_equal':bounds[0]==bounds[1],
            'coordinate_range':[min(d['coordinate'] for d in diffs),max(d['coordinate'] for d in diffs)] if diffs else None,
            'worst_examples':sorted(diffs,key=lambda d:d['absolute'],reverse=True)[:12],
            'topology':'same ordered index words, counts and vertex correspondence; geometric degeneracy/collision behavior not assessed'}
