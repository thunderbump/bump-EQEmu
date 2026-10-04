#ifndef EQEMU_TEST_SUITE_RUNNER_H
#define EQEMU_TEST_SUITE_RUNNER_H

#include "cppunit/cpptest.h"
#include <exception>
#include <iostream>
#include <string>

namespace EQEmuTest {

// Observe completed functions through the existing framework, preserving verbose diagnostics.
class CompletionOutput : public Test::TextOutput {
public:
	CompletionOutput() : Test::TextOutput(Test::TextOutput::Verbose) {}
	void initialize(int count) override { selected = count; }
	void test_start(const std::string &name) override
	{
		++started;
		current = name.substr(0, 256);
	}
	void test_end(const std::string &name, bool ok, const Test::Time &time) override
	{
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

	int selected = 0;
	int started = 0;
	int completed = 0;
	int failed = 0;
	bool finalized = false;
	std::string current;
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
		std::cerr << "Test exception in " << output.current << ": "
			<< std::string(error.what()).substr(0, 512) << '\n';
	}
	catch (...) {
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
