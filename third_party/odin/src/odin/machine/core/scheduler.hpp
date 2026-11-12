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

#ifndef ODIN_SCHEDULER_HPP
#define ODIN_SCHEDULER_HPP

#include <array>
#include <cstdint>

namespace odin {

/* The time of a machine, in cycles of its fastest clock since power-on. */
using Time = uint64_t;

/* Everything a scheduler holds. Plain data. */
struct SchedulerState {
	uint64_t now;
	std::array<uint64_t, 16> when;
};

/* The dates of what is to come. Each source has one event at most; the
 * machine runs its chips up to the earliest date, moves the time there and
 * takes the events that are due, the lowest source first. */
class Scheduler {
public:
	static constexpr Time NEVER = ~Time{0};
	static constexpr uint32_t SOURCES = 16;

	Scheduler() { reset(); }

	/* Power-on: time 0, nothing to come. */
	void reset();

	Time now() const { return m_now; }

	/* Dates the event of a source, not before now. */
	void set(uint32_t source, Time when);

	/* The earliest date, NEVER when nothing is to come. */
	Time next() const;

	/* Moves the time forward, not past the earliest date. */
	void advance(Time to);

	/* Takes the event of the lowest source due now; false if none is. */
	bool take(uint32_t &source);

	void save(SchedulerState &out) const;
	void load(const SchedulerState &in);

private:
	[[noreturn]] void fatal(const char *what, uint64_t value) const;

	Time m_now;
	std::array<Time, SOURCES> m_when;
};

} /* namespace odin */

#endif /* ODIN_SCHEDULER_HPP */
