#pragma once

#include "common/json/json.hpp"

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

// Keep every case identity and outcome inside the fixed fixture report bound.
// Full diagnostics were already emitted as bounded case-progress records.
inline std::string SerializeSuiteReport(nlohmann::json report)
{
    for (const size_t cap : {256u, 128u, 64u, 32u, 0u}) {
        for (auto &test : report["tests"]) {
            auto &diagnostic = test["diagnostic"].get_ref<std::string &>();
            if (diagnostic.size() > cap) {
                diagnostic = cap ? diagnostic.substr(0, cap - 3) + "..." : "";
            }
        }
        auto bytes = report.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
        if (bytes.size() <= 4096) { return bytes; }
    }
    return {};
}

} // namespace InserverTests
