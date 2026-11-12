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

/* The meshes of a display list with OpenGL (drawing.hpp): the rules of
 * the board (hikaru_draw.hpp) put into the uniforms of the mesh programs. */

#include "odin/host/renderer/hikaru/gl/drawing_gl.hpp"
#include "odin/host/renderer/hikaru/depth_bias.hpp"
#include "odin/host/gfx/gl_common.hpp"
#include "odin/common/repeats.hpp"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace odin {

namespace {

constexpr uint32_t NONE = ~0u;

} /* namespace */

void
GlDrawing::log(const char *fmt, ...) const
{
	va_list args;

	va_start(args, fmt);
	log_line(m_debug.log, fmt, args);
	va_end(args);
}

/* Nothing a program holds, nothing bound is of the list about to be drawn:
 * the pictures are drawn with other programs and textures in between. */
void
GlDrawing::begin_scene(const odin_display_list_t &list, const SceneDebug &debug)
{
	m_list = &list;
	m_debug = debug;
	m_program = nullptr;
	m_drawing++;
	m_scissor_vp_index = ~0u;
	m_unit0_texture = m_unit0_sampler = ~0u;
	m_blend[0] = ~0u;

	upload_vertices(list.verts.all, list.verts.num, list.indices.all, list.indices.num);

	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
	glViewport(0, 0, static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));

	/* The scissor test and the write masks hold for a clear too. An
	 * empty 3D buffer lets everything through: the clear color comes in
	 * on the screen, see compose. */
	glDepthMask(GL_TRUE);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_SCISSOR_TEST);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
}

void
GlDrawing::clear_depth()
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
	glDepthMask(GL_TRUE);
	glDisable(GL_SCISSOR_TEST);
	glClear(GL_DEPTH_BUFFER_BIT);
	glEnable(GL_SCISSOR_TEST);
	m_families_view = false;
	m_family = NO_FAMILY;
}

/* The meshes test the other family themselves (glsl/mesh.frag, FAMILIES). */
void
GlDrawing::begin_families()
{
	static const GLfloat none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	static const GLfloat farthest = 1.0f;

	glDepthMask(GL_TRUE);
	glDisable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, m_family_fb[1]);
	glClearBufferfv(GL_COLOR, 1, none);
	glClearBufferfv(GL_DEPTH, 0, &farthest);
	glBindFramebuffer(GL_FRAMEBUFFER, m_family_fb[0]);
	glClearBufferfv(GL_COLOR, 1, none);
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
	glEnable(GL_SCISSOR_TEST);
	m_families_view = true;
	m_family = NO_FAMILY;
}

void
GlDrawing::begin_pass(const PassRules &rules)
{
	/* The eye depths of the families are not blended (begin_families);
	 * where a buffer cannot be left out, the alpha of 1 of those
	 * fragments keeps them with the blends of the depth-writing types. */
	glEnable(GL_BLEND);
	if (gl::blend_per_buffer())
		glDisablei(GL_BLEND, 1);
	m_blend[0] = ~0u;
	set_blend(MeshBlend::REPLACE);
	glDepthMask(rules.depth_write ? GL_TRUE : GL_FALSE);
	m_pass.depth_bias = rules.depth_bias;
	m_pass.alpha_ref = rules.alpha_ref;
	glBindVertexArray(m_mesh_vao);
}

void
GlDrawing::end_pass()
{
	gl::check("a polygon type");
}

void
GlDrawing::end_scene()
{
	/* What the meshes left bound is the 2D drawing's then. */
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
	glBindVertexArray(0);
	glBindSampler(0, 0);
	m_unit0_sampler = 0;
	glDepthMask(GL_TRUE);
	glDisable(GL_SCISSOR_TEST);
	glFrontFace(GL_CCW);
	glViewport(0, 0, static_cast<GLsizei>(width()), static_cast<GLsizei>(height()));
	gl::check("a scene");
}

/* Each program draws a triangle without area once: the driver finishes its
 * work on it then. The fragment lists are bound for those that collect:
 * Mesa V3D reads their buffers even for no fragment. */
void
GlDrawing::begin_preparing()
{
	static const odin_vertex_body_t none[3] = {};

	upload_vertices(none, 3, nullptr, 0);
	if (!m_peel.on) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_lists.nodes);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_lists.count[0]);
		glBindImageTexture(0, m_lists.heads, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
	}
}

void
GlDrawing::prepare_program(uint64_t key)
{
	/* Not a key of no variant, nor those the card cannot run (draw_mesh,
	 * draws_native) */
	if (const std::optional<MeshVariant> v = MeshVariant::from_key(key);
	    !v || (v->collect && !gl::primitive_ids()) || (v->native && !gl::geometry_shaders()))
		return;
	/* Back from the loading screen, drawn in the window */
	glBindVertexArray(m_mesh_vao);
	glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fb);
	m_programs.add(key);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

void
GlDrawing::end_preparing()
{
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindVertexArray(0);
	gl::check("preparing the programs");
}

/* The program of the mesh, put in use if it is another one. */
void
GlDrawing::use_program(const MeshVariant &variant)
{
	const uint64_t key = variant.key();

	if (m_program && m_variant_key == key)
		return;

	m_variant = variant;
	m_variant_key = key;
	m_program = &m_programs.get(variant);
	glUseProgram(m_program->id);
}

void
GlDrawing::upload_viewport(const odin_mesh_t &mesh)
{
	const odin_viewport_t &vp = m_list->vp_list[mesh.vp_index];
	const MeshProgram::Locations &locs = m_program->locs;
	float projection[4][4];

	/* The clip window and the winding of the view (view_turned_over,
	 * hikaru_draw.hpp), whatever the program; OpenGL counts the lines from
	 * the bottom. */
	if (m_scissor_vp_index != mesh.vp_index) {
		const ClipWindow clip = clip_window(vp, m_scale);

		m_scissor_vp_index = mesh.vp_index;
		glScissor(clip.x, clip.y, clip.w, clip.h);
		glFrontFace(view_turned_over(vp) ? GL_CW : GL_CCW);
	}

	if (m_program->held.vp_index == mesh.vp_index)
		return;
	m_program->held.vp_index = mesh.vp_index;

	/* The window may leave the screen: VON4 sends some with a left edge
	 * at -22 or a top one at -4; the scissor cuts them to the screen. */
	check_view(vp);

	if (m_debug.log)
		log("vp  = %s", odin_viewport_str(&vp));

	view_projection(vp, projection);
	glUniformMatrix4fv(locs.projection, 1, GL_FALSE, &projection[0][0]);

	/* The depth queue and the fog of the view (hikaru_draw.hpp) */

	if (m_variant.has_cue) {
		float cue[2];

		view_cue(vp, cue);
		glUniform2fv(locs.fog, 1, cue);
	}

	if (m_variant.has_fog) {
		float fog[2], fog_color[3];

		view_fog(vp, fog, fog_color);
		glUniform2fv(locs.fog, 1, fog);
		glUniform3fv(locs.fog_color, 1, fog_color);
	}
}

/* The modelview of the instance i of the mesh, and the second modelview for
 * the vertices that follow it. The normals go through them too, see the
 * vertex shader. */
void
GlDrawing::upload_modelview(const odin_mesh_t &mesh, unsigned i)
{
	const MeshProgram::Locations &locs = m_program->locs;
	MeshProgram::Held &held = m_program->held;
	const bool none = mesh.mv_index == NONE;

	if (!none && held.mv_index == mesh.mv_index + i && held.mv2_index == mesh.mv2_index)
		return;
	held.mv_index = none ? NONE : mesh.mv_index + i;
	held.mv2_index = mesh.mv2_index;

	const odin_display_list_t &list = *m_list;
	const uint32_t at = modelview_of(list, mesh, i), second = modelview2_of(list, mesh, at);
	const odin_modelview_t &mv = at == list.num_mvs ? IDENTITY : list.mv_list[at];
	const odin_modelview_t &mv2 = second == list.num_mvs ? IDENTITY : list.mv_list[second];

	if (m_debug.log)
		log("mv  = [%u+%u] %s", mesh.mv_index, i, odin_modelview_str(&mv));
	glUniformMatrix4fv(locs.modelview, 1, GL_FALSE, &mv.mtx[0][0]);
	glUniformMatrix4fv(locs.modelview2, 1, GL_FALSE, &mv2.mtx[0][0]);
}

/* The texture of the mesh on the unit 0. The texture of a texhead and its
 * sampling stay bound; a texture made is left bound (make_texture). */
void
GlDrawing::bind_texture(const MeshDraw &draw)
{
	if (!m_variant.has_texture)
		return;
	if (m_unit0_texture != draw.texture.id) {
		m_unit0_texture = draw.texture.id;
		glActiveTexture(GL_TEXTURE0 + 0);
		glBindTexture(GL_TEXTURE_2D, draw.texture.id);
	}
	/* The sampling belongs to the texhead: two texheads may read the
	 * same texture with another wrap or filter. */
	if (draw.texture.id && m_unit0_sampler != draw.sampler.id) {
		m_unit0_sampler = draw.sampler.id;
		glBindSampler(0, draw.sampler.id);
	}
#ifdef ODIN_GLES
	if (const auto bias = m_sampler_bias.find(draw.sampler.id); bias != m_sampler_bias.end())
		glUniform1f(m_program->locs.lod_bias, bias->second);
#endif
}

void
GlDrawing::upload_lightset(const odin_mesh_t &mesh)
{
	const odin_lightset_t &ls = m_list->ls_list[mesh.ls_index];
	const MeshProgram::Locations &locs = m_program->locs;
	MeshProgram::Held &held = m_program->held;
	float color[3] = { 0.0f, 0.0f, 0.0f };

	if (!m_variant.has_lighting)
		return;

	/* The ambient colour comes from the viewport. */
	if (held.ls_index == mesh.ls_index && held.ls_vp_index == mesh.vp_index)
		return;
	held.ls_index = mesh.ls_index;
	held.ls_vp_index = mesh.vp_index;

	if (m_debug.log)
		log("lightset [%u] mask=%X = %s", mesh.ls_index, odin_lightset_mask(&ls),
		    odin_lightset_str(&ls));

	if (!m_debug.no_ambient && mesh.vp_index != NONE)
		view_ambient(m_list->vp_list[mesh.vp_index], color);
	glUniform3fv(locs.ambient, 1, color);

	for (unsigned i = 0; i < 4; i++) {
		const odin_light_t &light = ls.lights[i];
		const MeshProgram::Light &at = locs.lights[i];
		const float cone[2] = { light.cos_outer, light.cone_scale };
		float table[16];

		if (odin_lightset_mask(&ls) & (1u << i))
			continue;

		glUniform3fv(at.position, 1, light.position);
		glUniform3fv(at.direction, 1, light.direction);

		light_colour(light, color);
		if (m_debug.no_diffuse)
			color[0] = color[1] = color[2] = 0.0f;
		glUniform3fv(at.color, 1, color);
		glUniform1f(at.ambient, light_ambient(light));
		glUniform2fv(at.extents, 1, light.attenuation);
		glUniform3fv(at.spot, 1, light.spot);
		glUniform2fv(at.cone, 1, cone);
		light_table(m_list->ls_tables[mesh.ls_index][i], table);
		glUniform1fv(at.table, 16, table);
	}
}

void
GlDrawing::draw_instance(const odin_mesh_t &mesh, unsigned i)
{
	upload_modelview(mesh, i);
	if (m_variant.collect) {
		glUniform1ui(m_program->locs.order, m_pass.order);
		m_pass.order += mesh.num_tris;
	}
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.num_tris * 3), GL_UNSIGNED_INT,
	               reinterpret_cast<const void *>(size_t{mesh.first_index} * sizeof(uint32_t)));
}

void
GlDrawing::draw_mesh(const odin_mesh_t &mesh, const MeshDraw &draw)
{
	/* A translucent mesh needs gl_PrimitiveID, see begin_translucent */
	if (draw.variant.collect && !gl::primitive_ids())
		return;
	if (m_peel.on && draw.variant.collect && !m_peel.drawing) {
		m_peel.draws.push_back({ &mesh, draw });
		return;
	}
	/* Into the depth of its family, the other family's eye depth read; a
	 * mesh of no family into the scene (its colour alone) */
	if (const unsigned family = family_of(mesh, draw.variant); family != m_family && !draw.variant.collect) {
		m_family = family;
		glBindFramebuffer(GL_FRAMEBUFFER, family == NO_FAMILY ? m_scene_fb : m_family_fb[family]);
		if (family != NO_FAMILY) {
			glActiveTexture(GL_TEXTURE0 + MeshPrograms::UNIT_EYE);
			glBindTexture(GL_TEXTURE_2D, m_eye[1 - family]);
			glActiveTexture(GL_TEXTURE0);
		}
	}
	set_blend(draw.blend);
	use_program(draw.variant);
	const MeshProgram::Locations &locs = m_program->locs;
	MeshProgram::Held &held = m_program->held;

	if (held.drawing != m_drawing) {
		/* Nothing of this list yet. */
		held.drawing = m_drawing;
		held.vp_index = held.ls_index = held.ls_vp_index = NONE;
		held.mv_index = held.mv2_index = NONE;
		held.alpha_ref = held.depth_scale = held.sort_scale = held.scale = NAN;
		held.weigh_by_alpha = held.snap = -1;
		held.pattern = NONE;
	}
	if (m_variant.has_pattern && held.pattern != mesh.pattern) {
		held.pattern = mesh.pattern;
		glUniform1ui(locs.pattern, pattern_bits(mesh.pattern));
	}
	if (held.alpha_ref != m_pass.alpha_ref) {
		held.alpha_ref = m_pass.alpha_ref;
		glUniform1f(locs.alpha_ref, m_pass.alpha_ref);
	}
	if (held.weigh_by_alpha != static_cast<int>(draw.weigh_by_alpha)) {
		held.weigh_by_alpha = draw.weigh_by_alpha;
		glUniform1f(locs.weigh_by_alpha, draw.weigh_by_alpha ? 1.0f : 0.0f);
	}
	if (held.snap != static_cast<int>(draw.snap)) {
		held.snap = draw.snap;
		glUniform1ui(locs.snap, draw.snap ? 1u : 0u);
	}
	if (m_variant.native && held.scale != static_cast<float>(m_scale)) {
		held.scale = static_cast<float>(m_scale);
		glUniform1f(locs.scale, held.scale);
	}

	upload_viewport(mesh);
	bind_texture(draw);
	upload_lightset(mesh);

	/* The 101 moves the mesh by a fraction of its own depth, one way
	 * against the other types and another among the translucent
	 * fragments (depth_bias.hpp). It holds for the opaque types too: the
	 * lettering and the painted lines of the SGNASCAR track, the shadow
	 * of the AIRTRIX skater. */
	const int bias = m_pass.depth_bias ? static_cast<int>(mesh.depth_bias) : 0;
	const float depth_scale = depth_bias::test_scale(bias);
	if (held.depth_scale != depth_scale) {
		held.depth_scale = depth_scale;
		glUniform1f(locs.depth_scale, depth_scale);
	}
	const float sort_scale = depth_bias::sort_scale(bias);
	if (m_variant.collect && held.sort_scale != sort_scale) {
		held.sort_scale = sort_scale;
		glUniform1f(locs.sort_scale, sort_scale);
	}
	/* A translucent mesh meets what kept each pixel by the families of
	 * both, if the view has flagged meshes (0: it has none) */
	if (m_variant.collect) {
		glUniform1ui(locs.blend_mode, draw.blend_mode);
		glUniform1f(locs.family, depth_bias::family_sign(m_families_view, bias));
	}

	for (unsigned i = 0; i < mesh.num_instances; i++)
		draw_instance(mesh, i);
}

/* Uploads the vertices of a list and the indices of its triangles, once,
 * before its meshes are drawn. */
void
GlDrawing::upload_vertices(const odin_vertex_body_t *vertices, uint32_t num,
                           const uint32_t *indices, uint32_t num_indices)
{
	const auto attribute = [](GLuint index, GLint size, GLenum type, GLboolean normalized,
	                          size_t offset) {
		glVertexAttribPointer(index, size, type, normalized, sizeof(odin_vertex_body_t),
		                      reinterpret_cast<const void *>(offset));
		glEnableVertexAttribArray(index);
	};

	if (!m_mesh_vao) {
		glGenVertexArrays(1, &m_mesh_vao);
		glGenBuffers(1, &m_mesh_vbo);
		glGenBuffers(1, &m_mesh_ibo);
	}

	glBindVertexArray(m_mesh_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_mesh_vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             static_cast<GLsizeiptr>(sizeof(odin_vertex_body_t) * num),
	             vertices, GL_DYNAMIC_DRAW);
	/* The vertex array keeps the buffer of the indices. */
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_mesh_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
	             static_cast<GLsizeiptr>(sizeof(uint32_t) * num_indices),
	             indices, GL_DYNAMIC_DRAW);

	/* The locations of the mesh vertex shader */
	attribute(0, 3, GL_FLOAT, GL_FALSE, offsetof(odin_vertex_body_t, position));
	attribute(1, 3, GL_FLOAT, GL_FALSE, offsetof(odin_vertex_body_t, normal));
	attribute(2, 3, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(odin_vertex_body_t, diffuse));
	attribute(3, 3, GL_FLOAT, GL_FALSE, offsetof(odin_vertex_body_t, ambient));
	attribute(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(odin_vertex_body_t, specular));
	attribute(5, 3, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(odin_vertex_body_t, color1));
	attribute(6, 2, GL_FLOAT, GL_FALSE, offsetof(odin_vertex_body_t, texcoords));
	attribute(7, 1, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(odin_vertex_body_t, alpha));
	attribute(8, 1, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(odin_vertex_body_t, second_mv));

	glBindVertexArray(0);
	gl::check("the vertices");
}

/* The blend factors of OpenGL, sent when they change */
void
GlDrawing::set_blend(MeshBlend blend)
{
	static const GLenum factors[6][4] = {
		/* REPLACE */     { GL_ONE,       GL_ZERO,                GL_ZERO, GL_ZERO },
		/* FADED */       { GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
		/* ALPHA_OVER */  { GL_ONE,       GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
		/* PRODUCT */     { GL_DST_COLOR, GL_ZERO,                GL_ZERO, GL_ZERO },
		/* ALPHA_ADDED */ { GL_ONE,       GL_ONE,                 GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
		/* OVER */        { GL_ONE,       GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA },
	};
	const GLenum *const f = factors[static_cast<unsigned>(blend)];

	if (m_blend[0] == f[0] && m_blend[1] == f[1] && m_blend[2] == f[2] && m_blend[3] == f[3])
		return;
	m_blend[0] = f[0];
	m_blend[1] = f[1];
	m_blend[2] = f[2];
	m_blend[3] = f[3];
	glBlendFuncSeparate(f[0], f[1], f[2], f[3]);
}

} /* namespace odin */
