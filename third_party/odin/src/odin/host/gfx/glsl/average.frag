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

/* Each texel the mean of the block of the picture it covers, u_block texels
 * each way (even): a filtered tap at the corner of four texels reads their
 * mean, so a quarter of them */

#ifdef VULKAN
layout(location = 0) out vec4 o_colour;
layout(set = 0, binding = 0) uniform sampler2D u_picture;
layout(push_constant) uniform Block {
	ivec2 u_block;
};
#else
out vec4 o_colour;
uniform sampler2D u_picture;
uniform ivec2 u_block;
#endif

void main()
{
	ivec2 first = ivec2(gl_FragCoord.xy) * u_block;
	vec2 size = vec2(textureSize(u_picture, 0));
	vec3 sum = vec3(0.0);

	for (int y = 0; y < u_block.y; y += 2)
		for (int x = 0; x < u_block.x; x += 2)
			sum += texture(u_picture, (vec2(first + ivec2(x, y)) + 1.0) / size).rgb;
	o_colour = vec4(sum / float(u_block.x * u_block.y / 4), 1.0);
}
