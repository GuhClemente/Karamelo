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

/* Native 2D, as in mesh.geom: grow a screen-flat triangle to whole chip
 * pixels, extrapolating its values. pmx.frag keeps the fine pixels whose
 * chip pixel centre is inside. */

layout(triangles) in;
layout(triangle_strip, max_vertices = 4) out;

/* GLSL ES has no noperspective: pmx.vert gives the colours times w, and w;
 * the corners below share one w, so that the colours go plain to pmx.frag */
#ifdef GL_ES
#define SCREEN_LINEAR
layout(location = 0) in vec4 v_color[];
layout(location = 1) in vec4 v_offset[];
layout(location = 3) in float v_w[];
#define COLOR(i) (v_color[i] / v_w[i])
#define OFFSET(i) (v_offset[i] / v_w[i])
#else
#define SCREEN_LINEAR noperspective
layout(location = 0) noperspective in vec4 v_color[];
layout(location = 1) noperspective in vec4 v_offset[];
#define COLOR(i) v_color[i]
#define OFFSET(i) v_offset[i]
#endif
layout(location = 2) in vec2 v_uv[];

layout(location = 0) SCREEN_LINEAR out vec4 q_color;
layout(location = 1) SCREEN_LINEAR out vec4 q_offset;
layout(location = 2) out vec2 q_uv;
layout(location = 3) flat out vec2 q_corner0;
layout(location = 4) flat out vec2 q_corner1;
layout(location = 5) flat out vec2 q_corner2;

const vec2 SCREEN = vec2(640.0, 480.0);

void
corner(vec2 at, vec3 w, vec2 c[3])
{
	gl_Position = vec4((at / SCREEN * 2.0 - 1.0) * gl_in[0].gl_Position.w, gl_in[0].gl_Position.zw);
	q_color = w.x * COLOR(0) + w.y * COLOR(1) + w.z * COLOR(2);
	q_offset = w.x * OFFSET(0) + w.y * OFFSET(1) + w.z * OFFSET(2);
	q_uv = w.x * v_uv[0] + w.y * v_uv[1] + w.z * v_uv[2];
	q_corner0 = c[0];
	q_corner1 = c[1];
	q_corner2 = c[2];
	gl_PrimitiveID = gl_PrimitiveIDIn;
	EmitVertex();
}

void
main()
{
	vec2 s[3], c[3];

	/* Corners, and where the card places them: to 1/256 of a pixel */
	for (int i = 0; i < 3; i++) {
		s[i] = (gl_in[i].gl_Position.xy / gl_in[i].gl_Position.w * 0.5 + 0.5) * SCREEN;
		c[i] = floor(s[i] * 256.0 + 0.5) / 256.0;
	}
	float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[1].y - s[0].y) * (s[2].x - s[0].x);
	if (area == 0.0)
		return;

	/* Whole pixels around the corners, with the triangle's winding */
	vec2 lo = floor(min(c[0], min(c[1], c[2]))), hi = ceil(max(c[0], max(c[1], c[2])));
	vec2 box[4] = vec2[4](lo, vec2(hi.x, lo.y), vec2(lo.x, hi.y), hi);
	if (area < 0.0)
		box = vec2[4](lo, vec2(lo.x, hi.y), vec2(hi.x, lo.y), hi);
	for (int k = 0; k < 4; k++) {
		vec2 p = box[k];
		float w1 = ((p.x - s[0].x) * (s[2].y - s[0].y) - (p.y - s[0].y) * (s[2].x - s[0].x)) / area;
		float w2 = ((s[1].x - s[0].x) * (p.y - s[0].y) - (s[1].y - s[0].y) * (p.x - s[0].x)) / area;

		corner(p, vec3(1.0 - w1 - w2, w1, w2), c);
	}
	EndPrimitive();
}
