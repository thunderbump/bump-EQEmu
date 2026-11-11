"""Host-only verified OCI transport into a separately owned rootless daemon.

No store clone or retained tar archive. All immutable blobs are verified before
streaming the OCI tar to docker load. The existing proof store is read-only.
"""
import argparse
import hashlib
import io
import json
import subprocess
import tarfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--socket', required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
docker = ['docker', '--host', a.socket]
security = subprocess.check_output(docker + ['info', '--format', '{{json .SecurityOptions}}'], text=True)
assert 'name=rootless' in json.loads(security)
blobs = Path.home() / '.local/state/eqemu-rootless-proof/docker/containerd/daemon/io.containerd.content.v1.content/blobs/sha256'
digest = '0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d'
manifest_bytes = (blobs / digest).read_bytes()
assert hashlib.sha256(manifest_bytes).hexdigest() == digest
manifest = json.loads(manifest_bytes)
desc = [{'digest': 'sha256:' + digest, 'size': len(manifest_bytes)}, manifest['config'], *manifest['layers']]
for d in desc:
    name = d['digest'].removeprefix('sha256:')
    assert len(name) == 64 and all(c in '0123456789abcdef' for c in name)
    path = blobs / name
    with path.open('rb') as f:
        assert hashlib.file_digest(f, 'sha256').hexdigest() == name
    assert path.stat().st_size == d['size']
with a.output.open('x') as f:
    json.dump({'manifest': digest, 'verified_blobs': desc, 'transport': 'streamed OCI tar; no store clone'}, f, indent=2)
proc = subprocess.Popen(docker + ['load'], stdin=subprocess.PIPE)
try:
    with tarfile.open(fileobj=proc.stdin, mode='w|') as tar:
        def add(name, data):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
        add('oci-layout', b'{"imageLayoutVersion":"1.0.0"}')
        add('index.json', json.dumps({'schemaVersion': 2, 'manifests': [{
            'mediaType': 'application/vnd.oci.image.manifest.v1+json',
            'digest': 'sha256:' + digest, 'size': len(manifest_bytes),
            'annotations': {'org.opencontainers.image.ref.name': 'limits-offline'}}]}).encode())
        for name in dict.fromkeys(d['digest'].split(':')[1] for d in desc):
            tar.add(blobs / name, arcname='blobs/sha256/' + name, recursive=False)
    proc.stdin.close()
    assert proc.wait(timeout=300) == 0
except BaseException:
    proc.kill()
    proc.wait()
    raise
print(subprocess.check_output(docker + ['image', 'inspect', 'sha256:' + digest], text=True))
