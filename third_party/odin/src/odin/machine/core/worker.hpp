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

#ifndef ODIN_WORKER_HPP
#define ODIN_WORKER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace odin {

/* A thread that does one job at a time for the thread that owns it. The
 * display draws a frame on it while the machine runs the next one
 * (display.cpp).
 *
 * Every call comes from the owner. What a job and the owner both touch is
 * left alone by the owner from post to the end of wait. */
class Worker {
public:
	Worker();
	/* Waits for the job given last, then ends the thread. */
	~Worker();
	Worker(const Worker &) = delete;
	Worker &operator=(const Worker &) = delete;

	/* Gives a job once the one given last is done, and returns without
	 * waiting for it. */
	void post(std::function<void()> job);
	/* Waits for the job given last. */
	void wait();
	/* Waits for it at most that long: whether it is done. */
	bool wait_for(std::chrono::milliseconds time);

private:
	void loop();

	std::mutex m_mutex;
	std::condition_variable m_given, m_done;
	std::function<void()> m_job;
	/* A job is given and not done yet. Atomic, for wait to tell without
	 * the mutex that there is nothing to wait for. */
	std::atomic<bool> m_busy;
	bool m_stop;
	/* The last member: the thread starts with the others built. */
	std::thread m_thread;
};

} /* namespace odin */

#endif /* ODIN_WORKER_HPP */
