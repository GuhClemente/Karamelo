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

#ifndef ODIN_HIKARU_DRAW_HPP
#define ODIN_HIKARU_DRAW_HPP

#include <cstdint>

#include "odin/machine/chips/antarctic/antarctic_list.hpp"
#include "odin/machine/chips/eurasia/eurasia_layers.hpp"

/* What the board asks to be drawn, worked out from its lists in plain data,
 * without a word of a graphics API: the rules of the board are written here
 * once, and the code of each API (OpenGL, Vulkan) only turns them into its
 * own calls. */

namespace odin {

/* The screen of the hardware, in pixels; a byte of a colour of the board
 * as a share */
inline constexpr unsigned SCREEN_WIDTH = 640, SCREEN_HEIGHT = 480;
inline constexpr float INV255 = 1.0f / 255.0f;

/* How a texture is read, as its texhead says: along u and v, its filter,
 * the level moved */
struct TextureSampling {
	enum class Wrap : uint8_t { CLAMP, REPEAT, MIRROR };
	enum class Filter : uint8_t { NEAREST, LINEAR, TRILINEAR };

	Wrap u, v;
	Filter filter;
	float lod_bias;
};

inline TextureSampling
sampling_of(const odin_texhead_t &th)
{
	TextureSampling s;

	/* Mirror wins over repeat, else clamp. */
	s.u = odin_tex_mirroru(&th) ? TextureSampling::Wrap::MIRROR :
	      odin_tex_repeatu(&th) ? TextureSampling::Wrap::REPEAT : TextureSampling::Wrap::CLAMP;
	s.v = odin_tex_mirrorv(&th) ? TextureSampling::Wrap::MIRROR :
	      odin_tex_repeatv(&th) ? TextureSampling::Wrap::REPEAT : TextureSampling::Wrap::CLAMP;
	/* Filter of the 0C1 mode; mode 3 is left at the sampler defaults in
	 * the port, trilinear here (HEURISTIC). */
	switch (odin_tex_filter(&th)) {
	case 0:
		s.filter = TextureSampling::Filter::NEAREST;
		break;
	case 2:
		s.filter = TextureSampling::Filter::LINEAR;
		break;
	default:
		s.filter = TextureSampling::Filter::TRILINEAR;
		break;
	}
	/* The byte of the 0C1 moves the level. HEURISTIC: eighths of a level,
	 * signed, on top of the -0.5 of the X360 port (825C6650), which
	 * ignores the byte and filters with an anisotropy of 8 instead. The
	 * floor of the VON4 arena sends D0 and its cabinet shows the finest
	 * level where a pixel is 25 texels long: sixteenths do not reach it.
	 * How the board picks the level is not known; the one of OpenGL
	 * stands here. */
	s.lod_bias = -0.5f + static_cast<float>(odin_tex_bias(&th)) * 0.125f;
	return s;
}

/* How a mesh goes over what is drawn. The passes that do not blend replace
 * the pixel and cover what is below the 3D (they leave 0 in the alpha of
 * the colour buffer, which holds what the 3D lets through); a mesh faded
 * by the depth queue lets through what it does not hide. The blend modes
 * of a material (A81 bits 17:16), as the X360 port applies them (825B4290):
 * source alpha over the destination, the product, source alpha added, the
 * source as it is over the destination. Where the source is weighted by
 * its alpha the shader does it, before the clamp (weighs_by_alpha). The
 * translucent type is blended by the shader that sorts its fragments,
 * which does the same. */
enum class MeshBlend : uint8_t { REPLACE, FADED, ALPHA_OVER, PRODUCT, ALPHA_ADDED, OVER };

/* The blend of a mode of a material, 0 to 3 */
inline MeshBlend
material_blend(unsigned mode)
{
	constexpr MeshBlend MODES[4] = { MeshBlend::ALPHA_OVER, MeshBlend::PRODUCT, MeshBlend::ALPHA_ADDED, MeshBlend::OVER };

	return MODES[mode & 3];
}

inline bool
weighs_by_alpha(unsigned mode)
{
	return mode == 0 || mode == 2;
}

/* How the meshes of a polygon type are drawn: the render passes of the
 * X360 port (825B2408, 825B3EB0). The opaque and shadow types write the
 * depth without blending nor alpha test; the transparent type is
 * punch-through (alpha > 0.5, depth written, no blending); the background
 * and the translucent type blend with the mode of their material, keep the
 * depth and drop alpha <= 1/255. The background alone is not moved by the
 * depth bias of 101. */
struct PassRules {
	bool depth_write;
	bool depth_bias;
	float alpha_ref;	/* fragments at or below it are dropped; -1: none */
	bool material_blend;	/* else REPLACE, or FADED by the depth queue */
};

/* A view of a list (021, 621, 011 words; odin_viewport_t). Its clip window
 * is cut by the scissor, whole pixels of the screen of the hardware. A view
 * turned over by a distance below 0 shows the faces it would show the
 * right way: the rear-view mirror of SGNASCAR is such a view, its meshes
 * wound as ahead and nothing else told (021). */
inline bool
view_turned_over(const odin_viewport_t &vp)
{
	return (vp.clip.n / vp.clip.f) * (vp.clip.n / vp.clip.f2) < 0.0f;
}

/* The projection of a view, the matrix of glFrustum (its columns first;
 * taken from Mesa). The centre and the clip window are screen coordinates,
 * y down: the view is centred on the offset, reaches the four edges of the
 * screen and is cut by the window. The driver panels of SGNASCAR sit 40
 * lines below the middle and stop under their banner; the letterboxed
 * robots of von4 fill the lines 39 to 360, as on the cabinets. The BOOTROM
 * menu has its centre on the top left corner of the screen. The far plane
 * is put at 1e5 (HEURISTIC). The projection plane is clip.f pixels away on
 * the screen for x and clip.f2 pixels away for y. */
inline void
view_projection(const odin_viewport_t &vp, float out[4][4])
{
	const float kx = vp.clip.n / vp.clip.f;
	const float ky = vp.clip.n / vp.clip.f2;
	const float l = (0.0f - vp.offset.x) * kx;
	const float r = (static_cast<float>(SCREEN_WIDTH) - vp.offset.x) * kx;
	const float b = (vp.offset.y - static_cast<float>(SCREEN_HEIGHT)) * ky;
	const float t = vp.offset.y * ky;
	const float n = vp.clip.n, f = 1e5f;

	out[0][0] = (2.0f * n) / (r - l);
	out[1][0] = 0.0f;
	out[2][0] = (r + l) / (r - l);
	out[3][0] = 0.0f;

	out[0][1] = 0.0f;
	out[1][1] = (2.0f * n) / (t - b);
	out[2][1] = (t + b) / (t - b);
	out[3][1] = 0.0f;

	out[0][2] = 0.0f;
	out[1][2] = 0.0f;
	out[2][2] = - (f + n) / (f - n);
	out[3][2] = - (2.0f * f * n) / (f - n);

	out[0][3] = 0.0f;
	out[1][3] = 0.0f;
	out[2][3] = -1.0f;
	out[3][3] = 0.0f;
}

/* The depth queue of a view, 621 words without a colour: P = 1 / (end -
 * start), Q = near / start (PHARRIER library 0C015A3E) */
inline void
view_cue(const odin_viewport_t &vp, float out[2])
{
	out[0] = vp.clip.n / vp.depth.near_by_edge;
	out[1] = vp.depth.inv_range;
}

/* Its fog, 621 words: P = 1 / (end - start), Q = near / end (von4 library
 * 0C716320); the port fogs linearly from start to end. Its colour. */
inline void
view_fog(const odin_viewport_t &vp, float fog[2], float colour[3])
{
	const float p = vp.depth.inv_range, q = vp.depth.near_by_edge;

	fog[0] = fog[1] = 0.0f;
	if (p > 0.0f && q > 0.0f) {
		fog[0] = vp.clip.n / q - 1.0f / p;
		fog[1] = p;
	}
	for (unsigned c = 0; c < 3; c++)
		colour[c] = static_cast<float>(vp.depth.mask[c]) * INV255;
}

/* The ambient light of a view (011) */
inline void
view_ambient(const odin_viewport_t &vp, float out[3])
{
	for (unsigned c = 0; c < 3; c++)
		out[c] = static_cast<float>(vp.color.ambient[c]) * INV255;
}

/* A light of a lightset as the shader takes it: its colour (051), its
 * ambient part in sixteenths, the points of its table, 0x400 is 1.0 */
inline void
light_colour(const odin_light_t &light, float out[3])
{
	for (unsigned c = 0; c < 3; c++)
		out[c] = static_cast<float>(light.diffuse[c]) * INV255;
}

inline float
light_ambient(const odin_light_t &light)
{
	return static_cast<float>(odin_light_ambient(&light)) * (1.0f / 16.0f);
}

inline void
light_table(const uint16_t points[16], float out[16])
{
	for (unsigned j = 0; j < 16; j++)
		out[j] = static_cast<float>(points[j]) * (1.0f / 1024.0f);
}

/* The 32 patterns of the X360 port (823000D0) a mesh may be drawn with
 * (501), a bit a pixel, bit 0 at the top left */
inline uint16_t
pattern_bits(unsigned pattern)
{
	constexpr uint16_t PATTERNS[32] = {
		0xFFFF, 0xFBFE, 0xFAFA, 0x7ADA, 0x5A5A, 0x5258, 0x5050, 0x1040,
		0x0000, 0x0401, 0x0505, 0x8525, 0xA5A5, 0xADA7, 0xAFAF, 0xEFBF,
		0xFFFF, 0xFDF7, 0xF5F5, 0xE5B5, 0xA5A5, 0xA4A1, 0xA0A0, 0x8020,
		0x0000, 0x0208, 0x0A0A, 0x1A4A, 0x5A5A, 0x5B5E, 0x5F5F, 0x7FDF,
	};

	return PATTERNS[pattern & 31];
}

/* How bright the colours of a 2D layer are shown: A2BGR10 four times (as
 * Valkyrie had it; HEURISTIC) */
inline float
layer_brightness(const odin_layer_t &layer)
{
	return layer.format == ODIN_FORMAT_A2BGR10 ? 4.0f : 1.0f;
}

/* The picture of a frame composed from the 3D picture and the 2D layers of
 * unit 0 of the display chip (the unit 1 is probably the second monitor),
 * from what the 781 and the 181 tell alone: the two inputs of the blender,
 * each the 3D picture or a layer (PHARRIER blends layer 2 in for its logo
 * screens, 781 = 0704/0705), and their factor (two 3D buffers were blended
 * when their list was drawn); a layer shown on its own where it is no
 * input; what each is multiplied by; the keys, the layers above or below
 * the 3D, the inputs read upside down (the layers), the colour the empty 3D
 * lets through. A layer not shown has no part. */
struct Compose {
	enum class Source : uint8_t { PICTURE, LAYER_1, LAYER_2 };

	Source front, back;
	float factor;
	bool alone[2];
	float mult_front, mult_back, mult_alone[2];
	bool keyed[2], keyed_3d, above[2];
	bool invert_front, invert_back;
	float clear[4];
};

inline Compose
compose_of(const odin_fb_config_t &fb, const odin_layers_t &layers, const bool shown[2])
{
	const uint32_t n1 = odin_fb_num_1(&fb), n2 = odin_fb_num_2(&fb);
	const uint32_t rate = odin_fb_blend(&fb) ? odin_fb_alpha(&fb) : 0;
	const auto source = [](uint32_t n) {
		return n < 2 ? Compose::Source::PICTURE : n == 2 ? Compose::Source::LAYER_1 : Compose::Source::LAYER_2;
	};
	const float mult[2] = { layer_brightness(layers.layer[0][0]), layer_brightness(layers.layer[0][1]) };
	/* What an input is multiplied by: its layer's brightness, 1 for the
	 * 3D or a layer not shown */
	const auto mult_of = [&](Compose::Source s) {
		return (s == Compose::Source::LAYER_1 && shown[0]) ? mult[0] :
		       (s == Compose::Source::LAYER_2 && shown[1]) ? mult[1] : 1.0f;
	};
	Compose c;

	c.front = source(n1);
	c.back = source(n2);
	c.factor = (n1 < 2 && n2 < 2) ? 0.0f : static_cast<float>(rate) * (1.0f / 255);
	for (unsigned i = 0; i < 2; i++) {
		const Compose::Source layer = i == 0 ? Compose::Source::LAYER_1 : Compose::Source::LAYER_2;

		c.alone[i] = shown[i] && c.front != layer && c.back != layer;
		c.mult_alone[i] = c.alone[i] ? mult[i] : 0.0f;
		c.keyed[i] = layers.layer[0][i].keyed != 0;
		c.above[i] = layers.layer[0][i].above != 0;
	}
	c.mult_front = mult_of(c.front);
	c.mult_back = mult_of(c.back);
	c.keyed_3d = layers.fb_keyed != 0;
	c.invert_front = n1 >= 2;
	c.invert_back = n2 >= 2;
	for (unsigned k = 0; k < 4; k++)
		c.clear[k] = static_cast<float>(fb.clear[k]) * INV255;
	return c;
}

inline PassRules
pass_rules(unsigned polytype)
{
	switch (polytype) {
	case ODIN_POLYTYPE_TRANSPARENT:
		return { true, true, 0.5f, false };
	case ODIN_POLYTYPE_TRANSLUCENT:
		return { false, true, 1.0f / 255.0f, true };
	case ODIN_POLYTYPE_BACKGROUND:
		return { false, false, 1.0f / 255.0f, true };
	default:
		return { true, true, -1.0f, false };
	}
}

} /* namespace odin */

#endif /* ODIN_HIKARU_DRAW_HPP */
