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

#include "common/compression.h"
#include "zone/map_v2.h"
#include "cppunit/cpptest.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

class MapV2Test: public Test::Suite
{
public:
	MapV2Test()
	{
		TEST_ADD(MapV2Test::TestPackedPlacements);
		TEST_ADD(MapV2Test::TestTerrain);
		TEST_ADD(MapV2Test::TestTruncatedRecords);
		TEST_ADD(MapV2Test::TestRejectedData);
	}

private:
	using Bytes = std::vector<char>;

	template <typename T>
	static void Append(Bytes &bytes, T value)
	{
		const char *first = reinterpret_cast<const char *>(&value);
		bytes.insert(bytes.end(), first, first + sizeof(value));
	}

	static void Floats(Bytes &bytes, std::initializer_list<float> values)
	{
		for (float value : values) {
			Append(bytes, value);
		}
	}

	static void Name(Bytes &bytes, const std::string &name)
	{
		bytes.insert(bytes.end(), name.begin(), name.end());
		bytes.push_back(0);
	}

	static Bytes Header(std::initializer_list<uint32> counts, float units = 1.0f)
	{
		Bytes bytes;
		for (uint32 count : counts) {
			Append(bytes, count);
		}
		Append(bytes, units);
		return bytes;
	}

	static void Set(Bytes &bytes, size_t offset, uint32 value)
	{
		std::memcpy(bytes.data() + offset, &value, sizeof(value));
	}

	static Bytes Placements(size_t name_length, float direct_z = 0, float group_z = 0)
	{
		// Three base vertices/indices, one skipped vertex/index, one model,
		// two direct placements and one group containing two placements.
		auto bytes = Header({3, 3, 1, 1, 1, 2, 1, 0, 0});
		Floats(bytes, {0,0,0, 1,0,0, 0,1,0});
		for (uint32 index : {0u, 1u, 2u}) { Append(bytes, index); }
		Floats(bytes, {9,9,9});
		Append(bytes, uint32(0));
		std::string name(name_length, 'm');
		Name(bytes, name);
		Append(bytes, uint32(3));
		Append(bytes, uint32(3));
		Floats(bytes, {1,2,3, 2,2,3, 1,3,3});
		// Packed 13-byte polygons. Nonzero visibility is intentionally not just 1.
		for (uint32 index : {0u,1u,2u}) { Append(bytes, index); }
		Append(bytes, uint8(1));
		for (uint32 index : {0u,2u,1u}) { Append(bytes, index); }
		Append(bytes, uint8(7));
		for (uint32 index : {999u,999u,999u}) { Append(bytes, index); }
		Append(bytes, uint8(0)); // Invisible indices are never dereferenced.
		Name(bytes, name);
		Floats(bytes, {10,20,30, 0,0,direct_z, 2,3,4});
		Name(bytes, "missing");
		Floats(bytes, {0,0,0, 0,0,0, 1,1,1});
		Floats(bytes, {100,200,300, 0,0,group_z, 2,3,4, 10,20,30});
		Append(bytes, uint32(2));
		Name(bytes, name);
		Floats(bytes, {1,2,3, 0,0,0, 2,3,4});
		Name(bytes, "missing");
		Floats(bytes, {0,0,0, 0,0,0, 1,1,1});
		return bytes;
	}

	static Bytes Terrain()
	{
		auto bytes = Header({0,0,0,0,0,0,0,3,1}, 2);
		Append(bytes, uint8(1));
		Floats(bytes, {10,20,5});
		Append(bytes, uint8(0));
		Floats(bytes, {30,40});
		Append(bytes, uint8(0));
		Floats(bytes, {1,2,3,4});
		Append(bytes, uint8(0));
		Floats(bytes, {50,60});
		Append(bytes, uint8(1)); // A hidden terrain quad contributes no geometry.
		Floats(bytes, {1,2,3,4});
		return bytes;
	}

	Bytes Compress(const Bytes &bytes)
	{
		// These tiny fixture streams need only the existing writer's overhead allowance.
		Bytes compressed(bytes.size() + 128);
		uint32 size = EQ::DeflateData(bytes.empty() ? "" : bytes.data(),
			static_cast<uint32>(bytes.size()), compressed.data(), static_cast<uint32>(compressed.size()));
		if (!size) { throw std::runtime_error("Could not compress V2 map fixture"); }
		compressed.resize(size);
		return compressed;
	}

	bool Decode(const Bytes &bytes, MapV2::Mesh &mesh)
	{
		auto compressed = Compress(bytes);
		return MapV2::Decode(compressed.data(), static_cast<uint32>(compressed.size()),
			static_cast<uint32>(bytes.size()), mesh);
	}

	void ExpectMesh(const MapV2::Mesh &mesh, const std::vector<glm::vec3> &vertices,
		const std::vector<uint32> &indices)
	{
		TEST_ASSERT(mesh.vertices.size() == vertices.size());
		TEST_ASSERT(mesh.indices == indices);
		for (size_t i = 0; i < std::min(mesh.vertices.size(), vertices.size()); ++i) {
			TEST_ASSERT(mesh.vertices[i].x == vertices[i].x);
			TEST_ASSERT(mesh.vertices[i].y == vertices[i].y);
			TEST_ASSERT(mesh.vertices[i].z == vertices[i].z);
		}
	}

	void ExpectRejected(const Bytes &compressed, uint32 inflated_size)
	{
		MapV2::Mesh mesh{{{-9,-8,-7}}, {99}};
		TEST_ASSERT(!MapV2::Decode(compressed.data(), static_cast<uint32>(compressed.size()), inflated_size, mesh));
		ExpectMesh(mesh, {{-9,-8,-7}}, {99});
	}

	void Reject(const Bytes &bytes)
	{
		ExpectRejected(Compress(bytes), static_cast<uint32>(bytes.size()));
	}

	void TestPackedPlacements()
	{
		// Before the model name the payload has 104 bytes. Name+NUL lengths
		// 2..5 put the first model scalar at every possible alignment modulo four.
		for (size_t length = 1; length <= 4; ++length) {
			MapV2::Mesh mesh;
			TEST_ASSERT(Decode(Placements(length), mesh));
			ExpectMesh(mesh, {{0,0,0},{1,0,0},{0,1,0},
				{26,12,42},{26,14,42},{29,12,42},{26,12,42},{29,12,42},{26,14,42},
				{244,116,390},{244,120,390},{253,116,390},
				{244,116,390},{253,116,390},{244,120,390}},
				{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14});
		}
		MapV2::Mesh rotated;
		TEST_ASSERT(Decode(Placements(1, std::acos(-1.0f) / 2, 90), rotated));
		TEST_ASSERT(rotated.vertices.size() == 15);
		if (rotated.vertices.size() != 15) { return; }
		// Independent quarter-turn expectations; direct angles are radians,
		// group angles degrees. The production constant is intentionally unchanged.
		const std::vector<glm::vec3> expected{{23,6,42},{26,6,42},{23,4,42},
			{229,94,390},{235,94,390},{229,88,390}};
		const size_t positions[]{3,4,5,9,10,11};
		for (size_t i = 0; i < expected.size(); ++i) {
			auto v = rotated.vertices[positions[i]];
			TEST_ASSERT(std::fabs(v.x - expected[i].x) < 0.001f);
			TEST_ASSERT(std::fabs(v.y - expected[i].y) < 0.001f);
			TEST_ASSERT(std::fabs(v.z - expected[i].z) < 0.001f);
		}
	}

	void TestTerrain()
	{
		MapV2::Mesh mesh;
		TEST_ASSERT(Decode(Terrain(), mesh));
		ExpectMesh(mesh, {{10,20,5},{12,20,5},{12,22,5},{10,22,5},
			{30,40,1},{32,40,3},{32,42,4},{30,42,2}},
			{3,1,2,3,0,1,7,5,6,7,4,5});

		auto grid = Header({0,0,0,0,0,0,0,1,2});
		Append(grid, uint8(0));
		Floats(grid, {0,0});
		for (int i = 0; i < 4; ++i) { Append(grid, uint8(0)); }
		Floats(grid, {0,1,2,3,4,5,6,7,8});
		TEST_ASSERT(Decode(grid, mesh));
		ExpectMesh(mesh, {{0,0,0},{1,0,3},{1,1,4},{0,1,1},{1,2,5},
			{0,2,2},{2,0,6},{2,1,7},{2,2,8}},
			{3,1,2,3,0,1,5,2,4,5,3,2,2,6,7,2,1,6,4,7,8,4,2,7});
	}

	void TestTruncatedRecords()
	{
		for (const auto &complete : {Placements(1), Terrain()}) {
			// Recompress each prefix so rejection must come from the real parser,
			// including partial names/counts/polygons/placements/groups/tiles.
			for (size_t size = 0; size < complete.size(); ++size) {
				Reject(Bytes(complete.begin(), complete.begin() + size));
			}
		}
	}

	void TestRejectedData()
	{
		auto valid = Placements(1);
		auto compressed = Compress(valid);
		ExpectRejected({}, static_cast<uint32>(valid.size()));
		ExpectRejected({'b','a','d'}, static_cast<uint32>(valid.size()));
		ExpectRejected(compressed, 0);
		ExpectRejected(compressed, static_cast<uint32>(valid.size() - 1));
		ExpectRejected(compressed, static_cast<uint32>(valid.size() + 1));
		compressed.pop_back();
		ExpectRejected(compressed, static_cast<uint32>(valid.size()));
		for (size_t field = 0; field < 8; ++field) {
			auto bytes = valid;
			Set(bytes, field * sizeof(uint32), std::numeric_limits<uint32>::max());
			Reject(bytes);
		}
		auto bad_index = valid;
		Set(bad_index, 40 + 3 * 12, 3); // Base mesh has only three vertices.
		Reject(bad_index);
		const size_t model_counts = 104 + 2; // One-byte name and NUL.
		for (size_t offset : {model_counts, model_counts + 4}) {
			auto bytes = valid;
			Set(bytes, offset, std::numeric_limits<uint32>::max());
			Reject(bytes);
		}
		auto bad_poly = valid;
		Set(bad_poly, model_counts + 8 + 3 * 12, 3);
		Reject(bad_poly);
		auto missing_nul = Header({0,0,0,0,1,0,0,0,0});
		missing_nul.insert(missing_nul.end(), 20, 'x');
		Reject(missing_nul);
		auto bad_flag = Terrain();
		bad_flag[40] = 2;
		Reject(bad_flag);
		for (uint32 side : {65535u, std::numeric_limits<uint32>::max()}) {
			auto dimensions = Terrain();
			Set(dimensions, 8 * sizeof(uint32), side);
			Reject(dimensions);
		}
		auto partial_triangle = Header({3,1,0,0,0,0,0,0,0});
		Floats(partial_triangle, {0,0,0, 1,0,0, 0,1,0});
		Append(partial_triangle, uint32(0));
		Reject(partial_triangle);
		MapV2::Mesh unchanged{{{-9,-8,-7}}, {99}};
		TEST_ASSERT(!MapV2::Decode(nullptr, 1, 40, unchanged));
		ExpectMesh(unchanged, {{-9,-8,-7}}, {99});
		Reject(Header({0,0,0,0,0,0,0,0,0})); // No raycast geometry.
		auto trailing = valid;
		trailing.push_back(0);
		Reject(trailing);
	}
};
