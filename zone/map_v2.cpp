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
#include "map_v2.h"
#include "common/compression.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace
{
void RotateVertex(glm::vec3 &v, float rx, float ry, float rz) {
	glm::vec3 nv = v;

	nv.y = (std::cos(rx) * v.y) - (std::sin(rx) * v.z);
	nv.z = (std::sin(rx) * v.y) + (std::cos(rx) * v.z);

	v = nv;

	nv.x = (std::cos(ry) * v.x) + (std::sin(ry) * v.z);
	nv.z = -(std::sin(ry) * v.x) + (std::cos(ry) * v.z);

	v = nv;

	nv.x = (std::cos(rz) * v.x) - (std::sin(rz) * v.y);
	nv.y = (std::sin(rz) * v.x) + (std::cos(rz) * v.y);

	v = nv;
}

void ScaleVertex(glm::vec3 &v, float sx, float sy, float sz) {
	v.x = v.x * sx;
	v.y = v.y * sy;
	v.z = v.z * sz;
}

void TranslateVertex(glm::vec3 &v, float tx, float ty, float tz) {
	v.x = v.x + tx;
	v.y = v.y + ty;
	v.z = v.z + tz;
}

struct ModelEntry
{
	struct Poly
	{
		uint32 v1, v2, v3;
		uint8 vis;
	};
	std::vector<glm::vec3> verts;
	std::vector<Poly> polys;
};

// V2 is a byte stream, not an array of aligned C++ objects.
struct InvalidData {};

class Cursor
{
public:
	Cursor(const char *data, size_t size): m_data(data), m_remaining(size) {}

	void Require(size_t count, size_t width) const
	{
		if (count > m_remaining / width) {
			throw InvalidData{};
		}
	}

	template <typename T>
	T Read()
	{
		Require(1, sizeof(T));
		T value;
		std::memcpy(&value, m_data, sizeof(value));
		Skip(1, sizeof(T));
		return value;
	}

	std::string String()
	{
		const char *end = static_cast<const char *>(std::memchr(m_data, 0, m_remaining));
		if (!end) {
			throw InvalidData{};
		}
		std::string value(m_data, end);
		Skip(value.size() + 1, 1);
		return value;
	}

	void Skip(size_t count, size_t width)
	{
		Require(count, width);
		size_t bytes = count * width;
		m_data += bytes;
		m_remaining -= bytes;
	}

	bool Empty() const { return m_remaining == 0; }

private:
	const char *m_data;
	size_t m_remaining;
};

template <typename T>
void RequireCapacity(const std::vector<T> &values, size_t count)
{
	if (count > values.max_size()) {
		throw InvalidData{};
	}
}

void RequireAppend(const std::vector<glm::vec3> &vertices, const std::vector<uint32> &indices,
	size_t vertex_count, size_t index_count)
{
	const auto limit = std::numeric_limits<uint32>::max();
	if (vertices.size() > limit || vertex_count > limit - vertices.size() ||
		indices.size() > limit || index_count > limit - indices.size()) {
		throw InvalidData{};
	}
	RequireCapacity(vertices, vertices.size() + vertex_count);
	RequireCapacity(indices, indices.size() + index_count);
}

void RequireIndices(const ModelEntry::Poly &poly, size_t vertex_count)
{
	if (poly.v1 >= vertex_count || poly.v2 >= vertex_count || poly.v3 >= vertex_count) {
		throw InvalidData{};
	}
}

MapV2::Mesh DecodePayload(const std::vector<char> &buffer)
{
	Cursor reader(buffer.data(), buffer.size());
	uint32 vert_count;
	uint32 ind_count;
	uint32 nc_vert_count;
	uint32 nc_ind_count;
	uint32 model_count;
	uint32 plac_count;
	uint32 plac_group_count;
	uint32 tile_count;
	uint32 quads_per_tile;
	float units_per_vertex;

	vert_count = reader.Read<uint32>();

	ind_count = reader.Read<uint32>();

	nc_vert_count = reader.Read<uint32>();

	nc_ind_count = reader.Read<uint32>();

	model_count = reader.Read<uint32>();

	plac_count = reader.Read<uint32>();

	plac_group_count = reader.Read<uint32>();

	tile_count = reader.Read<uint32>();

	quads_per_tile = reader.Read<uint32>();

	units_per_vertex = reader.Read<float>();

	reader.Require(vert_count, 3 * sizeof(float));
	std::vector<glm::vec3> verts;
	RequireCapacity(verts, vert_count);
	verts.reserve(vert_count);
	std::vector<uint32> indices;

	for (uint32 i = 0; i < vert_count; ++i) {
		float x;
		float y;
		float z;

		x = reader.Read<float>();

		y = reader.Read<float>();

		z = reader.Read<float>();

		verts.emplace_back(x, y, z);
	}

	reader.Require(ind_count, sizeof(uint32));
	RequireCapacity(indices, ind_count);
	indices.reserve(ind_count);
	for (uint32 i = 0; i < ind_count; ++i) {
		uint32 index = reader.Read<uint32>();
		if (index >= verts.size()) {
			throw InvalidData{};
		}
		indices.emplace_back(index);
	}

	reader.Skip(nc_vert_count, 3 * sizeof(float));
	reader.Skip(nc_ind_count, sizeof(uint32));

	reader.Require(model_count, 1 + 2 * sizeof(uint32));
	std::map<std::string, std::unique_ptr<ModelEntry>> models;
	for (uint32 i = 0; i < model_count; ++i) {
		std::unique_ptr<ModelEntry> me(new ModelEntry);
		std::string name = reader.String();

		uint32 vert_count = reader.Read<uint32>();

		uint32 poly_count = reader.Read<uint32>();

		reader.Require(vert_count, 3 * sizeof(float));
		me->RequireCapacity(verts, vert_count);
	verts.reserve(vert_count);
		for (uint32 j = 0; j < vert_count; ++j) {
			float x = reader.Read<float>();
			float y = reader.Read<float>();
			float z = reader.Read<float>();

			me->verts.emplace_back(x, y, z);
		}

		reader.Require(poly_count, 3 * sizeof(uint32) + sizeof(uint8));
		RequireCapacity(me->polys, poly_count);
		me->polys.reserve(poly_count);
		for (uint32 j = 0; j < poly_count; ++j) {
			uint32 v1 = reader.Read<uint32>();
			uint32 v2 = reader.Read<uint32>();
			uint32 v3 = reader.Read<uint32>();
			uint8 vis = reader.Read<uint8>();

			ModelEntry::Poly p;
			p.v1 = v1;
			p.v2 = v2;
			p.v3 = v3;
			p.vis = vis;
			me->polys.push_back(p);
		}

		models[name] = std::move(me);
	}

	reader.Require(plac_count, 1 + 9 * sizeof(float));
	for (uint32 i = 0; i < plac_count; ++i) {
		std::string name = reader.String();

		float x = reader.Read<float>();
		float y = reader.Read<float>();
		float z = reader.Read<float>();

		float x_rot = reader.Read<float>();
		float y_rot = reader.Read<float>();
		float z_rot = reader.Read<float>();

		float x_scale = reader.Read<float>();
		float y_scale = reader.Read<float>();
		float z_scale = reader.Read<float>();

		if (models.count(name) == 0)
			continue;

		auto &model = models[name];
		auto &mod_polys = model->polys;
		auto &mod_verts = model->verts;
		for (uint32 j = 0; j < mod_polys.size(); ++j) {
			auto &current_poly = mod_polys[j];
			if (current_poly.vis == 0)
				continue;
			RequireIndices(current_poly, mod_verts.size());
			RequireAppend(verts, indices, 3, 3);
			auto v1 = mod_verts[current_poly.v1];
			auto v2 = mod_verts[current_poly.v2];
			auto v3 = mod_verts[current_poly.v3];

			RotateVertex(v1, x_rot, y_rot, z_rot);
			RotateVertex(v2, x_rot, y_rot, z_rot);
			RotateVertex(v3, x_rot, y_rot, z_rot);

			ScaleVertex(v1, x_scale, y_scale, z_scale);
			ScaleVertex(v2, x_scale, y_scale, z_scale);
			ScaleVertex(v3, x_scale, y_scale, z_scale);

			TranslateVertex(v1, x, y, z);
			TranslateVertex(v2, x, y, z);
			TranslateVertex(v3, x, y, z);

			verts.emplace_back(v1.y, v1.x, v1.z); // x/y swapped
			verts.emplace_back(v2.y, v2.x, v2.z);
			verts.emplace_back(v3.y, v3.x, v3.z);

			indices.emplace_back((uint32)verts.size() - 3);
			indices.emplace_back((uint32)verts.size() - 2);
			indices.emplace_back((uint32)verts.size() - 1);
		}
	}

	reader.Require(plac_group_count, 12 * sizeof(float) + sizeof(uint32));
	for (uint32 i = 0; i < plac_group_count; ++i) {
		float x = reader.Read<float>();
		float y = reader.Read<float>();
		float z = reader.Read<float>();

		float x_rot = reader.Read<float>();
		float y_rot = reader.Read<float>();
		float z_rot = reader.Read<float>();

		float x_scale = reader.Read<float>();
		float y_scale = reader.Read<float>();
		float z_scale = reader.Read<float>();

		float x_tile = reader.Read<float>();
		float y_tile = reader.Read<float>();
		float z_tile = reader.Read<float>();

		uint32 p_count = reader.Read<uint32>();

		reader.Require(p_count, 1 + 9 * sizeof(float));
		for (uint32 j = 0; j < p_count; ++j) {
			std::string name = reader.String();

			float p_x = reader.Read<float>();
			float p_y = reader.Read<float>();
			float p_z = reader.Read<float>();

			float p_x_rot = reader.Read<float>() * 3.14159f / 180;
			float p_y_rot = reader.Read<float>() * 3.14159f / 180;
			float p_z_rot = reader.Read<float>() * 3.14159f / 180;

			float p_x_scale = reader.Read<float>();
			float p_y_scale = reader.Read<float>();
			float p_z_scale = reader.Read<float>();

			if (models.count(name) == 0)
				continue;

			auto &model = models[name];

			for (size_t k = 0; k < model->polys.size(); ++k) {
				auto &poly = model->polys[k];
				if (poly.vis == 0)
					continue;
				RequireIndices(poly, model->verts.size());
				RequireAppend(verts, indices, 3, 3);
				glm::vec3 v1, v2, v3;

				v1 = model->verts[poly.v1];
				v2 = model->verts[poly.v2];
				v3 = model->verts[poly.v3];

				ScaleVertex(v1, p_x_scale, p_y_scale, p_z_scale);
				ScaleVertex(v2, p_x_scale, p_y_scale, p_z_scale);
				ScaleVertex(v3, p_x_scale, p_y_scale, p_z_scale);

				TranslateVertex(v1, p_x, p_y, p_z);
				TranslateVertex(v2, p_x, p_y, p_z);
				TranslateVertex(v3, p_x, p_y, p_z);

				RotateVertex(v1, x_rot * 3.14159f / 180.0f, 0, 0);
				RotateVertex(v2, x_rot * 3.14159f / 180.0f, 0, 0);
				RotateVertex(v3, x_rot * 3.14159f / 180.0f, 0, 0);

				RotateVertex(v1, 0, y_rot * 3.14159f / 180.0f, 0);
				RotateVertex(v2, 0, y_rot * 3.14159f / 180.0f, 0);
				RotateVertex(v3, 0, y_rot * 3.14159f / 180.0f, 0);

				glm::vec3 correction(p_x, p_y, p_z);

				RotateVertex(correction, x_rot * 3.14159f / 180.0f, 0, 0);

				TranslateVertex(v1, -correction.x, -correction.y, -correction.z);
				TranslateVertex(v2, -correction.x, -correction.y, -correction.z);
				TranslateVertex(v3, -correction.x, -correction.y, -correction.z);

				RotateVertex(v1, p_x_rot, 0, 0);
				RotateVertex(v2, p_x_rot, 0, 0);
				RotateVertex(v3, p_x_rot, 0, 0);

				RotateVertex(v1, 0, -p_y_rot, 0);
				RotateVertex(v2, 0, -p_y_rot, 0);
				RotateVertex(v3, 0, -p_y_rot, 0);

				RotateVertex(v1, 0, 0, p_z_rot);
				RotateVertex(v2, 0, 0, p_z_rot);
				RotateVertex(v3, 0, 0, p_z_rot);

				TranslateVertex(v1, correction.x, correction.y, correction.z);
				TranslateVertex(v2, correction.x, correction.y, correction.z);
				TranslateVertex(v3, correction.x, correction.y, correction.z);

				RotateVertex(v1, 0, 0, z_rot * 3.14159f / 180.0f);
				RotateVertex(v2, 0, 0, z_rot * 3.14159f / 180.0f);
				RotateVertex(v3, 0, 0, z_rot * 3.14159f / 180.0f);

				ScaleVertex(v1, x_scale, y_scale, z_scale);
				ScaleVertex(v2, x_scale, y_scale, z_scale);
				ScaleVertex(v3, x_scale, y_scale, z_scale);

				TranslateVertex(v1, x_tile, y_tile, z_tile);
				TranslateVertex(v2, x_tile, y_tile, z_tile);
				TranslateVertex(v3, x_tile, y_tile, z_tile);

				TranslateVertex(v1, x, y, z);
				TranslateVertex(v2, x, y, z);
				TranslateVertex(v3, x, y, z);

				verts.emplace_back(v1.y, v1.x, v1.z); // x/y swapped
				verts.emplace_back(v2.y, v2.x, v2.z);
				verts.emplace_back(v3.y, v3.x, v3.z);

				indices.emplace_back((uint32)verts.size() - 3);
				indices.emplace_back((uint32)verts.size() - 2);
				indices.emplace_back((uint32)verts.size() - 1);
			}
		}
	}

	reader.Require(tile_count, 1 + 3 * sizeof(float));
	uint64 side = quads_per_tile;
	uint64 quad_count = tile_count ? side * side : 0;
	// Check addition before squaring: UINT32_MAX + 1 squared exceeds uint64.
	if (tile_count && side == std::numeric_limits<uint32>::max()) {
		throw InvalidData{};
	}
	uint64 vertex_count = tile_count ? (side + 1) * (side + 1) : 0;
	if (quad_count > std::numeric_limits<uint32>::max() ||
		vertex_count > std::numeric_limits<uint32>::max()) {
		throw InvalidData{};
	}
	uint32 ter_quad_count = static_cast<uint32>(quad_count);
	uint32 ter_vert_count = static_cast<uint32>(vertex_count);
	std::vector<uint8> flags;
	std::vector<float> floats;
	for (uint32 i = 0; i < tile_count; ++i) {
		uint8 flat_byte = reader.Read<uint8>();
		if (flat_byte > 1) {
			throw InvalidData{};
		}
		bool flat = flat_byte == 1;

		float x;
		x = reader.Read<float>();

		float y;
		y = reader.Read<float>();

		if (flat) {
			RequireAppend(verts, indices, 4, 6);
			float z;
			z = reader.Read<float>();

			float QuadVertex1X = x;
			float QuadVertex1Y = y;
			float QuadVertex1Z = z;

			float QuadVertex2X = QuadVertex1X + (quads_per_tile * units_per_vertex);
			float QuadVertex2Y = QuadVertex1Y;
			float QuadVertex2Z = QuadVertex1Z;

			float QuadVertex3X = QuadVertex2X;
			float QuadVertex3Y = QuadVertex1Y + (quads_per_tile * units_per_vertex);
			float QuadVertex3Z = QuadVertex1Z;

			float QuadVertex4X = QuadVertex1X;
			float QuadVertex4Y = QuadVertex3Y;
			float QuadVertex4Z = QuadVertex1Z;

			uint32 current_vert = (uint32)verts.size() + 3;
			verts.emplace_back(QuadVertex1X, QuadVertex1Y, QuadVertex1Z);
			verts.emplace_back(QuadVertex2X, QuadVertex2Y, QuadVertex2Z);
			verts.emplace_back(QuadVertex3X, QuadVertex3Y, QuadVertex3Z);
			verts.emplace_back(QuadVertex4X, QuadVertex4Y, QuadVertex4Z);

			indices.emplace_back(current_vert);
			indices.emplace_back(current_vert - 2);
			indices.emplace_back(current_vert - 1);

			indices.emplace_back(current_vert);
			indices.emplace_back(current_vert - 3);
			indices.emplace_back(current_vert - 2);
		}
		else {
			reader.Require(ter_quad_count, sizeof(uint8));
			RequireCapacity(flags, ter_quad_count);
			flags.resize(ter_quad_count);
			//read flags
			for (uint32 j = 0; j < ter_quad_count; ++j) {
				uint8 f;
				f = reader.Read<uint8>();

				flags[j] = f;
			}

			reader.Require(ter_vert_count, sizeof(float));
			RequireCapacity(floats, ter_vert_count);
			floats.resize(ter_vert_count);
			//read floats
			for (uint32 j = 0; j < ter_vert_count; ++j) {
				float f;
				f = reader.Read<float>();

				floats[j] = f;
			}

			int row_number = -1;
			std::map<std::tuple<float, float, float>, uint32> cur_verts;
			for (uint32 quad = 0; quad < ter_quad_count; ++quad) {
				if ((quad % quads_per_tile) == 0) {
					++row_number;
				}

				if (flags[quad] & 0x01)
					continue;

				float QuadVertex1X = x + (row_number * units_per_vertex);
				float QuadVertex1Y = y + (quad % quads_per_tile) * units_per_vertex;
				float QuadVertex1Z = floats[quad + row_number];

				float QuadVertex2X = QuadVertex1X + units_per_vertex;
				float QuadVertex2Y = QuadVertex1Y;
				float QuadVertex2Z = floats[quad + row_number + quads_per_tile + 1];

				float QuadVertex3X = QuadVertex1X + units_per_vertex;
				float QuadVertex3Y = QuadVertex1Y + units_per_vertex;
				float QuadVertex3Z = floats[quad + row_number + quads_per_tile + 2];

				float QuadVertex4X = QuadVertex1X;
				float QuadVertex4Y = QuadVertex1Y + units_per_vertex;
				float QuadVertex4Z = floats[quad + row_number + 1];

				RequireAppend(verts, indices, 0, 6);
				uint32 i1, i2, i3, i4;
				std::tuple<float, float, float> t = std::make_tuple(QuadVertex1X, QuadVertex1Y, QuadVertex1Z);
				auto iter = cur_verts.find(t);
				if (iter != cur_verts.end()) {
					i1 = iter->second;
				}
				else {
					RequireAppend(verts, indices, 1, 0);
					i1 = (uint32)verts.size();
					verts.emplace_back(QuadVertex1X, QuadVertex1Y, QuadVertex1Z);
					cur_verts[std::make_tuple(QuadVertex1X, QuadVertex1Y, QuadVertex1Z)] = i1;
				}

				t = std::make_tuple(QuadVertex2X, QuadVertex2Y, QuadVertex2Z);
				iter = cur_verts.find(t);
				if (iter != cur_verts.end()) {
					i2 = iter->second;
				}
				else {
					RequireAppend(verts, indices, 1, 0);
					i2 = (uint32)verts.size();
					verts.emplace_back(QuadVertex2X, QuadVertex2Y, QuadVertex2Z);
					cur_verts[std::make_tuple(QuadVertex2X, QuadVertex2Y, QuadVertex2Z)] = i2;
				}

				t = std::make_tuple(QuadVertex3X, QuadVertex3Y, QuadVertex3Z);
				iter = cur_verts.find(t);
				if (iter != cur_verts.end()) {
					i3 = iter->second;
				}
				else {
					RequireAppend(verts, indices, 1, 0);
					i3 = (uint32)verts.size();
					verts.emplace_back(QuadVertex3X, QuadVertex3Y, QuadVertex3Z);
					cur_verts[std::make_tuple(QuadVertex3X, QuadVertex3Y, QuadVertex3Z)] = i3;
				}

				t = std::make_tuple(QuadVertex4X, QuadVertex4Y, QuadVertex4Z);
				iter = cur_verts.find(t);
				if (iter != cur_verts.end()) {
					i4 = iter->second;
				}
				else {
					RequireAppend(verts, indices, 1, 0);
					i4 = (uint32)verts.size();
					verts.emplace_back(QuadVertex4X, QuadVertex4Y, QuadVertex4Z);
					cur_verts[std::make_tuple(QuadVertex4X, QuadVertex4Y, QuadVertex4Z)] = i4;
				}

				indices.emplace_back(i4);
				indices.emplace_back(i2);
				indices.emplace_back(i3);

				indices.emplace_back(i4);
				indices.emplace_back(i1);
				indices.emplace_back(i2);
			}
		}
	}

	if (!reader.Empty() || verts.empty() || indices.empty() || indices.size() % 3 != 0) {
		throw InvalidData{};
	}
	return {std::move(verts), std::move(indices)};
}

}

bool MapV2::Decode(const char *data, std::uint32_t data_size, std::uint32_t buffer_size, Mesh &output)
{
	if (!data || !data_size || buffer_size < 9 * sizeof(uint32) + sizeof(float)) {
		return false;
	}
	std::vector<char> buffer;
	if (buffer_size > buffer.max_size()) {
		return false;
	}
	buffer.resize(buffer_size);
	if (EQ::InflateData(data, data_size, buffer.data(), buffer_size) != buffer_size) {
		return false;
	}
	try {
		auto mesh = DecodePayload(buffer);
		output.vertices.swap(mesh.vertices);
		output.indices.swap(mesh.indices);
		return true;
	}
	catch (const InvalidData &) {
		return false;
	}
}
