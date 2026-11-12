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

/* For the whole picture triangle, after the translucent fragments of a
 * Neon 250 scene went to the lists of their pixels (pmx.frag, LIST): each
 * pixel blends them over the colour below, the farthest first (the smallest
 * 1/W) and, at the same depth, in the order they were sent to the TA (PG,
 * the chip's programming guide, p. 82), with the chip's blend codes. The colour is held as 0 to 255, each
 * result rounded to the nearest before the next, as an 8-bit buffer does
 * (sort.frag). KEPT of them are put in order at a time, each time reading
 * the whole list; each one read goes down the KEPT slots, always the same
 * ones, so that the card keeps them in its registers. The smoke of Smashing
 * Drive leaves up to 15 on a pixel. */

#define KEPT 16

/* As pmx.frag: the colour as RGBA8, the rank << 6 | the blend codes */
struct node_t {
	uint color;
	float depth;
	uint next;
	uint order;
};
#ifdef VULKAN
layout(std430, set = 0, binding = 0) buffer nodes_t {
	node_t u_nodes[];
};
layout(set = 0, binding = 1, r32ui) uniform uimage2D u_heads;
layout(set = 0, binding = 2) uniform sampler2D u_below;
#else
layout(binding = 0, r32ui) uniform uimage2D u_heads;
layout(std430, binding = 0) buffer nodes_t {
	node_t u_nodes[];
};
uniform sampler2D u_below;
#endif

layout(location = 0) out vec4 o_color;

bool
comes_before(float depth_a, uint order_a, float depth_b, uint order_b)
{
	return depth_a < depth_b || (depth_a == depth_b && order_a < order_b);
}

/* Zero, one, the other colour and its inverse, source alpha and its
 * inverse, destination alpha (all ones) and its inverse; the source's
 * "other colour" is the destination's, the destination's the source's */
vec3
factor(uint code, vec3 other, float alpha)
{
	if (code == 0u)
		return vec3(0.0);
	if (code == 1u)
		return vec3(1.0);
	if (code == 2u)
		return other;
	if (code == 3u)
		return 1.0 - other;
	if (code == 4u)
		return vec3(alpha);
	if (code == 5u)
		return vec3(1.0 - alpha);
	if (code == 6u)
		return vec3(1.0);
	return vec3(0.0);
}

void
main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	uint head = imageLoad(u_heads, pixel).r;
	float depth[KEPT], last_depth = -1.0;
	uint order[KEPT], index[KEPT], last_order = 0u, num, i;
	precise vec3 color;
	int s;

	if (head == 0u)
		discard;
	color = floor(texelFetch(u_below, pixel, 0).rgb * 255.0 + 0.5);

	for (;;) {
		num = 0u;
		for (i = head; i != 0u; i = u_nodes[i - 1u].next) {
			float d = u_nodes[i - 1u].depth;
			uint o = u_nodes[i - 1u].order, x = i - 1u;

			if (!comes_before(last_depth, last_order, d, o))
				continue;
			/* Down the slots taken; the first one free takes what is left */
			for (s = 0; s < KEPT; s++) {
				if (uint(s) == num) {
					depth[s] = d;
					order[s] = o;
					index[s] = x;
					break;
				}
				if (comes_before(d, o, depth[s], order[s])) {
					float slot_depth = depth[s];
					uint slot_order = order[s], slot_index = index[s];

					depth[s] = d;
					order[s] = o;
					index[s] = x;
					d = slot_depth;
					o = slot_order;
					x = slot_index;
				}
			}
			num++;
		}

		for (s = 0; s < KEPT; s++) {
			if (uint(s) == num)
				break;

			vec4 src = unpackUnorm4x8(u_nodes[index[s]].color);
			uint mode = u_nodes[index[s]].order & 63u;
			vec3 below = color / 255.0;
			precise vec3 sum = src.rgb * factor(mode & 7u, below, src.a) +
			                   below * factor(mode >> 3, src.rgb, src.a);

			color = floor(clamp(sum, 0.0, 1.0) * 255.0 + 0.5);
		}

		if (num <= uint(KEPT))
			break;
		last_depth = depth[KEPT - 1];
		last_order = order[KEPT - 1];
	}

	o_color = vec4(color / 255.0, 1.0);
}
