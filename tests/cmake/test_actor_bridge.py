"""Configure-level regressions for the production zone bridge integration.

Run with: python3 tests/cmake/test_actor_bridge.py
No Cargo, Rust compilation, server dependencies, or VM fixtures are executed.
Platform cases simulate target metadata; they are not Windows link qualification.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake")


@unittest.skipUnless(CMAKE, "cmake is required")
class ActorBridgeConfigure(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="actor-cmake-", dir=ROOT)
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.source = self.work / "source"
        self.source.mkdir()
        (self.source / "main.cpp").write_text("int main() { return 0; }\n")
        # Exercise exactly the function called by zone/CMakeLists.txt, without
        # requiring the server's unrelated vcpkg/Perl/Lua dependencies.
        (self.source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(BridgeRegression LANGUAGES CXX)
set(CMAKE_SYSTEM_NAME "${{TEST_SYSTEM}}")
set(CMAKE_SYSTEM_PROCESSOR "${{TEST_PROCESSOR}}")
set(CMAKE_CROSSCOMPILING "${{TEST_CROSS}}")
add_executable(zone main.cpp)
include("{ROOT.as_posix()}/cmake/ActorBridge.cmake")
eqemu_link_actor_bridge(zone)
if(TARGET actor_bridge)
    get_target_property(archive actor_bridge IMPORTED_LOCATION)
    get_target_property(dependencies actor_bridge MANUALLY_ADDED_DEPENDENCIES)
    get_target_property(libraries zone LINK_LIBRARIES)
    get_target_property(definitions zone COMPILE_DEFINITIONS)
    file(WRITE "${{CMAKE_BINARY_DIR}}/bridge.txt"
        "${{archive}}\\n${{dependencies}}\\n${{libraries}}\\n${{definitions}}\\n")
endif()
''')

    def configure(self, *, system="Linux", processor="x86_64", cross=False,
                  target_dir=None, build="build", succeeds=True):
        env = os.environ.copy()
        env.pop("CARGO_TARGET_DIR", None)
        # This deliberately unusable Cargo command must never run at configure.
        env["CARGO"] = str(self.work / "cargo-must-not-run")
        if target_dir is not None:
            env["CARGO_TARGET_DIR"] = str(target_dir)
        binary = self.source / build
        result = subprocess.run([
            CMAKE, "-S", str(self.source), "-B", str(binary),
            f"-DTEST_SYSTEM={system}", f"-DTEST_PROCESSOR={processor}",
            f"-DTEST_CROSS={'ON' if cross else 'OFF'}",
        ], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode == 0, succeeds, result.stdout)
        return binary, result.stdout

    def bridge_archive(self, binary):
        lines = (binary / "bridge.txt").read_text().splitlines()
        self.assertEqual(lines[1:], ["actor_bridge_build", "actor_bridge",
                                     "EQEMU_RUST_BOOTSTRAP_BRIDGE"])
        return Path(lines[0])

    def test_in_source_build_and_nested_preset_default(self):
        archives = []
        for build in ("build", "build/release"):
            binary, _ = self.configure(build=build)
            archive = self.bridge_archive(binary)
            self.assertFalse(archive.is_relative_to(self.source))
            self.assertTrue(archive.is_relative_to(self.work))
            self.assertTrue(str(archive).endswith(
                "x86_64-unknown-linux-gnu/release/libactor_bridge.a"))
            archives.append(archive)
        self.assertNotEqual(*archives)

    def test_external_override(self):
        external = self.work / "guest-target"
        binary, _ = self.configure(target_dir=external)
        self.assertTrue(self.bridge_archive(binary).is_relative_to(external))

    def test_reject_source_and_relative_source_outputs(self):
        for directory in (self.source, self.source / "target", "rust-target"):
            _, output = self.configure(target_dir=directory, succeeds=False)
            self.assertIn("outside the source tree", output)

    def test_unsupported_platforms_have_no_bridge_or_startup_definition(self):
        for system, processor, cross in (("Windows", "AMD64", False),
                                          ("Darwin", "arm64", False),
                                          ("Linux", "aarch64", False),
                                          ("Linux", "x86_64", True)):
            binary, _ = self.configure(system=system, processor=processor,
                                       cross=cross, build=f"build-{system}-{processor}-{cross}",
                                       target_dir=self.source / "invalid-for-linux")
            self.assertFalse((binary / "bridge.txt").exists())


if __name__ == "__main__":
    unittest.main()
