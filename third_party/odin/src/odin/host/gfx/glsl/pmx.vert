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

/* Neon 250 vertex: screen position and 1/W */

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_offset;

#ifdef VULKAN
layout(push_constant) uniform Draw {
	float most_w;
	float lod_bias;
	int flags;
	float scale;
} draw;
#define MOST_W draw.most_w
#else
uniform float u_most_w;
#define MOST_W u_most_w
#endif

#ifdef GL_ES
/* GLSL ES has no noperspective: the colours times w, and w, go with the
 * perspective; pmx.frag and pmx.geom divide the one by the other, which
 * gives the colours straight across the screen, as noperspective does */
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec4 v_offset;
layout(location = 3) out float v_w;
#else
layout(location = 0) noperspective out vec4 v_color;
layout(location = 1) noperspective out vec4 v_offset;
#endif
layout(location = 2) out vec2 v_uv;

void
main()
{
	/* The chip compares 1/W, larger is nearer: stored linearly across the
	 * polygon */
	const float SCREEN_W = 640.0, SCREEN_H = 480.0;
	float w = 1.0 / max(a_position.z, 1e-7);
	float depth = a_position.z / MOST_W;
	float x = a_position.x * 2.0 / SCREEN_W - 1.0;
	float y = 1.0 - a_position.y * 2.0 / SCREEN_H;

#if defined(VULKAN) || defined(DEPTH_FROM_0)
	gl_Position = vec4(x * w, y * w, depth * w, w);
#else
	gl_Position = vec4(x * w, y * w, (2.0 * depth - 1.0) * w, w);
#endif
	v_uv = a_uv;
	/* Bytes B, G, R, A */
	v_color = a_color.bgra;
	v_offset = a_offset.bgra;
#ifdef GL_ES
	v_color *= w;
	v_offset *= w;
	v_w = w;
#endif
}
