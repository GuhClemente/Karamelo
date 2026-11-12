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

#include "odin/machine/core/scheduler.hpp"

#include <cstdio>
#include <cstdlib>

#include "odin/common/stop_message.hpp"

namespace odin {

void Scheduler::fatal(const char *what, uint64_t value) const
{
	stop_fatal("scheduler at %llu: %s (%llu)", static_cast<unsigned long long>(m_now), what,
	           static_cast<unsigned long long>(value));
}

void Scheduler::reset()
{
	m_now = 0;
	m_when.fill(NEVER);
}

void Scheduler::set(uint32_t source, Time when)
{
	if (source >= SOURCES) {
		fatal("no such source", source);
	}
	if (when < m_now) {
		fatal("event dated in the past", when);
	}
	m_when[source] = when;
}

Time Scheduler::next() const
{
	Time earliest = NEVER;
	for (const Time when : m_when) {
		if (when < earliest) {
			earliest = when;
		}
	}
	return earliest;
}

void Scheduler::advance(Time to)
{
	if (to < m_now) {
		fatal("time moved backwards", to);
	}
	if (to > next()) {
		fatal("time moved past an event", to);
	}
	m_now = to;
}

bool Scheduler::take(uint32_t &source)
{
	for (uint32_t i = 0; i < SOURCES; i++) {
		if (m_when[i] <= m_now) {
			m_when[i] = NEVER;
			source = i;
			return true;
		}
	}
	return false;
}

void Scheduler::save(SchedulerState &out) const
{
	out.now = m_now;
	out.when = m_when;
}

void Scheduler::load(const SchedulerState &in)
{
	m_now = in.now;
	m_when = in.when;
}

} /* namespace odin */
