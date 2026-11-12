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

/* The mesh programs with OpenGL (programs_gl.hpp). */

#include "odin/host/renderer/hikaru/gl/programs_gl.hpp"
#include "odin/host/gfx/program_cache.hpp"
#include "odin/host/gfx/gl_common.hpp"
#include "odin/host/gfx/glsl_texts.hpp"
#include "odin/host/app/user_files.hpp"

#include <cinttypes>
#include <cstdio>
#include <string>

namespace odin {

MeshPrograms::~MeshPrograms()
{
	glUseProgram(0);
	for (const auto &[key, program] : m_programs)
		glDeleteProgram(program.id);
}

MeshProgram &
MeshPrograms::get(const MeshVariant &variant)
{
	const uint64_t key = variant.key();
	const auto held = m_programs.find(key);

	if (held != m_programs.end())
		return held->second;
	m_list.add(key);
	return m_programs.emplace(key, compile(variant)).first->second;
}

void
MeshPrograms::add(uint64_t key)
{
	const std::optional<MeshVariant> variant = MeshVariant::from_key(key);

	if (variant && !m_programs.contains(key))
		m_programs.emplace(key, compile(*variant));
}

void
MeshPrograms::keep_in(const std::string &folder)
{
	GLint formats = 0;

	m_kept.clear();
	glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
	if (folder.empty() || formats <= 0)
		return;
	m_kept = folder;
	m_driver.clear();
	for (const GLenum name : { GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION }) {
		const GLubyte *const text = glGetString(name);

		m_driver += text ? reinterpret_cast<const char *>(text) : "";
		m_driver += '\n';
	}
}

GLuint
MeshPrograms::link(const std::string &name, const std::string &vertex, const std::string &fragment,
                   const std::string &geometry)
{
	const char *const between = geometry.empty() ? nullptr : geometry.c_str();
	/* The kept file is checked against the vertex shader and the geometry
	 * one after it */
	const std::string first = vertex + geometry;

	if (m_kept.empty()) {
		m_compiled++;
		return gl::compile_program(vertex.c_str(), fragment.c_str(), false, between);
	}

	const std::string path = m_kept + "/" + name + ".bin";

	if (const std::optional<std::vector<uint8_t>> file = read_file(path)) {
		if (const std::optional<KeptProgram> kept = program_from_file(*file, m_driver, first, fragment)) {
			const GLuint id = glCreateProgram();
			GLint status = GL_FALSE;

			glProgramBinary(id, kept->format, kept->binary.data(), static_cast<GLsizei>(kept->binary.size()));
			glGetProgramiv(id, GL_LINK_STATUS, &status);
			if (status == GL_TRUE) {
				m_read_back++;
				return id;
			}
			/* A program the driver no longer takes: compiled again,
			 * without the error a format it dropped leaves */
			glDeleteProgram(id);
			glGetError();
		}
	}

	const GLuint id = gl::compile_program(vertex.c_str(), fragment.c_str(), true, between);
	GLint size = 0;

	m_compiled++;
	glGetProgramiv(id, GL_PROGRAM_BINARY_LENGTH, &size);
	if (size > 0) {
		KeptProgram kept{0, std::vector<uint8_t>(static_cast<size_t>(size))};
		GLsizei got = 0;
		GLenum format = 0;

		glGetProgramBinary(id, size, &got, &format, kept.binary.data());
		if (got > 0) {
			kept.format = format;
			kept.binary.resize(static_cast<size_t>(got));
			write_kept(path, program_file(m_driver, first, fragment, kept));
		}
	}
	return id;
}

MeshProgram
MeshPrograms::compile(const MeshVariant &variant)
{
	std::string defines;
	const auto define = [&](const char *name, unsigned value) {
		defines += "#define ";
		defines += name;
		defines += ' ';
		defines += std::to_string(value);
		defines += '\n';
	};

	define("HAS_TEXTURE", variant.has_texture);
	define("HAS_LIGHTING", variant.has_lighting);
	define("SHADING_MODE", variant.shading_mode);
	define("ALPHA_MODE", variant.alpha_mode);
	define("TEX_MODE", variant.tex_mode);
	for (unsigned i = 0; i < 4; i++) {
		const MeshVariant::Light &light = variant.lights[i];
		const std::string n = std::to_string(i);

		define(("HAS_LIGHT" + n).c_str(), light.on);
		define(("LIGHT" + n + "_TYPE").c_str(), static_cast<unsigned>(light.type));
		define(("LIGHT" + n + "_ATT_TYPE").c_str(), static_cast<unsigned>(light.attenuation));
	}
	define("HAS_FOG", variant.has_fog);
	define("HAS_CUE", variant.has_cue);
	define("COLLECT", variant.collect);
	define("HAS_PATTERN", variant.has_pattern);
	define("NATIVE_2D", variant.native);
	define("FAMILIES", variant.families);

	/* Kept under the key, in hexadecimal */
	char hex[17];

	std::snprintf(hex, sizeof(hex), "%016" PRIx64, variant.key());

	MeshProgram program;
	program.id = link(hex, defines + odin_glsl_mesh_vert, defines + odin_glsl_mesh_frag,
	                  variant.native ? odin_glsl_mesh_geom : "");

	const auto find = [&](const char *name) {
		return glGetUniformLocation(program.id, name);
	};
	MeshProgram::Locations &locs = program.locs;

	locs.projection = find("u_projection");
	locs.modelview = find("u_modelview");
	locs.modelview2 = find("u_modelview2");
	locs.depth_scale = find("u_depth_scale");
	locs.sort_scale = find("u_sort_scale");
	locs.snap = find("u_snap");
	locs.scale = find("u_scale");
	for (unsigned i = 0; i < 4; i++) {
		const std::string light = "u_lights[" + std::to_string(i) + "].";

		locs.lights[i].position = find((light + "position").c_str());
		locs.lights[i].direction = find((light + "direction").c_str());
		locs.lights[i].color = find((light + "color").c_str());
		locs.lights[i].ambient = find((light + "ambient").c_str());
		locs.lights[i].extents = find((light + "extents").c_str());
		locs.lights[i].spot = find((light + "spot").c_str());
		locs.lights[i].cone = find((light + "cone").c_str());
		locs.lights[i].table = find((light + "table").c_str());
	}
	locs.ambient = find("u_ambient");
	locs.fog = find("u_fog");
	locs.fog_color = find("u_fog_color");
	locs.alpha_ref = find("u_alpha_ref");
	locs.weigh_by_alpha = find("u_weigh_by_alpha");
	locs.order = find("u_order");
	locs.blend_mode = find("u_blend_mode");
	locs.pattern = find("u_pattern");
	locs.lod_bias = find("u_lod_bias");
	locs.family = find("u_family");

	/* The texture units never change. */
	glUseProgram(program.id);
	glUniform1i(find("u_texture"), UNIT_TEXTURE);
	glUniform1i(find("u_solid_depth"), UNIT_SOLID_DEPTH);
	glUniform1i(find("u_peel_last"), UNIT_PEEL_LAST);
	glUniform1i(find("u_eye"), UNIT_EYE);
	glUniform1i(find("u_flagged_eye"), UNIT_FLAGGED_EYE);
	gl::check("a mesh program");

	return program;
}

} /* namespace odin */
