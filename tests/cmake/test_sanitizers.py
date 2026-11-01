"""Check sanitizer flag placement in the real CMake target graph, without builds.

Run: python3 tests/cmake/test_sanitizers.py
Package targets are stubs and the forced compiler is /usr/bin/false. No native
compiler, Cargo, format compiler probes, or candidate executable runs here.
These tests do not qualify the guest toolchain or sanitizer runtime.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake")
FALSE = shutil.which("false")


@unittest.skipUnless(CMAKE and FALSE, "cmake and false are required")
class SanitizerConfigure(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="eqemu-sanitizer-cmake-")
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.hook = self.work / "packages.cmake"
        self.hook.write_text(f'''
# Skip existing compiler controls: this test only generates build commands.
include("{ROOT.as_posix()}/cmake/FormatWarnings.cmake")
function(eqemu_check_format_warnings)
endfunction()
macro(find_package)
endmacro()
foreach(name Boost::dynamic_bitset Boost::foreach Boost::tuple cereal::cereal
        fmt::fmt unofficial::libmariadb libuv::uv_a OpenSSL::SSL OpenSSL::Crypto
        ZLIB::ZLIB unofficial-sodium::sodium RecastNavigation::Detour Threads::Threads)
    add_library(${{name}} INTERFACE IMPORTED)
endforeach()
set(OPENSSL_VERSION "3.0.0")
# Simulate both parsers being available without discovering host packages.
set(PerlLibs_FOUND TRUE)
set(PERL_INCLUDE_PATH "{self.work.as_posix()}")
set(PERL_LIBRARY "perl-stub")
''')

    def configure(self, *, enabled=None, compiler="GNU", system="Linux", frontend="GNU",
                  build="build", succeeds=True):
        toolchain = self.work / f"{build}.cmake"
        toolchain.write_text(f'''
set(CMAKE_SYSTEM_NAME "{system}")
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_CXX_COMPILER "{FALSE}")
set(CMAKE_CXX_COMPILER_ID "{compiler}")
set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT "{frontend}")
set(CMAKE_CXX_COMPILER_VERSION 13.0.0)
set(CMAKE_CXX_COMPILER_ID_RUN TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
set(CMAKE_CXX_STANDARD_COMPUTED_DEFAULT 17)
set(CMAKE_CXX_EXTENSIONS_COMPUTED_DEFAULT ON)
''')
        binary = self.work / build
        args = [CMAKE, "-S", str(ROOT), "-B", str(binary), "-G", "Unix Makefiles",
                f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
                f"-DCMAKE_PROJECT_EQEmu_INCLUDE={self.hook}",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DEQEMU_BUILD_TESTS=ON",
                "-DEQEMU_BUILD_PCH=OFF"]
        if enabled is not None:
            args.append(f"-DEQEMU_SANITIZERS={'ON' if enabled else 'OFF'}")
        env = os.environ.copy()
        env["CARGO"] = FALSE
        env["CARGO_TARGET_DIR"] = str(self.work / "cargo-unused")
        result = subprocess.run(args, env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode == 0, succeeds, result.stdout)
        return binary, result.stdout

    def test_gnu_and_clang_cover_project_targets_only(self):
        for compiler in ("GNU", "Clang"):
            binary, _ = self.configure(enabled=True, compiler=compiler, build=compiler)
            commands = json.loads((binary / "compile_commands.json").read_text())
            covered = set()
            for entry in commands:
                command = entry["command"]
                path = Path(entry["file"])
                source = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path.relative_to(binary)
                excluded = source.parts[0] == "libs" or source.parts[:2] == ("tests", "cppunit")
                self.assertEqual("-fsanitize=address,undefined" in command,
                                 not excluded, command)
                if not excluded:
                    for flag in ("-fno-sanitize-recover=all", "-g", "-O1",
                                 "-fno-omit-frame-pointer", "-fno-optimize-sibling-calls"):
                        self.assertIn(flag, command)
                    # Preserve the five existing patch-source -O0 exceptions.
                    patch_o0 = source.as_posix() in {
                        f"common/patches/{name}.cpp" for name in ("sod", "sof", "rof", "rof2", "uf")
                    }
                    self.assertEqual(re.findall(r"\s(-O\S+)", command)[-1],
                                     "-O0" if patch_o0 else "-O1", command)
                    covered.add(source.parts[0])
            self.assertTrue({"common", "tests", "world", "zone"} <= covered)
            for target, directory in (("tests", "tests"), ("world", "world"),
                                      ("zone", "zone"), ("tests_sanitizer_controls", "tests")):
                link = (binary / directory / "CMakeFiles" / f"{target}.dir" / "link.txt").read_text()
                self.assertIn("-fsanitize=address,undefined", link)
                self.assertIn("-fno-sanitize-recover=all", link)
            self.assertIn("sanitizer_controls.cpp", " ".join(e["file"] for e in commands))

    def test_default_and_explicit_off_leave_commands_unchanged(self):
        for enabled, build in ((None, "default"), (False, "off")):
            binary, _ = self.configure(enabled=enabled, build=build)
            commands = (binary / "compile_commands.json").read_text()
            self.assertNotIn("-fsanitize", commands)
            self.assertNotIn("sanitizer_controls.cpp", commands)
            self.assertNotIn("-fsanitize", (binary / "zone/CMakeFiles/zone.dir/link.txt").read_text())

    def test_unsupported_selection_fails_before_dependency_discovery(self):
        for compiler, system, frontend in (("GNU", "Darwin", "GNU"),
                                           ("AppleClang", "Linux", "GNU"),
                                           ("Clang", "Linux", "MSVC")):
            _, output = self.configure(enabled=True, compiler=compiler, system=system,
                                       frontend=frontend, build=f"bad-{compiler}-{system}",
                                       succeeds=False)
            self.assertIn("requires Linux GCC or Clang with a GNU-style frontend", output)


if __name__ == "__main__":
    unittest.main()
