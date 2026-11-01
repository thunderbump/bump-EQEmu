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
#pragma once

#include "common/eqemu_config.h"
#include "cppunit/cpptest.h"

class EQEmuConfigTest : public Test::Suite {
public:
	EQEmuConfigTest()
	{
		TEST_ADD(EQEmuConfigTest::DisableConfigChecks);
	}

private:
	class ConfigFixture : public EQEmuConfig {
	public:
		ConfigFixture() = default;

		void ParseFlag(const Json::Value& value = Json::Value())
		{
			_root = Json::Value(Json::objectValue);
			// Keep this instance-local parse out of legacy UCS file migration.
			_root["server"]["ucs"]["host"] = "localhost";
			if (!value.isNull()) {
				_root["server"]["disable_config_checks"] = value;
			}
			parse_config();
		}
	};

	void DisableConfigChecks()
	{
		ConfigFixture config;
		TEST_ASSERT(!config.DisableConfigChecks);
		config.ParseFlag();
		TEST_ASSERT(!config.DisableConfigChecks);
		config.ParseFlag("true");
		TEST_ASSERT(config.DisableConfigChecks);
		config.ParseFlag("false");
		TEST_ASSERT(!config.DisableConfigChecks);
		config.ParseFlag(true);
		TEST_ASSERT(config.DisableConfigChecks);
		config.ParseFlag(false);
		TEST_ASSERT(!config.DisableConfigChecks);
		config.ParseFlag("true");
		config.ParseFlag();
		TEST_ASSERT(!config.DisableConfigChecks);
	}
};
