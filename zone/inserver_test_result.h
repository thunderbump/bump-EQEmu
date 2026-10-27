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

// Compose the melee result only after the staged restoration has returned.
// Reserve room for both failure causes and the truthful restoration suffix.
inline Result CompleteMeleeRestoration(Result result, bool restored, std::string restoration_error)
{
    if (!restored && restoration_error.empty()) {
        restoration_error = "melee prior zone/host restoration failed";
    }
    if (!restoration_error.empty()) {
        result.outcome = Outcome::Failed;
        if (restoration_error.size() > 128) {
            restoration_error = restoration_error.substr(0, 125) + "...";
        }
    }
    std::string suffix;
    if (!restoration_error.empty()) {
        suffix = (result.diagnostic.empty() ? "" : "; ") + restoration_error;
    }
    if (restored) { suffix += " prior-zone-restored"; }
    const auto original_limit = 256 - suffix.size();
    if (result.diagnostic.size() > original_limit) {
        result.diagnostic = result.diagnostic.substr(0, original_limit - 3) + "...";
    }
    result.diagnostic += suffix;
    return result;
}

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
