/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "tests/atobool_test.h"
#include "tests/bot_loot_request_test.h"
#include "tests/data_verification_test.h"
#include "tests/fallback_dialogue_test.h"
#include "tests/fixed_memory_test.h"
#include "tests/fixed_memory_variable_test.h"
#include "tests/hextoi_32_64_test.h"
#include "tests/ipc_mutex_test.h"
#include "tests/memory_mapped_file_test.h"
#include "tests/skills_util_test.h"
#include "tests/string_util_test.h"
#include "tests/task_state_test.h"

#include "common/path_manager.h"
#include "common/platform.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <memory>

const EQEmuConfig *Config;

int main()
{
	const auto original_cwd = std::filesystem::current_path();
	const auto runtime_path = std::filesystem::temp_directory_path() / (
		"eqemu-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
	);

	try {
		std::filesystem::create_directories(runtime_path / "shared");
		std::filesystem::create_directories(runtime_path / "logs");
		std::filesystem::create_directories(runtime_path / "maps");
		std::filesystem::create_directories(runtime_path / "quests");
		std::filesystem::create_directories(runtime_path / "plugins");
		std::filesystem::create_directories(runtime_path / "lua_modules");

		{
			std::ofstream config(runtime_path / "eqemu_config.json");
			config << R"json({
  "server": {
    "directories": {
      "shared_memory": "shared",
      "logs": "logs",
      "maps": "maps",
      "quests": "quests",
      "plugins": "plugins",
      "lua_modules": "lua_modules",
      "patches": ".",
      "opcodes": "."
    }
  }
})json";
		}

		std::filesystem::current_path(runtime_path);

		RegisterExecutablePlatform(ExePlatformClientImport);
		EQEmuLogSys::Instance()->LoadLogSettingsDefaults();
		PathManager::Instance()->Init();

		auto ConfigLoadResult = EQEmuConfig::LoadConfig();
		Config = EQEmuConfig::get();
		std::unique_ptr<Test::Output> output(new Test::TextOutput(Test::TextOutput::Verbose));
		Test::Suite                   tests;
		tests.add(new MemoryMappedFileTest());
		tests.add(new IPCMutexTest());
		tests.add(new FixedMemoryHashTest());
		tests.add(new FixedMemoryVariableHashTest());
		tests.add(new atoboolTest());
		tests.add(new hextoi_32_64_Test());
		tests.add(new StringUtilTest());
		tests.add(new DataVerificationTest());
		tests.add(new BotLootRequestTest());
		tests.add(new FallbackDialogueTest());
		tests.add(new SkillsUtilsTest());
		tests.add(new TaskStateTest());
		const bool success = tests.run(*output, true);
		std::filesystem::current_path(original_cwd);
		std::filesystem::remove_all(runtime_path);
		return success ? 0 : 1;
	}
	catch (std::exception &ex) {
		std::filesystem::current_path(original_cwd);
		std::filesystem::remove_all(runtime_path);
		LogError("Test Failure [{}]", ex.what());
		return 1;
	}
}
