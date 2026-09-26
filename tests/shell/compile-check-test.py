#!/usr/bin/env python3
"""Checks for compilation scope, uncommitted input preservation and bounded cleanup."""
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "scripts/compile-check"
loader = importlib.machinery.SourceFileLoader("compile_check", str(SCRIPT))
spec = importlib.util.spec_from_loader(loader.name, loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)


class CompileCheckTests(unittest.TestCase):
    def test_compilation_database_controls_coverage(self):
        entries = [{"file": "/home/eqemu/code/zone/a.cpp", "command": "clang++ -o zone/CMakeFiles/zone.dir/a.cpp.o -c /home/eqemu/code/zone/a.cpp"}]
        self.assertEqual(module.object_targets(entries, ["zone/a.cpp"]), ["zone/CMakeFiles/zone.dir/a.cpp.o"])
        with self.assertRaises(module.Inconclusive):
            module.object_targets(entries, ["zone/new.cpp"])
        entries[0]["command"] = "clang++ -o ../outside.o -c a.cpp"
        with self.assertRaises(module.Inconclusive):
            module.object_targets(entries, ["zone/a.cpp"])

    def test_snapshot_uses_edits_new_headers_and_deletions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "repo"; root.mkdir()
            dest = Path(tmp) / "snapshot"; dest.mkdir()
            (root / "a.cpp").write_text("uncommitted edit")
            (root / "new.h").write_text("new untracked header")
            digest = module.snapshot(root, dest, ["a.cpp", "new.h", "deleted.cpp"], float("inf"))
            self.assertEqual((dest / "a.cpp").read_text(), "uncommitted edit")
            self.assertEqual((dest / "new.h").read_text(), "new untracked header")
            self.assertFalse((dest / "deleted.cpp").exists())
            self.assertEqual(len(digest), 64)
            os.mkfifo(root / "pipe.cpp")
            with self.assertRaises(module.Inconclusive):
                module.snapshot(root, dest, ["pipe.cpp"], float("inf"))
            (root / "escape.cpp").symlink_to("/etc/passwd")
            with self.assertRaises(module.Inconclusive):
                module.selected_sources(root, ["escape.cpp"])
            with self.assertRaises(module.Inconclusive):
                module.selected_sources(root, ["new.h"])

    def test_timeout_removes_only_named_container_and_preserves_worktree(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp); root = tmp / "repo"; (root / "scripts").mkdir(parents=True)
            script = root / "scripts/compile-check"; script.write_bytes(SCRIPT.read_bytes()); script.chmod(0o755)
            (root / "a.cpp").write_text("current edit")
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(["git", "-C", str(root), "-c", "user.name=test", "-c", "user.email=test@localhost", "commit", "-qm", "test"], check=True)
            bin_dir = tmp / "bin"; bin_dir.mkdir(); docker = bin_dir / "docker"
            docker.write_text('''#!/usr/bin/env python3
import sys,time,os,json
with open(os.environ['CALLS'], 'a') as f: f.write(json.dumps(sys.argv[1:])+'\\n')
if sys.argv[1:3]==['image','inspect']: print('sha256:test')
if sys.argv[1]=='run': time.sleep(5)
'''); docker.chmod(0o755)
            env = dict(os.environ, PATH=str(bin_dir)+":"+os.environ["PATH"], XDG_STATE_HOME=str(tmp / "state"), CALLS=str(tmp / "calls"))
            result = subprocess.run([str(script), "--timeout", "1", "a.cpp"], cwd=root, env=env, capture_output=True, text=True, timeout=8)
            self.assertEqual(result.returncode, 2, result.stderr)
            payload = json.loads(result.stdout)
            self.assertEqual(payload["reason"], "time budget exceeded")
            calls = [json.loads(line) for line in (tmp / "calls").read_text().splitlines()]
            launch = next(call for call in calls if call[0] == "run")
            cleanup = next(call for call in calls if call[:2] == ["rm", "-f"])
            self.assertEqual(cleanup[2], launch[launch.index("--name")+1])
            self.assertEqual((root / "a.cpp").read_text(), "current edit")
            self.assertFalse((root / "build").exists())
            self.assertTrue((Path(payload["evidence_dir"]) / "compile.log").exists())


if __name__ == "__main__":
    unittest.main()
