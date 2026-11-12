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


#include "odin/host/renderer/hikaru/renderer_times.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

#include "odin/common/host_clock.hpp"

namespace odin {

namespace {

const char *const NAMES[DrawTimes::NUM_PARTS] = {
	"the list before, counted and kept",
	"vertices, buffers cleared",
	"background polygons",
	"punch-through polygons",
	"opaque polygons",
	"shadow polygons",
	"translucent polygons, collected",
	"translucent fragments, sorted",
	"textures decoded",
	"the picture into its 3D buffer",
	"2D layers as textures",
	"picture and layers composed",
	"to the size of the window",
	"handed to the window",
};

double
ms(uint64_t ns, uint64_t frames)
{
	return frames ? static_cast<double>(ns) / 1e6 / static_cast<double>(frames) : 0.0;
}

} /* namespace */

DrawTimes::DrawTimes(Drawing &card, bool on)
	: m_card(card), m_on(on), m_stamps(on && card.has_stamps()), m_part(NUM_PARTS), m_within(false),
	  m_frame(0), m_sums{}, m_drawn(0), m_read(0), m_lists(0), m_fragments(0), m_fragments_worst(0)
{
}

DrawTimes::~DrawTimes()
{
	if (!m_stamps)
		return;
	for (std::vector<Mark> &frame : m_frames)
		for (const Mark &mark : frame)
			m_card.forget_stamp(mark.stamp, true);
}

void
DrawTimes::begin()
{
	if (!m_on)
		return;
	if (m_within)
		end();

	/* The frame drawn HELD frames ago leaves its place. */
	m_frame = (m_frame + 1) % HELD;
	read(m_frames[m_frame]);
	m_within = true;
}

DrawTimes::Part
DrawTimes::enter(Part part)
{
	const Part before = m_part;
	Mark mark = { part, host_ns(), 0 };

	if (m_stamps)
		mark.stamp = m_card.put_stamp();
	m_frames[m_frame].push_back(mark);
	m_part = part;
	if (part < NUM_PARTS)
		m_sums[part].entered++;
	return before;
}

void
DrawTimes::end()
{
	if (!m_within)
		return;
	enter(NUM_PARTS);
	m_within = false;
	/* The card would get the last stamp with the next frame only. */
	if (m_stamps)
		m_card.send_commands();

	const std::vector<Mark> &frame = m_frames[m_frame];
	std::array<uint64_t, NUM_PARTS> host{};

	for (size_t i = 0; i + 1 < frame.size(); i++)
		host[frame[i].part] += frame[i + 1].host - frame[i].host;
	for (unsigned i = 0; i < NUM_PARTS; i++) {
		m_sums[i].host += host[i];
		m_sums[i].host_worst = std::max(m_sums[i].host_worst, host[i]);
	}
	m_drawn++;
}

/* The stamps of a frame, if the card has them all: the last one tells. A
 * frame it does not have yet is left out. */
void
DrawTimes::read(std::vector<Mark> &frame)
{
	if (m_stamps && !frame.empty()) {
		const bool ready = m_card.reached(frame.back().stamp);

		if (ready) {
			std::array<uint64_t, NUM_PARTS> card{};
			uint64_t at = m_card.stamp_time(frame[0].stamp);

			for (size_t i = 0; i + 1 < frame.size(); i++) {
				const uint64_t next = m_card.stamp_time(frame[i + 1].stamp);

				if (next > at)
					card[frame[i].part] += next - at;
				at = next;
			}
			for (unsigned i = 0; i < NUM_PARTS; i++) {
				m_sums[i].card += card[i];
				m_sums[i].card_worst = std::max(m_sums[i].card_worst, card[i]);
			}
			m_read++;
		}
		/* A stamp the card still owes is not asked again. */
		for (const Mark &mark : frame)
			m_card.forget_stamp(mark.stamp, ready);
	}
	frame.clear();
}

void
DrawTimes::fragments(uint32_t num)
{
	if (!m_on)
		return;
	m_lists++;
	m_fragments += num;
	m_fragments_worst = std::max(m_fragments_worst, num);
}

void
DrawTimes::tell() const
{
	uint64_t host = 0, card = 0;

	if (!m_on || !m_drawn)
		return;

	std::fprintf(stderr, "drawing: %" PRIu64 " frames, a frame in ms: on the thread [its worst frame]", m_drawn);
	if (m_stamps)
		std::fprintf(stderr, ", for the graphics card [its worst frame] over %" PRIu64 " frames\n", m_read);
	else
		std::fprintf(stderr, "; the graphics card tells no time\n");

	for (unsigned i = 0; i < NUM_PARTS; i++) {
		const Sum &sum = m_sums[i];

		if (!sum.entered)
			continue;
		host += sum.host;
		card += sum.card;
		std::fprintf(stderr, "drawing:  %-34s %7.3f [%7.3f]", NAMES[i],
		             ms(sum.host, m_drawn), ms(sum.host_worst, 1));
		if (m_stamps)
			std::fprintf(stderr, "  %7.3f [%7.3f]", ms(sum.card, m_read), ms(sum.card_worst, 1));
		if (sum.meshes)
			std::fprintf(stderr, "  %.0f meshes, %.0f triangles a frame",
			             static_cast<double>(sum.meshes) / static_cast<double>(m_drawn),
			             static_cast<double>(sum.triangles) / static_cast<double>(m_drawn));
		if (i == PART_TEXTURES)
			std::fprintf(stderr, "  %" PRIu64 " in all", sum.entered);
		if (i == PART_SORT && m_lists)
			std::fprintf(stderr, "  %.0f fragments a list, %u at most",
			             static_cast<double>(m_fragments) / static_cast<double>(m_lists),
			             m_fragments_worst);
		std::fprintf(stderr, "\n");
	}
	std::fprintf(stderr, "drawing:  %-34s %7.3f          ", "all", ms(host, m_drawn));
	if (m_stamps)
		std::fprintf(stderr, "  %7.3f", ms(card, m_read));
	std::fprintf(stderr, "\n");
}

} /* namespace odin */
