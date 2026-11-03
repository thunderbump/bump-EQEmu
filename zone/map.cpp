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
#include "common/compression.h"
#include "common/file.h"
#include "common/memory/ksm.h"
#include "common/misc_functions.h"
#include "zone/client.h"
#include "zone/map.h"
#include "zone/map_v2.h"
#include "zone/raycast_mesh.h"
#include "zone/zone.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

#if defined(__linux__)
#include <time.h>
#endif

namespace {

// Temporary diagnostic: repeat earlier samples in the latest bounded worker context.
// KiB values are Linux /proc/self/status samples, not allocation or phase peaks.
class MapMemoryDiagnostic
{
public:

	enum class Stage { BeforeDecode, AfterDecode, AfterRaycast };

	MapMemoryDiagnostic() noexcept
	{
		const int saved_errno = errno;
#if defined(__linux__)
		started_available_ = clock_gettime(CLOCK_MONOTONIC, &started_) == 0 && started_.tv_sec >= 0;
#endif
		errno = saved_errno;
	}

	void Record(Stage stage, const MapV2::Mesh *mesh = nullptr) noexcept
	{
		const int saved_errno = errno;
		char elapsed[21] = "unavailable";
		char rss[21] = "unavailable";
		char hwm[21] = "unavailable";
#if defined(__linux__)
		timespec now{};
		if (started_available_ && clock_gettime(CLOCK_MONOTONIC, &now) == 0 &&
			(now.tv_sec > started_.tv_sec || (now.tv_sec == started_.tv_sec && now.tv_nsec >= started_.tv_nsec))) {
			auto seconds = static_cast<unsigned long long>(now.tv_sec - started_.tv_sec);
			long nanoseconds = now.tv_nsec - started_.tv_nsec;
			if (nanoseconds < 0) {
				--seconds;
				nanoseconds += 1000000000L;
			}
			if (seconds <= (std::numeric_limits<unsigned long long>::max() - 999) / 1000) {
				std::snprintf(elapsed, sizeof(elapsed), "%llu", seconds * 1000 + nanoseconds / 1000000);
			}
		}

		if (FILE *status = std::fopen("/proc/self/status", "r")) {
			char line[160];
			const auto read_kib = [&line](const char *key, char *value, std::size_t capacity) {
				const auto key_length = std::strlen(key);
				if (std::strncmp(line, key, key_length) != 0) {
					return;
				}
				const char *begin = line + key_length;
				while (*begin == ' ' || *begin == '\t') {
					++begin;
				}
				unsigned long long kib;
				const auto parsed = std::from_chars(begin, line + std::strlen(line), kib);
				if (parsed.ec == std::errc{} && std::strcmp(parsed.ptr, " kB\n") == 0) {
					// Linux's status "kB" denotes 1024 bytes.
					std::snprintf(value, capacity, "%llu", kib);
				}
			};
			for (unsigned int lines = 0; lines < 128 && std::fgets(line, sizeof(line), status); ++lines) {
				read_kib("VmRSS:", rss, sizeof(rss));
				read_kib("VmHWM:", hwm, sizeof(hwm));
			}
			std::fclose(status);
		}
#endif
		const auto index = static_cast<std::size_t>(stage);
		std::snprintf(samples_[index], sizeof(samples_[index]), "%s,%s,%s", elapsed, rss, hwm);
		char counts[128] = "v=unavailable i=unavailable t=unavailable";
		if (mesh) {
			std::snprintf(counts, sizeof(counts), "v=%zu i=%zu t=%zu",
				mesh->vertices.size(), mesh->indices.size(), mesh->indices.size() / 3);
		}
		const char *stages[] = {"pre_decode", "post_decode", "post_raycast"};
		// Fixed ASCII fields fit below the worker's 500-byte encoded context limit.
		char context[480];
		const int length = std::snprintf(context, sizeof(context),
			"EQEMU_MAP_MEMORY_V1 stage=%s elapsed_origin=pre_decode scope=process hwm_scope=lifetime "
			"pre_ms_rssKiB_hwmKiB=%s decoded_ms_rssKiB_hwmKiB=%s raycast_ms_rssKiB_hwmKiB=%s %s",
			stages[index], samples_[0], samples_[1], samples_[2], counts);
		if (length >= 0 && static_cast<std::size_t>(length) < sizeof(context)) {
			std::fprintf(stderr, "EQEMU_NATIVE_DIAGNOSTIC_CONTEXT %s\n", context);
		} else {
			std::fprintf(stderr, "EQEMU_NATIVE_DIAGNOSTIC_CONTEXT EQEMU_MAP_MEMORY_V1 stage=%s record=unavailable\n", stages[index]);
		}
		std::fflush(stderr);
		errno = saved_errno;
	}

private:
	char samples_[3][64] = {"not_sampled", "not_sampled", "not_sampled"};
#if defined(__linux__)
	timespec started_{};
	bool started_available_ = false;
#endif
};

} // namespace

struct Map::impl
{
	RaycastMesh *rm;
};

Map::Map() {
	imp = nullptr;
}

Map::~Map() {
	if(imp) {
		imp->rm->release();
		safe_delete(imp);
	}
}

float Map::FindBestZ(glm::vec3 &start, glm::vec3 *result) const {
	if (!imp) {
		return BEST_Z_INVALID;
	}

	glm::vec3 tmp;
	if (!result) {
		result = &tmp;
	}

	start.z += RuleI(Map, FindBestZHeightAdjust);
	glm::vec3 from(start.x, start.y, start.z);
	glm::vec3 to(start.x, start.y, BEST_Z_INVALID);
	float hit_distance;
	bool hit = false;

	hit = imp->rm->raycast((const RmReal*)&from, (const RmReal*)&to, (RmReal*)result, nullptr, &hit_distance);
	if (hit && zone->newzone_data.underworld != 0.0f && result->z < zone->newzone_data.underworld) {
		hit = false;
	}

	if (hit) {
		return result->z;
	}

	// Find nearest Z above us
	to.z = -BEST_Z_INVALID;
	hit = imp->rm->raycast((const RmReal*)&from, (const RmReal*)&to, (RmReal*)result, nullptr, &hit_distance);
	if (zone->newzone_data.max_z != 0.0f && result->z > zone->newzone_data.max_z) {
		hit = false;
	}

	if (hit) {
		return result->z;
	}

	return BEST_Z_INVALID;
}

float Map::FindClosestZ(glm::vec3 &start, glm::vec3 *result) const {
	// Unlike FindBestZ, this method finds the closest Z value above or below the specified point.
	//
	if (!imp)
		return false;

	float ClosestZ = BEST_Z_INVALID;

	glm::vec3 tmp;
	if (!result)
		result = &tmp;

	glm::vec3 from(start.x, start.y, start.z);
	glm::vec3 to(start.x, start.y, BEST_Z_INVALID);
	float hit_distance;
	bool hit = false;

	// first check is below us
	hit = imp->rm->raycast((const RmReal*)&from, (const RmReal*)&to, (RmReal*)result, nullptr, &hit_distance);
	if (hit) {
		ClosestZ = result->z;

	}

	// Find nearest Z above us
	to.z = -BEST_Z_INVALID;
	hit = imp->rm->raycast((const RmReal*)&from, (const RmReal*)&to, (RmReal*)result, nullptr, &hit_distance);
	if (hit) {
		if (std::abs(from.z - result->z) < std::abs(ClosestZ - from.z))
			return result->z;
	}

	return ClosestZ;
}

float Map::FindCeiling(glm::vec3 &start, glm::vec3 *result) const {
	// Unlike FindBestZ, this method finds the closest Z above point.

	if (!imp) {
		return false;
	}

	glm::vec3 tmp;
	if (!result) {
		result = &tmp;
	}

	glm::vec3 from(start.x, start.y, start.z);
	glm::vec3 to(start.x, start.y, -BEST_Z_INVALID);
	float hit_distance;
	bool hit = false;

	// Find nearest Z above us
	hit = imp->rm->raycast((const RmReal*)&from, (const RmReal*)&to, (RmReal*)result, nullptr, &hit_distance);
	if (hit) {
		return result->z;
	}

	return BEST_Z_INVALID;
}

float Map::FindGround(glm::vec3 &start, glm::vec3 *result) const {
	// Unlike FindBestZ, this method finds the closest Z below point.

	if (!imp) {
		return false;
	}

	glm::vec3 tmp;

	if (!result) {
		result = &tmp;
	}

	glm::vec3 from(start.x, start.y, start.z);
	glm::vec3 to(start.x, start.y, BEST_Z_INVALID);
	float hit_distance;
	bool hit = false;

	// Find nearest Z below us
	hit = imp->rm->raycast((const RmReal*)&from, (const RmReal*)&to, (RmReal*)result, nullptr, &hit_distance);

	if (hit && zone->newzone_data.underworld != 0.0f && result->z < zone->newzone_data.underworld) {
		hit = false;
	}

	if (hit) {
		return result->z;
	}

	return BEST_Z_INVALID;
}

bool Map::LineIntersectsZone(glm::vec3 start, glm::vec3 end, float step, glm::vec3 *result) const {
	if(!imp)
		return false;
	return imp->rm->raycast((const RmReal*)&start, (const RmReal*)&end, (RmReal*)result, nullptr, nullptr);
}

bool Map::LineIntersectsZoneNoZLeaps(glm::vec3 start, glm::vec3 end, float step_mag, glm::vec3 *result) const {
	if (!imp)
		return false;

	float z = BEST_Z_INVALID;
	glm::vec3 step;
	glm::vec3 cur;
	cur.x = start.x;
	cur.y = start.y;
	cur.z = start.z;

	step.x = end.x - start.x;
	step.y = end.y - start.y;
	step.z = end.z - start.z;
	float factor = step_mag / sqrt(step.x*step.x + step.y*step.y + step.z*step.z);

	step.x *= factor;
	step.y *= factor;
	step.z *= factor;

	int steps = 0;

	if (step.x > 0 && step.x < 0.001f)
		step.x = 0.001f;
	if (step.y > 0 && step.y < 0.001f)
		step.y = 0.001f;
	if (step.z > 0 && step.z < 0.001f)
		step.z = 0.001f;
	if (step.x < 0 && step.x > -0.001f)
		step.x = -0.001f;
	if (step.y < 0 && step.y > -0.001f)
		step.y = -0.001f;
	if (step.z < 0 && step.z > -0.001f)
		step.z = -0.001f;

	//while we are not past end
	//always do this once, even if start == end.
	while(cur.x != end.x || cur.y != end.y || cur.z != end.z)
	{
		steps++;
		glm::vec3 me;
		me.x = cur.x;
		me.y = cur.y;
		me.z = cur.z;
		glm::vec3 hit;

		float best_z = FindBestZ(me, &hit);
		float diff = best_z - z;
		diff = diff < 0 ? -diff : diff;

		if (z <= BEST_Z_INVALID || best_z <= BEST_Z_INVALID || diff < 12.0)
			z = best_z;
		else
			return true;

		//look at current location
		if(LineIntersectsZone(start, end, step_mag, result))
		{
			return true;
		}

		//move 1 step
		if (cur.x != end.x)
			cur.x += step.x;
		if (cur.y != end.y)
			cur.y += step.y;
		if (cur.z != end.z)
			cur.z += step.z;

		//watch for end conditions
		if ( (cur.x > end.x && end.x >= start.x) || (cur.x < end.x && end.x <= start.x) || (step.x == 0) ) {
			cur.x = end.x;
		}
		if ( (cur.y > end.y && end.y >= start.y) || (cur.y < end.y && end.y <= start.y) || (step.y == 0) ) {
			cur.y = end.y;
		}
		if ( (cur.z > end.z && end.z >= start.z) || (cur.z < end.z && end.z < start.z) || (step.z == 0) ) {
			cur.z = end.z;
		}
	}

	//walked entire line and didnt run into anything...
	return false;
}

bool Map::CheckLoS(glm::vec3 myloc, glm::vec3 oloc) const {
	if(!imp)
		return false;

	return !imp->rm->raycast((const RmReal*)&myloc, (const RmReal*)&oloc, nullptr, nullptr, nullptr);
}

// returns true if a collision happens
bool Map::DoCollisionCheck(glm::vec3 myloc, glm::vec3 oloc, glm::vec3 &outnorm, float &distance) const {
	if(!imp)
		return false;

	return imp->rm->raycast((const RmReal*)&myloc, (const RmReal*)&oloc, nullptr, (RmReal *)&outnorm, (RmReal *)&distance);
}

Map *Map::LoadMapFile(std::string file) {
	std::transform(file.begin(), file.end(), file.begin(), ::tolower);
	std::string filename = fmt::format("{}/base/{}.map", PathManager::Instance()->GetMapsPath(), file);

	LogInfo("Attempting to load Map File [{}]", filename.c_str());

	auto m = new Map();
	if (m->Load(filename)) {
		return m;
	}

	delete m;
	return nullptr;
}

#ifdef USE_MAP_MMFS
bool Map::Load(std::string filename, bool force_mmf_overwrite)
{
	if (LoadMMF(filename, force_mmf_overwrite)) {
		LogInfo("Loaded .MMF Map File in place of [{}]", filename.c_str());
		return true;
	}
#else

/**
 * @param filename
 * @return
 */
bool Map::Load(const std::string &filename)
{
#endif /*USE_MAP_MMFS*/

	FILE *map_file = fopen(filename.c_str(), "rb");
	if (map_file) {
		uint32 version;
		if (fread(&version, sizeof(version), 1, map_file) != 1) {
			fclose(map_file);
			return false;
		}

		if (version == 0x01000000) {
			LogInfo("Loaded V1 Map File [{}]", filename.c_str());
			bool loaded_map_file = LoadV1(map_file);
			fclose(map_file);

			if (loaded_map_file) {
				LogInfo("Loaded V1 Map File [{}]", filename.c_str());
			} else {
				LogError("Failed to load V1 Map File [{}]", filename.c_str());
			}

#ifdef USE_MAP_MMFS
			if (loaded_map_file)
				return SaveMMF(filename, force_mmf_overwrite);
#endif /*USE_MAP_MMFS*/

			return loaded_map_file;
		}
		else if (version == 0x02000000) {
			LogInfo("Loading V2 Map File [{}]", filename.c_str());
			bool loaded_map_file = LoadV2(map_file);
			fclose(map_file);

			if (loaded_map_file) {
				LogInfo("Loaded V2 Map File [{}]", filename.c_str());
			} else {
				LogError("Failed to load V2 Map File [{}]", filename.c_str());
			}

#ifdef USE_MAP_MMFS
			if (loaded_map_file)
				return SaveMMF(filename, force_mmf_overwrite);
#endif /*USE_MAP_MMFS*/

			return loaded_map_file;
		}
		else {
			fclose(map_file);
			return false;
		}
	}

	return false;
}

bool Map::LoadV1(FILE *f) {
	uint32 face_count;
	uint16 node_count;
	uint32 facelist_count;

	if(fread(&face_count, sizeof(face_count), 1, f) != 1) {
		return false;
	}

	if(fread(&node_count, sizeof(node_count), 1, f) != 1) {
		return false;
	}

	if(fread(&facelist_count, sizeof(facelist_count), 1, f) != 1) {
		return false;
	}

	std::vector<glm::vec3> verts;
	std::vector<uint32> indices;
	for(uint32 i = 0; i < face_count; ++i) {
		glm::vec3 a;
		glm::vec3 b;
		glm::vec3 c;
		float normals[4];
		if(fread(&a, sizeof(glm::vec3), 1, f) != 1) {
			return false;
		}

		if(fread(&b, sizeof(glm::vec3), 1, f) != 1) {
			return false;
		}

		if(fread(&c, sizeof(glm::vec3), 1, f) != 1) {
			return false;
		}

		if(fread(normals, sizeof(normals), 1, f) != 1) {
			return false;
		}

		size_t sz = verts.size();
		verts.push_back(a);
		indices.push_back((uint32)sz);

		verts.push_back(b);
		indices.push_back((uint32)sz + 1);

		verts.push_back(c);
		indices.push_back((uint32)sz + 2);
	}

	if(imp) {
		imp->rm->release();
		imp->rm = nullptr;
	} else {
		imp = new impl;
	}

	imp->rm = createRaycastMesh((RmUint32)verts.size(), (const RmReal*)&verts[0], face_count, &indices[0]);

	if(!imp->rm) {
		delete imp;
		imp = nullptr;
		return false;
	}

	return true;
}

bool Map::LoadV2(FILE *f) {
	uint32 data_size;
	if (fread(&data_size, sizeof(data_size), 1, f) != 1) {
		return false;
	}

	uint32 buffer_size;
	if (fread(&buffer_size, sizeof(buffer_size), 1, f) != 1) {
		return false;
	}

	if (!data_size || buffer_size < 9 * sizeof(uint32) + sizeof(float)) {
		return false;
	}
	std::vector<char> data;
	if (data_size > data.max_size()) {
		return false;
	}
	data.resize(data_size);
	if (fread(data.data(), 1, data_size, f) != data_size) {
		return false;
	}

	MapV2::Mesh mesh;
	MapMemoryDiagnostic diagnostic;
	diagnostic.Record(MapMemoryDiagnostic::Stage::BeforeDecode);
	if (!MapV2::Decode(data.data(), data_size, buffer_size, mesh)) {
		return false;
	}
	auto &verts = mesh.vertices;
	auto &indices = mesh.indices;
	uint32 face_count = indices.size() / 3;

	if (imp) {
		imp->rm->release();
		imp->rm = nullptr;
	}
	else {
		imp = new impl;
	}

	diagnostic.Record(MapMemoryDiagnostic::Stage::AfterDecode, &mesh);
	imp->rm = createRaycastMesh((RmUint32)verts.size(), (const RmReal*)verts.data(), face_count, indices.data());
	diagnostic.Record(MapMemoryDiagnostic::Stage::AfterRaycast, &mesh);

	if (!imp->rm) {
		delete imp;
		imp = nullptr;
		return false;
	}

	return true;
}


#ifdef USE_MAP_MMFS
inline void strip_map_extension(std::string& map_file_name)
{
	auto ext_off = map_file_name.find(".map");
	if (ext_off != std::string::npos)
		map_file_name.erase(ext_off, strlen(".map"));
}

inline bool add_mmf_extension(std::string& mmf_file_name)
{
	if (mmf_file_name.empty())
		return false;

	mmf_file_name.append(".mmf");
	size_t dot_check = std::count(mmf_file_name.begin(), mmf_file_name.end(), '.');

	return (dot_check == 1);
}

bool Map::LoadMMF(const std::string& map_file_name, bool force_mmf_overwrite)
{
	if (force_mmf_overwrite)
		return false;

	std::string mmf_file_name = map_file_name;
	strip_map_extension(mmf_file_name);
	if (!add_mmf_extension(mmf_file_name)) {
		LogInfo("Failed to load Map MMF file: [{}]", mmf_file_name.c_str());
		return false;
	}

	FILE *f = fopen(mmf_file_name.c_str(), "rb");
	if (!f) {
		LogInfo("Failed to load Map MMF file: [{}] - could not open file", mmf_file_name.c_str());
		return false;
	}

	uint32 file_version;
	if (fread(&file_version, sizeof(uint32), 1, f) != 1) {
		fclose(f);
		LogInfo("Failed to load Map MMF file: [{}] - f@file_version", mmf_file_name.c_str());
		return false;
	}

	uint32 rm_buffer_size;
	if (fread(&rm_buffer_size, sizeof(uint32), 1, f) != 1) {
		fclose(f);
		LogInfo("Failed to load Map MMF file: [{}] - f@rm_buffer_size", mmf_file_name.c_str());
		return false;
	}

	uint32 rm_buffer_crc32;
	if (fread(&rm_buffer_crc32, sizeof(uint32), 1, f) != 1) {
		fclose(f);
		LogInfo("Failed to load Map MMF file: [{}] - f@rm_buffer_crc32", mmf_file_name.c_str());
		return false;
	}
	if (rm_buffer_crc32 != /*crc32_check*/ 0) {
		fclose(f);
		LogInfo("Failed to load Map MMF file: [{}] - bad rm_buffer checksum", mmf_file_name.c_str());
		return false;
	}

	uint32 mmf_buffer_size;
	if (fread(&mmf_buffer_size, sizeof(uint32), 1, f) != 1) {
		fclose(f);
		LogInfo("Failed to load Map MMF file: [{}] - f@mmf_buffer_size", mmf_file_name.c_str());
		return false;
	}

	std::vector<char> mmf_buffer(mmf_buffer_size);
	if (fread(mmf_buffer.data(), mmf_buffer_size, 1, f) != 1) {
		fclose(f);
		LogInfo("Failed to load Map MMF file: [{}] - f@mmf_buffer", mmf_file_name.c_str());
		return false;
	}

	fclose(f);

	std::vector<char> rm_buffer(rm_buffer_size);
	uint32 v = EQ::InflateData(mmf_buffer.data(), mmf_buffer_size, rm_buffer.data(), rm_buffer_size);

	if (imp) {
		imp->rm->release();
		imp->rm = nullptr;
	}
	else {
		imp = new impl;
	}

	bool load_success = false;
	imp->rm = loadRaycastMesh(rm_buffer, load_success);
	if (imp->rm && !load_success) {
		imp->rm->release();
		imp->rm = nullptr;
	}

	if (!imp->rm) {
		delete imp;
		imp = nullptr;
		LogInfo("Failed to load Map MMF file: [{}] - null RaycastMesh", mmf_file_name.c_str());
		return false;
	}

	return true;
}

bool Map::SaveMMF(const std::string& map_file_name, bool force_mmf_overwrite)
{
	if (!imp || !imp->rm) {
		LogInfo("Failed to save Map MMF file - No implementation (map_file_name: [{}])", map_file_name.c_str());
		return false;
	}

	std::string mmf_file_name = map_file_name;
	strip_map_extension(mmf_file_name);
	if (!add_mmf_extension(mmf_file_name)) {
		LogInfo("Failed to save Map MMF file: [{}]", mmf_file_name.c_str());
		return false;
	}

	FILE* f = fopen(mmf_file_name.c_str(), "rb");
	if (f) {
		fclose(f);
		if (!force_mmf_overwrite)
			return true;
	}

	std::vector<char> rm_buffer; // size set in MyRaycastMesh::serialize()
	serializeRaycastMesh(imp->rm, rm_buffer);
	if (rm_buffer.empty()) {
		LogInfo("Failed to save Map MMF file: [{}] - empty RaycastMesh buffer", mmf_file_name.c_str());
		return false;
	}

	uint32 rm_buffer_size = rm_buffer.size();
	uint32 mmf_buffer_size = EQ::EstimateDeflateBuffer(rm_buffer.size());

	std::vector<char> mmf_buffer(mmf_buffer_size);

	mmf_buffer_size = EQ::DeflateData(rm_buffer.data(), rm_buffer.size(), mmf_buffer.data(), mmf_buffer.size());
	if (!mmf_buffer_size) {
		LogInfo("Failed to save Map MMF file: [{}] - null MMF buffer size", mmf_file_name.c_str());
		return false;
	}

	f = fopen(mmf_file_name.c_str(), "wb");
	if (!f) {
		LogInfo("Failed to save Map MMF file: [{}] - could not open file", mmf_file_name.c_str());
		return false;
	}

	uint32 file_version = 0;
	if (fwrite(&file_version, sizeof(uint32), 1, f) != 1) {
		fclose(f);
		std::remove(mmf_file_name.c_str());
		LogInfo("Failed to save Map MMF file: [{}] - f@file_version", mmf_file_name.c_str());
		return false;
	}

	if (fwrite(&rm_buffer_size, sizeof(uint32), 1, f) != 1) {
		fclose(f);
		std::remove(mmf_file_name.c_str());
		LogInfo("Failed to save Map MMF file: [{}] - f@rm_buffer_size", mmf_file_name.c_str());
		return false;
	}

	uint32 rm_buffer_crc32 = 0;
	if (fwrite(&rm_buffer_crc32, sizeof(uint32), 1, f) != 1) {
		fclose(f);
		std::remove(mmf_file_name.c_str());
		LogInfo("Failed to save Map MMF file: [{}] - f@rm_buffer_crc32", mmf_file_name.c_str());
		return false;
	}

	if (fwrite(&mmf_buffer_size, sizeof(uint32), 1, f) != 1) {
		fclose(f);
		std::remove(mmf_file_name.c_str());
		LogInfo("Failed to save Map MMF file: [{}] - f@mmf_buffer_size", mmf_file_name.c_str());
		return false;
	}

	if (fwrite(mmf_buffer.data(), mmf_buffer_size, 1, f) != 1) {
		fclose(f);
		std::remove(mmf_file_name.c_str());
		LogInfo("Failed to save Map MMF file: [{}] - f@mmf_buffer", mmf_file_name.c_str());
		return false;
	}

	fclose(f);

	return true;
}

#endif /*USE_MAP_MMFS*/
