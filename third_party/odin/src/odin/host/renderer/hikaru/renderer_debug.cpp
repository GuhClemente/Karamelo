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

/* Debug: what helps to look at a scene, switches the keys change
 * (ODIN_DEBUG_KEYS), a log of what is drawn (HR_LOG), the fingerprint of a
 * list (ODIN_LIST_FP). */

#include "odin/host/renderer/hikaru/renderer.hpp"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>

namespace odin {

namespace {

/* The values a switch takes, the first at the start, and its key */
struct Control {
	int min, max;
	char key;	/* 0: none */
};

constexpr Control CONTROLS[] = {
	{  0, 1, 0 },	/* DEBUG_LOG */
	{  0, 1, '1' },	/* DEBUG_NO_LAYER1 */
	{  0, 1, '2' },	/* DEBUG_NO_LAYER2 */
	{  0, 1, '3' },	/* DEBUG_NO_3D */
	{  0, 1, 't' },	/* DEBUG_NO_TEXTURES */
	{  0, 1, 'u' },	/* DEBUG_NO_MIPMAPS */
	{ -1, 7, 'y' },	/* DEBUG_SELECT_POLYTYPE: -1 draws them all; P pauses */
	{  0, 1, 'l' },	/* DEBUG_NO_LIGHTING */
	{  0, 1, 'a' },	/* DEBUG_NO_AMBIENT */
	{  0, 1, 'd' },	/* DEBUG_NO_DIFFUSE */
	{  0, 1, 'g' },	/* DEBUG_NO_FOG */
};

} /* namespace */

void
Renderer::init_debug()
{
	static_assert(sizeof(CONTROLS) / sizeof(CONTROLS[0]) == NUM_DEBUG);

	for (unsigned i = 0; i < NUM_DEBUG; i++) {
		m_debug[i] = CONTROLS[i].min;
		m_debug_held[i] = false;
	}
	m_debug[DEBUG_LOG] = m_options.log ? 1 : 0;
}

/* The keys of the switches are keys of the players too: they act with the
 * debug keys only, once for each press. */
void
Renderer::update_debug()
{
	if (m_options.debug_keys) {
		char *msg = m_message;

		for (unsigned i = 0; i < NUM_DEBUG; i++) {
			const bool down = CONTROLS[i].key &&
			                  m_options.key_down(m_options.ctx, CONTROLS[i].key);

			if (down && !m_debug_held[i]) {
				m_debug[i] += 1;
				if (i == DEBUG_NO_MIPMAPS)
					m_textures.invalidate(-1);
				if (m_debug[i] > CONTROLS[i].max)
					m_debug[i] = CONTROLS[i].min;
			}
			m_debug_held[i] = down;
			msg += std::sprintf(msg, "%d|", m_debug[i]);
		}
		m_textures.set_no_mipmaps(m_debug[DEBUG_NO_MIPMAPS] != 0);
	}

	/* The log of one frame only */
	if (m_options.log_frame)
		m_debug[DEBUG_LOG] = (static_cast<int>(m_frame.number) == m_options.log_frame);
	m_textures.set_log(logging() ? stdout : nullptr);

	if (logging()) {
		const odin_fb_config_t &fb = m_frame.fb;

		log("frame %u clear=%02X%02X%02X%02X 181=%08X 781=%08X", m_frame.number,
		    fb.clear[0], fb.clear[1], fb.clear[2], fb.clear[3], fb.w181, fb.w781);
	}
}

void
log_line(std::FILE *log, const char *fmt, va_list args)
{
	std::fputs("\tHR: ", log);
	std::vfprintf(log, fmt, args);
	std::fputc('\n', log);
}

void
Renderer::log(const char *fmt, ...) const
{
	va_list args;

	va_start(args, fmt);
	log_line(stdout, fmt, args);
	va_end(args);
}

/* The mesh about to be drawn and the objects of the list it is drawn
 * with */
void
Renderer::log_mesh(const odin_mesh_t &mesh) const
{
	log("==== DRAWING MESH @%p (#vertices=%u #instances=%u 103alpha=%.3f addr=%08X) ====",
	    static_cast<const void *>(&mesh), mesh.num_tris * 3, mesh.num_instances,
	    static_cast<double>(mesh.poly_alpha), mesh.addr[0]);

	log("RENDSTATE D @%p #instances = %u", static_cast<const void *>(&mesh), mesh.num_instances);
	if (mesh.vp_index < m_list->num_vps)
		log("RENDSTATE D %u vp:  %s", mesh.num,
		    odin_viewport_str(&m_list->vp_list[mesh.vp_index]));
	if (mesh.mv_index < m_list->num_mvs)
		log("RENDSTATE D %u mv:  %s", mesh.num,
		    odin_modelview_str(&m_list->mv_list[mesh.mv_index]));
	if (mesh.mat_index < m_list->num_mats)
		log("RENDSTATE D %u mat: %s", mesh.num,
		    odin_material_str(&m_list->mat_list[mesh.mat_index]));
	if (mesh.tex_index < m_list->num_texs)
		log("RENDSTATE D %u tex: %s", mesh.num,
		    odin_texhead_str(&m_list->tex_list[mesh.tex_index]));
	if (mesh.ls_index < m_list->num_lss)
		log("RENDSTATE D %u ls:  %s", mesh.num,
		    odin_lightset_str(&m_list->ls_list[mesh.ls_index]));
}

/* One line for the list of each frame that draws one, to compare two
 * builds, and a line for each mesh of the frame of the list dump. */
void
Renderer::print_list_fingerprint() const
{
	uint32_t meshes, tris;
	const uint64_t fp = odin_display_list_fingerprint(
		m_list, &m_frame.fb, &meshes, &tris,
		(m_options.list_dump == static_cast<int>(m_frame.number)) ? stderr : nullptr);

	std::fprintf(stderr, "LISTFP frame=%u fp=%016" PRIx64 " meshes=%u tris=%u\n",
	             m_frame.number, fp, meshes, tris);
}

} /* namespace odin */
