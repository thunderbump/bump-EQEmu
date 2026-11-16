"""Regenerate the small public receipt from retained private runtime artifacts."""
from collections import Counter
import json
from pathlib import Path

ROOT=Path.home()/'.local/state/eqemu-map-translation/server-collision-npc-completion-20261115'
rows=[]
for zone,label,prefix in [('qeynos2','native-v5',''),('poknowledge','native-v3','poknowledge-')]:
    for arm in ('OFF','ON','OFF-repeat'):
        out=ROOT/(prefix+arm+'-'+label)
        inputs=json.loads((out/'inputs.json').read_text())
        receipt=json.loads((out/'results/smoke.json').read_text())
        route=receipt.get('route',{})
        rows.append({'zone':zone,'arm':arm,'artifact':out.name,
            'harness_commit':inputs['harness_commit'],'binary_source':inputs['binary_source'],
            'executables':receipt['executables'],
            'worker_exit':json.loads((out/'exit.json').read_text())['exit'],
            'verdict':json.loads((out/'verdict.json').read_text()),'exits':receipt['exits'],
            'removed':bool(json.loads((out/'cleanup.json').read_text())['removed']),
            'timer_retired':receipt.get('probe_quiesced',False),
            'memory':receipt['memory_after'],
            'queries':{k:{f:r[f] for f in ('ground','los')} for k,r in receipt.get('queries',{}).items()},
            'native_records':len(route.get('native',[])),
            'callers':dict(Counter(r['caller']+'/'+r['kind'] for r in route.get('native',[]))),
            'floors':route.get('floors'), 'poses':len(route.get('poses',[])),
            'final_pose':route.get('poses',[None])[-1],
            'observed_offsets':sorted({r['result'] for r in route.get('native',[]) if r['caller']=='FixZ' and r['kind']=='pose'})})
print(json.dumps({'artifact_root':str(ROOT),'cases':rows},indent=2))
raise SystemExit(0 if all(r['worker_exit']==0 and all(r['verdict'].values()) and r['removed'] and r['timer_retired'] for r in rows) else 1)
