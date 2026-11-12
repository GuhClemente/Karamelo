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

/* One peeled layer blended as sort.frag does */

uniform sampler2D u_below;
uniform sampler2D u_color;
uniform sampler2D u_last;
uniform sampler2D u_layer;
uniform sampler2D u_info;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragLast;

void
main (void)
{
	ivec2 pixel = ivec2 (gl_FragCoord.xy);
	vec4 last = texelFetch (u_last, pixel, 0);
	vec4 info = texelFetch (u_info, pixel, 0);
	precise vec4 color;
	precise float through;

	if (last.x == 2.0)
		color = floor (texelFetch (u_below, pixel, 0) * 255.0 + 0.5);
	else
		color = texelFetch (u_color, pixel, 0);

	if (info.a > 0.5) {
		vec4 src = texelFetch (u_layer, pixel, 0);
		uint mode = uint (info.x);

		through = color.a * (1.0 - src.a);
		if (mode == 1u)
			color = vec4 (src.rgb * color.rgb, 0.0);
		else if (mode == 2u)
			color = vec4 (src.rgb * 255.0 + color.rgb, through);
		else
			color = vec4 (src.rgb * 255.0 + color.rgb * (1.0 - src.a), through);
		color = floor (clamp (color, 0.0, 255.0) + 0.5);
		last = vec4 (info.z, info.y, 0.0, 0.0);
	}

	fragColor = color;
	fragLast = last;
}
