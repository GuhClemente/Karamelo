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

#ifndef ODIN_PMX1_SCENE_HPP
#define ODIN_PMX1_SCENE_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace odin {

/* A Neon 250 scene as the host GPU draws it, in chip order, translucent
 * polygons last; a triangle's rank in the scene is its order in the TA */
struct PmxVertex {
	float x, y, w;		/* screen position; 1/W */
	float u, v;
	uint32_t color, offset;	/* bytes B, G, R, A */
};

enum class PmxWrap : uint8_t { REPEAT, MIRROR, CLAMP };

struct PmxDraw {
	uint32_t first, count;	/* vertices, three per triangle */
	uint32_t image;		/* PmxImage::id; 0: none */
	float lod_bias;
	uint8_t compare;	/* 0 never, 1 <, 2 =, 3 <=, 4 >, 5 !=, 6 >=, 7 always */
	uint8_t src, dst;	/* the blend codes: zero, one, other colour and its
				 * inverse, source alpha and its inverse, those of
				 * the destination */
	uint8_t shading;	/* 0 decal, 1 modulate, 2 decal alpha, 3 modulate alpha */
	uint8_t fog;		/* 0 table, 1 vertex, 2 none, 3 table as alpha */
	PmxWrap wrap_u, wrap_v;
	bool blend, write_z, use_alpha, ignore_texture_alpha, offset, smooth, mipmap;
	bool native;		/* covers the chip's pixels */
	/* One of the translucent draws that follow each other, the chip
	 * sorting their fragments pixel by pixel from far to near, those of
	 * equal depth in their order (PG, the chip's programming guide, p. 82) */
	bool per_pixel;
};

/* A texture: RGBA, levels from the largest down */
struct PmxImage {
	uint32_t id;
	unsigned width, height;
	std::vector<std::vector<uint32_t>> levels;
};

struct PmxScene {
	std::vector<PmxVertex> vertices;
	std::vector<PmxDraw> draws;
	float most_w = 1.0f;	/* the largest 1/W */
	bool rendered = false;	/* a frame may have none */
	/* The fog table: 128 steps of 1/W times the density, the fog at the
	 * start and the end of each; the fog colours (bytes B, G, R) */
	std::array<uint8_t, 256> fog_table{};
	float fog_density = 0.0f;
	uint32_t fog_color = 0, fog_vertex_color = 0;
	/* What the GPU should load and forget first */
	std::vector<std::shared_ptr<const PmxImage>> new_images;
	std::vector<uint32_t> gone_images;
};

} /* namespace odin */

#endif
