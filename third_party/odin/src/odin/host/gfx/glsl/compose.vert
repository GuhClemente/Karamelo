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

/* The picture shown and the 2D layers, composed (compose.frag) */

/* Vulkan (-DVULKAN): where the shader finds what it reads */
#ifdef VULKAN
/* The four corners of the window, as OpenGL has them in its vertex array
 * (pictures_gl.cpp, build_quad), drawn as a strip */
const vec3 CORNERS[4] = vec3[] (vec3 (1.0, 0.0, 0.0), vec3 (0.0, 0.0, 0.0), vec3 (1.0, 1.0, 0.0), vec3 (0.0, 1.0, 0.0));
const vec2 TEXCOORDS[4] = vec2[] (vec2 (1.0, 1.0), vec2 (0.0, 1.0), vec2 (1.0, 0.0), vec2 (0.0, 0.0));
#define i_position CORNERS[gl_VertexIndex]
#define i_texcoords TEXCOORDS[gl_VertexIndex]
const mat4 u_projection =
#elif defined(GL_ES)
const mat4 u_projection =
#else
uniform mat4 u_projection =
#endif
	mat4 (vec4 ( 2.0,  0.0,  0.0,  0.0),
	      vec4 ( 0.0, -2.0,  0.0,  0.0),
	      vec4 ( 0.0,  0.0, -1.0,  0.0),
	      vec4 (-1.0,  1.0,  0.0,  1.0));

#ifdef VULKAN
layout(location = 0) out vec2 p_texcoords;
#else
layout(location = 0) in vec3 i_position;
layout(location = 1) in vec2 i_texcoords;

out vec2 p_texcoords;
#endif

void main (void) {
	gl_Position = u_projection * vec4 (i_position, 1.0);
	p_texcoords = i_texcoords;
}
