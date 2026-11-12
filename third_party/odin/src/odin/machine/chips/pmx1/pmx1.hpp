/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ODIN_PMX1_HPP
#define ODIN_PMX1_HPP

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include "odin/machine/chips/pmx1/pmx1_scene.hpp"

namespace odin {

/* The 3D core of the PowerVR Neon 250: when the primary core (Pmx1Core)
 * starts a render, the scene it built is drawn on the host's GPU (HLE: the
 * chip's ISP and TSP are not run pixel by pixel). */
class Pmx1 {
public:
	/* `vram` is the 32 MB the slave SH-4 sees at 18000000 */
	explicit Pmx1(std::span<uint8_t> vram);

	/* Draws the scene the core built, from the 3D registers (by index / 4) */
	void draw(std::span<const uint32_t, 0x200> regs_3d);

	static constexpr unsigned WIDTH = 640, HEIGHT = 480;
	/* Scenes draw at `fine` times screen resolution. Flat polygons: as
	 * they are (0), corners snapped to pixels (1), or over the pixels
	 * they cover (2). `card_sorts`: the host's GPU sorts translucent
	 * fragments pixel by pixel as the chip does (PmxDraw::per_pixel). */
	void set_look(unsigned fine, unsigned flat_mode, bool card_sorts)
	{
		m_fine = fine;
		m_flat_mode = flat_mode;
		m_card_sorts = card_sorts;
	}
	/* The last scene, with everything it needs loaded since the previous
	 * call */
	PmxScene take_scene();
	/* Who knows the writes to the frame buffer: `keep` is told of the
	 * bytes from-to of a texture just read, and answers a time; `kept`
	 * says whether nothing wrote those bytes since that time. A texture
	 * nothing wrote is not read again. */
	void set_pages(std::function<bool(uint32_t from, uint32_t to, uint64_t since)> kept,
	               std::function<uint64_t(uint32_t from, uint32_t to)> keep)
	{
		m_kept = std::move(kept);
		m_keep = std::move(keep);
	}


private:
	uint32_t word(uint32_t offset) const;
	struct State {
		uint32_t isp, tsp, tcw;
	};
	struct Vertex {
		float x, y, w, u, v;
		uint32_t argb, offset;
	};
	void render_tiles(uint32_t region_base, uint32_t object_base);
	void take_list(uint32_t address, unsigned list);
	void draw_object(uint32_t object_base, uint32_t start, uint32_t form, uint32_t mask, bool translucent);
	void draw_triangles(const State &state, uint32_t first, uint32_t size, uint32_t mask, bool translucent);
	void say(unsigned what, const char *text);
	struct Triangle {
		State state;
		Vertex a, b, c;
		bool translucent, native, sorted, per_pixel;
	};
	void push_triangle(const State &state, Vertex a, Vertex b, Vertex c, bool translucent);
	struct Point {
		double x, y;
	};
	static Point overlap_of(const Triangle &l, const Triangle &r);
	void sort_translucent(size_t from);
	void build_scene();
	uint32_t image_of(uint32_t tsp, uint32_t tcw);
	struct Texture {
		uint32_t codebook, base, width, height, side, stride;
		unsigned format, levels;
		float lod_scale;
		bool vq, palettised, linear, mipmap, flip_u, flip_v, clamp_u, clamp_v, bilinear;
	};
	Texture texture_of(uint32_t tsp, uint32_t tcw) const;
	void extent(const Texture &t, uint32_t &from, uint32_t &to) const;
	uint64_t hash_of(uint32_t from, uint32_t to) const;
	static uint32_t mip_of(const Texture &t, uint32_t size_x);
	uint32_t fetch(const Texture &t, uint32_t size_x, uint32_t size_y, uint32_t mip, uint32_t x, uint32_t y) const;

	std::span<uint8_t> m_vram;
	std::function<bool(uint32_t, uint32_t, uint64_t)> m_kept;
	std::function<uint64_t(uint32_t, uint32_t)> m_keep;
	std::vector<Triangle> m_triangles;
	unsigned m_fine = 1, m_flat_mode = 0;
	bool m_card_sorts = false;
	/* The texels a row of a stride texture takes, for this render */
	uint32_t m_stride = WIDTH;
	/* The scene, and what is known of the textures on the GPU */
	PmxScene m_scene;
	struct CardImage {
		uint32_t id;
		uint64_t hash;
		unsigned used;	/* the last render that used it */
		uint64_t read;	/* when its bytes were read, for m_kept */
	};
	std::unordered_map<uint64_t, CardImage> m_card_images;
	uint32_t m_next_image = 1;
	uint32_t m_last_isp = 0, m_last_tsp = 0, m_last_tcw = 0;
	unsigned m_rendered = 0;
	unsigned m_said = 0;
	/* The strips of a scene by their start (take_list), the starts of each
	 * pass's opaque then translucent list */
	static constexpr uint32_t MOST_PASSES = 8;
	std::vector<uint16_t> m_strips;
	std::array<std::vector<uint32_t>, 2 * MOST_PASSES> m_lists;
};

} /* namespace odin */

#endif
