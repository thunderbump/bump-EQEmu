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

#include "common/eq_packet_structs.h"
#include "cppunit/cpptest.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

static_assert(alignof(GroundSpawn) >= alignof(std::string), "GroundSpawn must align its string");
static_assert(alignof(GroundSpawns) >= alignof(GroundSpawn), "GroundSpawns must align its elements");
static_assert(sizeof(GroundSpawn) % alignof(std::string) == 0, "GroundSpawn stride must align strings");
static_assert(alignof(Make_Pet_Struct) == 1, "Preceding packet must remain packed");
static_assert(alignof(ApproveZone_Struct) == 1, "Following packet must remain packed");

class GroundSpawnsTest : public Test::Suite
{
public:
	GroundSpawnsTest()
	{
		TEST_ADD(GroundSpawnsTest::TestArrayStorage);
	}

private:
	void TestArrayStorage()
	{
		const auto aligned = [](const auto &value) {
			using Value = std::remove_reference_t<decltype(value)>;
			return reinterpret_cast<std::uintptr_t>(std::addressof(value)) % alignof(Value) == 0;
		};
		GroundSpawns spawns{};
		TEST_ASSERT_EQUALS(std::size_t{50}, std::size(spawns.spawn));
		TEST_ASSERT(aligned(spawns));
		// Include a value larger than the string object without assuming an ABI or inline capacity.
		const std::string names[] = {"ground", std::string(4 * sizeof(std::string), 'g')};

		for (auto &spawn : spawns.spawn) {
			TEST_ASSERT(aligned(spawn));
			TEST_ASSERT(aligned(spawn.max_x));
			TEST_ASSERT(aligned(spawn.max_y));
			TEST_ASSERT(aligned(spawn.min_x));
			TEST_ASSERT(aligned(spawn.min_y));
			TEST_ASSERT(aligned(spawn.max_z));
			TEST_ASSERT(aligned(spawn.heading));
			TEST_ASSERT(aligned(spawn.name));
			TEST_ASSERT(aligned(spawn.item_id));
			TEST_ASSERT(aligned(spawn.max_allowed));
			TEST_ASSERT(aligned(spawn.respawn_timer));
			TEST_ASSERT(aligned(spawn.fix_z));
			TEST_ASSERT_EQUALS(0.0f, spawn.max_x);
			TEST_ASSERT_EQUALS(0.0f, spawn.max_y);
			TEST_ASSERT_EQUALS(0.0f, spawn.min_x);
			TEST_ASSERT_EQUALS(0.0f, spawn.min_y);
			TEST_ASSERT_EQUALS(0.0f, spawn.max_z);
			TEST_ASSERT_EQUALS(0.0f, spawn.heading);
			TEST_ASSERT(spawn.name.empty());
			TEST_ASSERT_EQUALS(uint32{0}, spawn.item_id);
			TEST_ASSERT_EQUALS(uint32{1}, spawn.max_allowed);
			TEST_ASSERT_EQUALS(uint32{1}, spawn.respawn_timer);
			TEST_ASSERT(spawn.fix_z);

			for (const auto &name : names) {
				spawn.name = name;
				TEST_ASSERT_EQUALS(name, spawn.name);
				std::string copied(spawn.name);
				TEST_ASSERT_EQUALS(name, copied);
				copied.clear();
				TEST_ASSERT(copied.empty());
				copied = spawn.name;
				TEST_ASSERT_EQUALS(name, copied);
				std::string moved(std::move(copied));
				TEST_ASSERT_EQUALS(name, moved);
				spawn.name.clear();
				TEST_ASSERT(spawn.name.empty());
				spawn.name = std::move(moved);
				TEST_ASSERT_EQUALS(name, spawn.name);
				spawn.name.clear();
				TEST_ASSERT(spawn.name.empty());
			}
		}
	}
};
