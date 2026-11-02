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

#include <memory>
#include <cstdint>
#include <thread>

typedef struct uv_loop_s uv_loop_t;

namespace EQ {

class EventLoop
{
public:
	static EventLoop& Get();

	~EventLoop();
	EventLoop(const EventLoop&) = delete;
	EventLoop& operator=(const EventLoop&) = delete;

	void Process();
	void Run();
	void Shutdown();
	// Owners must stop/close their resources first. Never call from a loop callback.
	// A failed close leaves the loop intact so the owner can finish cleanup and retry.
	int Close(uint64_t timeout_ms);

	uv_loop_t* Handle() { return m_closed ? nullptr : m_loop.get(); }

private:
	EventLoop();

	std::unique_ptr<uv_loop_t> m_loop;
	std::thread::id m_owner = std::this_thread::get_id();
	bool m_running = false;
	bool m_closed = false;
};

} // namespace EQ
