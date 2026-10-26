#pragma once

#include "cppunit/cpptest.h"
#include "zone/inserver_test_progress.h"
#include "zone/inserver_test_result.h"

#include <array>
#include <memory>
#include <stdexcept>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

class InserverTestProgressTest : public Test::Suite {
public:
	InserverTestProgressTest()
	{
		TEST_ADD(InserverTestProgressTest::InterruptedCase);
		TEST_ADD(InserverTestProgressTest::FailedCase);
        TEST_ADD(InserverTestProgressTest::RestorationDeadlineBoundary);
		TEST_ADD(InserverTestProgressTest::BoundedEscapedFields);
#ifndef _WIN32
		TEST_ADD(InserverTestProgressTest::FlushBeforeReturn);
#endif
	}

private:
	using File = std::unique_ptr<FILE, decltype(&std::fclose)>;
	const std::string run_id = std::string(32, 'a');

	nlohmann::json ReadEvent(FILE *file)
	{
		std::array<char, 4097> line{};
		if (!std::fgets(line.data(), line.size(), file)) {
			throw std::runtime_error("missing progress line");
		}
		const std::string bytes = line.data();
		const std::string prefix = "EQEMU_INSERVER_PROGRESS ";
		if (bytes.compare(0, prefix.size(), prefix) != 0 || bytes.back() != '\n') {
			throw std::runtime_error("invalid progress line");
		}
		return nlohmann::json::parse(bytes.substr(prefix.size()));
	}

	void InterruptedCase()
	{
		File file(std::tmpfile(), std::fclose);
		TEST_ASSERT(file != nullptr);
		TEST_ASSERT(InserverTests::EmitCaseProgress(file.get(), run_id, "first", 0));
		TEST_ASSERT(InserverTests::EmitCaseProgress(file.get(), run_id, "first", 25, true, "done"));
		TEST_ASSERT(InserverTests::EmitCaseProgress(file.get(), run_id, "blocked", 0));
		std::rewind(file.get());
		const auto first = ReadEvent(file.get());
		const auto ended = ReadEvent(file.get());
		const auto blocked = ReadEvent(file.get());
		TEST_ASSERT(first["event"] == "case-start" && !first.contains("passed"));
		TEST_ASSERT(ended["event"] == "case-end" && ended["passed"] == true);
		TEST_ASSERT(ended["elapsed_ms"] == 25 && ended["diagnostic"] == "done");
		TEST_ASSERT(blocked["version"] == 1 && blocked["run_id"] == run_id);
		TEST_ASSERT(blocked["name"] == "blocked" && blocked["event"] == "case-start");
		TEST_ASSERT(!blocked.contains("passed") && !blocked.contains("diagnostic"));
		TEST_ASSERT(std::fgetc(file.get()) == EOF);
	}

	void FailedCase()
	{
		File file(std::tmpfile(), std::fclose);
		TEST_ASSERT(file != nullptr);
		TEST_ASSERT(InserverTests::EmitCaseProgress(file.get(), run_id, "failed", 10001, false, "deadline"));
		std::rewind(file.get());
		const auto event = ReadEvent(file.get());
		TEST_ASSERT(event["event"] == "case-end" && event["passed"] == false);
		TEST_ASSERT(event["elapsed_ms"] == 10001 && event["diagnostic"] == "deadline");
	}

    void RestorationDeadlineBoundary()
    {
        using namespace InserverTests;
        for (const uint32_t cap : {10000u, 30000u}) {
            // The registry withholds a terminal result until real restoration
            // finishes. The same completion boundary must reject a late pass.
            const auto within = EnforceScenarioDeadline({Outcome::Passed, "restored"}, cap, cap);
            TEST_ASSERT(within.outcome == Outcome::Passed);
            const auto pending = EnforceScenarioDeadline({Outcome::Pending, ""}, cap, cap);
            TEST_ASSERT(pending.outcome == Outcome::Pending);
            const auto active_overdue = EnforceScenarioDeadline(pending, cap + 1, cap);
            TEST_ASSERT(active_overdue.outcome == Outcome::Failed);
            const auto restored_late = EnforceScenarioDeadline({Outcome::Passed, "restored"}, cap + 1, cap);
            TEST_ASSERT(restored_late.outcome == Outcome::Failed);
            const auto failed = EnforceScenarioDeadline({Outcome::Failed, "original failure"}, cap + 1, cap);
            TEST_ASSERT(failed.outcome == Outcome::Failed && failed.diagnostic == "original failure");
            File file(std::tmpfile(), std::fclose);
            TEST_ASSERT(file != nullptr);
            TEST_ASSERT(EmitCaseProgress(file.get(), run_id, "restored-late", cap + 1,
                restored_late.outcome == Outcome::Passed, restored_late.diagnostic));
            std::rewind(file.get());
            const auto event = ReadEvent(file.get());
            TEST_ASSERT(event["event"] == "case-end" && event["passed"] == false);
            TEST_ASSERT(event["elapsed_ms"] == cap + 1);
            TEST_ASSERT(event["diagnostic"] == "in-server scenario exceeded its bounded deadline");
        }
    }

	void BoundedEscapedFields()
	{
		File file(std::tmpfile(), std::fclose);
		TEST_ASSERT(file != nullptr);
		// Worst-case JSON escaping and a split UTF-8 code point must stay bounded
		// and parse as one event, without injecting another console line.
		const std::string diagnostic = std::string(255, '\n') + "\xc3\xa9";
		TEST_ASSERT(InserverTests::EmitCaseProgress(file.get(), std::string(100, '\x01'),
			std::string(100, '\x01'), UINT32_MAX, false, diagnostic));
		TEST_ASSERT(std::ftell(file.get()) <= 4096);
		std::rewind(file.get());
		const auto event = ReadEvent(file.get());
		TEST_ASSERT(event["run_id"].get<std::string>().size() == 32);
		TEST_ASSERT(event["name"].get<std::string>().size() == 64);
		TEST_ASSERT(event["elapsed_ms"] == UINT32_MAX);
		TEST_ASSERT(event["diagnostic"].get<std::string>() == std::string(255, '\n') + "\xef\xbf\xbd");
		TEST_ASSERT(std::fgetc(file.get()) == EOF);
	}

#ifndef _WIN32
	void FlushBeforeReturn()
	{
		int descriptors[2];
		TEST_ASSERT(::pipe(descriptors) == 0);
		File writer(::fdopen(descriptors[1], "w"), std::fclose);
		if (!writer) {
			::close(descriptors[0]);
			::close(descriptors[1]);
			TEST_FAIL("cannot open progress pipe");
			return;
		}
		std::array<char, 4096> output_buffer{};
		std::setvbuf(writer.get(), output_buffer.data(), _IOFBF, output_buffer.size());
		::fcntl(descriptors[0], F_SETFL, O_NONBLOCK);
		const bool emitted = InserverTests::EmitCaseProgress(writer.get(), run_id, "blocked", 0);
		std::array<char, 4096> input{};
		// Read before closing/flushing the writer. A buffered start would leave
		// this empty when native execution blocks immediately after emission.
		const auto bytes = ::read(descriptors[0], input.data(), input.size());
		writer.reset();
		::close(descriptors[0]);
		TEST_ASSERT(emitted && bytes > 0);
	}
#endif
};
