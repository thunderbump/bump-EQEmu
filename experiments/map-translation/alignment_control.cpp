// Deliberate negative control: qualify UBSan's packed-scalar diagnostic.
#include <cstdint>
int main() {
 alignas(uint32_t) char bytes[8] = {};
 volatile uint32_t *unaligned = reinterpret_cast<volatile uint32_t *>(bytes + 1);
 return *unaligned;
}
