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
#include "tcp_server.h"

#include "common/event/event_loop.h"
#include <stdexcept>

void on_close_tcp_server_handle(uv_handle_t* handle) {
	delete (uv_tcp_t *)handle;
}

EQ::Net::TCPServer::TCPServer()
{
	m_socket = nullptr;
}

EQ::Net::TCPServer::~TCPServer() {
	Close();
}

void EQ::Net::TCPServer::Listen(int port, bool ipv6, std::function<void(std::shared_ptr<TCPConnection>)> cb)
{
	if (ipv6) {
		Listen("::", port, ipv6, cb);
	}
	else {
		Listen("0.0.0.0", port, ipv6, cb);
	}
}

void EQ::Net::TCPServer::Listen(const std::string &addr, int port, bool ipv6, std::function<void(std::shared_ptr<TCPConnection>)> cb)
{
	if (m_socket) {
		return;
	}

	m_on_new_connection = cb;

	auto loop = EQ::EventLoop::Get().Handle();
	if (!loop) {
		throw std::logic_error("Cannot listen on a closed event loop");
	}
	m_socket = new uv_tcp_t;
	memset(m_socket, 0, sizeof(uv_tcp_t));
	const int initialized = uv_tcp_init(loop, m_socket);
	if (initialized != 0) {
		delete m_socket;
		m_socket = nullptr;
		throw std::runtime_error(uv_strerror(initialized));
	}

	sockaddr_storage iaddr{};
	const int address_result = ipv6
		? uv_ip6_addr(addr.c_str(), port, reinterpret_cast<sockaddr_in6*>(&iaddr))
		: uv_ip4_addr(addr.c_str(), port, reinterpret_cast<sockaddr_in*>(&iaddr));
	if (address_result != 0) {
		Close();
		throw std::runtime_error(uv_strerror(address_result));
	}

	const int bound = uv_tcp_bind(m_socket, (sockaddr*)&iaddr, 0);
	if (bound != 0) {
		Close();
		throw std::runtime_error(uv_strerror(bound));
	}
	m_socket->data = this;

	const int listening = uv_listen((uv_stream_t*)m_socket, 128, [](uv_stream_t* server, int status) {
		if (status < 0) {
			return;
		}

		auto loop = EQ::EventLoop::Get().Handle();
		uv_tcp_t *client = new uv_tcp_t;
		memset(client, 0, sizeof(uv_tcp_t));
		if (uv_tcp_init(loop, client) != 0) {
			delete client;
			return;
		}

		if (uv_accept(server, (uv_stream_t*)client) < 0) {
			uv_close(reinterpret_cast<uv_handle_t*>(client), on_close_tcp_server_handle);
			return;
		}

		EQ::Net::TCPServer *s = (EQ::Net::TCPServer*)server->data;
		s->AddClient(client);
	});
	if (listening != 0) {
		Close();
		throw std::runtime_error(uv_strerror(listening));
	}
}

void EQ::Net::TCPServer::Close()
{
	if (m_socket) {
		uv_close((uv_handle_t*)m_socket, on_close_tcp_server_handle);
		m_socket = nullptr;
	}
}

void EQ::Net::TCPServer::AddClient(uv_tcp_t *c)
{
	std::shared_ptr<TCPConnection> client(new TCPConnection(c));
	auto callback = m_on_new_connection;
	if (callback) {
		callback(client);
	}
}

int EQ::Net::TCPServer::LocalPort() const
{
	if (!m_socket) {
		return 0;
	}
	sockaddr_storage addr{};
	int length = sizeof(addr);
	if (uv_tcp_getsockname(m_socket, reinterpret_cast<sockaddr*>(&addr), &length) != 0) {
		return 0;
	}
	return addr.ss_family == AF_INET6
		? ntohs(reinterpret_cast<const sockaddr_in6*>(&addr)->sin6_port)
		: ntohs(reinterpret_cast<const sockaddr_in*>(&addr)->sin_port);
}
