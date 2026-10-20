#!/usr/bin/env python3
"""Use the trusted isolated formatter; optionally check its receipt before commit."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile


RESULT_LIMIT = 1536 * 1024


def load_json(data):
	if len(data) > RESULT_LIMIT:
		raise ValueError("Formatter JSON exceeds size limit")
	value = json.loads(data)
	if not isinstance(value, dict):
		raise ValueError("Expected formatter JSON object")
	return value


def verify_receipt(result, snapshot, tool):
	"""Receipts are local convenience data, never full-validation credentials."""
	if (type(result.get("version")) is not int or result["version"] != 1
		or result.get("scope") != "working-snapshot-format" or result.get("operation") != "check"
		or type(result.get("exit_code")) is not int or result["exit_code"] != 0
		or result.get("cleanup_complete") is not True or result.get("vm_started") is not True
		or any(result.get(k) != snapshot[k] for k in ("base", "head", "input_snapshot_sha256"))
		or any(result.get(k) != tool[k] for k in ("recipe_sha256", "implementation_sha256", "rust_input_sha256"))
		or snapshot["recipe_sha256"] != tool["recipe_sha256"]
		or result.get("changed_files") != []
		or result.get("output_snapshot_sha256") != snapshot["input_snapshot_sha256"]):
		raise ValueError("Missing, stale or failed formatting receipt. Run python3 utils/scripts/dev-format.py --check before committing.")


def rust_trigger(name):
	"""Conservative routing only; the trusted frontend owns actual snapshot scope."""
	path = PurePosixPath(name)
	return path.suffix == ".rs" or path.name in (
		"Cargo.toml", "Cargo.lock", "rustfmt.toml", ".rustfmt.toml", "rust-toolchain", "rust-toolchain.toml"
	) or ".cargo" in path.parts


def verify_index(snapshot, indexed):
	"""Require every formatter input to equal the index, including new files/config."""
	if set(snapshot["indexed_paths"]) != {entry["path"] for entry in snapshot["files"]}:
		raise ValueError("Formatter inputs differ between index and working copy. Stage complete intended Rust/config changes and deletions, then run dev-format.py --check.")
	for entry in snapshot["files"]:
		if indexed.get(entry["path"]) != (entry["mode"], entry["sha256"]):
			raise ValueError("Partial staging or untracked formatter input: " + entry["path"] + ". Stage the complete intended Rust/config changes, then run dev-format.py --check.")
	if any(name in indexed for name in snapshot["deleted"]):
		raise ValueError("Unstaged formatter-input deletion; stage the complete deletion before checking")


def git(root, *args):
	return subprocess.check_output(["git", "-c", "core.fsmonitor=false", "-c", "core.hooksPath=/dev/null", "-C", str(root), *args], stderr=subprocess.STDOUT)


def client_path(root, requested):
	if requested is None:
		try:
			requested = git(root, "config", "--local", "--get", "eqemu.formatterClient").decode().strip()
		except subprocess.CalledProcessError:
			raise ValueError("Missing formatter client. Set local eqemu.formatterClient to the absolute reviewed format-worker-snapshot installation; see BUILD.md.") from None
	path = Path(requested)
	if (not path.is_absolute() or path.resolve() != path or not path.is_file()
		or not os.access(path, os.X_OK) or root == path or root in path.parents):
		raise ValueError("Formatter client must be an absolute executable reviewed installation outside this checkout")
	return str(path)


def call_client(client, *args):
	return load_json(subprocess.check_output([client, *args], timeout=60))


def receipt_path(root):
	return Path(git(root, "rev-parse", "--path-format=absolute", "--git-path", "eqemu-format-check.json").decode().strip())


def verify_index_path(root, git_directory, requested):
	"""Git exports GIT_INDEX_FILE even for ordinary commits and linked worktrees."""
	if requested and (root / requested).resolve() != (git_directory / "index").resolve():
		raise ValueError("Alternate Git indexes/commit --only are unsupported by this optional hook; use the ordinary staged index")


def staged_check(root):
	# Unlike --git-path index, the Git directory is independent of GIT_INDEX_FILE.
	git_directory = Path(git(root, "rev-parse", "--absolute-git-dir").decode().strip())
	verify_index_path(root, git_directory, os.environ.get("GIT_INDEX_FILE"))
	git(root, "diff", "--no-ext-diff", "--no-textconv", "--cached", "--check")
	names = git(root, "diff", "--no-ext-diff", "--no-textconv", "--cached", "--name-only", "-z").decode().split("\0")
	if not any(rust_trigger(name) for name in names if name):
		return
	client = client_path(root, None)
	tool = call_client(client, "--identity")
	base = git(root, "rev-parse", "HEAD").decode().strip()
	snapshot = call_client(client, "--repo", str(root), "--base", base, "--snapshot-identity")
	indexed = {}
	for record in git(root, "ls-files", "--stage", "-z").split(b"\0"):
		if not record:
			continue
		meta, name = record.split(b"\t", 1)
		mode, oid, stage = meta.decode().split()
		if stage != "0":
			raise ValueError("Unresolved index conflict")
		indexed[name.decode()] = (mode, oid)
	# Only read the formatter's bounded inventory, not native/generated binaries.
	for entry in snapshot["files"]:
		item = indexed.get(entry["path"])
		if item:
			indexed[entry["path"]] = (item[0], hashlib.sha256(git(root, "cat-file", "blob", item[1])).hexdigest())
	verify_index(snapshot, indexed)
	try:
		result = load_json(receipt_path(root).read_bytes())
	except FileNotFoundError:
		raise ValueError("No isolated formatting receipt. Run python3 utils/scripts/dev-format.py --check before committing.") from None
	verify_receipt(result, snapshot, tool)
	print("Staged Rust/config inputs match the isolated formatting check")


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--check", action="store_true", help="run isolated check-only and save a commit-hook receipt")
	parser.add_argument("--staged", action="store_true", help="hook mode: check index and prior receipt, never boot a VM")
	parser.add_argument("--client", help="absolute reviewed installed format-worker-snapshot path")
	args = parser.parse_args()
	root = Path(git(Path.cwd(), "rev-parse", "--show-toplevel").decode().strip())
	if root.resolve() != root:
		raise ValueError("Checkout path cannot traverse symlinks")
	if args.staged:
		if args.check or args.client:
			raise ValueError("--staged cannot be combined with formatter execution options")
		staged_check(root)
		return
	client = client_path(root, args.client)
	tool = call_client(client, "--identity")
	base = git(root, "rev-parse", "HEAD").decode().strip()
	common = ["--repo", str(root), "--base", base]
	snapshot = call_client(client, *common, "--snapshot-identity")
	with tempfile.TemporaryDirectory(prefix="eqemu-dev-format-") as tmp:
		output = Path(tmp) / "result.json"
		code = subprocess.call([client, *common, *(["--check"] if args.check else []), "--output", str(output)])
		if code:
			detail = ""
			if output.is_file():
				failed = load_json(output.read_bytes())
				if isinstance(failed.get("error"), str):
					detail = " " + failed["error"][:2000]
			raise ValueError("Isolated formatting failed; working copy was preserved." + detail + " Inspect the client diagnostics and retry.")
		current = call_client(client, *common, "--snapshot-identity")
		if current != snapshot:
			raise ValueError("Working snapshot changed during formatting; no output applied")
		if args.check:
			result = load_json(output.read_bytes())
			verify_receipt(result, snapshot, tool)
			receipt = receipt_path(root)
			fd, name = tempfile.mkstemp(prefix="eqemu-format-check-", dir=receipt.parent)
			try:
				with os.fdopen(fd, "wb") as stream:
					stream.write(json.dumps(result).encode())
				os.replace(name, receipt)
			finally:
				Path(name).unlink(missing_ok=True)
			print("Isolated formatting check passed; receipt saved in Git private storage")
		else:
			# The reviewed frontend owns snapshot policy, full result validation and
			# rollback-safe application. This repository never applies source bytes.
			subprocess.check_call([client, *common, "--apply-result", str(output),
				"--expected-input", snapshot["input_snapshot_sha256"],
				"--expected-recipe", tool["recipe_sha256"],
				"--expected-implementation", tool["implementation_sha256"],
				"--output", str(Path(tmp) / "applied.json")])
			print("Formatting applied; review and stage changes explicitly")


if __name__ == "__main__":
	try:
		main()
	except (ValueError, OSError, subprocess.SubprocessError, KeyError) as error:
		if isinstance(error, subprocess.CalledProcessError) and error.output:
			print(error.output.decode(errors="replace"), file=sys.stderr)
		print("dev-format: " + str(error), file=sys.stderr)
		sys.exit(1)
