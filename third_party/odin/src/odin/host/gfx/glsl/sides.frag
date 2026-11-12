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

/* The sides lit by the picture: each pixel takes the edge of the picture
 * nearest to it, from the mean of the picture in u_near, smoothly (a
 * B-spline through its texels), giving way to the smaller u_far and going
 * from the light u_light.x at the picture to u_light.y at the edge of the
 * window. Drawn over a smaller image of the window: u_rect, the picture in
 * it, from the bottom; u_band, how wide the sides are each way in it. */

#ifdef VULKAN
layout(location = 0) out vec4 o_colour;
layout(set = 0, binding = 0) uniform sampler2D u_near;
layout(set = 0, binding = 1) uniform sampler2D u_far;
layout(push_constant) uniform Sides {
	vec4 u_rect;
	vec2 u_band;
	vec2 u_light;
	float u_window_h;
};
/* The window has its top line first under Vulkan: y counted from the
 * bottom, as OpenGL counts it */
#define FRAG_COORD vec2(gl_FragCoord.x, u_window_h - gl_FragCoord.y)
#else
out vec4 o_colour;
uniform sampler2D u_near;
uniform sampler2D u_far;
uniform vec4 u_rect;
uniform vec2 u_band;
uniform vec2 u_light;
#define FRAG_COORD gl_FragCoord.xy
#endif

/* Four filtered taps, each the weights of two texels of the B-spline each
 * way */
vec3 smooth_texture(sampler2D picture, vec2 uv)
{
	vec2 size = vec2(textureSize(picture, 0));
	vec2 p = uv * size - 0.5;
	vec2 i = floor(p), f = p - i;
	vec2 w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0;
	vec2 w1 = (4.0 - 6.0 * f * f + 3.0 * f * f * f) / 6.0;
	vec2 w2 = (1.0 + 3.0 * f + 3.0 * f * f - 3.0 * f * f * f) / 6.0;
	vec2 w3 = f * f * f / 6.0;
	vec2 g0 = w0 + w1, g1 = w2 + w3;
	vec2 h0 = (i - 0.5 + w1 / g0) / size, h1 = (i + 1.5 + w3 / g1) / size;

	return g0.y * (g0.x * texture(picture, h0).rgb + g1.x * texture(picture, vec2(h1.x, h0.y)).rgb) +
	       g1.y * (g0.x * texture(picture, vec2(h0.x, h1.y)).rgb + g1.x * texture(picture, h1).rgb);
}

/* The point of the picture nearest to uv, at the middle of an edge texel at
 * most */
vec2 edge(sampler2D picture, vec2 uv)
{
	vec2 half_texel = 0.5 / vec2(textureSize(picture, 0));

	return clamp(uv, half_texel, 1.0 - half_texel);
}

void main()
{
	vec2 uv = (FRAG_COORD - u_rect.xy) / u_rect.zw;
	vec2 out_of = max(max(-uv, uv - 1.0), 0.0) * u_rect.zw / max(u_band, 1.0);
	float t = clamp(max(out_of.x, out_of.y), 0.0, 1.0);
	vec3 near = smooth_texture(u_near, edge(u_near, uv));
	vec3 far = smooth_texture(u_far, edge(u_far, uv));

	o_colour = vec4(mix(near, far, t) * mix(u_light.x, u_light.y, t), 1.0);
}
