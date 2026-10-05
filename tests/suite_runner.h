#ifndef EQEMU_TEST_SUITE_RUNNER_H
#define EQEMU_TEST_SUITE_RUNNER_H

#include "cppunit/cpptest.h"
#include <chrono>
#include <exception>
#include <iostream>
#include <string>

namespace EQEmuTest {

// Observe full lifecycles through the existing framework, preserving verbose diagnostics.
class CompletionOutput : public Test::TextOutput {
public:
	CompletionOutput() : Test::TextOutput(Test::TextOutput::Verbose) {}
	void initialize(int count) override { selected = count; }
	void suite_start(int count, const std::string &name) override
	{
		suite = name.substr(0, 256);
		Test::TextOutput::suite_start(count, name);
	}
	void test_start(const std::string &name) override
	{
		++started;
		current = (suite + "::" + name.substr(0, 256)).substr(0, 256);
		active = true;
		start = Clock::now();
	}
	void test_end(const std::string &name, bool ok, const Test::Time &time) override
	{
		report_timing(ok, true);
		++completed;
		if (!ok) {
			++failed;
			std::cerr << "Failed test: " << name.substr(0, 256) << '\n';
		}
		current.clear();
		Test::TextOutput::test_end(name, ok, time);
	}
	void finished(int count, const Test::Time &time) override
	{
		finalized = count == selected;
		Test::TextOutput::finished(count, time);
	}

	// setup/teardown exceptions escape the framework: report the interrupted
	// lifecycle, but do not manufacture test_end or alter summary counts.
	void interrupted() { if (active) report_timing(false, false); }

	int selected = 0;
	int started = 0;
	int completed = 0;
	int failed = 0;
	bool finalized = false;
	std::string current;

private:
	using Clock = std::chrono::steady_clock;
	Clock::time_point start;
	std::string suite;
	bool active = false;

	static std::string json_name(const std::string &name)
	{
		const char *hex = "0123456789abcdef";
		std::string escaped;
		for (unsigned char c : name) {
			// Escape non-ASCII bytes too, keeping arbitrary registered names
			// bounded, single-line and valid JSON without locale assumptions.
			if (c < 0x20 || c >= 0x7f) {
				escaped += "\\u00";
				escaped += hex[c >> 4];
				escaped += hex[c & 15];
			} else {
				if (c == '"' || c == '\\') escaped += '\\';
				escaped += c;
			}
		}
		return escaped;
	}

	void report_timing(bool ok, bool complete)
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
			Clock::now() - start).count();
		active = false;
		// TextOutput uses carriage-return progress; start a separate log line.
		std::cout << "\nEQEMU_TEST_TIMING {\"version\":1,\"name\":\""
			<< json_name(current) << "\",\"status\":\"" << (ok ? "passed" : "failed")
			<< "\",\"completed\":" << (complete ? "true" : "false")
			<< ",\"elapsed_ns\":" << elapsed << "}\n";
	}
};

// Shared by the real utility entry point and deliberately failing control builds.
inline int RunSuite(Test::Suite &tests)
{
	CompletionOutput output;
	bool succeeded = false;
	try {
		succeeded = tests.run(output, true);
	}
	catch (const std::exception &error) {
		output.interrupted();
		std::cerr << "Test exception in " << output.current << ": "
			<< std::string(error.what()).substr(0, 512) << '\n';
	}
	catch (...) {
		output.interrupted();
		std::cerr << "Unknown test exception in " << output.current << '\n';
	}
	const bool passed = succeeded && output.finalized && output.selected > 0
		&& output.started == output.selected && output.completed == output.selected
		&& output.failed == 0;
	std::cout << "EQEMU_TEST_RESULT {\"version\":1,\"selected\":" << output.selected
		<< ",\"started\":" << output.started << ",\"completed\":" << output.completed
		<< ",\"failed\":" << output.failed << ",\"finalized\":"
		<< (output.finalized ? "true" : "false") << ",\"passed\":"
		<< (passed ? "true" : "false") << "}\n" << std::flush;
	return passed && std::cout.good() ? 0 : 1;
}

} // namespace EQEmuTest
#endif
