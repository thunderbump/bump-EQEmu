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

#include <functional>
#include <memory>
#include <string>

struct uv_getaddrinfo_s;

namespace EQ::Net {

using dns_callback_t = std::function<void(const std::string&)>;

class DNSRequest
{
public:
	// Owning loop thread only. Cancellation does not imply callback retirement.
	int Cancel();
	bool Pending() const { return m_request != nullptr; }
private:
	friend std::shared_ptr<DNSRequest> DNSLookup(const std::string&, int, bool, dns_callback_t);
	uv_getaddrinfo_s* m_request = nullptr;
	bool m_cancelled = false;
};

std::shared_ptr<DNSRequest> DNSLookup(const std::string& addr, int port, bool ipv6, dns_callback_t cb);

std::string DNSLookupSync(const std::string& addr, int port, bool ipv6 = false);

} // namespace EQ::Net
