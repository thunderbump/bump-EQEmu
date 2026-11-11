"""Host-only compact aggregation of checkpointed evidence. Does not run engines."""
import argparse, collections, json, statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
s=json.loads((a.run/'summary.json').read_text()); identity=json.loads((a.run/'identity.json').read_text()); assets=s['assets']
assert len(assets)==s['declared']==len({x['input']['path'] for x in assets}), 'incomplete or duplicate corpus'
counts=collections.Counter(); stages=collections.Counter(); totals=collections.Counter(); declarations=collections.Counter(); rows=[]; peaks=[]; times=[]; removed=0
for x in assets:
    key=('supplemental' if x['input']['path'].startswith('legacy/') else 'base')+'/'+x['input']['version']+'/'+x['outcome'];counts[key]+=1
    stages[x.get('failure_category',x.get('stage','tooling'))]+=1
    n=x.get('numeric',{}); totals.update({k:n.get(k,0) for k in ('queries','hits','misses','classification_changes','field_bit_changes','signed_zero','nonfinite')})
    for k,v in x.get('header_declarations',{}).items():
        if k!='proof_of_branch_execution' and v: declarations[k]+=1
    peak=max([int(t['after']['memory.peak']) for t in x.get('stages',[])]+[0]);peaks.append(peak);times.append(x['seconds'])
    removed+=sum(z['bytes'] for z in x.get('removed',[]))
    rows.append({'path':x['input']['path'],'bytes':x['input']['bytes'],'sha256':x['input']['sha256'],'version':x['input']['version'],'outcome':x['outcome'],'stage':x.get('stage'),'failure_category':x.get('failure_category'),'error':x.get('error',x.get('tooling_error')),'geometry':x.get('geometry'),'queries':n.get('queries',0),'hits':n.get('hits',0),'misses':n.get('misses',0),'classification_changes':n.get('classification_changes',0),'field_bit_changes':n.get('field_bit_changes',0),'seconds':x['seconds'],'cgroup_peak_bytes':peak,'retained_bytes_at_checkpoint':x['retained_bytes']})
def distribution(xs):
    ys=sorted(xs);return {'min':min(ys),'median':statistics.median(ys),'p95':ys[int(.95*(len(ys)-1))],'max':max(ys)}
report={'executed_source':identity['source'],'image':identity['image'],'declared':s['declared'],'processed':s['processed'],'outcomes':s['outcomes'],'by_family_format_outcome':dict(counts),'stage_counts':dict(stages),'totals':dict(totals),'header_declarations_not_execution_proof':dict(declarations),'seconds':distribution(times),'cgroup_peak_bytes':distribution(peaks),'worst_time':sorted(rows,key=lambda x:x['seconds'],reverse=True)[:5],'worst_memory':sorted(rows,key=lambda x:x['cgroup_peak_bytes'],reverse=True)[:5],'successful_derived_bytes_removed':removed,'retained_asset_bytes':sum(p.stat().st_size for p in (a.run/'assets').rglob('*') if p.is_file()),'assets':rows,'limits':['Installed assets only; database overrides unaudited','Header census does not prove parser branch execution','Ordinary comparisons only; no corpus sanitizer qualification','Shared C++ decoder means decoder correctness needs independent oracle','No CXX lifetime, Map/cache or game integration qualification']}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('assets','worst_time','worst_memory')},indent=2))
