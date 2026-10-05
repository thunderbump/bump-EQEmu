#include "suite_runner.h"
#include <sstream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;

// No sleeps or performance thresholds: compare the reported duration with an
// interval sampled inside setup and teardown, not just inside the test body.
class Lifecycle : public Test::Suite {
public:
	Lifecycle(const std::string &name, const std::string &mode) : mode(mode)
	{
		register_test(static_cast<Func>(&Lifecycle::Check), name);
	}
	void setup() override
	{
		begin = Clock::now();
		if (mode == "setup") {
			end = Clock::now();
			throw std::runtime_error("setup control");
		}
	}
	void Check()
	{
		if (mode == "body") throw std::runtime_error("body control");
		TEST_ASSERT(mode != "assert");
	}
	void tear_down() override
	{
		end = Clock::now();
		if (mode == "teardown") throw std::runtime_error("teardown control");
	}
	Clock::time_point begin, end;
private:
	std::string mode;
};

bool check(const std::string &mode)
{
	Test::Suite suite;
	auto *first = new Lifecycle("First::Check", mode);
	suite.add(first);
	suite.add(new Lifecycle("Second::Check", "pass"));
	std::ostringstream log, errors;
	auto *old_out = std::cout.rdbuf(log.rdbuf());
	auto *old_err = std::cerr.rdbuf(errors.rdbuf());
	const int result = EQEmuTest::RunSuite(suite);
	std::cout.rdbuf(old_out);
	std::cerr.rdbuf(old_err);
	const std::string text = log.str();
	const bool interrupted = mode == "setup" || mode == "teardown";
	const bool passed = mode == "pass";
	const std::string timing = "EQEMU_TEST_TIMING {\"version\":1,\"name\":\"First::Check\",\"status\":\""
		+ std::string(passed ? "passed" : "failed") + "\",\"completed\":"
		+ (interrupted ? "false" : "true") + ",\"elapsed_ns\":";
	const auto pos = text.find(timing);
	if (pos == std::string::npos || result != (passed ? 0 : 1)) return false;
	const auto elapsed = std::stoll(text.substr(pos + timing.size()));
	if (elapsed < 0) return false;
	if (elapsed < std::chrono::duration_cast<std::chrono::nanoseconds>(
		first->end - first->begin).count()) return false;
	const std::string summary = "EQEMU_TEST_RESULT {\"version\":1,\"selected\":2,\"started\":"
		+ std::string(interrupted ? "1" : "2") + ",\"completed\":"
		+ (interrupted ? "0" : "2") + ",\"failed\":"
		+ (interrupted || passed ? "0" : "1") + ",\"finalized\":"
		+ (interrupted ? "false" : "true") + ",\"passed\":"
		+ (passed ? "true" : "false") + "}";
	if (text.find(summary) == std::string::npos) return false;
	const auto second = text.find("EQEMU_TEST_TIMING {\"version\":1,\"name\":\"Second::Check\"");
	return interrupted ? second == std::string::npos : second > pos && second != std::string::npos;
}

bool bounded_name()
{
	EQEmuTest::CompletionOutput output;
	std::ostringstream log;
	auto *old = std::cout.rdbuf(log.rdbuf());
	output.suite_start(0, "Escaped");
	output.test_start("quote\"slash\\line\n" + std::string(300, 'x'));
	const bool bounded = output.current.size() == 256;
	output.interrupted();
	output.interrupted(); // An interrupted lifecycle must be reported only once.
	std::cout.rdbuf(old);
	const auto text = log.str();
	const auto pos = text.find("EQEMU_TEST_TIMING ");
	return bounded && pos != std::string::npos
		&& text.find("EQEMU_TEST_TIMING ", pos + 1) == std::string::npos
		&& text.find("Escaped::quote\\\"slash\\\\line\\u000a") != std::string::npos
		&& output.completed == 0 && output.failed == 0;
}
} // namespace

int main()
{
	for (const auto *mode : {"pass", "assert", "body", "setup", "teardown"}) {
		if (!check(mode)) {
			std::cerr << "Reporting control failed: " << mode << '\n';
			return 1;
		}
	}
	if (!bounded_name()) {
		std::cerr << "Bounded name reporting control failed\n";
		return 1;
	}
	std::cout << "Reporting controls passed\n";
	return 0;
}
