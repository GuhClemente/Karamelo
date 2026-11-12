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

#ifndef ODIN_PICTURE_FILTER_GL_HPP
#define ODIN_PICTURE_FILTER_GL_HPP

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "odin/host/gfx/gl_common.hpp"

#include "odin/host/video/picture_preset.hpp"

namespace odin {

/* A filter of the picture as the GLSL presets of RetroArch make one (a
 * .glslp file): passes, each a GLSL file with a VERTEX and a FRAGMENT
 * part, each drawing into a texture the next ones read, the last into the
 * window. They run by the rules of the OpenGL renderer of 86Box, which the
 * filters of Lola Retro are written for (filters/lola-retro, from its
 * folder 86box; its tools/86box-preview.py tells these rules):
 *  - every texture is rounded up to a power of two, its picture in its
 *    lower left corner, the right way up; TexCoord spans the picture;
 *  - a pass is as large as its input (scale_type source), the window
 *    (viewport) or a number of pixels (absolute), times its scale;
 *    float_framebuffer gives it colours of 32-bit floats;
 *  - the texture a pass draws is read with the filter_linear of the next
 *    pass and its own wrap_mode;
 *  - the uniforms are Texture, InputSize, TextureSize, OutputSize (the
 *    window, for every pass), FrameCount, FrameDirection, MVPMatrix and
 *    each parameter by its name; from the second pass also OrigTexture
 *    with its sizes, and PassNTexture (what pass N - 1 drew) with the
 *    sizes of its input;
 *  - the last pass has no scale and draws straight into the window. */
class PictureFilter {
public:
	/* The filter of a preset, its passes built; nothing, said on stderr,
	 * when the preset cannot be read or a pass does not build. On the
	 * thread that draws, its context current. */
	static std::unique_ptr<PictureFilter> load(const std::string &preset);
	~PictureFilter();
	PictureFilter(const PictureFilter &) = delete;
	PictureFilter &operator=(const PictureFilter &) = delete;

	/* The framebuffer to draw the picture of the board into, w by h
	 * pixels from its lower left corner */
	GLuint frame(unsigned w, unsigned h);
	/* That picture through the passes, into the rectangle of the window
	 * from x, y (from the bottom), w by h; `count` the frames drawn so
	 * far (FrameCount) */
	void draw(int x, int y, unsigned w, unsigned h, unsigned count);

private:
	using Scale = preset::Plan::Scale;

	/* A texture a pass reads: what it holds, its size, the size of the
	 * picture in it, how it is read */
	struct Picture {
		GLuint texture = 0, fb = 0;
		std::array<float, 2> size{}, texture_size{};
		bool linear = true, is_float = false;
		GLint wrap = GL_CLAMP_TO_BORDER;
	};

	struct Pass {
		GLuint program = 0, vao = 0, vbo = 0;
		std::array<Scale, 2> scale_type{ Scale::SOURCE, Scale::SOURCE };
		std::array<float, 2> scale{ 1.0f, 1.0f };
		bool window = false;	/* the last: into the window */
		Picture out;
		/* The uniforms and the parameters it uses */
		GLint texture, input_size, texture_size, output_size, frame_count, frame_direction, mvp;
		GLint orig_texture, orig_input_size, orig_texture_size;
		std::vector<std::array<GLint, 3>> passes;	/* PassNTexture, ...TextureSize, ...InputSize */
		std::vector<std::pair<GLint, float>> parameters;
	};

	PictureFilter() = default;
	bool build(const preset::Plan &plan);
	void find_uniforms(Pass &pass, size_t i, const std::vector<std::pair<std::string, float>> &parameters);
	void make_corners(Pass &pass);
	void size(unsigned w, unsigned h);
	GLuint sampler(bool linear, GLint wrap);

	std::vector<Pass> m_passes;
	Picture m_frame;
	std::array<unsigned, 2> m_frame_size{}, m_window{};
	std::map<std::pair<bool, GLint>, GLuint> m_samplers;
};

} /* namespace odin */

#endif /* ODIN_PICTURE_FILTER_GL_HPP */
