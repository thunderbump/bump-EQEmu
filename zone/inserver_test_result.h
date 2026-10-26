#pragma once

#include <cstdint>
#include <string>

namespace InserverTests {

enum class Outcome { Pending, Passed, Failed };

struct Result {
    Outcome outcome;
    std::string diagnostic;
};

// Use at the active-case boundary and again after truthful cleanup/restoration.
// A terminal failure keeps its original diagnostic; cleanup cannot turn a late
// success into an on-time one. Exactly the existing cap is still permitted.
inline Result EnforceScenarioDeadline(Result result, uint64_t elapsed_ms, uint32_t deadline_ms)
{
    if (result.outcome != Outcome::Failed && elapsed_ms > deadline_ms) {
        return {Outcome::Failed, "in-server scenario exceeded its bounded deadline"};
    }
    return result;
}

} // namespace InserverTests
