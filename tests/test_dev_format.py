"""Trusted data/file fixtures only. Never execute hooks, Cargo, native code or a VM."""

import base64
from copy import deepcopy
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
	"dev_format", Path(__file__).resolve().parents[1] / "utils/scripts/dev-format.py"
)
FORMAT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FORMAT)


class FormatterContractTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.addCleanup(self.tmp.cleanup)
		self.root = Path(self.tmp.name)
		self.name = "rust/actors/core/src/lib.rs"
		self.before = b"pub fn value()->u8{1}\n"
		self.after = b"pub fn value() -> u8 {\n    1\n}\n"
		path = self.root / self.name
		path.parent.mkdir(parents=True)
		path.write_bytes(self.before)
		(self.root / "native.cpp").write_bytes(b"untouched\n")
		self.tool = dict(version=1, scope="working-snapshot-tool", recipe_sha256="a" * 64,
			implementation_sha256="b" * 64, rust_input_sha256="c" * 64)
		entry = dict(path=self.name, mode="100644", bytes=len(self.before), sha256=FORMAT.sha(self.before))
		self.snapshot = dict(version=1, scope="working-snapshot-input", base="d" * 40, head="d" * 40,
			recipe_sha256=self.tool["recipe_sha256"], files=[entry], deleted=[])
		payload = deepcopy(self.snapshot)
		payload["files"][0]["content_base64"] = base64.b64encode(self.before).decode()
		self.snapshot["input_snapshot_sha256"] = FORMAT.sha(FORMAT.canonical(payload))
		self.snapshot["indexed_paths"] = [self.name]
		self.result = dict(version=1, scope="working-snapshot-format", operation="format",
			base=self.snapshot["base"], head=self.snapshot["head"],
			input_snapshot_sha256=self.snapshot["input_snapshot_sha256"],
			recipe_sha256=self.tool["recipe_sha256"], implementation_sha256=self.tool["implementation_sha256"],
			rust_input_sha256=self.tool["rust_input_sha256"], exit_code=0,
			cleanup_complete=True, vm_started=True, changed_files=[dict(path=self.name,
				before_sha256=FORMAT.sha(self.before), after_sha256=FORMAT.sha(self.after),
				content_base64=base64.b64encode(self.after).decode())])
		payload["files"][0].update(bytes=len(self.after), sha256=FORMAT.sha(self.after),
			content_base64=base64.b64encode(self.after).decode())
		self.result["output_snapshot_sha256"] = FORMAT.sha(FORMAT.canonical(payload))

	def assert_unchanged(self):
		self.assertEqual((self.root / self.name).read_bytes(), self.before)
		self.assertEqual((self.root / "native.cpp").read_bytes(), b"untouched\n")

	def test_format_changes_only_bound_rust_file(self):
		self.assertEqual(FORMAT.apply_changes(self.root, self.snapshot, self.result, self.tool), 1)
		self.assertEqual((self.root / self.name).read_bytes(), self.after)
		self.assertEqual((self.root / "native.cpp").read_bytes(), b"untouched\n")
		self.assertEqual(list(self.root.rglob(".eqemu-format-*")), [])

	def test_noop_is_valid_without_writes(self):
		self.result["changed_files"] = []
		self.result["output_snapshot_sha256"] = self.snapshot["input_snapshot_sha256"]
		self.assertEqual(FORMAT.apply_changes(self.root, self.snapshot, self.result, self.tool), 0)
		self.assert_unchanged()

	def test_check_receipt_accepts_only_exact_success(self):
		self.result.update(operation="check", changed_files=[], output_snapshot_sha256=self.snapshot["input_snapshot_sha256"])
		self.assertEqual(FORMAT.verify_result(self.result, self.snapshot, self.tool, "check"), {})
		for key, bad in (("input_snapshot_sha256", "f" * 64), ("implementation_sha256", "f" * 64),
			("recipe_sha256", "f" * 64), ("rust_input_sha256", "f" * 64), ("head", "f" * 40),
			("exit_code", 1), ("cleanup_complete", False), ("vm_started", False), ("operation", "format")):
			with self.subTest(key=key):
				bad_result = dict(self.result, **{key: bad})
				with self.assertRaises(ValueError):
					FORMAT.verify_result(bad_result, self.snapshot, self.tool, "check")

	def test_stale_source_preserves_edit(self):
		(self.root / self.name).write_bytes(b"new edit\n")
		with self.assertRaises(ValueError):
			FORMAT.apply_changes(self.root, self.snapshot, self.result, self.tool)
		self.assertEqual((self.root / self.name).read_bytes(), b"new edit\n")

	def test_invalid_output_digest_rejected_before_writes(self):
		self.result["output_snapshot_sha256"] = "f" * 64
		with self.assertRaises(ValueError):
			FORMAT.apply_changes(self.root, self.snapshot, self.result, self.tool)
		self.assert_unchanged()

	def test_invalid_change_rejected_before_writes(self):
		change = self.result["changed_files"][0]
		for key, value in (("path", "native.cpp"), ("path", "../outside.rs"),
			("path", "/outside.rs"), ("before_sha256", "f" * 64),
			("after_sha256", "f" * 64), ("content_base64", "!!invalid")):
			with self.subTest(key=key, value=value):
				result = deepcopy(self.result)
				result["changed_files"] = [dict(change, **{key: value})]
				with self.assertRaises(ValueError):
					FORMAT.apply_changes(self.root, self.snapshot, result, self.tool)
				self.assert_unchanged()

	def test_duplicate_change_rejected(self):
		self.result["changed_files"] *= 2
		with self.assertRaises(ValueError):
			FORMAT.apply_changes(self.root, self.snapshot, self.result, self.tool)
		self.assert_unchanged()

	def test_symlinked_source_rejected_without_outside_write(self):
		outside = self.root / "outside.rs"
		outside.write_bytes(self.before)
		(self.root / self.name).unlink()
		(self.root / self.name).symlink_to(outside)
		with self.assertRaises(OSError):
			FORMAT.apply_changes(self.root, self.snapshot, self.result, self.tool)
		self.assertEqual(outside.read_bytes(), self.before)

	def test_symlinked_source_parent_rejected(self):
		parent = (self.root / self.name).parent
		parent.rename(parent.with_name("original"))
		parent.symlink_to(parent.with_name("original"), target_is_directory=True)
		with self.assertRaises(OSError):
			FORMAT.apply_changes(self.root, self.snapshot, self.result, self.tool)

	def test_preflight_catches_change_before_replace(self):
		original = FORMAT.materialize
		calls = 0
		def changed(root, snapshot):
			nonlocal calls
			calls += 1
			if calls == 2:
				(root / self.name).write_bytes(b"concurrent edit\n")
			return original(root, snapshot)
		with patch.object(FORMAT, "materialize", side_effect=changed):
			with self.assertRaises(ValueError):
				FORMAT.apply_changes(self.root, self.snapshot, self.result, self.tool)
		self.assertEqual((self.root / self.name).read_bytes(), b"concurrent edit\n")
		self.assertEqual(list(self.root.rglob(".eqemu-format-*")), [])

	def test_index_must_equal_entire_formatter_input(self):
		indexed = {self.name: ("100644", FORMAT.sha(self.before))}
		FORMAT.verify_index(self.snapshot, indexed)
		for bad in ({}, {self.name: ("100644", "f" * 64)}, {self.name: ("100755", FORMAT.sha(self.before))}):
			with self.assertRaises(ValueError):
				FORMAT.verify_index(self.snapshot, bad)

	def test_unstaged_deletion_rejected(self):
		self.snapshot["deleted"] = ["rust/actors/removed.rs"]
		with self.assertRaises(ValueError):
			FORMAT.verify_index(self.snapshot, {self.name: ("100644", FORMAT.sha(self.before)),
				"rust/actors/removed.rs": ("100644", "f" * 64)})

	def test_staged_new_file_deleted_from_worktree_rejected(self):
		self.snapshot["indexed_paths"].append("rust/actors/new.rs")
		with self.assertRaises(ValueError):
			FORMAT.verify_index(self.snapshot, {self.name: ("100644", FORMAT.sha(self.before))})

	def test_untracked_new_file_or_ignored_configuration_rejected(self):
		self.snapshot["indexed_paths"] = []
		with self.assertRaises(ValueError):
			FORMAT.verify_index(self.snapshot, {})

	def test_native_and_documentation_changes_do_not_require_formatter(self):
		for name in ("zone/npc.cpp", "zone/npc.h", "rust/actors/README.md", "BUILD.md"):
			self.assertFalse(FORMAT.rust_trigger(name))
		for name in (self.name, "rust/actors/Cargo.toml", "rustfmt.toml", ".cargo/config.toml"):
			self.assertTrue(FORMAT.rust_trigger(name))

	def test_json_budget(self):
		with self.assertRaises(ValueError):
			FORMAT.load_json(b" " * (FORMAT.RESULT_LIMIT + 1))


if __name__ == "__main__":
	unittest.main()
