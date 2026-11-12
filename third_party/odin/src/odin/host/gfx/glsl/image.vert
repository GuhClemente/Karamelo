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

/* An image over the viewport it is given (the bezel, the sides blurred): a
 * triangle that covers it, the top of the image at the top */

#ifdef VULKAN
layout(location = 0) out vec2 place;
#define VERTEX_ID gl_VertexIndex
#else
out vec2 place;
#define VERTEX_ID gl_VertexID
#endif

void main (void) {
	vec2 corner = vec2 (VERTEX_ID & 1, VERTEX_ID >> 1) * 2.0;

#ifdef VULKAN
	/* The window of Vulkan has its top line first: the same corner is the
	 * top of the viewport there, the bottom with OpenGL */
	place = corner;
#else
	place = vec2 (corner.x, 1.0 - corner.y);
#endif
	gl_Position = vec4 (corner * 2.0 - 1.0, 0.0, 1.0);
}
