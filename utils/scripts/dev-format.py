#!/usr/bin/env python3
"""Use the trusted isolated formatter; optionally check its receipt before commit."""

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import secrets
import stat
import subprocess
import sys
import tempfile


RESULT_LIMIT = 1536 * 1024
FILE_LIMIT = 512 * 1024
HEX = re.compile(r"[a-f0-9]{64}")


def canonical(value):
	return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def sha(data):
	return hashlib.sha256(data).hexdigest()


def safe_path(name):
	if not isinstance(name, str):
		raise ValueError("Invalid formatter path")
	path = PurePosixPath(name)
	if (path.is_absolute() or str(path) != name or not path.parts
		or any(p in (".", "..", ".git") for p in path.parts)
		or any(ord(c) < 32 for c in name)):
		raise ValueError("Unsafe formatter path")
	return path


def open_parent(root, name):
	"""Pin an existing source directory without traversing symlinks."""
	parts = safe_path(name).parts
	fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
	try:
		for part in parts[:-1]:
			child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
			os.close(fd)
			fd = child
		return fd, parts[-1]
	except BaseException:
		os.close(fd)
		raise


def read_source(root, name):
	"""Read regular files without following any source-directory symlinks."""
	fd, leaf = open_parent(root, name)
	try:
		file_fd = os.open(leaf, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW, dir_fd=fd)
		with os.fdopen(file_fd, "rb") as stream:
			info = os.fstat(stream.fileno())
			if not stat.S_ISREG(info.st_mode) or info.st_size > FILE_LIMIT:
				raise ValueError("Unsupported source file: " + name)
			data = stream.read(FILE_LIMIT + 1)
			if len(data) != info.st_size or len(data) > FILE_LIMIT:
				raise ValueError("Source changed while reading: " + name)
		return data, "100755" if info.st_mode & 0o111 else "100644"
	finally:
		os.close(fd)


def load_json(data):
	if len(data) > RESULT_LIMIT:
		raise ValueError("Formatter JSON exceeds size limit")
	value = json.loads(data)
	if not isinstance(value, dict):
		raise ValueError("Expected formatter JSON object")
	return value


def verify_tool(tool):
	if (type(tool.get("version")) is not int or tool["version"] != 1 or tool.get("scope") != "working-snapshot-tool"
		or any(not isinstance(tool.get(k), str) or not HEX.fullmatch(tool[k])
			for k in ("recipe_sha256", "implementation_sha256", "rust_input_sha256"))):
		raise ValueError("Unsupported formatter identity; install the reviewed snapshot client")


def verify_result(result, snapshot, tool, operation):
	"""Check all result bindings and returned bytes before permitting any writes."""
	verify_tool(tool)
	if (len(canonical(result)) > RESULT_LIMIT
		or type(result.get("version")) is not int or result["version"] != 1
		or result.get("scope") != "working-snapshot-format"
		or result.get("operation") != operation
		or result.get("exit_code") != 0 or type(result.get("exit_code")) is not int
		or result.get("cleanup_complete") is not True or result.get("vm_started") is not True
		or any(result.get(k) != snapshot.get(k) for k in ("base", "head", "input_snapshot_sha256"))
		or any(result.get(k) != tool[k] for k in ("recipe_sha256", "implementation_sha256", "rust_input_sha256"))
		or snapshot.get("recipe_sha256") != tool["recipe_sha256"]
		or not isinstance(result.get("changed_files"), list) or len(result["changed_files"]) > 128):
		raise ValueError("Formatter failed or returned incomplete/mismatched evidence")
	files = {entry["path"]: entry for entry in snapshot["files"]}
	changes = {}
	for change in result["changed_files"]:
		if (not isinstance(change, dict)
			or set(change) != {"path", "before_sha256", "after_sha256", "content_base64"}
			or not isinstance(change["path"], str) or change["path"] not in files
			or not change["path"].endswith(".rs") or change["path"] in changes
			or change["before_sha256"] != files[change["path"]]["sha256"]):
			raise ValueError("Formatter returned an out-of-scope or duplicate change")
		safe_path(change["path"])
		data = base64.b64decode(change["content_base64"], validate=True)
		if len(data) > FILE_LIMIT or sha(data) != change["after_sha256"] or sha(data) == change["before_sha256"]:
			raise ValueError("Formatter change hash or size mismatch")
		changes[change["path"]] = data
	if sum(map(len, changes.values())) > 1024 * 1024 or (operation == "check" and changes):
		raise ValueError("Formatter change budget or check-only contract violated")
	if not changes and result.get("output_snapshot_sha256") != snapshot["input_snapshot_sha256"]:
		raise ValueError("Formatter no-op output identity mismatch")
	return changes


def materialize(root, snapshot):
	"""Reconstruct the worker's canonical snapshot from its inventory and exact bytes."""
	files = []
	for entry in snapshot["files"]:
		data, mode = read_source(root, entry["path"])
		if sha(data) != entry["sha256"] or len(data) != entry["bytes"] or mode != entry["mode"]:
			raise ValueError("Source changed since snapshot capture: " + entry["path"])
		files.append(dict(entry, content_base64=base64.b64encode(data).decode()))
	payload = {k: snapshot[k] for k in ("version", "scope", "base", "head", "recipe_sha256", "deleted")}
	payload["files"] = files
	if sha(canonical(payload)) != snapshot["input_snapshot_sha256"]:
		raise ValueError("Snapshot content identity mismatch")
	return payload


def apply_changes(root, snapshot, result, tool):
	changes = verify_result(result, snapshot, tool, "format")
	payload = materialize(root, snapshot)
	for entry in payload["files"]:
		if entry["path"] in changes:
			data = changes[entry["path"]]
			entry.update(bytes=len(data), sha256=sha(data), content_base64=base64.b64encode(data).decode())
	if sha(canonical(payload)) != result.get("output_snapshot_sha256"):
		raise ValueError("Formatted output snapshot identity mismatch")
	# Preflight every path and byte first. No staging, additions, deletions or config edits.
	prepared = []
	try:
		for name, data in changes.items():
			directory, leaf = open_parent(root, name)
			tmp = ".eqemu-format-" + secrets.token_hex(12)
			prepared.append((directory, tmp, leaf))
			mode = 0o755 if next(e for e in snapshot["files"] if e["path"] == name)["mode"] == "100755" else 0o644
			fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, mode, dir_fd=directory)
			with os.fdopen(fd, "wb") as stream:
				stream.write(data)
				os.fchmod(stream.fileno(), mode)
		materialize(root, snapshot)
		for directory, tmp, leaf in prepared:
			os.replace(tmp, leaf, src_dir_fd=directory, dst_dir_fd=directory)
	finally:
		for directory, tmp, _ in prepared:
			try:
				os.unlink(tmp, dir_fd=directory)
			except FileNotFoundError:
				pass
			finally:
				os.close(directory)
	return len(changes)


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
	return subprocess.check_output(["git", "-c", "core.fsmonitor=false", "-c", "core.hooksPath=/dev/null", "-C", str(root), *args])


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


def staged_check(root):
	if os.environ.get("GIT_INDEX_FILE"):
		raise ValueError("Alternate Git indexes/commit --only are unsupported by this optional hook; use the ordinary staged index")
	git(root, "diff", "--no-ext-diff", "--no-textconv", "--cached", "--check")
	names = git(root, "diff", "--no-ext-diff", "--no-textconv", "--cached", "--name-only", "-z").decode().split("\0")
	if not any(rust_trigger(name) for name in names if name):
		return
	client = client_path(root, None)
	tool = call_client(client, "--identity")
	verify_tool(tool)
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
			indexed[entry["path"]] = (item[0], sha(git(root, "cat-file", "blob", item[1])))
	verify_index(snapshot, indexed)
	receipt = Path(git(root, "rev-parse", "--path-format=absolute", "--git-path", "eqemu-format-check.json").decode().strip())
	try:
		result = load_json(receipt.read_bytes())
	except FileNotFoundError:
		raise ValueError("No isolated formatting receipt. Run python3 utils/scripts/dev-format.py --check before committing.") from None
	verify_result(result, snapshot, tool, "check")
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
	verify_tool(tool)
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
		result = load_json(output.read_bytes())
		current = call_client(client, *common, "--snapshot-identity")
		if current != snapshot:
			raise ValueError("Working snapshot changed during formatting; no output applied")
		if args.check:
			verify_result(result, snapshot, tool, "check")
			receipt = Path(git(root, "rev-parse", "--path-format=absolute", "--git-path", "eqemu-format-check.json").decode().strip())
			fd, name = tempfile.mkstemp(prefix="eqemu-format-check-", dir=receipt.parent)
			try:
				with os.fdopen(fd, "wb") as stream:
					stream.write(canonical(result))
				os.replace(name, receipt)
			finally:
				Path(name).unlink(missing_ok=True)
			print("Isolated formatting check passed; receipt saved in Git private storage")
		else:
			print("Formatted {} Rust files; review and stage the changes explicitly".format(apply_changes(root, snapshot, result, tool)))


if __name__ == "__main__":
	try:
		main()
	except (ValueError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
		print("dev-format: " + str(error), file=sys.stderr)
		sys.exit(1)
