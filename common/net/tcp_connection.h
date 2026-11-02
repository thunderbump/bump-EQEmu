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
#pragma once

#include "common/net/tcp_connection_pooling.h"

#include "common/platform/platform.h"
#include "uv.h"

#include <functional>
#include <memory>
#include <string>

namespace EQ
{
	namespace Net
	{
		struct TCPConnectionState;
		class TCPConnectRequest
		{
		public:
			void Cancel();
			bool Pending() const { return m_pending; }
		private:
			friend class TCPConnection;
			uv_tcp_t* m_socket = nullptr;
			bool m_pending = false;
			bool m_cancelled = false;
		};

		class TCPConnection
		{
		public:
			TCPConnection(uv_tcp_t *socket);
			~TCPConnection();

			static std::shared_ptr<TCPConnectRequest> Connect(const std::string &addr, int port, bool ipv6, std::function<void(std::shared_ptr<TCPConnection>)> cb);
			static size_t PendingWrites();

			void Start();
			void OnRead(std::function<void(TCPConnection*, const unsigned char *, size_t)> cb);
			void OnDisconnect(std::function<void(TCPConnection*)> cb);
			void Disconnect();
			void Read(const char *data, size_t count);
			void Write(const char *data, size_t count);

			bool IsConnected() const;
			std::string LocalIP() const;
			int LocalPort() const;
			std::string RemoteIP() const;
			int RemotePort() const;

		private:
			TCPConnection();

			uv_tcp_t *m_socket;
			TCPConnectionState* m_state;
			std::function<void(TCPConnection*, const unsigned char *, size_t)> m_on_read_cb;
			std::function<void(TCPConnection*)> m_on_disconnect_cb;
		};
	}
}
