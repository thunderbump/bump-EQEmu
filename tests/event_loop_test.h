#pragma once

#include "common/event/event_loop.h"
#include "common/event/timer.h"
#include "common/net/reliable_stream_connection.h"
#include "common/net/dns.h"
#include "common/net/tcp_server.h"
#include "common/net/servertalk_client_connection.h"
#include "common/net/servertalk_legacy_client_connection.h"
#include "cppunit/cpptest.h"

#include <future>
#include <chrono>
#include <memory>
#include <stdexcept>

class EventLoopTest : public Test::Suite
{
public:
	EventLoopTest()
	{
		TEST_ADD(EventLoopTest::TimerClose);
		TEST_ADD(EventLoopTest::ReliableClose);
		TEST_ADD(EventLoopTest::NetworkCancel);
		TEST_ADD(EventLoopTest::TCPWrapperLifetime);
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
			std::shared_ptr<const EQ::Net::ReliableStreamConnectionManager::CloseStatus> status;
			bool queued_send = false;
			{
				EQ::Net::ReliableStreamConnectionManager manager;
				EQ::Net::ReliableStreamConnect request{};
				request.max_packet_size = EQ::Net::HostToNetwork(uint32_t{512});
				EQ::Net::ReliableStreamConnection connection(&manager, request, "127.0.0.1", 9);
				connection.Close(); // Server-side constructor supplies valid encoding state.
				status = manager.Close();
				queued_send = status->pending_sends > 0 && !status->handles_retired;
				manager.Close();
			} // Both handle storage and pending send state must outlive this owner.
			bool partial_failure = true;
#ifdef __linux__
			uv_udp_t blocker{};
			const bool initialized = uv_udp_init(loop.Handle(), &blocker) == 0;
			partial_failure = false;
			if (initialized) {
				sockaddr_in address{};
				uv_ip4_addr("0.0.0.0", 0, &address);
				const int bound = uv_udp_bind(&blocker, reinterpret_cast<sockaddr*>(&address), 0);
				int length = sizeof(address);
				const int named = uv_udp_getsockname(&blocker, reinterpret_cast<sockaddr*>(&address), &length);
				EQ::Net::ReliableStreamConnectionManagerOptions options;
				options.port = ntohs(address.sin_port);
				if (bound == 0 && named == 0) {
					try {
						EQ::Net::ReliableStreamConnectionManager conflicting(options);
					} catch (const std::runtime_error&) {
						partial_failure = true;
					}
				}
				uv_close(reinterpret_cast<uv_handle_t*>(&blocker), nullptr);
			}
#endif
			const bool closed = loop.Close(1000) == 0;
			return closed && queued_send && partial_failure && status->pending_sends == 0 && status->handles_retired;
		});
		TEST_ASSERT(result.get());
	}

	void NetworkCancel()
	{
		auto result = std::async(std::launch::async, [] {
			auto& loop = EQ::EventLoop::Get();
			int dns_callbacks = 0;
			auto dns = EQ::Net::DNSLookup("127.0.0.1", 9, false, [&](const auto&) { ++dns_callbacks; });
			const bool was_pending = dns->Pending();
			const int cancellation = dns->Cancel();
			// Cancellation/EBUSY is not completion; the loop must retire the request.
			const bool still_pending = dns->Pending();
			dns->Cancel();
			{
				EQ::Net::ServertalkClient client("127.0.0.1", 0, false, "Test", "");
				EQ::Net::ServertalkLegacyClient legacy("127.0.0.1", 0, false);
				client.Close();
				client.Close();
				legacy.Close();
				legacy.Close();
			} // Late resolver completion must not touch either destroyed client.
			EQ::Net::TCPServer server;
			server.Close();
			server.Listen("127.0.0.1", 0, false, [](auto) {});
			int connect_callbacks = 0;
			auto connect = EQ::Net::TCPConnection::Connect("127.0.0.1", server.LocalPort(), false,
				[&](auto) { ++connect_callbacks; });
			const bool connect_pending = connect->Pending();
			connect->Cancel();
			connect->Cancel();
			const bool connect_still_pending = connect->Pending();
			server.Close();
			server.Close();
			int failed_connects = 0;
			auto invalid = EQ::Net::TCPConnection::Connect("invalid-address", 9, false,
				[&](auto connection) { failed_connects += !connection; });
			const bool immediate_failure = !invalid->Pending() && failed_connects == 1;
			bool invalid_listen = false;
			try {
				EQ::Net::TCPServer invalid_server;
				invalid_server.Listen("invalid-address", 9, false, [](auto) {});
			} catch (const std::runtime_error&) {
				invalid_listen = true;
			}
			const bool closed = loop.Close(2000) == 0;
			int closed_callbacks = 0;
			auto closed_dns = EQ::Net::DNSLookup("127.0.0.1", 9, false, [&](const auto&) { ++closed_callbacks; });
			auto closed_connect = EQ::Net::TCPConnection::Connect("127.0.0.1", 9, false, [&](auto) { ++closed_callbacks; });
			return was_pending && still_pending && (cancellation == 0 || cancellation == UV_EBUSY)
				&& connect_pending && connect_still_pending && closed && !dns->Pending() && !connect->Pending()
				&& dns_callbacks == 0 && connect_callbacks == 0 && immediate_failure && invalid_listen
				&& closed_callbacks == 2 && !closed_dns->Pending() && !closed_connect->Pending();
		});
		TEST_ASSERT(result.get());
	}

	void TCPWrapperLifetime()
	{
		auto result = std::async(std::launch::async, [] {
			auto& loop = EQ::EventLoop::Get();
			auto server = std::make_unique<EQ::Net::TCPServer>();
			std::shared_ptr<EQ::Net::TCPConnection> client, peer;
			server->Listen("127.0.0.1", 0, false, [&](auto connection) { peer = connection; });
			auto connect = EQ::Net::TCPConnection::Connect("127.0.0.1", server->LocalPort(), false,
				[&](auto connection) { client = connection; });
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while ((!client || !peer) && std::chrono::steady_clock::now() < deadline) {
				loop.Process();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			const bool connected = client && peer;
			int notifications = 0;
			if (connected) {
				client->OnDisconnect([&](auto) { notifications += 100; });
				client->Write("pooled", 6);
				client->Disconnect();
				client.reset(); // Queued writes and close must not dereference this wrapper.
				peer->OnDisconnect([&](auto) {
					++notifications;
					peer.reset();
					server.reset(); // Release the parent during the close notification.
				});
				peer->Write("pooled", 6);
				peer->Disconnect();
			} else {
				connect->Cancel();
				if (client) { client->Disconnect(); }
				if (peer) { peer->Disconnect(); }
				server->Close();
			}
			const bool closed = loop.Close(2000) == 0;
			return connected && closed && notifications == 1 && !server && !peer
				&& EQ::Net::TCPConnection::PendingWrites() == 0;
		});
		TEST_ASSERT(result.get());
	}
};
