"""Host-selected AFK cleanup adapter. Hold validation leases until deletion ends.

Configure fixture_resources.<name>.cleanup_adapter with this trusted file's
absolute path. AFK calls cleanup_targets(directory, job, apply=False), and owns
job retention, lifecycle locks, deletion markers and removal. This adapter owns
EQEmu release checks and the two generated fixture directories it yields.
"""

import fcntl
import json
import shutil
import subprocess
from contextlib import ExitStack, contextmanager
from pathlib import Path


def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True, timeout=30).stdout.strip()


def physical(path):
    path = Path(path)
    if not path.is_absolute() or path.resolve() != path or path.is_symlink():
        raise ValueError("cleanup path must be physical and absolute")
    return path


def overlaps(a, b):
    return a.is_relative_to(b) or b.is_relative_to(a)


def container_mounts():
    ids = run("docker", "ps", "-aq").split()
    if not ids:
        return []
    containers = json.loads(run("docker", "inspect", *ids))
    return [Path(m["Source"]).resolve() for c in containers for m in c.get("Mounts", []) if m.get("Source")]


def referenced_paths(value):
    if isinstance(value, dict):
        for child in value.values():
            yield from referenced_paths(child)
    elif isinstance(value, list):
        for child in value:
            yield from referenced_paths(child)
    elif isinstance(value, str) and value.startswith("/"):
        yield Path(value).resolve()


@contextmanager
def cleanup_targets(directory, job, *, apply=False):
    """Refuse uncertain release, dirty clones, bound code, and baseline references."""
    directory = physical(directory)
    resource = job["fixture_resource"]
    home = physical(resource["worker_home"])
    stack = physical(resource["stack_path"])
    workspace = physical(Path(job["workspace_root"]) / job["id"])
    leases = [home / "locks/validation-slot.lock", stack / ".validation-worker-code.lock"]
    with ExitStack() as locks:
        for lease in leases:
            # Guard files coordinate newer workers; mkdir also excludes old workers.
            guard = physical(Path(str(lease) + ".guard"))
            handle = locks.enter_context(guard.open("a"))
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            lease.mkdir()
            locks.callback(lease.rmdir)
        evidence = physical(directory / "fixture-evidence")
        if (evidence / "docker-creation-pending").exists():
            raise ValueError("Docker creation is unconfirmed; recover validation first")
        lifetime = evidence / "lifetime.json"
        if lifetime.is_file() and (evidence / "docker-owned").exists():
            owner = json.loads(lifetime.read_text())
            token = owner["token"]
            for kind in ("container", "volume", "network"):
                args = ["docker", kind, "ls"]
                if kind == "container":
                    args.append("--all")
                if run(*args, "-q", "--filter", f"label=org.eqemu.validation={token}"):
                    raise ValueError("owned Docker resources remain; recover validation first")
        targets = []
        if "fixtures" in job["expected_phases"]:
            result = json.loads((evidence / "result.json").read_text())
            checkout = physical(result["checkout_dir"])
            metadata = result["request_metadata"]
            source = metadata["source"]
            if (
                result.get("status") not in {"passed", "failed"}
                or source.get("type") != "fetch"
                or result.get("expected_commit") != job["head"]
                or source.get("commit") != job["head"]
                or source.get("repo") != str(workspace / "fixtures")
                or result.get("evidence_dir") != str(evidence)
                or checkout.parent != home / "checkouts"
                or checkout.name != metadata.get("run_id")
            ):
                raise ValueError("validation checkout ownership is unproven")
            if checkout.exists():
                if not (checkout / ".git").is_dir() or (checkout / ".git").is_symlink():
                    raise ValueError("validation checkout is not an independent clone")
                if run("git", "-C", str(checkout), "rev-parse", "HEAD") != job["head"] or run("git", "-C", str(checkout), "status", "--porcelain", "--untracked-files=all"):
                    raise ValueError("validation checkout has unpublished changes")
            targets.append(checkout)
            prepared = physical(evidence / "prepared-fixture")
            manifest = prepared / "migration-rehearsal-manifest.json"
            if prepared.exists():
                data = json.loads(manifest.read_text())
                if data.get("source", {}).get("fixture_preparer_commit") != job["head"]:
                    raise ValueError("prepared fixture ownership is unproven")
                if any(p.is_symlink() for p in prepared.rglob("*")):
                    raise ValueError("prepared fixture contains symlinks")
            targets.append(prepared)
        guarded = [workspace, *targets]
        bound = (stack / "code").resolve()
        if any(overlaps(bound, p) for p in guarded):
            raise ValueError("validation stack still references cleanup target")
        # Fail closed on Docker inspection failure. Stopped containers retain mounts
        # too, and must be removed by their owner before their inputs are collected.
        if any(overlaps(mount, p) for mount in container_mounts() for p in guarded):
            raise ValueError("container still references cleanup target")
        for name in ("migration-rehearsal-baseline.json", "migration-rehearsal-manifest.json"):
            baseline = home / name
            if baseline.exists():
                value = json.loads(baseline.read_text())
                if any(overlaps(ref, p) for ref in referenced_paths(value) for p in guarded):
                    raise ValueError("baseline still references cleanup target")
        if apply and "fixtures" in job["expected_phases"] and manifest.exists():
            # Preserve identity/hash metadata alongside the retained logs/results.
            retained = evidence / "prepared-fixture-manifest.json"
            if retained.is_symlink():
                raise ValueError("unsafe retained fixture manifest")
            shutil.copyfile(manifest, retained)
        yield targets
