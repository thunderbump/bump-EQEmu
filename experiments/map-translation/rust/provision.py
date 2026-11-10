"""Host transport: provision Rust dependencies in a disposable rootless image."""
import argparse, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--socket', required=True); p.add_argument('--image', required=True); p.add_argument('--output', type=Path, required=True)
a=p.parse_args(); a.output.mkdir(parents=True, exist_ok=False)
d=['docker','--host',a.socket]
assert 'name=rootless' in json.loads(subprocess.check_output(d+['info','--format','{{json .SecurityOptions}}']))
cid=subprocess.check_output(d+['create','--memory','2g','--cpus','2','--entrypoint','/bin/bash',a.image,'-lc','set -eu; apt-get update; apt-get install -y --no-install-recommends time; mkdir -p /results; dpkg-query -W time > /results/time-package.txt; cd /crate; if [ ! -f Cargo.lock ]; then cargo generate-lockfile; fi; cargo fetch --locked; cp Cargo.lock /results/; rustc -Vv > /results/rustc.txt'],text=True).strip()
try:
 subprocess.run(d+['cp',str(Path(__file__).resolve().parent)+'/.',cid+':/crate'],check=True)
 with (a.output/'provision.log').open('w') as f: subprocess.run(d+['start','-a',cid],stdout=f,stderr=subprocess.STDOUT,check=True)
 assert subprocess.check_output(d+['inspect','--format','{{.State.ExitCode}}',cid]).strip()==b'0'
 subprocess.run(d+['cp',cid+':/results',str(a.output)],check=True)
 image=subprocess.check_output(d+['commit',cid],text=True).strip()
 (a.output/'image.txt').write_text(image+'\n')
finally:
 subprocess.run(d+['rm','-f',cid],check=True)
