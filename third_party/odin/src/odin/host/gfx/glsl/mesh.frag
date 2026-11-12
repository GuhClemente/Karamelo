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

/* A mesh of the display list, after fragment_lists.glsl and the defines of
 * its variant (programs_gl.cpp): each define is a constant the
 * branches below test, which the compiler folds. With Vulkan, the variant
 * is in specialization constants instead, all but COLLECT: gl_PrimitiveID
 * needs geometry or tessellation shaders of the card even in code never run; the data of
 * the list in buffers, the numbers of the mesh's in push constants
 * (scene_vk.cpp). */

struct light_t {
	vec3 position;
	vec3 direction;
	vec3 color;
	float ambient;
	vec2 extents;
	vec3 spot;
	vec2 cone;
	float table[16];
};

#ifndef FAMILIES
#define FAMILIES 0
#endif
#ifdef VULKAN
#ifndef COLLECT
#define COLLECT 0
#endif
layout(constant_id = 1) const int HAS_TEXTURE = 0;
layout(constant_id = 2) const int HAS_LIGHTING = 0;
layout(constant_id = 3) const int SHADING_MODE = 0;
layout(constant_id = 4) const int ALPHA_MODE = 0;
layout(constant_id = 5) const int TEX_MODE = 0;
layout(constant_id = 6) const int HAS_LIGHT0 = 0;
layout(constant_id = 7) const int HAS_LIGHT1 = 0;
layout(constant_id = 8) const int HAS_LIGHT2 = 0;
layout(constant_id = 9) const int HAS_LIGHT3 = 0;
layout(constant_id = 10) const int LIGHT0_TYPE = 0;
layout(constant_id = 11) const int LIGHT1_TYPE = 0;
layout(constant_id = 12) const int LIGHT2_TYPE = 0;
layout(constant_id = 13) const int LIGHT3_TYPE = 0;
layout(constant_id = 14) const int LIGHT0_ATT_TYPE = 0;
layout(constant_id = 15) const int LIGHT1_ATT_TYPE = 0;
layout(constant_id = 16) const int LIGHT2_ATT_TYPE = 0;
layout(constant_id = 17) const int LIGHT3_ATT_TYPE = 0;
layout(constant_id = 18) const int HAS_FOG = 0;
layout(constant_id = 19) const int HAS_CUE = 0;
layout(constant_id = 20) const int HAS_PATTERN = 0;
layout(constant_id = 21) const int NATIVE_2D = 0;

struct view_t {
	mat4 projection;
	vec2 fog;
	vec2 cue;
	vec3 fog_color;
	vec3 ambient;
};
struct lightset_t {
	light_t lights[4];
};
layout(std430, set = 0, binding = 0) readonly buffer views_t {
	view_t u_views[];
};
layout(std430, set = 0, binding = 2) readonly buffer lightsets_t {
	lightset_t u_lightsets[];
};
layout(set = 1, binding = 0) uniform texture2D u_texels;
layout(set = 2, binding = 0) uniform sampler u_sampling;
#if COLLECT != 0
layout(std430, set = 3, binding = 2) buffer count_t {
	uint u_count;
};
#endif
#if COLLECT != 0
layout(set = 3, binding = 3) uniform sampler2D u_solid_depth;
#endif
#if COLLECT != 0 || FAMILIES != 0
layout(set = 3, binding = 4) uniform sampler2D u_eye;
#endif
#if COLLECT != 0
layout(set = 3, binding = 5) uniform sampler2D u_flagged_eye;
#endif
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
#define u_lights u_lightsets[u_lightset].lights
#define u_ambient u_views[u_view].ambient
#define u_texture sampler2D (u_texels, u_sampling)
#define u_fog (HAS_FOG != 0 ? u_views[u_view].fog : u_views[u_view].cue)
#define u_fog_color u_views[u_view].fog_color
/* As Mesa's OpenGL normalizes: its SPIR-V path divides by the length */
#define normalize(v) ((v) * inversesqrt (dot ((v), (v))))

layout(location = 0) out vec4 fragColor;
#if FAMILIES != 0
layout(location = 1) out vec4 fragEye;
#endif
#elif defined(PEELING) && COLLECT != 0
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragInfo;
#elif FAMILIES != 0
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragEye;
#else
out vec4 fragColor;
#endif

#ifndef VULKAN
uniform light_t		u_lights[4];
uniform vec3		u_ambient;
uniform sampler2D	u_texture;
uniform vec2		u_fog;
uniform vec3		u_fog_color;
uniform float		u_alpha_ref;
uniform float		u_weigh_by_alpha;
uniform uint		u_pattern;
#ifdef GL_ES
/* OpenGL ES samplers have no bias of their own */
uniform float		u_lod_bias;
#endif

#ifndef PEELING
layout(std430, binding = 1) buffer count_t {
	uint u_count;
};
#else
uniform sampler2D	u_peel_last;
#endif
uniform sampler2D	u_solid_depth;
uniform sampler2D	u_eye;
uniform sampler2D	u_flagged_eye;
uniform float		u_family;
uniform uint		u_order;
uniform uint		u_blend_mode;
uniform float		u_depth_scale;
uniform float		u_sort_scale;
uniform float		u_scale;
#endif

/* What the vertices give, as it runs over the triangle; main takes it into
 * the p_ values, which Native 2D moves for some pixels */
layout(location = 0) in vec4 q_position;
layout(location = 1) in vec3 q_normal;
layout(location = 2) in vec3 q_diffuse;
layout(location = 3) in vec4 q_specular;
layout(location = 4) in vec3 q_color1;
layout(location = 5) in vec3 q_ambient;
layout(location = 6) in vec2 q_texcoords;
layout(location = 7) in float q_alpha;
layout(location = 8) flat in vec2 p_corner0;
layout(location = 9) flat in vec2 p_corner1;
layout(location = 10) flat in vec2 p_corner2;
vec4 p_position;
vec3 p_normal;
vec3 p_diffuse;
vec4 p_specular;
vec3 p_color1;
vec3 p_ambient;
vec2 p_texcoords;
float p_alpha;

/* Native 2D (mesh.geom): the point c, in pixels of the hardware, on the
 * side of the edge a to b that holds the triangle of that area, or on the
 * edge where the card takes it in at that size, a left or a low one */
bool
holds (vec2 a, vec2 b, vec2 c, float area)
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
in_triangle (vec2 c)
{
	float area = (p_corner1.x - p_corner0.x) * (p_corner2.y - p_corner0.y) -
	             (p_corner1.y - p_corner0.y) * (p_corner2.x - p_corner0.x);

	return area != 0.0 && holds (p_corner0, p_corner1, c, area) && holds (p_corner1, p_corner2, c, area) &&
	       holds (p_corner2, p_corner0, c, area);
}

void
apply_light (inout vec3 diffuse,
             inout vec3 specular,
             in light_t light,
             in int type,
             in int att_type)
{
	vec3 normal = normalize (p_normal);
	vec3 light_direction;
	float distance, attenuation, n_dot_l, r_dot_v;

	if (type == 3) {
		diffuse += light.color;
		return;
	}

	if (type == 0) {
		distance = length (light.direction);
		light_direction = light.direction / distance;
	} else {
		vec3 delta = light.position - p_position.xyz;
		distance = length (delta);
		light_direction = delta / distance;
	}

	/* X360 825C5BA0: sat (P d + P Q), sat (P d^2 + P Q). The port does
	 * not attenuate the types 2 and 3; the hardware does, on 1 / d and
	 * 1 / d^2 (the far red light of the PHARRIER logo scene, which
	 * leaves the asteroids grey on the cabinet). */
	if (att_type == 0)
		attenuation = clamp (light.extents.x * (distance + light.extents.y), 0.0, 1.0);
	else if (att_type == 1)
		attenuation = clamp (light.extents.x * (distance * distance + light.extents.y), 0.0, 1.0);
	else if (att_type == 2)
		attenuation = clamp (light.extents.x * (1.0 / distance + light.extents.y), 0.0, 1.0);
	else if (att_type == 3)
		attenuation = clamp (light.extents.x * (1.0 / (distance * distance) + light.extents.y), 0.0, 1.0);
	else
		attenuation = 1.0;

	/* The cone. sat ((L . axis - cos outer) * scale), the axis pointing
	 * from the target to the light, is not the light itself as in the
	 * X360 port (825C5498): it addresses the table of the light, whose
	 * sixteen points go from the cutoff to the cone angle with straight
	 * lines between them (US patent 6,333,742 of SEGA; the 194 loads
	 * the table). HEURISTIC: the first point is taken at the cutoff,
	 * the last at the cone angle, and nothing is lit beyond the cutoff,
	 * which the patent says is deleted. */
	if (type == 2) {
		float where = 15.0 * clamp ((dot (light_direction, normalize (-light.spot)) - light.cone.x) * light.cone.y, 0.0, 1.0);
		int point = min (int (where), 14);
		attenuation *= (where > 0.0) ? mix (light.table[point], light.table[point + 1], where - float (point)) : 0.0;
	}

	n_dot_l = dot (normal, light_direction);
	/* The ambient level of the 051 is a constant share of the colour,
	 * cut by the distance and by the cone like the Lambert term
	 * (HEURISTIC: the scale and the cuts are read from the cabinets). */
	diffuse += attenuation * (max (n_dot_l, 0.0) + light.ambient) * light.color;

	/* The reflected ray against the view. The vo3d pixel shader takes
	 * the Blinn half vector, whose lobe is about four times as wide for
	 * the same exponent: it washes out the PHARRIER logo seen from the
	 * front, which the cabinet shows plain blue. */
	r_dot_v = dot (reflect (-light_direction, normal), -normalize (p_position.xyz));
	if (n_dot_l > 0.0 && r_dot_v > 0.0)
		specular += attenuation * pow (r_dot_v, 255.0 * p_specular.a) * light.color;
}

void
main (void)
{
	vec4 color;
	vec3 base;
	float alpha;
	ivec2 pixel = ivec2 (gl_FragCoord.xy);

	p_position = q_position;
	p_normal = q_normal;
	p_diffuse = q_diffuse;
	p_specular = q_specular;
	p_color1 = q_color1;
	p_ambient = q_ambient;
	p_texcoords = q_texcoords;
	p_alpha = q_alpha;
	/* Native 2D: a fine pixel drawn when the centre of its pixel of the
	 * hardware is in the triangle; one off the triangle takes the values
	 * of that centre, which is on it (they run straight: the rates of the
	 * neighbours tell them) */
	if (NATIVE_2D != 0) {
		vec4 dx0 = dFdx (q_position), dy0 = dFdy (q_position);
		vec3 dx1 = dFdx (q_normal), dy1 = dFdy (q_normal);
		vec3 dx2 = dFdx (q_diffuse), dy2 = dFdy (q_diffuse);
		vec4 dx3 = dFdx (q_specular), dy3 = dFdy (q_specular);
		vec3 dx4 = dFdx (q_color1), dy4 = dFdy (q_color1);
		vec3 dx5 = dFdx (q_ambient), dy5 = dFdy (q_ambient);
		vec2 dx6 = dFdx (q_texcoords), dy6 = dFdy (q_texcoords);
		float dx7 = dFdx (q_alpha), dy7 = dFdy (q_alpha);
		vec2 at = gl_FragCoord.xy / u_scale, centre = floor (at) + 0.5;

		if (!in_triangle (centre))
			discard;
		if (!in_triangle (at)) {
			vec2 d = (centre - at) * u_scale;

			p_position += dx0 * d.x + dy0 * d.y;
			p_normal += dx1 * d.x + dy1 * d.y;
			p_diffuse += dx2 * d.x + dy2 * d.y;
			p_specular += dx3 * d.x + dy3 * d.y;
			p_color1 += dx4 * d.x + dy4 * d.y;
			p_ambient += dx5 * d.x + dy5 * d.y;
			p_texcoords += dx6 * d.x + dy6 * d.y;
			p_alpha += dx7 * d.x + dy7 * d.y;
		}
	}

	if (HAS_PATTERN != 0) {
		/* 501: bit 0 is the top left pixel of the 4 x 4 pattern. The pattern
		 * is laid in the pixels drawn: at a finer scale its dots are finer
		 * than those of the hardware, and hardly show. */
		ivec2 dot = pixel & 3;
		if (((u_pattern >> uint ((3 - dot.y) * 4 + dot.x)) & 1u) == 0u)
			discard;
	}
	/* The depth buffer holds the logarithm of the eye depth: with the
	 * near planes the games send, the projected depth cannot tell apart
	 * surfaces that the cabinets do (the shadow of the AIRTRIX combo
	 * labels lies 0.06 behind them at a depth of 560). */
	float frag_depth = log2 (1.0 + 1.0 / gl_FragCoord.w) * (1.0 / 24.0);
	gl_FragDepth = frag_depth;

#if FAMILIES != 0
	/* In a view with both families of the 101 (depth_bias.hpp), a mesh is
	 * drawn into the depth of its own, where the 101 moves it, and meets
	 * the other family at the eye depth that family left, its own unmoved;
	 * it leaves its own (0: none) and its scale. Equal ones keep the
	 * pixel, as the depth test does. */
	float eye_depth = 1.0 / (gl_FragCoord.w * u_depth_scale);
	float other = texelFetch (u_eye, pixel, 0).r;
	if (other != 0.0 && eye_depth > other * 1.000001)
		discard;
	fragEye = vec4 (eye_depth, u_depth_scale, 0.0, 1.0);
#endif

	if (HAS_TEXTURE != 0) {
#ifdef GL_ES
		vec4 texel = texture (u_texture, p_texcoords, u_lod_bias);
#else
		vec4 texel = texture (u_texture, p_texcoords);
#endif

		if (ALPHA_MODE != 0) {
			/* 881 bit a: the vertex alpha blends the texel over the 091 colour
			 * (vo3d.0x01100). */
			base = mix (p_diffuse, texel.rgb, p_alpha);
			alpha = texel.a;
		} else if (TEX_MODE == 1) {
			/* The texel alpha blends the texel over the 091 colour, the polygon
			 * alpha alone remains (vo3d.0x01010). */
			base = mix (p_diffuse, texel.rgb, texel.a);
			alpha = p_alpha;
		} else if (TEX_MODE == 2) {
			/* The texel modulates the 291 colour: the vo3d.0x01020 vertex
			 * shader sends ve_polygon_color1, not color0. */
			base = texel.rgb * p_color1;
			alpha = texel.a * p_alpha;
		} else if (TEX_MODE == 3) {
			/* The 091 colour with the texel alpha (vo3d.0x01030). */
			base = p_diffuse;
			alpha = texel.a * p_alpha;
		} else if (TEX_MODE == 4) {
			/* The texel blends the 291 colour over the 091 colour (vo3d.0x01040). */
			base = mix (p_diffuse, p_color1, texel.rgb);
			alpha = texel.a * p_alpha;
		} else {
			base = texel.rgb;
			alpha = texel.a * p_alpha;
		}
	} else if (ALPHA_MODE != 0) {
		/* 881 bit a: the vertex alpha blends the 291 colour over the 091
		 * colour (vo3d.0x00100). */
		base = mix (p_diffuse, p_color1, p_alpha);
		alpha = 1.0;
	} else {
		base = p_diffuse;
		alpha = p_alpha;
	}

	if (HAS_LIGHTING != 0) {
		/* vo3d: (ambient + diffuse + emission) * base + specular * material;
		 * mode 1 has the ambient, mode 2 the emission, mode 3 neither and no
		 * specular (X360 825B4DE8). */
		vec3 diffuse  = vec3 (0.0);
		vec3 specular = vec3 (0.0);
		vec3 ambient  = (SHADING_MODE == 1) ? u_ambient * p_ambient : vec3 (0.0);
		vec3 emission = (SHADING_MODE == 2) ? p_ambient : vec3 (0.0);

		if (HAS_LIGHT0 != 0)
			apply_light (diffuse, specular, u_lights[0], LIGHT0_TYPE, LIGHT0_ATT_TYPE);
		if (HAS_LIGHT1 != 0)
			apply_light (diffuse, specular, u_lights[1], LIGHT1_TYPE, LIGHT1_ATT_TYPE);
		if (HAS_LIGHT2 != 0)
			apply_light (diffuse, specular, u_lights[2], LIGHT2_TYPE, LIGHT2_ATT_TYPE);
		if (HAS_LIGHT3 != 0)
			apply_light (diffuse, specular, u_lights[3], LIGHT3_TYPE, LIGHT3_ATT_TYPE);

		color = vec4 ((ambient + diffuse + emission) * base, alpha);
		if (SHADING_MODE != 3) {
			color.rgb += specular * p_specular.rgb * (255.0 / 64.0);
		}
	} else {
		color = vec4 (base, alpha);
	}

	/* Alpha test of the render pass (X360 825B2408). */
	if (color.a <= u_alpha_ref)
		discard;

#if COLLECT != 0
	/* A translucent fragment is kept unless it is behind the other
	 * types, whose depth buffer has steps of 6e-8. In a view with both
	 * families of the 101 (depth_bias.hpp), the pixel is kept by the
	 * family whose eye depth there is the nearer: against a mesh of its
	 * own family, the depths moved (the unflagged ones as the buffer has
	 * them); against the other, the eye depths. */
	float solid = texelFetch (u_solid_depth, pixel, 0).r;
	bool behind = frag_depth > solid + 3.0e-8;

	if (u_family != 0.0) {
		vec2 unflagged = texelFetch (u_eye, pixel, 0).xy, flagged = texelFetch (u_flagged_eye, pixel, 0).xy;
		bool by_flagged = flagged.x != 0.0 && (unflagged.x == 0.0 || flagged.x <= unflagged.x);
		float eye_depth = 1.0 / (gl_FragCoord.w * u_depth_scale);

		if (by_flagged && u_family < 0.0)
			behind = eye_depth * u_depth_scale > flagged.x * flagged.y * 1.000001;
		else if (by_flagged || (unflagged.x != 0.0 && u_family < 0.0))
			behind = eye_depth > (by_flagged ? flagged.x : unflagged.x) * 1.000001;
	}
	if (frag_depth >= 1.0 || behind)
		discard;
#endif

	if (HAS_FOG != 0) {
		/* X360 vo3d: linear fog on the eye depth from start to end. */
		float fog = clamp ((-p_position.z - u_fog.x) * u_fog.y, 0.0, 1.0);
		fragColor = vec4 (mix (color.rgb, u_fog_color, fog), color.a);
	} else {
		fragColor = color;
	}

	if (HAS_CUE != 0) {
		/* Depth queue without a colour: the pixel fades into what is behind
		 * it, linearly on the eye depth from start to end. */
		float cue = clamp ((-p_position.z - u_fog.x) * u_fog.y, 0.0, 1.0);
		if (cue >= 1.0)
			discard;
		fragColor.a = 1.0 - cue;
	}

	/* A blended colour is weighted by its alpha before it is clamped: a
	 * lit colour above 1 keeps its excess (the galaxy of the PHARRIER
	 * logo scene, whose veils burn to white on the cabinet). The blend
	 * function takes the source as it is then. */
	if (u_weigh_by_alpha > 0.5)
		fragColor.rgb *= fragColor.a;

#if COLLECT != 0 && defined(PEELING)
	float d = log2 (1.0 + u_sort_scale / (gl_FragCoord.w * u_depth_scale)) * (1.0 / 24.0);
	float o = float (u_order + uint (gl_PrimitiveID));
	vec2 last = texelFetch (u_peel_last, pixel, 0).xy;
	if (!(last.x > d || (last.x == d && last.y < o)))
		discard;
	gl_FragDepth = d * 0.5;
	fragColor = clamp (fragColor, 0.0, 1.0);
	fragInfo = vec4 (float (u_blend_mode), o, d, 1.0);
#elif COLLECT != 0
	uint n = atomicAdd (u_count, 1u);
	if (n < uint (u_nodes.length ())) {
		u_nodes[n].color = clamp (fragColor, 0.0, 1.0);
		/* Sorted on the depth the 101 gives among translucent
		 * fragments, not the one it gives against the other types
		 * (depth_bias.hpp). */
		u_nodes[n].depth = log2 (1.0 + u_sort_scale / (gl_FragCoord.w * u_depth_scale)) *
		                   (1.0 / 24.0);
		u_nodes[n].next = imageAtomicExchange (u_heads, pixel, n + 1u);
		u_nodes[n].order = u_order + uint (gl_PrimitiveID);
		u_nodes[n].mode = u_blend_mode;
	}
#endif
}
