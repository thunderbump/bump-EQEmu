#!/usr/bin/env python3
"""Exercise interruption/recovery at the real worker command boundary."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKER = ROOT / 'scripts/validation-worker.sh'


class LifetimeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='validation-lifetime-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'source'
        (self.source / 'scripts').mkdir(parents=True)
        validate = self.source / 'scripts/validate.sh'
        validate.write_text('''#!/usr/bin/env python3
import json, os, pathlib, signal, time
if os.environ.get('MODE') == 'pass':
    raise SystemExit(0)
state = pathlib.Path(os.environ['DOCKER_STATE'])
resources = json.loads(state.read_text())
for kind in ('container', 'network', 'volume'):
    resources[kind+'-owned'] = dict(kind=kind, stopped=os.environ.get('STOPPED_CONTAINER')=='1', labels={'org.eqemu.validation': os.environ['VALIDATION_WORKER_LIFETIME_TOKEN']})
state.write_text(json.dumps(resources))
pathlib.Path(os.environ['VALIDATION_WORKER_DOCKER_MARKER']).touch()
pathlib.Path(os.environ['MARKER']).write_text(str(os.getpid()))
if os.environ.get('MODE') == 'resist':
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
time.sleep(120)
''')
        validate.chmod(0o755)
        for args in [('init',), ('config','user.email','test@example.com'), ('config','user.name','test'), ('add','.'), ('commit','-m','test')]:
            self.git(*args)
        self.head = self.git('rev-parse','HEAD')
        self.worker = self.root / 'worker'
        self.stack = self.root / 'stack'
        self.stack.mkdir()
        (self.stack / '.env').write_text('ENV=development\n')
        (self.stack / 'code').symlink_to(self.source)
        self.marker = self.root / 'started'
        self.state = self.root / 'docker.json'
        self.foreign = {'shared-db': {'kind': 'container', 'labels': {'org.eqemu.validation': 'another-run'}}}
        self.state.write_text(json.dumps(self.foreign))
        fakebin = self.root / 'bin'
        fakebin.mkdir()
        docker = fakebin / 'docker'
        docker.write_text('''#!/usr/bin/env python3
import json, os, pathlib, sys
path=pathlib.Path(os.environ['DOCKER_STATE']); state=json.loads(path.read_text())
kind, action, *args = sys.argv[1:]
if os.environ.get('DOCKER_FAIL') == '1': raise SystemExit(1)
if action == 'ls':
    token=args[-1].split('=',2)[-1]
    print('\\n'.join(k for k,v in state.items() if v['kind']==kind and v['labels'].get('org.eqemu.validation')==token and (kind!='container' or not v.get('stopped') or '--all' in args)))
elif action == 'inspect':
    labels=state[args[-1]]['labels']
    print(json.dumps([{'Config': {'Labels': labels}} if kind=='container' else {'Labels':labels}]))
elif action == 'rm':
    if kind=='volume' and 'container-owned' in state: raise SystemExit(1)
    del state[args[-1]]; path.write_text(json.dumps(state))
else: raise SystemExit(2)
''')
        docker.chmod(0o755)
        self.env = {**os.environ, 'PATH': str(fakebin)+':'+os.environ['PATH'], 'VALIDATION_WORKER_HOME': str(self.worker), 'MARKER': str(self.marker), 'DOCKER_STATE': str(self.state)}
        self.processes = []
        self.groups = []
        self.addCleanup(self.kill_remaining)

    def git(self,*args):
        return subprocess.check_output(['git','-C',str(self.source),*args],stderr=subprocess.DEVNULL,text=True).strip()

    def kill_remaining(self):
        for group in self.groups:
            try: os.killpg(group, signal.SIGKILL)
            except ProcessLookupError: pass
        for process in self.processes:
            process.wait(timeout=10)

    def request(self, name, timeout=60):
        evidence = self.root / name
        request = self.root / (name+'.json')
        request.write_text(json.dumps(dict(project='bump-eqemu',repo=str(self.source),ref=self.head,commit=self.head,profile='tier1',run_id=name,evidence_dir=str(evidence),timeout_seconds=timeout,stack=dict(role='validation',path=str(self.stack)))))
        return request, evidence

    def start(self, name='first', timeout=60, mode='hang', extra=None):
        request, evidence = self.request(name,timeout)
        log=open(self.root/(name+'.log'),'wb');self.addCleanup(log.close)
        process=subprocess.Popen([str(WORKER),'run','--request',str(request)],env={**self.env,'MODE':mode,**(extra or {})},start_new_session=True,stdout=log,stderr=log)
        self.processes.append(process); self.groups.append(process.pid)
        return process,request,evidence

    def await_started(self):
        deadline=time.monotonic()+10
        while not self.marker.exists() and time.monotonic()<deadline: time.sleep(.02)
        self.assertTrue(self.marker.exists())
        group=os.getpgid(int(self.marker.read_text()));self.groups.append(group)
        return group

    def recover(self,request):
        return subprocess.run([str(WORKER),'recover','--request',str(request)],env=self.env,text=True,capture_output=True,timeout=15)

    def assert_clean(self):
        self.assertFalse((self.worker/'locks/validation-slot.lock').exists())
        self.assertFalse((self.stack/'.validation-worker-code.lock').exists())
        self.assertEqual(os.readlink(self.stack/'code'),str(self.source))
        self.assertEqual(json.loads(self.state.read_text()),self.foreign)

    def next_passes(self):
        process,_,evidence=self.start('next',mode='pass')
        self.assertEqual(process.wait(timeout=15),0,(self.root/'next.log').read_text())
        self.assertEqual(json.loads((evidence/'result.json').read_text())['status'],'passed')
        self.assert_clean()

    def test_outer_term_cleans_then_next_run_passes(self):
        process,_,evidence=self.start();self.await_started()
        os.killpg(process.pid,signal.SIGTERM)
        self.assertEqual(process.wait(timeout=15),143)
        self.assertEqual(json.loads((evidence/'result.json').read_text())['category'],'interrupted')
        self.assert_clean();self.next_passes()

    def test_stack_path_alias_restores_the_same_owned_binding(self):
        alias=self.root/'stack-alias';alias.symlink_to(self.stack,target_is_directory=True)
        self.stack=alias
        self.test_outer_term_cleans_then_next_run_passes()

    def test_deadline_kills_term_resistant_child(self):
        process,_,evidence=self.start(timeout=2,mode='resist');self.await_started()
        self.assertNotEqual(process.wait(timeout=15),0)
        self.assertEqual(json.loads((evidence/'result.json').read_text())['category'],'timeout')
        self.assert_clean();self.next_passes()

    def test_hard_kill_retains_live_child_lease_then_recovers(self):
        process,request,evidence=self.start();group=self.await_started()
        os.killpg(process.pid,signal.SIGKILL);process.wait(timeout=5)
        before=(evidence/'result.json').read_bytes()
        self.assertNotEqual(self.recover(request).returncode,0)
        self.assertEqual((evidence/'result.json').read_bytes(),before)
        self.assertTrue((self.stack/'.validation-worker-code.lock').exists())
        os.killpg(group,signal.SIGKILL);time.sleep(.2)
        result=self.recover(request)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((evidence/'result.json').read_bytes(),before)
        self.assert_clean();self.next_passes()

    def test_changed_binding_is_preserved_and_recovery_is_retryable(self):
        process,request,_=self.start();self.await_started()
        original=os.readlink(self.stack/'code')
        (self.stack/'code').unlink();(self.stack/'code').symlink_to('/operator-replacement')
        os.killpg(process.pid,signal.SIGTERM)
        self.assertEqual(process.wait(timeout=15),1)
        self.assertEqual(os.readlink(self.stack/'code'),'/operator-replacement')
        self.assertNotEqual(self.recover(request).returncode,0)
        (self.stack/'code').unlink();(self.stack/'code').symlink_to(original)
        result=self.recover(request)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assert_clean();self.next_passes()

    def test_cleanup_failure_keeps_leases_and_next_run_recovers(self):
        process,_,_=self.start(extra={'DOCKER_FAIL':'1'});self.await_started()
        os.killpg(process.pid,signal.SIGTERM)
        self.assertEqual(process.wait(timeout=15),1)
        self.assertTrue((self.worker/'locks/validation-slot.lock/owner.json').exists())
        self.next_passes()

    def test_another_worker_home_cannot_take_live_stack(self):
        process,_,_=self.start();self.await_started()
        before=os.readlink(self.stack/'code')
        second,_,evidence=self.start('other',mode='pass',extra={'VALIDATION_WORKER_HOME':str(self.root/'other-worker')})
        self.assertNotEqual(second.wait(timeout=15),0)
        self.assertEqual(json.loads((evidence/'result.json').read_text())['category'],'stack_busy')
        self.assertEqual(os.readlink(self.stack/'code'),before)
        os.killpg(process.pid,signal.SIGTERM);process.wait(timeout=15)
        self.assert_clean()

    def test_stopped_container_is_removed_before_its_volume(self):
        process,_,_=self.start(extra={'STOPPED_CONTAINER':'1'});self.await_started()
        os.killpg(process.pid,signal.SIGTERM)
        self.assertEqual(process.wait(timeout=15),143)
        self.assert_clean();self.next_passes()

    def test_cancel_during_lease_wait_does_not_launch_validation(self):
        process,_,_=self.start();self.await_started()
        request,evidence=self.request('waiting')
        value=json.loads(request.read_text());value['lock_wait_seconds']=120
        request.write_text(json.dumps(value))
        waiting=subprocess.Popen([str(WORKER),'run','--request',str(request)],env=self.env,start_new_session=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        self.processes.append(waiting);self.groups.append(waiting.pid)
        time.sleep(.3)
        os.killpg(waiting.pid,signal.SIGTERM)
        self.assertEqual(waiting.wait(timeout=3),143)
        self.assertFalse((evidence/'lifetime.json').exists())
        self.assertIsNone(process.poll())
        os.killpg(process.pid,signal.SIGTERM);process.wait(timeout=15)
        self.assert_clean()

    def test_hard_kill_at_lease_publication_and_retirement_is_recoverable(self):
        for point in ('publish', 'retire', 'restore'):
            with self.subTest(point=point):
                request,evidence=self.request(point)
                evidence.mkdir()
                checkout=self.source
                if point=='restore':
                    checkout=self.root/'candidate';checkout.mkdir()
                    (self.stack/'code').unlink();(self.stack/'code').symlink_to(checkout)
                    (evidence/'stack-binding.json').write_text(json.dumps(dict(restore_status='pending', code_path=str(self.stack/'code'),target=str(checkout),previous_target=str(self.source),previous_kind='symlink')))
                script = r"""
import importlib.util, os, pathlib, sys
spec=importlib.util.spec_from_file_location('lifetime',sys.argv[1]);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
point=sys.argv[2]; original=pathlib.Path.rename
count=0
def crash(self,target):
    global count
    result=original(self,target)
    if (point=='publish' and '.pending-' in self.name) or (point=='retire' and '.released-' in pathlib.Path(target).name):
        os._exit(99)
    return result
pathlib.Path.rename=crash
original_replace=pathlib.Path.replace
def crash_restore(self,target):
    if point=='restore' and self.name.startswith('.validation-restore-'):
        os._exit(99)
    return original_replace(self,target)
pathlib.Path.replace=crash_restore
sys.argv=['lifetime',*sys.argv[3:]]
module.main()
"""
                args=['python3','-c',script,str(ROOT/'scripts/validation-lifetime.py'),point,'--worker-home',str(self.worker),'--stack',str(self.stack),'--evidence',str(evidence),'--checkout',str(checkout),'--wait','0','--timeout','10','--','true']
                result=subprocess.run(args,env=self.env,capture_output=True,text=True,timeout=10)
                self.assertEqual(result.returncode,99,result.stderr)
                result=self.recover(request)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assert_clean()
        self.next_passes()

    def test_legacy_unowned_lock_is_never_deleted(self):
        lock=self.worker/'locks/validation-slot.lock';lock.mkdir(parents=True)
        process,_,_=self.start(mode='pass')
        self.assertNotEqual(process.wait(timeout=15),0)
        self.assertTrue(lock.is_dir())
        self.assertEqual(os.readlink(self.stack/'code'),str(self.source))


if __name__=='__main__':
    unittest.main()
