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

#include "common/net/packet.h"
#include "cppunit/cpptest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <stdexcept>

class PacketTest : public Test::Suite
{
public:
	PacketTest()
	{
		TEST_ADD(PacketTest::TestScalarBytesAlignment);
		TEST_ADD(PacketTest::TestStaticViewBoundaries);
		TEST_ADD(PacketTest::TestDynamicGrowthOverflow);
	}

private:
	template <typename Check>
	void ForEachScalar(Check check)
	{
		using EQ::Net::Packet;
		check(int8_t{-57}, &Packet::GetInt8, &Packet::PutInt8);
		check(int16_t{-12345}, &Packet::GetInt16, &Packet::PutInt16);
		check(int32_t{-123456789}, &Packet::GetInt32, &Packet::PutInt32);
		check(int64_t{-1234567890123456789LL}, &Packet::GetInt64, &Packet::PutInt64);
		check(uint8_t{213}, &Packet::GetUInt8, &Packet::PutUInt8);
		check(uint16_t{54321}, &Packet::GetUInt16, &Packet::PutUInt16);
		check(uint32_t{3456789012U}, &Packet::GetUInt32, &Packet::PutUInt32);
		check(uint64_t{12345678901234567890ULL}, &Packet::GetUInt64, &Packet::PutUInt64);
		for (float value : {3.25f, 0.0f, -0.0f}) {
			check(value, &Packet::GetFloat, &Packet::PutFloat);
		}
		for (double value : {-123.125, 0.0, -0.0}) {
			check(value, &Packet::GetDouble, &Packet::PutDouble);
		}
	}

	void TestScalarBytesAlignment()
	{
		ForEachScalar([this](auto value, auto get, auto put) {
			// Expected bytes come from a live native object, independently of Packet.
			const auto *native = reinterpret_cast<const unsigned char *>(&value);
			for (size_t base : {size_t{0}, size_t{1}, size_t{5}}) {
				for (size_t offset : {size_t{0}, size_t{1}, size_t{5}, size_t{7}}) {
					alignas(std::max_align_t) std::array<unsigned char, 64> storage;
					storage.fill(0xa5);
					auto expected = storage;
					for (size_t i = 0; i < sizeof(value); ++i) {
						expected[base + offset + i] = native[i];
					}
					storage = expected;
					EQ::Net::StaticPacket packet(storage.data() + base, 32);
					const EQ::Net::Packet &read = packet;
					const auto actual = (read.*get)(offset);
					TEST_ASSERT(std::memcmp(&value, &actual, sizeof(value)) == 0);
					TEST_ASSERT(storage == expected);

					storage.fill(0xa5);
					(packet.*put)(offset, value);
					TEST_ASSERT(storage == expected);
				}
			}
		});
	}

	void TestStaticViewBoundaries()
	{
		ForEachScalar([this](auto value, auto get, auto put) {
			alignas(std::max_align_t) std::array<unsigned char, 64> storage;
			storage.fill(0xa5);
			const size_t length = 3 + sizeof(value);
			// Constructor-original views avoid the separate copy/move capacity defect.
			EQ::Net::StaticPacket packet(storage.data() + 1, length);
			const EQ::Net::Packet &read = packet;
			(packet.*put)(3, value);
			const auto expected = storage;
			const auto actual = (read.*get)(3);
			TEST_ASSERT(std::memcmp(&value, &actual, sizeof(value)) == 0);
			TEST_ASSERT(storage == expected);
			TEST_ASSERT_EQUALS(length, packet.Length());

			for (size_t available = 0; available < sizeof(value); ++available) {
				EQ::Net::StaticPacket truncated(storage.data() + 1, available);
				const EQ::Net::Packet &short_read = truncated;
				TEST_THROWS((short_read.*get)(0), const std::out_of_range &);
			}
			const size_t maximum = std::numeric_limits<size_t>::max();
			for (size_t offset : {size_t{4}, length, length + 1, maximum - sizeof(value) + 1, maximum}) {
				TEST_THROWS((read.*get)(offset), const std::out_of_range &);
				TEST_THROWS((packet.*put)(offset, value), const std::out_of_range &);
				TEST_ASSERT(storage == expected);
				TEST_ASSERT_EQUALS(length, packet.Length());
			}
			TEST_ASSERT(packet.Resize(0));
			TEST_THROWS((read.*get)(0), const std::out_of_range &);
			TEST_ASSERT(!packet.Resize(length + 1));
			TEST_ASSERT_EQUALS(size_t{0}, packet.Length());
			(packet.*put)(3, value);
			TEST_ASSERT_EQUALS(length, packet.Length());
			TEST_ASSERT(storage == expected);
		});
	}

	void TestDynamicGrowthOverflow()
	{
		ForEachScalar([this](auto value, auto get, auto put) {
			EQ::Net::DynamicPacket packet;
			const EQ::Net::Packet &read = packet;
			const size_t maximum = std::numeric_limits<size_t>::max();
			TEST_THROWS((read.*get)(0), const std::out_of_range &);
			TEST_THROWS((read.*get)(maximum), const std::out_of_range &);
			// Every writer offset here overflows even for a one-byte scalar.
			for (size_t offset : {maximum - sizeof(value) + 1, maximum}) {
				TEST_THROWS((packet.*put)(offset, value), const std::out_of_range &);
				TEST_ASSERT_EQUALS(size_t{0}, packet.Length());
			}

			std::array<unsigned char, 64> expected{};
			const auto *native = reinterpret_cast<const unsigned char *>(&value);
			for (size_t offset : {size_t{5}, size_t{1}, size_t{17}}) {
				for (size_t i = 0; i < sizeof(value); ++i) {
					expected[offset + i] = native[i];
				}
				(packet.*put)(offset, value);
				const size_t length = (offset == 1 ? 5 : offset) + sizeof(value);
				TEST_ASSERT_EQUALS(length, packet.Length());
				TEST_ASSERT(std::memcmp(expected.data(), read.Data(), length) == 0);
				const auto actual = (read.*get)(offset);
				TEST_ASSERT(std::memcmp(&value, &actual, sizeof(value)) == 0);
			}
			const size_t length = packet.Length();
			for (size_t offset : {length - sizeof(value) + 1, length, length + 1, maximum}) {
				TEST_THROWS((read.*get)(offset), const std::out_of_range &);
			}
			for (size_t offset : {maximum - sizeof(value) + 1, maximum}) {
				TEST_THROWS((packet.*put)(offset, value), const std::out_of_range &);
				TEST_ASSERT_EQUALS(length, packet.Length());
				TEST_ASSERT(std::memcmp(expected.data(), read.Data(), length) == 0);
			}
		});
	}
};
