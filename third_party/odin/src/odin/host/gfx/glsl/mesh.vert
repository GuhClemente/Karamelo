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

/* A mesh of the display list, after the defines of its variant
 * (programs_gl.cpp). With Vulkan, the views and the modelviews of the
 * list are in buffers, the numbers of the mesh's in push constants
 * (scene_vk.cpp). */

#ifdef VULKAN
struct view_t {
	mat4 projection;
	vec2 fog;
	vec2 cue;
	vec3 fog_color;
	vec3 ambient;
};
layout(std430, set = 0, binding = 0) readonly buffer views_t {
	view_t u_views[];
};
layout(std430, set = 0, binding = 1) readonly buffer modelviews_t {
	mat4 u_modelviews[];
};
layout(push_constant) uniform mesh_t {
	uint u_view, u_mv, u_mv2, u_lightset;
	float u_depth_scale, u_alpha_ref, u_weigh_by_alpha;
	uint u_pattern;
	uint u_order, u_blend_mode;
	float u_sort_scale;
	uint u_snap;
	float u_scale;
	float u_family;
};
/* 0: the card clips z from 0 to w, not from -w to w as OpenGL */
layout(constant_id = 0) const int GL_CLIP_SPACE = 1;
#define u_projection u_views[u_view].projection
#define u_modelview u_modelviews[u_mv]
#define u_modelview2 u_modelviews[u_mv2]
/* A matrix times a vector, its columns summed from the first, as Mesa's
 * OpenGL does: its SPIR-V path sums them from the last, which rounds
 * otherwise */
vec4 mul (mat4 m, vec4 v) { return m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * v.w; }
vec3 mul (mat3 m, vec3 v) { return m[0] * v.x + m[1] * v.y + m[2] * v.z; }
#else
uniform mat4 u_projection;
uniform mat4 u_modelview;
uniform mat4 u_modelview2;
uniform float u_depth_scale;
uniform uint u_snap;
#define mul(m, v) ((m) * (v))
#endif

/* OpenGL 4.1 matches the stages by name */
#ifdef PEELING
#if NATIVE_2D != 0
#define p_position g_position
#define p_normal g_normal
#define p_diffuse g_diffuse
#define p_specular g_specular
#define p_color1 g_color1
#define p_ambient g_ambient
#define p_texcoords g_texcoords
#define p_alpha g_alpha
#else
#define p_position q_position
#define p_normal q_normal
#define p_diffuse q_diffuse
#define p_specular q_specular
#define p_color1 q_color1
#define p_ambient q_ambient
#define p_texcoords q_texcoords
#define p_alpha q_alpha
#endif
#endif

layout(location = 0) in vec3 i_position;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec3 i_diffuse;
layout(location = 3) in vec3 i_ambient;
layout(location = 4) in vec4 i_specular;
layout(location = 5) in vec3 i_color1;
layout(location = 6) in vec2 i_texcoords;
layout(location = 7) in float i_alpha;
layout(location = 8) in float i_second_mv;

layout(location = 0) out vec4 p_position;
layout(location = 1) out vec3 p_normal;
layout(location = 2) out vec3 p_diffuse;
layout(location = 3) out vec4 p_specular;
layout(location = 4) out vec3 p_color1;
layout(location = 5) out vec3 p_ambient;
layout(location = 6) out vec2 p_texcoords;
layout(location = 7) out float p_alpha;
/* The corners of the triangle, for mesh.frag: given by mesh.geom alone */
layout(location = 8) flat out vec2 p_corner0;
layout(location = 9) flat out vec2 p_corner1;
layout(location = 10) flat out vec2 p_corner2;

void main (void) {
	/* 1: the second modelview, 64: without the 3x3, 128: without
	 * the last row. A normal goes through the 3x3 of its modelview,
	 * as the position does (US patent 6,714,690 of SEGA: the same
	 * matrix, the translation switched off). */
	int which = int (i_second_mv * 255.0 + 0.5);
	if (which == 1) {
		p_position = mul (u_modelview2, vec4 (i_position, 1.0));
		p_normal = mul (mat3 (u_modelview2), i_normal);
	} else if (which == 0) {
		p_position = mul (u_modelview, vec4 (i_position, 1.0));
		p_normal = mul (mat3 (u_modelview), i_normal);
	} else if ((which & 1) != 0) {
		bool rotate = (which & 64) == 0;
		vec3 pos = rotate ? mul (mat3 (u_modelview2), i_position) : i_position;
		if ((which & 128) == 0)
			pos += u_modelview2[3].xyz;
		p_position = vec4 (pos, 1.0);
		p_normal = rotate ? mul (mat3 (u_modelview2), i_normal) : i_normal;
	} else {
		bool rotate = (which & 64) == 0;
		vec3 pos = rotate ? mul (mat3 (u_modelview), i_position) : i_position;
		if ((which & 128) == 0)
			pos += u_modelview[3].xyz;
		p_position = vec4 (pos, 1.0);
		p_normal = rotate ? mul (mat3 (u_modelview), i_normal) : i_normal;
	}
	/* The depth bias brings the whole position nearer: same pixel. */
	gl_Position = mul (u_projection, vec4 (p_position.xyz * u_depth_scale, 1.0));
#ifdef VULKAN
	if (GL_CLIP_SPACE == 0)
		gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
#endif
	/* Snap 2D: its corners on the pixel corners of the screen of the
	 * hardware (Renderer::draw_mesh), each edge between the pixels it
	 * covers at that size: a corner to 1/256 of a pixel, as the card
	 * rasterizes, and a centre on a left or low edge taken in. */
	if (u_snap != 0u) {
		const vec2 screen = vec2 (640.0, 480.0);
		vec2 at = floor ((gl_Position.xy / gl_Position.w * 0.5 + 0.5) * screen * 256.0 + 0.5) / 256.0;
		gl_Position.xy = (ceil (at - 0.5) / screen * 2.0 - 1.0) * gl_Position.w;
	}

	p_diffuse = i_diffuse;
	p_ambient = i_ambient;
	p_specular = i_specular;
	p_color1 = i_color1;
	p_texcoords = i_texcoords;
	p_alpha = i_alpha;
	p_corner0 = p_corner1 = p_corner2 = vec2 (0.0);
}
