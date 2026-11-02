/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "tcp_connection.h"

#include "common/event/event_loop.h"

#include <iostream>

namespace EQ::Net {
struct TCPConnectionState {
	TCPConnection* owner;
};
}

WriteReqPool tcp_write_pool;

size_t EQ::Net::TCPConnection::PendingWrites()
{
	return tcp_write_pool.in_use();
}

void on_close_handle(uv_handle_t* handle) {
	delete (uv_tcp_t *)handle;
}

EQ::Net::TCPConnection::TCPConnection(uv_tcp_t *socket)
{
	m_socket = socket;
	m_state = new TCPConnectionState{this};
	m_socket->data = m_state;
}

EQ::Net::TCPConnection::~TCPConnection() {
	if (m_state) {
		m_state->owner = nullptr;
	}
	Disconnect();
}

void EQ::Net::TCPConnectRequest::Cancel()
{
	m_cancelled = true;
	// uv_cancel does not support uv_connect_t. Socket close retires the request.
	if (m_socket && !uv_is_closing(reinterpret_cast<uv_handle_t*>(m_socket))) {
		uv_close(reinterpret_cast<uv_handle_t*>(m_socket), on_close_handle);
	}
}

std::shared_ptr<EQ::Net::TCPConnectRequest> EQ::Net::TCPConnection::Connect(const std::string &addr, int port, bool ipv6, std::function<void(std::shared_ptr<TCPConnection>)> cb)
{
	struct EQTCPConnectBaton {
		uv_tcp_t* socket;
		std::function<void(std::shared_ptr<TCPConnection>)> cb;
		std::shared_ptr<TCPConnectRequest> state;
	};
	auto state = std::make_shared<TCPConnectRequest>();
	auto loop = EQ::EventLoop::Get().Handle();
	if (!loop) {
		cb(nullptr);
		return state;
	}
	auto socket = new uv_tcp_t{};
	const int initialized = uv_tcp_init(loop, socket);
	if (initialized != 0) {
		delete socket;
		cb(nullptr);
		return state;
	}
	sockaddr_storage iaddr{};
	const int address_result = ipv6
		? uv_ip6_addr(addr.c_str(), port, reinterpret_cast<sockaddr_in6*>(&iaddr))
		: uv_ip4_addr(addr.c_str(), port, reinterpret_cast<sockaddr_in*>(&iaddr));
	if (address_result != 0) {
		uv_close(reinterpret_cast<uv_handle_t*>(socket), on_close_handle);
		cb(nullptr);
		return state;
	}
	auto connect = new uv_connect_t{};
	auto baton = new EQTCPConnectBaton{socket, std::move(cb), state};
	connect->data = baton;
	state->m_socket = socket;
	state->m_pending = true;
	const int submitted = uv_tcp_connect(connect, socket, reinterpret_cast<sockaddr*>(&iaddr),
		[](uv_connect_t* req, int status) {
			auto baton = static_cast<EQTCPConnectBaton*>(req->data);
			auto socket = baton->socket;
			auto cb = std::move(baton->cb);
			auto state = baton->state;
			state->m_socket = nullptr;
			state->m_pending = false;
			delete baton;
			delete req;
			if (status < 0 || state->m_cancelled) {
				if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(socket))) {
					uv_close(reinterpret_cast<uv_handle_t*>(socket), on_close_handle);
				}
				if (!state->m_cancelled) {
					cb(nullptr);
				}
				return;
			}
			cb(std::make_shared<TCPConnection>(socket));
		});
	if (submitted != 0) {
		state->m_socket = nullptr;
		state->m_pending = false;
		auto callback = std::move(baton->cb);
		delete baton;
		delete connect;
		uv_close(reinterpret_cast<uv_handle_t*>(socket), on_close_handle);
		callback(nullptr);
	}
	return state;
}

void EQ::Net::TCPConnection::Start()
{
	if (!m_socket) {
		return;
	}
	uv_read_start(
		(uv_stream_t *) m_socket, [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
			if (suggested_size > 65536) {
				buf->base = new char[suggested_size];
				buf->len  = suggested_size;
				return;
			}

			static thread_local char temp_buf[65536];
			buf->base = temp_buf;
			buf->len  = 65536;
		}, [](uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
			auto state = static_cast<TCPConnectionState*>(stream->data);
			auto connection = state->owner;

			if (connection && nread > 0) {
				connection->Read(buf->base, nread);
			}
			else if (connection && nread == UV_EOF) {
				connection->Disconnect();
			}
			else if (connection && nread < 0) {
				connection->Disconnect();
			}

			if (buf->len > 65536) {
				delete [] buf->base;
			}
		}
	);
}

void EQ::Net::TCPConnection::OnRead(std::function<void(TCPConnection*, const unsigned char*, size_t)> cb)
{
	m_on_read_cb = cb;
}

void EQ::Net::TCPConnection::OnDisconnect(std::function<void(TCPConnection*)> cb)
{
	m_on_disconnect_cb = cb;
}

void EQ::Net::TCPConnection::Disconnect()
{
	if (m_socket) {
		uv_read_stop(reinterpret_cast<uv_stream_t*>(m_socket));
		uv_close(reinterpret_cast<uv_handle_t*>(m_socket), [](uv_handle_t* handle) {
			auto state = static_cast<TCPConnectionState*>(handle->data);
			auto owner = state->owner;
			if (owner) {
				owner->m_state = nullptr;
				// The callback may release the last wrapper/parent reference.
				auto callback = owner->m_on_disconnect_cb;
				if (callback) {
					callback(owner);
				}
			}
			delete state;
			delete reinterpret_cast<uv_tcp_t*>(handle);
		});
		m_socket = nullptr;
	}
}

void EQ::Net::TCPConnection::Read(const char *data, size_t count)
{
	auto callback = m_on_read_cb;
	if (callback) {
		callback(this, (unsigned char*)data, count);
	}
}

void EQ::Net::TCPConnection::Write(const char* data, size_t count) {
	if (!m_socket || !data || count == 0) {
		std::cerr << "TCPConnection::Write - Invalid socket or data\n";
		return;
	}

	if (count <= TCP_BUFFER_SIZE) {
		// Fast path: use pooled request with embedded buffer
		auto req_opt = tcp_write_pool.acquire();
		if (!req_opt) {
			std::cerr << "TCPConnection::Write - Out of write requests\n";
			return;
		}

		TCPWriteReq* write_req = *req_opt;

		// Fill buffer and set context
		memcpy(write_req->buffer.data(), data, count);
		write_req->connection_state = m_state;
		write_req->magic = 0xC0FFEE;

		uv_buf_t buf = uv_buf_init(write_req->buffer.data(), static_cast<unsigned int>(count));

		int result = uv_write(
			&write_req->req,
			reinterpret_cast<uv_stream_t*>(m_socket),
			&buf,
			1,
			[](uv_write_t* req, int status) {
				auto* full_req = reinterpret_cast<TCPWriteReq*>(req);
				if (full_req->magic != 0xC0FFEE) {
					std::cerr << "uv_write callback - invalid magic, skipping release\n";
					return;
				}

				// libuv retires pending writes before the socket close callback.
				auto state = full_req->connection_state;
				full_req->connection_state = nullptr;
				if (status < 0 && state->owner) {
					std::cerr << "uv_write failed: " << uv_strerror(status) << std::endl;
					state->owner->Disconnect();
				}
				tcp_write_pool.release(full_req);
			}
		);

		if (result < 0) {
			std::cerr << "uv_write() failed immediately: " << uv_strerror(result) << std::endl;
			tcp_write_pool.release(write_req);
		}

	} else {
		// Slow path: allocate heap buffer for large write
		LogNetTCP("[TCPConnection] Large write of [{}] bytes, using heap buffer", count);

		char* heap_buffer = new char[count];
		memcpy(heap_buffer, data, count);

		uv_write_t* write_req = new uv_write_t;
		write_req->data = heap_buffer;

		uv_buf_t buf = uv_buf_init(heap_buffer, static_cast<unsigned int>(count));

		int result = uv_write(
			write_req,
			reinterpret_cast<uv_stream_t*>(m_socket),
			&buf,
			1,
			[](uv_write_t* req, int status) {
				char* data = static_cast<char*>(req->data);
				delete[] data;
				delete req;

				if (status < 0) {
					std::cerr << "uv_write (large) failed: " << uv_strerror(status) << std::endl;
				}
			}
		);

		if (result < 0) {
			std::cerr << "uv_write() (large) failed immediately: " << uv_strerror(result) << std::endl;
			delete[] heap_buffer;
			delete write_req;
		}
	}
}


std::string EQ::Net::TCPConnection::LocalIP() const
{
	sockaddr_storage addr;
	int addr_len = sizeof(addr);
	uv_tcp_getsockname(m_socket, (sockaddr*)&addr, &addr_len);

	char endpoint[64] = { 0 };
	if (addr.ss_family == AF_INET) {
		uv_ip4_name((const sockaddr_in*)&addr, endpoint, 64);
	}
	else if (addr.ss_family == AF_INET6) {
		uv_ip6_name((const sockaddr_in6*)&addr, endpoint, 64);
	}

	return endpoint;
}

int EQ::Net::TCPConnection::LocalPort() const
{
	sockaddr_storage addr;
	int addr_len = sizeof(addr);
	uv_tcp_getsockname(m_socket, (sockaddr*)&addr, &addr_len);

	char endpoint[64] = { 0 };
	if (addr.ss_family == AF_INET) {
		sockaddr_in *s = (sockaddr_in*)&addr;
		return ntohs(s->sin_port);
	}
	else if (addr.ss_family == AF_INET6) {
		sockaddr_in6 *s = (sockaddr_in6*)&addr;
		return ntohs(s->sin6_port);
	}

	return 0;
}

std::string EQ::Net::TCPConnection::RemoteIP() const
{
	if (!m_socket) {
		return "";
	}

	sockaddr_storage addr;
	int addr_len = sizeof(addr);
	uv_tcp_getpeername(m_socket, (sockaddr*)&addr, &addr_len);

	char endpoint[64] = { 0 };
	if (addr.ss_family == AF_INET) {
		uv_ip4_name((const sockaddr_in*)&addr, endpoint, 64);
	}
	else if(addr.ss_family == AF_INET6) {
		uv_ip6_name((const sockaddr_in6*)&addr, endpoint, 64);
	}

	return endpoint;
}

int EQ::Net::TCPConnection::RemotePort() const
{
	if (!m_socket) {
		return 0;
	}

	sockaddr_storage addr;
	int addr_len = sizeof(addr);
	uv_tcp_getpeername(m_socket, (sockaddr*)&addr, &addr_len);

	char endpoint[64] = { 0 };
	if (addr.ss_family == AF_INET) {
		sockaddr_in *s = (sockaddr_in*)&addr;
		return ntohs(s->sin_port);
	}
	else if (addr.ss_family == AF_INET6) {
		sockaddr_in6 *s = (sockaddr_in6*)&addr;
		return ntohs(s->sin6_port);
	}

	return 0;
}

/**
 * @return
 */
bool EQ::Net::TCPConnection::IsConnected() const
{
	return m_socket != nullptr;
}
