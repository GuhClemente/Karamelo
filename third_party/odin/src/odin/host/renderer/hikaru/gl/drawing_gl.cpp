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

/* What the graphics card of the host does for the renderer, with OpenGL
 * (drawing.hpp): made, its textures, the stamps of its clock. */

#include "odin/host/renderer/hikaru/gl/drawing_gl.hpp"

#include <vector>

#include "odin/host/gfx/gl_common.hpp"

namespace odin {

namespace {

/* The texels of the board as OpenGL keeps them and is given them, by
 * Texels */
struct GlTexels {
	GLenum internal, layout, type;
};
#ifdef ODIN_GLES
/* OpenGL ES has no 1555 or 4444 swizzles: reorder the bits */
constexpr GlTexels GL_TEXELS[] = {
	{ GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1 },
	{ GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4 },
	{ GL_RG8, GL_RG, GL_UNSIGNED_BYTE },
};
constexpr GLint SWIZZLES[][4] = {
	{ GL_BLUE, GL_GREEN, GL_RED, GL_ALPHA },
	{ GL_ALPHA, GL_BLUE, GL_GREEN, GL_RED },
	{ GL_RED, GL_RED, GL_RED, GL_GREEN },
};

/* A level of ABGR1555 as GL_UNSIGNED_SHORT_5_5_5_1 takes it, whole */
std::vector<uint16_t>
turned_1555(const TexelRect &l)
{
	std::vector<uint16_t> out(size_t{l.w} * l.h);
	const uint8_t *const in = static_cast<const uint8_t *>(l.texels);

	for (uint32_t y = 0; y < l.h; y++)
		for (uint32_t x = 0; x < l.w; x++) {
			const size_t at = (size_t{l.y + y} * l.row + l.x + x) * 2;
			const uint16_t v = static_cast<uint16_t>(in[at] | in[at + 1] << 8);

			out[size_t{y} * l.w + x] = static_cast<uint16_t>(v << 1 | v >> 15);
		}
	return out;
}
#else
constexpr GlTexels GL_TEXELS[] = {
	{ GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV },
	{ GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4_REV },
	{ GL_RGBA8, GL_RG, GL_UNSIGNED_BYTE },
};
/* LA8: the light in the three colours */
constexpr GLint LA8_SWIZZLE[4] = { GL_RED, GL_RED, GL_RED, GL_GREEN };
#endif

} /* namespace */

std::unique_ptr<Drawing>
Drawing::make_gl()
{
	return std::make_unique<GlDrawing>();
}

/* The texture is left bound on the unit 0: bind_texture binds the one of
 * the next mesh again. */
HostTexture
GlDrawing::make_texture(Texels texels, std::span<const TexelRect> levels)
{
	const GlTexels &gl = GL_TEXELS[static_cast<size_t>(texels)];
	const GLsizei num_levels = static_cast<GLsizei>(levels.size());
	GLuint id;

	glGenTextures(1, &id);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, id);
	m_unit0_texture = ~0u;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, num_levels - 1);
#ifdef ODIN_GLES
	for (unsigned i = 0; i < 4; i++)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R + i, SWIZZLES[static_cast<size_t>(texels)][i]);
#else
	if (texels == Texels::LA8)
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, LA8_SWIZZLE);
#endif

	/* The levels are allocated at once: given one by one, a 64x64 texture
	 * takes the NVIDIA driver 595 some 1.7 ms instead of 0.03. */
	gl::storage(num_levels, gl.internal, static_cast<GLsizei>(levels[0].w), static_cast<GLsizei>(levels[0].h));

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (GLsizei level = 0; level < num_levels; level++) {
		const TexelRect &l = levels[static_cast<size_t>(level)];

#ifdef ODIN_GLES
		if (texels == Texels::ABGR1555) {
			const std::vector<uint16_t> turned = turned_1555(l);

			glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, static_cast<GLsizei>(l.w), static_cast<GLsizei>(l.h),
			                gl.layout, gl.type, turned.data());
			continue;
		}
#endif
		glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(l.row));
		glPixelStorei(GL_UNPACK_SKIP_ROWS, static_cast<GLint>(l.y));
		glPixelStorei(GL_UNPACK_SKIP_PIXELS, static_cast<GLint>(l.x));
		glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, static_cast<GLsizei>(l.w), static_cast<GLsizei>(l.h),
		                gl.layout, gl.type, l.texels);
	}

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	gl::check("making a texture");
	return { id };
}

void
GlDrawing::forget_textures(std::span<const HostTexture> textures)
{
	std::vector<GLuint> ids;

	for (const HostTexture texture : textures)
		ids.push_back(texture.id);
	glDeleteTextures(static_cast<GLsizei>(ids.size()), ids.data());
	gl::check("deleting textures");
}

HostSampler
GlDrawing::make_sampler(const TextureSampling &sampling, unsigned anisotropy)
{
	const auto wrap = [](TextureSampling::Wrap w) -> GLint {
		return w == TextureSampling::Wrap::MIRROR ? GL_MIRRORED_REPEAT :
		       w == TextureSampling::Wrap::REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	};
	GLuint id;

	glGenSamplers(1, &id);
	glSamplerParameteri(id, GL_TEXTURE_WRAP_S, wrap(sampling.u));
	glSamplerParameteri(id, GL_TEXTURE_WRAP_T, wrap(sampling.v));
	switch (sampling.filter) {
	case TextureSampling::Filter::NEAREST:
		glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		break;
	case TextureSampling::Filter::LINEAR:
		glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		break;
	case TextureSampling::Filter::TRILINEAR:
		glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		break;
	}
#ifdef ODIN_GLES
	m_sampler_bias[id] = sampling.lod_bias;
#else
	glSamplerParameterf(id, GL_TEXTURE_LOD_BIAS, sampling.lod_bias);
#endif
	if (anisotropy > 1 && gl::anisotropic_filter())
		glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, static_cast<float>(anisotropy));
	return { id };
}

#ifdef ODIN_GLES
/* OpenGL ES has timestamps only with GL_EXT_disjoint_timer_query */
bool
GlDrawing::has_stamps() const
{
	return false;
}

uint32_t
GlDrawing::put_stamp()
{
	return 0;
}

bool
GlDrawing::reached(uint32_t)
{
	return true;
}

uint64_t
GlDrawing::stamp_time(uint32_t)
{
	return 0;
}

void
GlDrawing::forget_stamp(uint32_t, bool)
{
}
#else
bool
GlDrawing::has_stamps() const
{
	GLint bits = 0;

	glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
	return bits > 0;
}

uint32_t
GlDrawing::put_stamp()
{
	GLuint id;

	if (m_stamps.empty()) {
		glGenQueries(1, &id);
	} else {
		id = m_stamps.back();
		m_stamps.pop_back();
	}
	glQueryCounter(id, GL_TIMESTAMP);
	return id;
}

bool
GlDrawing::reached(uint32_t stamp)
{
	GLuint ready = 0;

	glGetQueryObjectuiv(stamp, GL_QUERY_RESULT_AVAILABLE, &ready);
	return ready != 0;
}

uint64_t
GlDrawing::stamp_time(uint32_t stamp)
{
	GLuint64 at = 0;

	glGetQueryObjectui64v(stamp, GL_QUERY_RESULT, &at);
	return at;
}

/* A stamp the card still owes is not put again. */
void
GlDrawing::forget_stamp(uint32_t stamp, bool again)
{
	if (again)
		m_stamps.push_back(stamp);
	else
		glDeleteQueries(1, &stamp);
}
#endif

} /* namespace odin */
