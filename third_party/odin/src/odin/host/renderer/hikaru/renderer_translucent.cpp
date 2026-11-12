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
 * Valkyrie
 * Copyright (C) 2011-2014, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

/* The translucent polygons of a viewport are blended far to near at every
 * pixel, whatever the triangle or the mesh they come from. On the PHARRIER
 * cabinet the faces of a SEGA letter, alpha 0.8 and drawn on both sides,
 * show the faces behind them and none over them, from any angle; the glass
 * of an AIRTRIX street lamp glows over its frame; a wall of fire of BRAVEFF
 * keeps its texture. The patents of the hardware peel them the deepest
 * first whatever the order they were sent in, with a depth register of
 * their own (US 6 320 580, US 6 680 741); how they break a tie is not
 * written (HEURISTIC: in the order sent). Whole triangles sorted on their
 * centroid break those letters, and writing the depth drops layers that
 * the cabinet shows. The X360 port reverses its list of objects instead
 * (825B3EB0). Neither writes the depth here.
 *
 * The meshes are drawn once. A fragment that is not behind the other types
 * goes to the list of its pixel with its colour, its depth, the blend mode
 * of its material and its rank in the display list; then every pixel
 * blends its list (glsl/sort.frag). The 101 of a mesh moves it one
 * way against the other types and another in that list (depth_bias.hpp). */

#include "odin/host/renderer/hikaru/renderer.hpp"
#include "odin/common/repeats.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace odin {

/* A fragment a pixel of the buffers, and four a pixel of the screen of the
 * hardware at least: the six games fit in that at the scale 1, but for one
 * list of VON4. */
uint32_t
Renderer::room_at_start() const
{
	return std::max(4 * WIDTH * HEIGHT, width() * height());
}

/* Room for `max` fragments. What the buffer held is lost. */
void
Renderer::size_lists(uint32_t max)
{
	m_fragments_max = max;
	m_card->size_fragments(max);
}

/* More room, for `used` fragments at least. False if the graphics card has
 * no more to give: the fragments that do not fit are lost. */
bool
Renderer::grow_lists(uint32_t used)
{
	const uint32_t limit = m_card->fragment_limit();
	const uint32_t twice = (m_fragments_max > limit / 2) ? limit : m_fragments_max * 2;
	const uint32_t wanted = std::min(std::max(used, twice), limit);

	if (wanted <= m_fragments_max) {
		if (!m_fragments_full)
			repeats::error("no room for %u translucent fragments, the card takes %u",
			               used, limit);
		m_fragments_full = true;
		return false;
	}
	std::printf("renderer: more room for %u translucent fragments\n", used);
	size_lists(wanted);
	return true;
}

/* Collect the fragments. Whether their buffer was large enough is known once
 * the graphics card has drawn them: nobody waits for it here, the count is
 * left for check_last_list. A list drawn again does wait, and collects once
 * more if the buffer is too small. */
void
Renderer::draw_translucent(unsigned vpi, const odin_mesh_t *meshes, unsigned num)
{
	uint32_t used = 1;	/* not known yet */

	m_card->begin_translucent();
	m_collect = true;
	for (;;) {
		m_card->begin_collect(vpi, m_last.slot);
		for (unsigned j = 0; j < num; j++) {
			set_material_blend(meshes[j]);
			draw_mesh(meshes[j]);
		}
		m_card->end_collect();
		if (!m_again) {
			m_last.views |= 1u << vpi;
			m_card->keep_count(vpi, m_last.slot);
			break;
		}
		used = m_card->count_now(vpi, m_last.slot);
		if (used <= m_fragments_max || !grow_lists(used))
			break;
	}
	m_collect = false;

	m_times.part(DrawTimes::PART_SORT);
	m_card->end_translucent(used != 0);
}

} /* namespace odin */
