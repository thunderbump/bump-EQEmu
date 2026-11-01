#include <climits>
#include <cstdio>
#include <cstring>

// Keep the failing operations visible at -O1 and give diagnostics named frames.
__attribute__((noinline)) int SanitizerUseAfterFree()
{
	volatile int *value = new int(42);
	delete value;
	return *value;
}

__attribute__((noinline)) int SanitizerOutOfBounds()
{
	volatile int *values = new int[1];
	volatile int index = 1;
	int value = values[index];
	delete[] values;
	return value;
}

__attribute__((noinline)) int SanitizerUndefined()
{
	volatile int value = INT_MAX;
	return value + 1;
}

__attribute__((noinline)) void SanitizerLeak()
{
	volatile int *value = new int(42);
	*value = 43;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::fprintf(stderr, "usage: tests_sanitizer_controls pass|use-after-free|out-of-bounds|undefined|leak\n");
		return 2;
	}
	if (std::strcmp(argv[1], "pass") == 0) {
		return 0;
	}
	if (std::strcmp(argv[1], "use-after-free") == 0) {
		SanitizerUseAfterFree();
		return 0;
	}
	if (std::strcmp(argv[1], "out-of-bounds") == 0) {
		SanitizerOutOfBounds();
		return 0;
	}
	if (std::strcmp(argv[1], "undefined") == 0) {
		SanitizerUndefined();
		return 0;
	}
	if (std::strcmp(argv[1], "leak") == 0) {
		SanitizerLeak();
		return 0; // Orderly exit is required for LeakSanitizer's exit-time check.
	}
	std::fprintf(stderr, "unknown sanitizer control: %s\n", argv[1]);
	return 2;
}
