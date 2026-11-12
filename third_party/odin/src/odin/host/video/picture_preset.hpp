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

#ifndef ODIN_PICTURE_PRESET_HPP
#define ODIN_PICTURE_PRESET_HPP

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/* The texts of a filter of the picture (picture_filter_gl.hpp), read as 86Box
 * reads them: the .glslp preset, and the GLSL file of each pass. */
namespace odin::preset {

/* 86Box takes a parameter whose name and description are 1 to 63
 * characters long */
constexpr size_t LONGEST_NAME = 63;

/* The keys of a preset as the reader of 86Box takes them (utils/ini.c): a
 * line that starts with # or ; is a comment and a [section] ends them; a
 * name ends at = or a space; a value is cut at its first #, loses its
 * quotes and its spaces; the first of two equal names counts. */
std::map<std::string, std::string> read_keys(const std::string &text);

/* A key that is true, false or a number, as 86Box reads it; false when
 * there is none */
bool key_on(const std::map<std::string, std::string> &keys, const std::string &name);

/* A part of a pass (VERTEX or FRAGMENT) as 86Box gives it to the driver:
 * its #version line first, then the extension, the part,
 * PARAMETER_UNIFORM and the rest of the file, its #pragma parameter
 * lines blanked */
std::string part(const std::string &source, const char *which);

/* The parameters a pass declares (#pragma parameter NAME "description"
 * default minimum maximum step), added to `parameters` unless there
 * already */
void declared(const std::string &source, std::vector<std::pair<std::string, float>> &parameters);

/* A preset read: each pass, its file (from the folder of the preset) and
 * its text, how what it draws is read (filter_linear of the next pass, its
 * own wrap_mode) and kept (float_framebuffer), its scale each way; the
 * parameters the passes declare, at the values the preset gives them */
struct Plan {
	enum class Scale { SOURCE, VIEWPORT, ABSOLUTE };
	enum class Wrap { BORDER, EDGE, REPEAT, MIRRORED };
	struct Pass {
		std::string file, source;
		bool linear = false, is_float = false;
		Wrap wrap = Wrap::BORDER;
		std::array<Scale, 2> scale_type{ Scale::SOURCE, Scale::SOURCE };
		std::array<float, 2> scale{ 1.0f, 1.0f };
	};
	std::vector<Pass> passes;
	std::vector<std::pair<std::string, float>> parameters;
};

/* The plan of a preset file; nothing, said on stderr, when it or a pass
 * cannot be read, it has no passes, or its last pass has a scale (it draws
 * into the window) */
std::optional<Plan> read_plan(const std::string &preset);

/* The power of two a size takes, as 86Box rounds the whole number of
 * pixels up */
inline float
power_of_two(float size)
{
	const unsigned whole = static_cast<unsigned>(size);
	unsigned power = 1;

	while (power < whole)
		power <<= 1;
	return static_cast<float>(power);
}

/* The texture of a picture of that size */
inline std::array<float, 2>
texture_of(const std::array<float, 2> &size)
{
	return { power_of_two(size[0]), power_of_two(size[1]) };
}

/* The size a pass of that scale draws in a window of that size, from the
 * size of its input */
inline std::array<float, 2>
pass_size(const std::array<Plan::Scale, 2> &type, const std::array<float, 2> &scale, const std::array<float, 2> &window,
          const std::array<float, 2> &in)
{
	std::array<float, 2> out;

	for (size_t axis = 0; axis < 2; axis++)
		out[axis] = type[axis] == Plan::Scale::VIEWPORT ? window[axis] * scale[axis] :
		            type[axis] == Plan::Scale::ABSOLUTE ? scale[axis] : in[axis] * scale[axis];
	return out;
}

/* The four corners of a pass, VertexCoord then TexCoord: TexCoord spans the
 * picture of its input in its texture */
inline std::array<float, 16>
corners(const std::array<float, 2> &size, const std::array<float, 2> &texture)
{
	const float u = size[0] / texture[0], v = size[1] / texture[1];

	return { -1, -1, 0, 0, -1, 1, 0, v, 1, -1, u, 0, 1, 1, u, v };
}

} /* namespace odin::preset */

#endif /* ODIN_PICTURE_PRESET_HPP */
