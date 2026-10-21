#pragma once

#include "common/json/json.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace InserverTests {

// Advisory console evidence only. A flushed start survives a blocked scenario;
// neither event replaces the complete, fresh suite report used for acceptance.
inline bool EmitCaseProgress(
	FILE *stream,
	const std::string &run_id,
	const std::string &name,
	uint64_t elapsed_ms,
	std::optional<bool> passed = std::nullopt,
	const std::string &diagnostic = ""
)
{
	nlohmann::json event = {
		{"version", 1}, {"run_id", run_id.substr(0, 32)},
		{"event", passed.has_value() ? "case-end" : "case-start"},
		{"name", name.substr(0, 64)}, {"elapsed_ms", elapsed_ms}
	};
	if (passed.has_value()) {
		event["passed"] = *passed;
		event["diagnostic"] = diagnostic.substr(0, 256);
	}
	// Escaping controls keeps each event on one line. Replacing incomplete UTF-8
	// also makes byte truncation safe for diagnostics containing multibyte text.
	const auto bytes = "EQEMU_INSERVER_PROGRESS " +
		event.dump(-1, ' ', true, nlohmann::json::error_handler_t::replace) + "\n";
	if (bytes.size() > 4096) {
		return false;
	}
	const bool written = std::fwrite(bytes.data(), 1, bytes.size(), stream) == bytes.size();
	const bool flushed = std::fflush(stream) == 0;
	return written && flushed;
}

} // namespace InserverTests
