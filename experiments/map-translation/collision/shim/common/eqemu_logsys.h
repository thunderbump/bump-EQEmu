#pragma once
// Standalone logging adapter only. The production allocator and raycaster compile unchanged.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#define LogInfo(...) ((void)0)
#define LogKSM(...) ((void)0)
#define LogKSMDetail(...) ((void)0)
