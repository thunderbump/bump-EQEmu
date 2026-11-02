#pragma once

#include "common/event/event_loop.h"
#include "common/event/timer.h"
#include "common/net/reliable_stream_connection.h"
#include "cppunit/cpptest.h"

#include <future>

class EventLoopTest : public Test::Suite
{
public:
	EventLoopTest()
	{
		TEST_ADD(EventLoopTest::TimerClose);
		TEST_ADD(EventLoopTest::ReliableClose);
	}

private:
	void TimerClose()
	{
		auto result = std::async(std::launch::async, [] {
			auto& loop = EQ::EventLoop::Get();
			EQ::Timer never_started([](EQ::Timer*) {});
			never_started.Stop();
			never_started.Stop();
			EQ::Timer timer(60000, true, [](EQ::Timer*) {});
			const bool busy = loop.Close(0) == UV_ETIMEDOUT;
			const bool wrong_thread = std::async(std::launch::async, [&loop] {
				return loop.Close(0);
			}).get() == UV_EPERM;
			int from_callback = 0;
			EQ::Timer once(0, false, [&from_callback, &loop](EQ::Timer*) {
				from_callback = loop.Close(0);
			});
			loop.Process();
			timer.Stop();
			timer.Stop();
			once.Stop();
			const bool closed = loop.Close(1000) == 0;
			return busy && wrong_thread && from_callback == UV_EBUSY && closed && loop.Close(0) == 0;
		});
		TEST_ASSERT(result.get());
	}

	void ReliableClose()
	{
		auto result = std::async(std::launch::async, [] {
			auto& loop = EQ::EventLoop::Get();
			bool queued_send = false;
			{
				EQ::Net::ReliableStreamConnectionManager manager;
				manager.OnNewConnection([&queued_send](const auto& connection) {
					connection->Close();
					queued_send = connection->GetStats().sent_bytes > 0;
				});
				manager.Connect("127.0.0.1", 9);
				manager.Close();
				manager.Close();
			} // Both handle storage and pending send state must outlive this owner.
			return loop.Close(1000) == 0 && queued_send;
		});
		TEST_ASSERT(result.get());
	}
};
