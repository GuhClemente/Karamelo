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

/* The picture at that light */

#ifdef VULKAN
/* The window of Vulkan has its top line first: a picture with the bottom
 * one first is read upside down (u_turn) */
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_colour;
layout(set = 0, binding = 0) uniform sampler2D u_picture;
layout(push_constant) uniform Show {
	float u_light;
	int u_turn;
};
#define UV vec2(v_uv.x, u_turn != 0 ? 1.0 - v_uv.y : v_uv.y)
#else
in vec2 v_uv;
out vec4 o_colour;
uniform sampler2D u_picture;
uniform float u_light;
#define UV v_uv
#endif
void main()
{
	o_colour = vec4(texture(u_picture, UV).rgb * u_light, 1.0);
}
