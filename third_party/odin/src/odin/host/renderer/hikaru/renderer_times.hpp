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


#ifndef ODIN_RENDERER_TIMES_HPP
#define ODIN_RENDERER_TIMES_HPP

#include <array>
#include <cstdint>
#include <vector>

#include "odin/host/renderer/hikaru/drawing.hpp"

namespace odin {

/* Debug (ODIN_DRAW_TIMES): what the drawing of a frame costs, part by part,
 * for looking at where the time of the renderer goes: the time its thread
 * spends in each part, and the time the graphics card takes for what the
 * part asks of it.
 *
 * The renderer names the part it enters; the time goes to that part until
 * it names another one. The time of the card is read from the stamps the
 * card puts in its stream of commands (Drawing::put_stamp), a few
 * frames later, when it has them: nobody waits for it. A card that lies
 * idle between two stamps, for its commands to come, counts that time as
 * well.
 *
 * Nothing is measured, and nothing is asked of the card, unless it is on. */
class DrawTimes {
public:
	enum Part : unsigned {
		PART_LAST_LIST,		/* the counts of the list before, its picture kept */
		PART_VERTICES,		/* of the list, and its buffers cleared */
		PART_BACKGROUND,	/* the polygon types, in the order they are drawn */
		PART_PUNCH_THROUGH,
		PART_OPAQUE,
		PART_SHADOWS,
		PART_COLLECT,		/* the translucent fragments into their lists */
		PART_SORT,		/* and sorted, blended over the colour below */
		PART_TEXTURES,		/* decoded, whatever the polygon type */
		PART_BLENDER,		/* the picture of the list into its 3D buffer */
		PART_LAYERS,		/* the 2D layers as textures */
		PART_COMPOSE,		/* the picture shown and the layers */
		PART_PRESENT,		/* brought to the size of the window */
		PART_SHOWN,		/* read back if asked, and handed to the window */

		NUM_PARTS
	};

	/* card: the one the renderer draws with, which outlives it */
	DrawTimes(Drawing &card, bool on);
	~DrawTimes();
	DrawTimes(const DrawTimes &) = delete;
	DrawTimes &operator=(const DrawTimes &) = delete;

	bool on() const { return m_on; }

	/* The drawing of a frame, from begin to end */
	void begin();
	void end();
	/* From here the time goes to this part. Returns the part it went
	 * to, for a part within another to give it back. */
	Part part(Part part)
	{
		return m_within ? enter(part) : part;
	}
	/* A mesh of that many triangles was drawn in the part. */
	void mesh(uint32_t triangles)
	{
		if (m_within) {
			m_sums[m_part].meshes++;
			m_sums[m_part].triangles += triangles;
		}
	}
	/* The translucent fragments of a list were counted. */
	void fragments(uint32_t num);

	/* On stderr, a line for each part that was entered. */
	void tell() const;

private:
	/* Where a part starts: the clock of the host, and the stamp of the
	 * card */
	struct Mark {
		Part part;	/* NUM_PARTS: the end of the frame */
		uint64_t host;
		uint32_t stamp;
	};
	/* The frames whose stamps the card has not been asked for yet */
	static constexpr unsigned HELD = 4;
	struct Sum {
		uint64_t host, card;		/* ns, over the frames */
		uint64_t host_worst, card_worst;	/* ns, of one frame */
		uint64_t entered, meshes, triangles;
	};

	Part enter(Part part);
	void read(std::vector<Mark> &frame);

	Drawing &m_card;
	bool m_on;
	bool m_stamps;		/* the card has stamps */
	Part m_part;
	bool m_within;		/* between begin and end */
	std::array<std::vector<Mark>, HELD> m_frames;
	unsigned m_frame;	/* the one being drawn */
	std::array<Sum, NUM_PARTS> m_sums;
	uint64_t m_drawn, m_read;	/* frames */
	uint64_t m_lists, m_fragments;
	uint32_t m_fragments_worst;
};

} /* namespace odin */

#endif /* ODIN_RENDERER_TIMES_HPP */
