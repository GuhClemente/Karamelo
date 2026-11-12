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

/* For the whole picture triangle, after fragment_lists.glsl: the translucent
 * fragments of every pixel blended over the colour below, the farthest first
 * and, at the same depth, in the order they were sent. The arithmetic is that
 * of an 8-bit colour buffer, each result rounded to the nearest before the next
 * (HEURISTIC: the precision of the blender of the hardware is not known); the
 * colour is held as 0 to 255 so that every host computes the same. KEPT of
 * them are put in order at a time, the same order whatever KEPT is: 8 costs
 * the Intel HD 520 and the Radeon RX 6600 the least (half and two thirds of
 * 32). */

#define KEPT 8

#ifdef VULKAN
layout(set = LISTS_SET, binding = 2) uniform sampler2D u_below;
layout(location = 0) out vec4 fragColor;
#else
uniform sampler2D u_below;
out vec4 fragColor;
#endif

bool
comes_before (float depth_a, uint order_a, float depth_b, uint order_b)
{
	return depth_a > depth_b ||
	       (depth_a == depth_b && order_a < order_b);
}

void
main (void)
{
	ivec2 pixel = ivec2 (gl_FragCoord.xy);
	uint head = imageLoad (u_heads, pixel).r;
	float depth[KEPT], last_depth = 2.0;
	uint order[KEPT], index[KEPT], last_order = 0u, i;
	precise vec4 color;
	precise float through;
	bool more;
	int num, j;

	if (head == 0u)
		discard;
	color = floor (texelFetch (u_below, pixel, 0) * 255.0 + 0.5);

	for (;;) {
		num = 0;
		more = false;
		for (i = head; i != 0u; i = u_nodes[i - 1u].next) {
			float d = u_nodes[i - 1u].depth;
			uint o = u_nodes[i - 1u].order;

			if (!comes_before (last_depth, last_order, d, o))
				continue;
			if (num < KEPT) {
				j = num++;
			} else {
				more = true;
				if (!comes_before (d, o, depth[KEPT - 1], order[KEPT - 1]))
					continue;
				j = KEPT - 1;
			}
			for (; j > 0 && comes_before (d, o, depth[j - 1], order[j - 1]); j--) {
				depth[j] = depth[j - 1];
				order[j] = order[j - 1];
				index[j] = index[j - 1];
			}
			depth[j] = d;
			order[j] = o;
			index[j] = i - 1u;
		}

		for (j = 0; j < num; j++) {
			vec4 src = u_nodes[index[j]].color;
			uint mode = u_nodes[index[j]].mode;

			through = color.a * (1.0 - src.a);
			if (mode == 1u)
				color = vec4 (src.rgb * color.rgb, 0.0);
			else if (mode == 2u)
				color = vec4 (src.rgb * 255.0 + color.rgb, through);
			else
				color = vec4 (src.rgb * 255.0 + color.rgb * (1.0 - src.a), through);
			color = floor (clamp (color, 0.0, 255.0) + 0.5);
		}

		if (!more)
			break;
		last_depth = depth[KEPT - 1];
		last_order = order[KEPT - 1];
	}

	fragColor = color / 255.0;
}
