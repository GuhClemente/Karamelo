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

/* The translucent fragments of a viewport, see renderer_translucent.cpp:
 * u_heads has the last one of each pixel and next the one before it, as
 * indices plus one; 0 ends a list. With Vulkan, in the set LISTS_SET of
 * the shader that comes after (translucent_vk.cpp). */

struct node_t {
	vec4 color;
	float depth;
	uint next;
	uint order;
	uint mode;
};
#ifdef VULKAN
layout(std430, set = LISTS_SET, binding = 0) buffer nodes_t {
	node_t u_nodes[];
};
layout(set = LISTS_SET, binding = 1, r32ui) uniform uimage2D u_heads;
#elif !defined(PEELING)
layout(binding = 0, r32ui) uniform uimage2D u_heads;
layout(std430, binding = 0) buffer nodes_t {
	node_t u_nodes[];
};
#endif
