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

/* For the whole picture triangle: the blender of the two 3D buffers, as the
 * patent of the hardware draws it (US 6,549,209, fig. 11): the rate of the
 * buffer kept counts one more from 0x80 up, 0xFF keeps it whole, and each
 * product loses its fraction before the sum. */

/* Vulkan (-DVULKAN): where the shader finds what it reads */
#ifdef VULKAN
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D u_drawn;
layout(set = 0, binding = 1) uniform sampler2D u_kept;
layout(push_constant) uniform Rate {
	int u_rate;
};
#else
out vec4 fragColor;
uniform sampler2D u_drawn;
uniform sampler2D u_kept;
uniform int u_rate;
#endif

void main (void) {
	ivec2 at = ivec2 (gl_FragCoord.xy);
	ivec4 drawn = ivec4 (texelFetch (u_drawn, at, 0) * 255.0 + 0.5);
	ivec4 kept = ivec4 (texelFetch (u_kept, at, 0) * 255.0 + 0.5);
	int k = u_rate + (u_rate >> 7);

	fragColor = vec4 (((kept * k) >> 8) + ((drawn * (256 - k)) >> 8)) / 255.0;
}
