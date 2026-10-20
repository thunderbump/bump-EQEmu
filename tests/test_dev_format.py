"""Trusted receipt/path fixtures only. Never execute hooks, Cargo or a VM."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
	"dev_format", Path(__file__).resolve().parents[1] / "utils/scripts/dev-format.py"
)
FORMAT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FORMAT)


class FormatterReceiptTests(unittest.TestCase):
	def setUp(self):
		self.name = "rust/actors/core/src/lib.rs"
		self.tool = dict(recipe_sha256="a" * 64, implementation_sha256="b" * 64, rust_input_sha256="c" * 64)
		self.snapshot = dict(base="d" * 40, head="d" * 40,
			recipe_sha256=self.tool["recipe_sha256"], input_snapshot_sha256="e" * 64,
			files=[dict(path=self.name, mode="100644", bytes=12, sha256="f" * 64)],
			deleted=[], indexed_paths=[self.name])
		self.receipt = dict(version=1, scope="working-snapshot-format", operation="check",
			base=self.snapshot["base"], head=self.snapshot["head"],
			input_snapshot_sha256=self.snapshot["input_snapshot_sha256"],
			output_snapshot_sha256=self.snapshot["input_snapshot_sha256"],
			**self.tool, exit_code=0, cleanup_complete=True, vm_started=True, changed_files=[])
		self.indexed = {self.name: ("100644", "f" * 64)}

	def test_receipt_accepts_exact_success(self):
		FORMAT.verify_receipt(self.receipt, self.snapshot, self.tool)

	def test_rejects_stale_source_or_tool_identity(self):
		for key in ("base", "head", "input_snapshot_sha256", "output_snapshot_sha256",
			"recipe_sha256", "implementation_sha256", "rust_input_sha256"):
			with self.subTest(key=key):
				with self.assertRaises(ValueError):
					FORMAT.verify_receipt(dict(self.receipt, **{key: "0" * 64}), self.snapshot, self.tool)

	def test_requires_successful_check_and_confirmed_cleanup(self):
		for key, value in (("version", True), ("exit_code", False), ("exit_code", 1),
			("operation", "format"), ("scope", "full-validation"), ("changed_files", [{}]),
			("cleanup_complete", False), ("vm_started", False)):
			with self.subTest(key=key, value=value):
				with self.assertRaises(ValueError):
					FORMAT.verify_receipt(dict(self.receipt, **{key: value}), self.snapshot, self.tool)

	def test_index_accepts_equal_bytes_with_unrelated_native_edits(self):
		FORMAT.verify_index(self.snapshot, dict(self.indexed, **{"zone/npc.cpp": ("100644", "native")}))

	def test_refuses_partial_bytes_modes_and_untracked_inputs(self):
		for bad in ({}, {self.name: ("100644", "0" * 64)}, {self.name: ("100755", "f" * 64)}):
			with self.assertRaises(ValueError):
				FORMAT.verify_index(self.snapshot, bad)

	def test_refuses_staged_new_file_deleted_from_working_copy(self):
		self.snapshot["indexed_paths"].append("rust/actors/new.rs")
		with self.assertRaises(ValueError):
			FORMAT.verify_index(self.snapshot, self.indexed)

	def test_refuses_untracked_new_file_or_ignored_configuration(self):
		self.snapshot["indexed_paths"] = []
		with self.assertRaises(ValueError):
			FORMAT.verify_index(self.snapshot, {})

	def test_refuses_unstaged_deletion(self):
		self.snapshot["deleted"] = ["rust/actors/removed.rs"]
		with self.assertRaises(ValueError):
			FORMAT.verify_index(self.snapshot, dict(self.indexed, **{"rust/actors/removed.rs": ("100644", "f" * 64)}))

	def test_native_and_documentation_changes_need_no_formatter(self):
		for name in ("zone/npc.cpp", "zone/npc.h", "rust/actors/README.md", "BUILD.md"):
			self.assertFalse(FORMAT.rust_trigger(name))
		for name in (self.name, "rust/actors/Cargo.toml", "rustfmt.toml", ".cargo/config.toml"):
			self.assertTrue(FORMAT.rust_trigger(name))

	def test_refuses_oversize_json_and_nonobject_results(self):
		for data in (b" " * (FORMAT.RESULT_LIMIT + 1), b"[]"):
			with self.assertRaises(ValueError):
				FORMAT.load_json(data)

	def test_client_path_requires_explicit_outside_executable(self):
		with tempfile.TemporaryDirectory() as tmp:
			root = Path(tmp) / "checkout"
			root.mkdir()
			client = Path(tmp) / "reviewed-client"
			client.write_text("not executed")
			with self.assertRaises(ValueError):
				FORMAT.client_path(root, str(client))
			client.chmod(0o755)
			self.assertEqual(FORMAT.client_path(root, str(client)), str(client))
			for name in ("relative-client", str(root / "client")):
				with self.assertRaises(ValueError):
					FORMAT.client_path(root, name)
			link = Path(tmp) / "link"
			link.symlink_to(client)
			with self.assertRaises(ValueError):
				FORMAT.client_path(root, str(link))

	def test_normal_exported_index_and_linked_worktree_index_are_allowed(self):
		with tempfile.TemporaryDirectory() as tmp:
			root = Path(tmp) / "checkout"
			git_directory = root / ".git"
			for requested in (None, "", ".git/index", str(git_directory / "index")):
				FORMAT.verify_index_path(root, git_directory, requested)
			linked = Path(tmp) / "linked"
			linked_git_directory = git_directory / "worktrees/linked"
			FORMAT.verify_index_path(linked, linked_git_directory, str(linked_git_directory / "index"))
			for requested in (".git/index.lock", str(root / "temporary-index")):
				with self.assertRaises(ValueError):
					FORMAT.verify_index_path(root, git_directory, requested)
			with self.assertRaises(ValueError):
				FORMAT.verify_index_path(linked, linked_git_directory, str(git_directory / "index"))


if __name__ == "__main__":
	unittest.main()
