/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/machine/core/worker.hpp"

#include <utility>

namespace odin {

Worker::Worker()
	: m_busy(false), m_stop(false), m_thread(&Worker::loop, this)
{
}

Worker::~Worker()
{
	wait();
	{
		const std::lock_guard lock(m_mutex);
		m_stop = true;
	}
	m_given.notify_one();
	m_thread.join();
}

void
Worker::post(std::function<void()> job)
{
	wait();
	{
		const std::lock_guard lock(m_mutex);
		m_job = std::move(job);
		m_busy = true;
	}
	m_given.notify_one();
}

void
Worker::wait()
{
	if (!m_busy)
		return;

	std::unique_lock lock(m_mutex);
	m_done.wait(lock, [this] { return !m_busy; });
}

bool
Worker::wait_for(std::chrono::milliseconds time)
{
	if (!m_busy)
		return true;

	std::unique_lock lock(m_mutex);
	return m_done.wait_for(lock, time, [this] { return !m_busy; });
}

void
Worker::loop()
{
	std::unique_lock lock(m_mutex);

	for (;;) {
		m_given.wait(lock, [this] { return m_job || m_stop; });
		if (!m_job)
			return;
		const std::function<void()> job = std::exchange(m_job, nullptr);

		lock.unlock();
		job();
		lock.lock();

		m_busy = false;
		m_done.notify_all();
	}
}

} /* namespace odin */
