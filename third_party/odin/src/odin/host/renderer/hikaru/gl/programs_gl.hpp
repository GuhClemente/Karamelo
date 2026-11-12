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

#ifndef ODIN_BOARD_PROGRAMS_GL_HPP
#define ODIN_BOARD_PROGRAMS_GL_HPP

/* The mesh programs of the board drawing with OpenGL (drawing_gl.hpp). */

#include <cstdint>
#include <string>
#include <unordered_map>

#include "odin/host/gfx/gl_common.hpp"

#include "odin/host/renderer/hikaru/renderer_programs.hpp"

namespace odin {

/* A compiled program of the mesh shaders */
struct MeshProgram {
	struct Light {
		GLint position, direction, color, ambient;
		GLint extents, spot, cone, table;
	};
	/* The locations of its uniforms, -1 for one the variant leaves out */
	struct Locations {
		GLint projection;
		GLint modelview;
		GLint modelview2;
		GLint depth_scale, sort_scale;
		GLint snap, scale;
		Light lights[4];
		GLint ambient;
		GLint fog, fog_color;
		GLint alpha_ref;
		GLint weigh_by_alpha;
		GLint order;
		GLint blend_mode;
		GLint pattern;
		GLint lod_bias;
		GLint family;
	};
	/* What it was given last, so as not to give it again: indices in
	 * the display list being drawn, forgotten at each scene
	 * (GlDrawing::begin_scene counts them). */
	struct Held {
		uint32_t drawing = ~0u;
		uint32_t vp_index, ls_index, ls_vp_index;
		uint32_t mv_index, mv2_index;
		float alpha_ref, depth_scale, sort_scale, scale;
		int weigh_by_alpha, snap;
		uint32_t pattern;
	};

	GLuint id;
	Locations locs;
	Held held;
};

/* The programs compiled so far, by variant.
 *
 * The first time a program is needed the graphics driver compiles it, and
 * the first time it draws the driver works on it again (NVIDIA 595: some
 * 20 ms, then 10 on the thread that draws): a stutter each time a game
 * shows something new. So the programs a game draws with are listed in a
 * file, which starts with a line that tells what it is and then has the
 * key of a program a line: listed programs are compiled, and draw once,
 * when the game starts. */
class MeshPrograms {
public:
	/* The texture units the programs read, set once; those of the
	 * translucent passes are in translucent_gl.cpp */
	enum : GLint {
		UNIT_TEXTURE = 0,
		UNIT_SOLID_DEPTH = 6,
		UNIT_PEEL_LAST = 7,
		UNIT_EYE = 8,		/* the other family's; the translucent ones: the unflagged */
		UNIT_FLAGGED_EYE = 9
	};

	~MeshPrograms();

	/* The program of a variant, compiled the first time. A program just
	 * compiled is left in use. */
	MeshProgram &get(const MeshVariant &variant);

	bool holds(uint64_t key) const { return m_programs.contains(key); }
	/* Compiles the program of a key read_program_list gave, unless it is
	 * held, and leaves it in use. */
	void add(uint64_t key);
	/* Lists in a file the programs compiled from now on, after the ones
	 * it lists already. */
	void list_in(const std::string &path) { m_list.list_in(path); }
	/* Keeps each program the driver links in that folder, and takes it
	 * from there the next times (program_cache.hpp), when the driver can
	 * give its programs; empty: none kept. */
	void keep_in(const std::string &folder);
	/* The programs made ready so far: read back from the folder, and
	 * compiled */
	unsigned read_back() const { return m_read_back; }
	unsigned compiled() const { return m_compiled; }
	/* The program of these sources (with a geometry shader, unless its
	 * text is empty), kept under that name in the folder: the one kept,
	 * when this driver made it from them and takes it back, else compiled
	 * and kept */
	GLuint link(const std::string &name, const std::string &vertex, const std::string &fragment,
	            const std::string &geometry);

private:
	MeshProgram compile(const MeshVariant &variant);

	std::unordered_map<uint64_t, MeshProgram> m_programs;
	ProgramList m_list;
	std::string m_kept, m_driver;	/* the folder, and the driver the programs are made by */
	unsigned m_read_back = 0, m_compiled = 0;
};

} /* namespace odin */

#endif /* ODIN_BOARD_PROGRAMS_GL_HPP */
