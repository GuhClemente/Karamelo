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

/* For the whole picture triangle: the picture of the frame at the size of
 * the window. Each pixel of the window takes the mean of the part of the
 * picture it covers, each pixel of the picture weighted by the share of it
 * that is covered. A window that is a whole number of times the picture
 * shows it pixel for pixel; a smaller one smooths it, which the hardware does
 * not. u_rect: where the rectangle starts in the window, and the pixels of
 * the picture one pixel of the window covers each way. */

/* Vulkan (glslang defines VULKAN): where the shader finds
 * what it reads */
#ifdef VULKAN
layout(set = 0, binding = 0) uniform sampler2D u_picture;
layout(push_constant) uniform Present {
	vec4 u_rect;
	float u_window_h;
};
layout(location = 0) out vec4 fragColor;
/* The window has its top line first under Vulkan: y counted from the
 * bottom, as OpenGL counts it; a picture (u_window_h 0) has its bottom
 * one first already */
#define FRAG_COORD vec2 (gl_FragCoord.x, u_window_h > 0.0 ? u_window_h - gl_FragCoord.y : gl_FragCoord.y)
#else
uniform sampler2D u_picture;
uniform vec4 u_rect;

out vec4 fragColor;
#define FRAG_COORD gl_FragCoord.xy
#endif

void main (void) {
	ivec2 last = textureSize (u_picture, 0) - 1;
	vec2 from = (FRAG_COORD - 0.5 - u_rect.xy) * u_rect.zw;
	vec2 to = from + u_rect.zw;
	ivec2 a = ivec2 (floor (from)), b = ivec2 (ceil (to)) - 1;
	vec3 sum = vec3 (0.0);

	for (int y = a.y; y <= b.y; y++) {
		float wy = min (to.y, float (y + 1)) - max (from.y, float (y));
		for (int x = a.x; x <= b.x; x++) {
			float wx = min (to.x, float (x + 1)) - max (from.x, float (x));
			sum += texelFetch (u_picture, clamp (ivec2 (x, y), ivec2 (0), last), 0).rgb * (wx * wy);
		}
	}
	fragColor = vec4 (sum / (u_rect.z * u_rect.w), 1.0);
}
