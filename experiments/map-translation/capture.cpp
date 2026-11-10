#include "zone/map_geometry.h"
#include <cstdint>
#include <cfenv>
#include <glm/detail/setup.hpp>
#include <zlib.h>
#ifdef __SSE__
#include <xmmintrin.h>
#endif
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

static void Word(std::ostream &out, uint32_t value) {
 for (unsigned i = 0; i < 4; ++i) out.put(static_cast<char>(value >> (8 * i)));
}

int main(int argc, char **argv) {
 try {
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE float32 required");
  if (argc == 2 && std::string(argv[1]) == "--runtime-info") {
   std::cout << "{\"rounding_mode\":" << std::fegetround()
             << ",\"glm_version\":" << GLM_VERSION
             << ",\"zlib_runtime\":\"" << zlibVersion() << "\"";
#ifdef __SSE__
   std::cout << ",\"mxcsr\":" << _mm_getcsr();
#endif
   std::cout << "}\n";
   return 0;
  }
  if (argc != 4) throw std::runtime_error("usage: map-capture input.map output.mesh input-sha256");
  std::string hash = argv[3];
  if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos)
   throw std::runtime_error("expected lowercase SHA256");
  FILE *f = fopen(argv[1], "rb");
  if (!f) throw std::runtime_error("cannot open input");
  uint32 version = 0;
  bool ok = fread(&version, 4, 1, f) == 1;
  std::vector<glm::vec3> vertices;
  std::vector<uint32> indices;
  if (ok) {
   if (version == 0x01000000) ok = MapGeometry::LoadV1(f, vertices, indices);
   else if (version == 0x02000000) ok = MapGeometry::LoadV2(f, vertices, indices);
   else ok = false;
  }
  fclose(f);
  if (!ok) throw std::runtime_error("production loader rejected map");
  if (vertices.size() > UINT32_MAX || indices.size() > UINT32_MAX || indices.size() % 3)
   throw std::runtime_error("invalid output counts");
  for (auto index : indices) if (index >= vertices.size()) throw std::runtime_error("index out of bounds");
  std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
  out.exceptions(std::ios::badbit | std::ios::failbit);
  out.write("EQMESH01", 8);
  Word(out, 1); Word(out, vertices.size()); Word(out, indices.size());
  out.write(hash.data(), 64);
  for (const auto &v : vertices) for (unsigned axis = 0; axis < 3; ++axis) {
   uint32_t bits; float value = v[axis]; std::memcpy(&bits, &value, 4); Word(out, bits);
  }
  for (auto index : indices) Word(out, index);
  out.close();
  std::cout << vertices.size() << " vertices " << indices.size() << " indices\n";
  return 0;
 } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
