#include "suite_runner.h"
#include <stdexcept>
#include <utility>

// A separate test executable: failure injection never enters the server or ordinary suite.
class RunnerControl : public Test::Suite {
public:
	explicit RunnerControl(std::string choice) : mode(std::move(choice))
	{
		TEST_ADD(RunnerControl::Check);
	}
	void setup() override
	{
		if (mode == "setup-exception") {
			throw std::runtime_error("intentional setup exception");
		}
	}
	void tear_down() override
	{
		if (mode == "teardown-exception") {
			throw std::runtime_error("intentional teardown exception");
		}
	}
	void Check()
	{
		if (mode == "body-exception") {
			throw std::runtime_error("intentional body exception");
		}
		TEST_ASSERT(mode == "pass" || mode == "teardown-exception");
	}
private:
	std::string mode;
};

int main(int argc, char **argv)
{
	if (argc != 2) {
		return 2;
	}
	const std::string mode = argv[1];
	if (mode != "pass" && mode != "fail" && mode != "empty"
		&& mode != "setup-exception" && mode != "body-exception"
		&& mode != "teardown-exception") {
		return 2;
	}
	Test::Suite suite;
	if (mode != "empty") {
		suite.add(new RunnerControl(mode));
	}
	return EQEmuTest::RunSuite(suite);
}
