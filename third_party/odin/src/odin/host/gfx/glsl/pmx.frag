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

/* Neon 250 pixel: texture over colour, then offset colour, then fog */

/* u_fog: the fog table, a texel a step, the fog at its start (r) and end (g) */
#ifdef VULKAN
layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(set = 0, binding = 1) uniform sampler2D u_fog;
layout(push_constant) uniform Draw {
	float most_w;
	float lod_bias;
	int flags;
	float scale;
	float fog_density;
	uint fog_color;
	uint fog_vertex_color;
#ifdef LIST
	uint order;
	uint mode;
#endif
} draw;
#define LOD_BIAS draw.lod_bias
#define FLAGS draw.flags
#define SCALE draw.scale
#define FOG_DENSITY draw.fog_density
#define FOG_COLOR unpackUnorm4x8(draw.fog_color).bgr
#define FOG_VERTEX_COLOR unpackUnorm4x8(draw.fog_vertex_color).bgr
#else
uniform sampler2D u_texture;
uniform sampler2D u_fog;
uniform float u_lod_bias;
uniform int u_flags;
uniform float u_scale;
uniform float u_fog_density;
uniform vec3 u_fog_color, u_fog_vertex_color;
#define LOD_BIAS u_lod_bias
#define FLAGS u_flags
#define SCALE u_scale
#define FOG_DENSITY u_fog_density
#define FOG_COLOR u_fog_color
#define FOG_VERTEX_COLOR u_fog_vertex_color
#endif

#ifdef NATIVE
/* As pmx.geom: the corners of its box share one w */
#ifdef GL_ES
#define SCREEN_LINEAR
#else
#define SCREEN_LINEAR noperspective
#endif
layout(location = 0) SCREEN_LINEAR in vec4 q_color;
layout(location = 1) SCREEN_LINEAR in vec4 q_offset;
layout(location = 2) in vec2 q_uv;
layout(location = 3) flat in vec2 q_corner0;
layout(location = 4) flat in vec2 q_corner1;
layout(location = 5) flat in vec2 q_corner2;
vec4 v_color, v_offset;
vec2 v_uv;
#elif defined(GL_ES)
/* pmx.vert: the colours times w, and w */
layout(location = 0) in vec4 e_color;
layout(location = 1) in vec4 e_offset;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in float e_w;
vec4 v_color, v_offset;
#else
layout(location = 0) noperspective in vec4 v_color;
layout(location = 1) noperspective in vec4 v_offset;
layout(location = 2) in vec2 v_uv;
#endif

layout(location = 0) out vec4 o_color;

#ifdef LIST
/* The fragment goes to the list of its pixel (pmx_sort.frag), tested first
 * against the depth the opaque polygons left: its colour, 8 bits a
 * component as inside the chip (PG, the chip's programming guide, p. 120); its depth; the one before it;
 * its rank in TA order << 6 | its blend codes, source | destination << 3 */
layout(early_fragment_tests) in;
struct node_t {
	uint color;
	float depth;
	uint next;
	uint order;
};
#ifdef VULKAN
layout(std430, set = 1, binding = 0) buffer nodes_t {
	node_t u_nodes[];
};
layout(set = 1, binding = 1, r32ui) uniform uimage2D u_heads;
layout(std430, set = 1, binding = 2) buffer count_t {
	uint u_count;
};
#define ORDER draw.order
#define MODE draw.mode
#else
layout(binding = 0, r32ui) uniform uimage2D u_heads;
layout(std430, binding = 0) buffer nodes_t {
	node_t u_nodes[];
};
layout(std430, binding = 1) buffer count_t {
	uint u_count;
};
/* The rank of the draw's first triangle; its blend codes, source | destination << 3 */
uniform uint u_order, u_mode;
#define ORDER u_order
#define MODE u_mode
#endif

/* Whether blending c gives back every colour below it (pmx_sort.frag's
 * factors): nothing added, and the colour below kept whole */
bool
keeps_below(vec4 c, uint mode)
{
	uint s = mode & 7u, d = mode >> 3;
	bool none = s == 0u || s == 7u || (s == 4u && c.a == 0.0) || (s == 5u && c.a == 1.0);
	bool whole = d == 1u || d == 6u || (d == 4u && c.a == 1.0) || (d == 5u && c.a == 0.0);

	for (int k = 0; k < 3; k++) {
		if (!none && c[k] != 0.0)
			return false;
		if (!whole && !(d == 2u && c[k] == 1.0) && !(d == 3u && c[k] == 0.0))
			return false;
	}
	return true;
}
#endif

#ifdef NATIVE
/* Native 2D, as in mesh.frag: whether c is inside edge a-b, or on an edge
 * the card would include at this scale */
bool
holds(vec2 a, vec2 b, vec2 c, float area)
{
	vec2 d = area > 0.0 ? b - a : a - b;
	float e = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);

	if (area < 0.0)
		e = -e;
	if (e != 0.0)
		return e > 0.0;
	return d.y < 0.0 || (d.y == 0.0 && d.x > 0.0);
}

bool
in_triangle(vec2 c)
{
	float area = (q_corner1.x - q_corner0.x) * (q_corner2.y - q_corner0.y) -
	             (q_corner1.y - q_corner0.y) * (q_corner2.x - q_corner0.x);

	return area != 0.0 && holds(q_corner0, q_corner1, c, area) && holds(q_corner1, q_corner2, c, area) &&
	       holds(q_corner2, q_corner0, c, area);
}
#endif

/* The table's fog at this 1/W (gl_FragCoord.w): 1/W times the density,
 * 1 to 256, 16 steps an octave, linear within a step (INFERRED: the
 * Dreamcast CLX2's) */
float
table_fog(float w)
{
	float z = clamp(w * FOG_DENSITY, 1.0, 255.9999);
	float octave = floor(log2(z));
	float step = z * 16.0 / exp2(octave) - 16.0;
	vec2 ends = texelFetch(u_fog, ivec2(int(octave) * 16 + int(step), 0), 0).rg;

	return mix(ends.r, ends.g, fract(step));
}

void
main()
{
#ifdef NATIVE
	/* Keep fine pixels whose chip pixel centre is in the triangle. Those
	 * outside it use that centre's values. */
	vec4 dx0 = dFdx(q_color), dy0 = dFdy(q_color), dx1 = dFdx(q_offset), dy1 = dFdy(q_offset);
	vec2 dx2 = dFdx(q_uv), dy2 = dFdy(q_uv);
	vec2 at = gl_FragCoord.xy / SCALE, centre = floor(at) + 0.5;

	if (!in_triangle(centre))
		discard;
	v_color = q_color;
	v_offset = q_offset;
	v_uv = q_uv;
	if (!in_triangle(at)) {
		vec2 d = (centre - at) * SCALE;

		v_color += dx0 * d.x + dy0 * d.y;
		v_offset += dx1 * d.x + dy1 * d.y;
		v_uv += dx2 * d.x + dy2 * d.y;
	}
#elif defined(GL_ES)
	v_color = e_color / e_w;
	v_offset = e_offset / e_w;
#endif
	/* Bits 0-3: textured, use alpha, ignore texture alpha, offset. Bits
	 * 4-5: shading. Bits 6-7: fog (PmxDraw::fog; by the vertex: the offset
	 * colour's alpha). */
	vec4 color = v_color;
	int fog = (FLAGS >> 6) & 3;

	if ((FLAGS & 2) == 0)
		color.a = 1.0;
	if ((FLAGS & 1) != 0) {
		vec4 texel = texture(u_texture, v_uv, LOD_BIAS);
		int shading = (FLAGS >> 4) & 3;

		if ((FLAGS & 4) != 0)
			texel.a = 1.0;
		if (shading == 0) {
			color = texel;
		} else if (shading == 1) {
			color = vec4(texel.rgb * color.rgb, texel.a);
		} else if (shading == 2) {
			color.rgb = mix(color.rgb, texel.rgb, texel.a);
		} else {
			color *= texel;
		}
	}
	if ((FLAGS & 8) != 0)
		color.rgb = min(color.rgb + v_offset.rgb, vec3(1.0));
	if (fog == 0)
		color.rgb = mix(color.rgb, FOG_COLOR, table_fog(gl_FragCoord.w));
	else if (fog == 1)
		color.rgb = mix(color.rgb, FOG_VERTEX_COLOR, v_offset.a);
	else if (fog == 3)
		color = vec4(FOG_COLOR, table_fog(gl_FragCoord.w));
#ifdef LIST
	/* HEURISTIC: rounded to the nearest 8 bits; PG p. 120 gives the
	 * width, not the rounding */
	uint bits = packUnorm4x8(color);

	if (keeps_below(unpackUnorm4x8(bits), MODE))
		return;

	uint n = atomicAdd(u_count, 1u);

	if (n < uint(u_nodes.length())) {
		u_nodes[n].color = bits;
		u_nodes[n].depth = gl_FragCoord.z;
		u_nodes[n].next = imageAtomicExchange(u_heads, ivec2(gl_FragCoord.xy), n + 1u);
		u_nodes[n].order = (ORDER + uint(gl_PrimitiveID)) << 6 | MODE;
	}
#else
	o_color = color;
#endif
}
