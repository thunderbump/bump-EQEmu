#!/usr/bin/env python3
"""Opt-in Docker proof: a killed attach client cannot release a live actor's lease."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.environ.get('EQEMU_TEST_REAL_DOCKER') == '1', 'requires opt-in local Docker')
class ContainerLifetime(unittest.TestCase):
    def test_term_resistant_container_removed_before_lease_release(self):
        with tempfile.TemporaryDirectory(prefix='actor-container-proof-') as directory:
            root = Path(directory)
            evidence = root/'evidence'; evidence.mkdir()
            stack = root/'stack'; stack.mkdir()
            checkout = root/'checkout'; checkout.mkdir()
            previous = root/'previous'; previous.mkdir()
            (stack/'code').symlink_to(previous)
            name = 'actor-proof-' + uuid.uuid4().hex
            volume = name + '-data'
            script = root/'actor.py'
            script.write_text('''import json, os, pathlib, subprocess, sys, time
root=pathlib.Path(sys.argv[1]); name=sys.argv[2]; volume=name+'-data'
evidence=root/'evidence'; stack=root/'stack'; checkout=root/'checkout'
label='org.eqemu.validation='+os.environ['VALIDATION_WORKER_LIFETIME_TOKEN']
(evidence/'stack-binding.json').write_text(json.dumps(dict(restore_status='pending',code_path=str(stack/'code'),target=str(checkout),previous_kind='symlink',previous_target=str(root/'previous'))))
(stack/'code').unlink(); (stack/'code').symlink_to(checkout)
pathlib.Path(os.environ['VALIDATION_WORKER_DOCKER_MARKER']).touch()
subprocess.run(['docker','volume','create','--label',label,volume],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['docker','create','--name',name,'--label',label,'--network','none','--user','0:0','--mount','type=volume,src='+volume+',dst=/proof','--entrypoint','sh','eqemulator/eqemu-server:v16-dev','-c',"trap '' TERM; touch /proof/started; while :; do sleep 1; done"],check=True,stdout=subprocess.DEVNULL)
client=subprocess.Popen(['docker','start','--attach',name],stdout=subprocess.DEVNULL)
(root/'client').write_text(str(client.pid))
raise SystemExit(client.wait())
''')
            command = ['python3',str(ROOT/'scripts/validation-lifetime.py'),
                       '--worker-home',str(root/'worker'),'--stack',str(stack),
                       '--evidence',str(evidence),'--checkout',str(checkout),
                       '--wait','0','--timeout','45','--','python3',str(script),str(root),name]
            process = subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    ready=subprocess.run(['docker','exec',name,'test','-f','/proof/started'],capture_output=True)
                    if ready.returncode==0: break
                    time.sleep(.1)
                else: self.fail('actor container did not start')
                subprocess.run(['docker','kill','--signal','TERM',name],check=True,capture_output=True)
                time.sleep(.2)
                running=subprocess.check_output(['docker','inspect','--format','{{.State.Running}}',name],text=True).strip()
                self.assertEqual(running,'true')
                self.assertTrue((stack/'.validation-worker-code.lock').exists())
                os.kill(int((root/'client').read_text()),signal.SIGKILL)
                _,stderr=process.communicate(timeout=20)
                self.assertNotEqual(process.returncode,0,stderr)
                self.assertNotEqual(subprocess.run(['docker','container','inspect',name],capture_output=True).returncode,0)
                self.assertNotEqual(subprocess.run(['docker','volume','inspect',volume],capture_output=True).returncode,0)
                self.assertFalse((stack/'.validation-worker-code.lock').exists())
                self.assertFalse((root/'worker/locks/validation-slot.lock').exists())
                self.assertEqual(os.readlink(stack/'code'),str(previous))
                next_evidence=root/'next'; next_evidence.mkdir()
                next_command=command[:command.index('--')]
                next_command[next_command.index('--evidence')+1]=str(next_evidence)
                result=subprocess.run(next_command+['--','true'],capture_output=True,text=True,timeout=10)
                self.assertEqual(result.returncode,0,result.stderr)
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.communicate(timeout=20)
                subprocess.run(['docker','rm','-f',name],capture_output=True)
                subprocess.run(['docker','volume','rm',volume],capture_output=True)


if __name__ == '__main__':
    unittest.main()
