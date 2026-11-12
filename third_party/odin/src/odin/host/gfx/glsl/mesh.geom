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

/* Native 2D (Renderer::draw_mesh): a mesh flat to the screen, in a finer
 * picture, covers the pixels it covers on the screen of the hardware. Each
 * triangle grows to the whole pixels of that screen around it, its values
 * carried on as they run over it (flat: straight on the screen), its depth
 * that of its first corner; mesh.frag keeps the fine pixels whose pixel of
 * the hardware has its centre in the triangle, given its corners. The way
 * of GPU Gems 2, chapter 42 (conservative rasterization). */

/* OpenGL 4.1 matches the stages by name */
#ifdef PEELING
#define p_position q_position
#define p_normal q_normal
#define p_diffuse q_diffuse
#define p_specular q_specular
#define p_color1 q_color1
#define p_ambient q_ambient
#define p_texcoords q_texcoords
#define p_alpha q_alpha
#endif

layout(triangles) in;
layout(triangle_strip, max_vertices = 4) out;

layout(location = 0) in vec4 g_position[];
layout(location = 1) in vec3 g_normal[];
layout(location = 2) in vec3 g_diffuse[];
layout(location = 3) in vec4 g_specular[];
layout(location = 4) in vec3 g_color1[];
layout(location = 5) in vec3 g_ambient[];
layout(location = 6) in vec2 g_texcoords[];
layout(location = 7) in float g_alpha[];

layout(location = 0) out vec4 p_position;
layout(location = 1) out vec3 p_normal;
layout(location = 2) out vec3 p_diffuse;
layout(location = 3) out vec4 p_specular;
layout(location = 4) out vec3 p_color1;
layout(location = 5) out vec3 p_ambient;
layout(location = 6) out vec2 p_texcoords;
layout(location = 7) out float p_alpha;
layout(location = 8) flat out vec2 p_corner0;
layout(location = 9) flat out vec2 p_corner1;
layout(location = 10) flat out vec2 p_corner2;

/* The screen of the hardware (hikaru_draw.hpp) */
const vec2 SCREEN = vec2 (640.0, 480.0);

/* A corner of the box at `at`, in pixels of the hardware, its values those
 * of the triangle there (weights w of its corners) */
void
corner (vec2 at, vec3 w, vec2 c[3])
{
	gl_Position = vec4 ((at / SCREEN * 2.0 - 1.0) * gl_in[0].gl_Position.w, gl_in[0].gl_Position.zw);
	p_position = w.x * g_position[0] + w.y * g_position[1] + w.z * g_position[2];
	p_normal = w.x * g_normal[0] + w.y * g_normal[1] + w.z * g_normal[2];
	p_diffuse = w.x * g_diffuse[0] + w.y * g_diffuse[1] + w.z * g_diffuse[2];
	p_specular = w.x * g_specular[0] + w.y * g_specular[1] + w.z * g_specular[2];
	p_color1 = w.x * g_color1[0] + w.y * g_color1[1] + w.z * g_color1[2];
	p_ambient = w.x * g_ambient[0] + w.y * g_ambient[1] + w.z * g_ambient[2];
	p_texcoords = w.x * g_texcoords[0] + w.y * g_texcoords[1] + w.z * g_texcoords[2];
	p_alpha = w.x * g_alpha[0] + w.y * g_alpha[1] + w.z * g_alpha[2];
	p_corner0 = c[0];
	p_corner1 = c[1];
	p_corner2 = c[2];
	gl_PrimitiveID = gl_PrimitiveIDIn;
	EmitVertex ();
}

void
main (void)
{
	vec2 s[3], c[3];

	/* Where the corners are, and where the card puts them: to 1/256 of
	 * a pixel */
	for (int i = 0; i < 3; i++) {
		s[i] = (gl_in[i].gl_Position.xy / gl_in[i].gl_Position.w * 0.5 + 0.5) * SCREEN;
		c[i] = floor (s[i] * 256.0 + 0.5) / 256.0;
	}
	float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[1].y - s[0].y) * (s[2].x - s[0].x);
	if (area == 0.0)
		return;

	/* The whole pixels around the corners, wound as the triangle */
	vec2 lo = floor (min (c[0], min (c[1], c[2]))), hi = ceil (max (c[0], max (c[1], c[2])));
	vec2 box[4] = vec2[4] (lo, vec2 (hi.x, lo.y), vec2 (lo.x, hi.y), hi);
	if (area < 0.0)
		box = vec2[4] (lo, vec2 (lo.x, hi.y), vec2 (hi.x, lo.y), hi);
	for (int k = 0; k < 4; k++) {
		vec2 p = box[k];
		float w1 = ((p.x - s[0].x) * (s[2].y - s[0].y) - (p.y - s[0].y) * (s[2].x - s[0].x)) / area;
		float w2 = ((s[1].x - s[0].x) * (p.y - s[0].y) - (s[1].y - s[0].y) * (p.x - s[0].x)) / area;

		corner (p, vec3 (1.0 - w1 - w2, w1, w2), c);
	}
	EndPrimitive ();
}
