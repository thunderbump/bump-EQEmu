#include "zone/map_geometry.h"
#include <cstdint>
#include <cfenv>
#include <glm/detail/setup.hpp>
#include <zlib.h>
#ifdef __SSE__
#include <xmmintrin.h>
#endif
#include <cstring>
#include <memory>
#include <iostream>
#include <limits>
#include <stdexcept>

static void Write(FILE *out, const char *data, size_t count) {
 if (fwrite(data, 1, count, out) != count) throw std::runtime_error("output write failed");
}

static void Word(FILE *out, uint32_t value) {
 char bytes[4];
 for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<char>(value >> (8 * i));
 Write(out, bytes, 4);
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
  // Exclusive creation also rejects symlinks/hardlinks to retained inputs.
  std::unique_ptr<FILE, decltype(&fclose)> output(fopen(argv[2], "wbx"), &fclose);
  if (!output) throw std::runtime_error("cannot exclusively create output; choose a fresh path");
  FILE *out = output.get();
  Write(out, "EQMESH01", 8);
  Word(out, 1); Word(out, vertices.size()); Word(out, indices.size());
  Write(out, hash.data(), 64);
  for (const auto &v : vertices) for (unsigned axis = 0; axis < 3; ++axis) {
   uint32_t bits; float value = v[axis]; std::memcpy(&bits, &value, 4); Word(out, bits);
  }
  for (auto index : indices) Word(out, index);
  if (fclose(output.release()) != 0) throw std::runtime_error("output close failed");
  std::cout << vertices.size() << " vertices " << indices.size() << " indices\n";
  return 0;
 } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
