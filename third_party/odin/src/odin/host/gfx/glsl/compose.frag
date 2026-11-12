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

/* The picture shown and the 2D layers, composed */

/* Vulkan (-DVULKAN): where the shader finds what it reads */
#ifdef VULKAN
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D u_front;
layout(set = 0, binding = 1) uniform sampler2D u_back;
layout(set = 0, binding = 2) uniform sampler2D u_layer1;
layout(set = 0, binding = 3) uniform sampler2D u_layer2;
layout(push_constant) uniform Compose {
	float u_factor, u_mult_f, u_mult_b, u_mult_1, u_mult_2;
	float u_keyed_1, u_keyed_2, u_keyed_3d, u_above_1, u_above_2;
	int u_invert_f, u_invert_b;
	vec4 u_clear;
};

layout(location = 0) in vec2 p_texcoords;
#else
out vec4 fragColor;
uniform sampler2D u_front;
uniform sampler2D u_back;
uniform sampler2D u_layer1;
uniform sampler2D u_layer2;
uniform float u_factor;
uniform float u_mult_f;
uniform float u_mult_b;
uniform float u_mult_1;
uniform float u_mult_2;
uniform float u_keyed_1;
uniform float u_keyed_2;
uniform float u_keyed_3d;
uniform float u_above_1;
uniform float u_above_2;
uniform int u_invert_f;
uniform int u_invert_b;
uniform vec4 u_clear;

in vec2 p_texcoords;
#endif

void main (void) {
	vec2 p_texcoords_inv = vec2 (p_texcoords.s, 1.0 - p_texcoords.t);
	vec4 texel_f, texel_b;

	if (u_invert_f == 0)
		texel_f = texture (u_front, p_texcoords);
	else
		texel_f = texture (u_front, p_texcoords_inv);
	texel_f = texel_f * u_mult_f;

	if (u_invert_b == 0)
		texel_b = texture (u_back,  p_texcoords);
	else
		texel_b = texture (u_back,  p_texcoords_inv);
	texel_b = texel_b * u_mult_b;

	vec4 texel_1 = texture (u_layer1, p_texcoords_inv);
	vec4 texel_2 = texture (u_layer2, p_texcoords_inv);
	float on_1 = (u_mult_1 > 0.0) ? 1.0 : 0.0;
	float on_2 = (u_mult_2 > 0.0) ? 1.0 : 0.0;
	/* The key is the alpha bit: any non-zero alpha covers (the 2-bit
	 * alpha of the A2BGR10 layers of PHARRIER holds 1).
	 */
	float cover_1 = (u_keyed_1 > 0.0) ? step (0.1, texel_1.a) : 1.0;
	float cover_2 = (u_keyed_2 > 0.0) ? step (0.1, texel_2.a) : 1.0;
	/* The alpha of a 3D buffer is what it lets through; a layer taken
	 * as a blender input lets nothing through.
	 */
	if (u_invert_f != 0)
		texel_f.a = 0.0;
	if (u_invert_b != 0)
		texel_b.a = 0.0;
	vec4 color = mix (texel_f, texel_b, u_factor);
	/* The clear color shows where the 3D holds nothing, and covers
	 * what is below it when its alpha bit is set.
	 */
	color.rgb += color.a * u_clear.rgb;
	color.a *= 1.0 - u_clear.a;
	float cover_3d = (u_keyed_3d > 0.0) ? 1.0 - step (0.999, color.a) : 1.0;

	/* A layer pixel with its alpha bit covers the 3D, black included (the
	 * black bands of the Racer attract); the others are transparent
	 * whatever their color (its green 0x03E0 boxes). Layer 1 sits above
	 * layer 2 (its HUD text over the band), as the cabinet footage shows.
	 *
	 * A layer can sit below the 3D, which then shows it where nothing
	 * was drawn (the AIRTRIX logo screens).
	 */
	vec3 below = color.rgb;
	below = mix (below, texel_2.rgb * u_mult_2, cover_2 * on_2 * (1.0 - u_above_2));
	below = mix (below, texel_1.rgb * u_mult_1, cover_1 * on_1 * (1.0 - u_above_1));
	color.rgb = mix (below, color.rgb, cover_3d);
	color.rgb = mix (color.rgb, texel_2.rgb * u_mult_2, cover_2 * on_2 * u_above_2);
	color.rgb = mix (color.rgb, texel_1.rgb * u_mult_1, cover_1 * on_1 * u_above_1);
	fragColor = color;
}
