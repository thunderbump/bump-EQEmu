#pragma once

#include "common/event/event_loop.h"
#include "common/event/timer.h"
#include "common/net/reliable_stream_connection.h"
#include "common/net/dns.h"
#include "common/net/tcp_server.h"
#include "common/net/servertalk_client_connection.h"
#include "common/net/servertalk_legacy_client_connection.h"
#include "common/net/servertalk_server.h"
#include "cppunit/cpptest.h"

#include <future>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

class EventLoopTest : public Test::Suite
{
public:
	EventLoopTest()
	{
		TEST_ADD(EventLoopTest::TimerClose);
		TEST_ADD(EventLoopTest::ReliableClose);
		TEST_ADD(EventLoopTest::NetworkCancel);
		TEST_ADD(EventLoopTest::TCPWrapperLifetime);
		TEST_ADD(EventLoopTest::ServertalkParentLifetime);
		TEST_ADD(EventLoopTest::ServertalkModernFrames);
		TEST_ADD(EventLoopTest::ServertalkLegacyFrames);
	}

private:
	static constexpr uint16_t FrameOpcode = 0x4321;
	using FrameRecord = std::tuple<char, uint16_t, std::string>;
	using FrameRecords = std::vector<FrameRecord>;

	template <typename T>
	static void AppendFrameScalar(std::string &bytes, T value)
	{
		bytes.append(reinterpret_cast<const char *>(&value), sizeof(value));
	}

	// Construct native wire bytes independently of the receive implementation.
	static std::string FrameBytes(bool legacy, const std::string &body, uint16_t opcode = FrameOpcode)
	{
		std::string bytes;
		if (legacy) {
			AppendFrameScalar(bytes, opcode);
			AppendFrameScalar(bytes, static_cast<uint16_t>(body.size() + 4));
		} else {
			AppendFrameScalar(bytes, static_cast<uint32_t>(body.size() + 6));
			AppendFrameScalar(bytes, static_cast<uint8_t>(EQ::Net::ServertalkMessage));
			AppendFrameScalar(bytes, static_cast<uint32_t>(body.size()));
			AppendFrameScalar(bytes, opcode);
		}
		bytes += body;
		return bytes;
	}

	static void RecordFrame(FrameRecords &records, char route, uint16_t opcode, EQ::Net::Packet &packet)
	{
		std::string body;
		// Empty legacy packets use DynamicPacket; its empty Data contract is separate.
		if (packet.Length() != 0) {
			body.assign(static_cast<const char *>(packet.Data()), packet.Length());
		}
		records.emplace_back(route, opcode, std::move(body));
	}

	struct FrameFixture
	{
		EQ::EventLoop &loop = EQ::EventLoop::Get();
		FrameRecords client_records, server_records;
		std::unique_ptr<EQ::Net::ServertalkServer> server;
		std::unique_ptr<EQ::Net::ServertalkClient> client;
		std::unique_ptr<EQ::Net::ServertalkLegacyClient> legacy_client;
		std::shared_ptr<EQ::Net::ServertalkServerConnection> child;
		bool close_attempted = false;
		bool closed = false;

		~FrameFixture() { Close(); }

		bool Close()
		{
			if (!close_attempted) {
				close_attempted = true;
				if (client) { client->Close(); }
				if (legacy_client) { legacy_client->Close(); }
				if (server) { server->Close(); }
				// Keep every owner and callback record alive through the bounded drain.
				closed = loop.Close(2000) == 0;
			}
			return closed;
		}

		template <typename Ready>
		bool Wait(Ready ready)
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while (!ready() && std::chrono::steady_clock::now() < deadline) {
				loop.Process();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return ready();
		}

		bool Start(bool legacy)
		{
			server = std::make_unique<EQ::Net::ServertalkServer>();
			EQ::Net::ServertalkServerOptions options;
			options.port = 0;
			server->Listen(options);
			server->OnConnectionIdentified(legacy ? "World" : "Test", [this](auto connection) {
				child = connection;
				child->OnMessage(FrameOpcode, [this](auto opcode, auto &packet) {
					RecordFrame(server_records, 's', opcode, packet);
				});
				child->OnMessage([this](auto opcode, auto &packet) {
					RecordFrame(server_records, 'g', opcode, packet);
				});
			});
			if (legacy) {
				legacy_client = std::make_unique<EQ::Net::ServertalkLegacyClient>("127.0.0.1", server->LocalPort(), false);
				legacy_client->OnMessage(FrameOpcode, [this](auto opcode, auto &packet) {
					RecordFrame(client_records, 's', opcode, packet);
				});
				legacy_client->OnMessage([this](auto opcode, auto &packet) {
					RecordFrame(client_records, 'g', opcode, packet);
				});
				if (!Wait([this] { return legacy_client->Handle() != nullptr; })) { return false; }
				const std::string body(sizeof(LoginserverNewWorldRequest), '\0');
				const auto hello = FrameBytes(true, body, ServerOP_NewLSInfo);
				legacy_client->Handle()->Write(hello.data(), 4);
				if (!Wait([this] { return child && EQ::Net::TCPConnection::PendingWrites() == 0; })) { return false; }
				if (!server_records.empty()) { return false; }
				// The real sniff consumed no body. Finish its buffered header without pumping peers.
				const auto tail = hello.substr(4) + FrameBytes(true, "a") + FrameBytes(true, "bc") + FrameBytes(true, "def");
				child->Handle()->Read(tail.data(), tail.size());
				const FrameRecords expected = {{'g', ServerOP_NewLSInfo, body},
					{'s', FrameOpcode, "a"}, {'g', FrameOpcode, "a"},
					{'s', FrameOpcode, "bc"}, {'g', FrameOpcode, "bc"},
					{'s', FrameOpcode, "def"}, {'g', FrameOpcode, "def"}};
				if (server_records != expected || !client_records.empty()) { return false; }
				server_records.clear();
			} else {
				client = std::make_unique<EQ::Net::ServertalkClient>("127.0.0.1", server->LocalPort(), false, "Test", "");
				client->OnMessage(FrameOpcode, [this](auto opcode, auto &packet) {
					RecordFrame(client_records, 's', opcode, packet);
				});
				client->OnMessage([this](auto opcode, auto &packet) {
					RecordFrame(client_records, 'g', opcode, packet);
				});
				if (!Wait([this] { return child && client->Handle() && EQ::Net::TCPConnection::PendingWrites() == 0; })) { return false; }
			}
			return client_records.empty() && server_records.empty();
		}
	};

	static bool CheckFrameStream(const std::shared_ptr<EQ::Net::TCPConnection> &connection, FrameRecords &records, bool legacy)
	{
		const std::vector<std::string> bodies = legacy ? std::vector<std::string>{"a", "bc", "def"}
			: std::vector<std::string>{"ab", "cde", "f"};
		FrameRecords expected;
		const auto delivered = [&](const std::string &body) {
			expected.emplace_back('s', FrameOpcode, body);
			expected.emplace_back('g', FrameOpcode, body);
		};
		std::string joined;
		for (const auto &body : bodies) { joined += FrameBytes(legacy, body); delivered(body); }
		// Modern totals 13/14/12 and legacy totals 5/6/7 force odd later headers.
		connection->Read(joined.data(), joined.size());
		if (records != expected) { return false; }
		for (const auto &body : bodies) {
			const auto frame = FrameBytes(legacy, body);
			for (size_t cut = 1; cut < frame.size(); ++cut) {
				connection->Read(frame.data(), cut);
				if (records != expected) { return false; }
				connection->Read(frame.data() + cut, frame.size() - cut);
				delivered(body);
				if (records != expected) { return false; }
			}
		}
		const auto first = FrameBytes(legacy, bodies[0]);
		const auto second = FrameBytes(legacy, bodies[1]);
		const auto partial = first + second.substr(0, 3);
		connection->Read(partial.data(), partial.size());
		delivered(bodies[0]);
		if (records != expected) { return false; }
		connection->Read(second.data() + 3, second.size() - 3);
		delivered(bodies[1]);
		if (records != expected) { return false; }

		std::string empty;
		if (legacy) {
			empty = FrameBytes(true, "");
			delivered("");
		} else {
			AppendFrameScalar(empty, uint32_t{0});
			AppendFrameScalar(empty, uint8_t{255}); // Unknown type is consumed, without dispatch.
		}
		empty += first;
		connection->Read(empty.data(), empty.size());
		delivered(bodies[0]);
		return records == expected;
	}

	static bool CheckFrameSession(bool legacy, uint32_t terminal_length)
	{
		FrameFixture fixture;
		bool passed = false;
		try {
			if (fixture.Start(legacy)) {
				const auto client = legacy ? fixture.legacy_client->Handle() : fixture.client->Handle();
				const auto server = fixture.child->Handle();
				passed = CheckFrameStream(client, fixture.client_records, legacy)
					&& CheckFrameStream(server, fixture.server_records, legacy);
				std::string pending;
				if (legacy) {
					AppendFrameScalar(pending, FrameOpcode);
					AppendFrameScalar(pending, static_cast<uint16_t>(terminal_length));
				} else {
					AppendFrameScalar(pending, terminal_length);
					AppendFrameScalar(pending, static_cast<uint8_t>(EQ::Net::ServertalkMessage));
				}
				const auto client_before = fixture.client_records;
				const auto server_before = fixture.server_records;
				client->Read(pending.data(), pending.size());
				server->Read(pending.data(), pending.size());
				passed = passed && fixture.client_records == client_before && fixture.server_records == server_before
					&& client->IsConnected() && server->IsConnected();
			}
		} catch (...) {
			passed = false;
		}
		const bool closed = fixture.Close();
		return passed && closed && EQ::Net::TCPConnection::PendingWrites() == 0;
	}

	void ServertalkModernFrames()
	{
		// Controlled receive-callback boundaries, not assumptions about TCP segmentation.
		auto result = std::async(std::launch::async, [] {
			return CheckFrameSession(false, std::numeric_limits<uint32_t>::max());
		});
		TEST_ASSERT(result.get());
	}

	void ServertalkLegacyFrames()
	{
		for (uint16_t length : {uint16_t{0}, uint16_t{1}, uint16_t{2}, uint16_t{3}, std::numeric_limits<uint16_t>::max()}) {
			auto result = std::async(std::launch::async, [length] { return CheckFrameSession(true, length); });
			TEST_ASSERT(result.get());
		}
	}

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

	void ServertalkParentLifetime()
	{
		auto result = std::async(std::launch::async, [] {
			auto& loop = EQ::EventLoop::Get();
			auto server = std::make_unique<EQ::Net::ServertalkServer>();
			EQ::Net::ServertalkServerOptions options;
			options.port = 0;
			server->Listen(options);
			std::vector<std::shared_ptr<EQ::Net::ServertalkServerConnection>> retained;
			server->OnConnectionIdentified("Test", [&](auto child) { retained.push_back(child); });
			int notifications = 0;
			server->OnConnectionRemoved("Test", [&](auto) {
				++notifications;
				server.reset(); // The other externally retained child must detach its parent.
			});
			EQ::Net::ServertalkClient first("127.0.0.1", server->LocalPort(), false, "Test", "");
			EQ::Net::ServertalkClient second("127.0.0.1", server->LocalPort(), false, "Test", "");
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while (retained.size() < 2 && std::chrono::steady_clock::now() < deadline) {
				loop.Process();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			const bool identified = retained.size() == 2;
			first.Close();
			second.Close();
			server->Close();
			const bool closed = loop.Close(2000) == 0;
			retained.clear();
			return identified && closed && !server && notifications == 1
				&& EQ::Net::TCPConnection::PendingWrites() == 0;
		});
		TEST_ASSERT(result.get());
	}
};
