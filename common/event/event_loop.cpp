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

#include "common/event/event_loop.h"
#include "uv.h"
#include <chrono>

namespace EQ {

EventLoop& EventLoop::Get()
{
	thread_local EventLoop inst;
	return inst;
}

EventLoop::EventLoop()
	: m_loop(std::make_unique<uv_loop_t>())
{
	memset(m_loop.get(), 0, sizeof(uv_loop_t));
	uv_loop_init(m_loop.get());
}

EventLoop::~EventLoop()
{
	if (!m_closed) {
		uv_loop_close(m_loop.get());
	}
}

void EventLoop::Process()
{
	if (m_closed || m_running) {
		return;
	}
	m_running = true;
	uv_run(m_loop.get(), UV_RUN_NOWAIT);
	m_running = false;
}

void EventLoop::Run()
{
	if (m_closed || m_running) {
		return;
	}
	m_running = true;
	uv_run(m_loop.get(), UV_RUN_DEFAULT);
	m_running = false;
}

void EventLoop::Shutdown()
{
	if (!m_closed) {
		uv_stop(m_loop.get());
	}
}

int EventLoop::Close(uint64_t timeout_ms)
{
	if (std::this_thread::get_id() != m_owner) {
		return UV_EPERM;
	}
	if (m_running) {
		return UV_EBUSY;
	}
	if (m_closed) {
		return 0;
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	for (;;) {
		Process();
		const int result = uv_loop_close(m_loop.get());
		if (result == 0) {
			m_closed = true;
			return 0;
		}
		if (result != UV_EBUSY) {
			return result;
		}
		if (std::chrono::steady_clock::now() >= deadline) {
			return UV_ETIMEDOUT;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

} // namespace EQ
