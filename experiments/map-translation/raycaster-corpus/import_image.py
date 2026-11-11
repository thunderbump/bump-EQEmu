"""Verify retained immutable OCI blobs; import into corpus-owned daemon only."""
import hashlib,io,json,subprocess,tarfile
from pathlib import Path
root=Path.home()/'.local/state/eqemu-map-translation/raycaster-corpus-20261110'
blobs=Path.home()/'.local/state/eqemu-rootless-proof/docker/containerd/daemon/io.containerd.content.v1.content/blobs/sha256'
digest='0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d'
manifest=json.loads((blobs/digest).read_bytes()); desc=[{'digest':'sha256:'+digest,'size':(blobs/digest).stat().st_size},manifest['config'],*manifest['layers']]
archive=root/'image.tar'
with tarfile.open(archive,'w') as tar:
 def add(name,data):
  t=tarfile.TarInfo(name);t.size=len(data);tar.addfile(t,io.BytesIO(data))
 add('oci-layout',b'{"imageLayoutVersion":"1.0.0"}')
 add('index.json',json.dumps({'schemaVersion':2,'manifests':[{'mediaType':'application/vnd.oci.image.manifest.v1+json','digest':'sha256:'+digest,'size':desc[0]['size'],'annotations':{'org.opencontainers.image.ref.name':'rust-raycaster-offline'}}]}).encode())
 seen=set()
 for d in desc:
  name=d['digest'].split(':')[1]
  if name in seen:continue
  seen.add(name);p=blobs/name
  with p.open('rb') as f: actual=hashlib.file_digest(f,'sha256').hexdigest()
  assert actual==name and p.stat().st_size==d['size']
  tar.add(p,arcname='blobs/sha256/'+name,recursive=False)
with archive.open('rb') as f:sha=hashlib.file_digest(f,'sha256').hexdigest()
(root/'image-import.json').write_text(json.dumps({'manifest':digest,'blobs':desc,'archive_sha256':sha,'bytes':archive.stat().st_size},indent=2))
subprocess.run(['docker','--host','unix:///run/user/1000/eqemu-raycaster-corpus.sock','load','-i',str(archive)],check=True)
