// Deliberate negative control: qualify UBSan's packed-scalar diagnostic.
#include <cstdint>

__attribute__((noinline)) static uint32_t Load(const uint32_t *value) {
 return *value;
}

int main(int argc, char **) {
 alignas(uint32_t) char bytes[8] = {};
 // argc is one in the worker invocation. Keep the access non-volatile:
 // Clang intentionally omits alignment diagnostics on volatile pointers.
 return Load(reinterpret_cast<const uint32_t *>(bytes + argc));
}
