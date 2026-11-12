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

/* A display list drawn: its viewports, its polygon types, its meshes, by
 * the rules of the board (hikaru_draw.hpp); the card draws them
 * (drawing.hpp). */

#include "odin/host/renderer/hikaru/renderer.hpp"
#include "odin/host/renderer/hikaru/depth_bias.hpp"
#include "odin/common/repeats.hpp"

#include <cmath>

namespace odin {

namespace {

constexpr uint32_t NONE = ~0u;

LightAttenuation
attenuation_of(const odin_light_t &light)
{
	if (odin_light_att_type(&light) == 0 &&
	    light.attenuation[0] == 1.0f &&
	    light.attenuation[1] == 1.0f)
		return LightAttenuation::NONE;
	return static_cast<LightAttenuation>(odin_light_att_type(&light));
}

LightType
type_of(const odin_light_t &light)
{
	const bool has_position = (light.flags & ODIN_LIGHT_HAS_POSITION) != 0;

	if (attenuation_of(light) == LightAttenuation::NONE) {
		/* A parallel light sent with a null direction lights every
		 * side (HEURISTIC, read from the BRAVEFF cabinet: the light
		 * its stages add to every lightset). */
		if ((light.flags & ODIN_LIGHT_HAS_DIRECTION) && light.direction[0] == 0.0f &&
		    light.direction[1] == 0.0f && light.direction[2] == 0.0f)
			return LightType::UNDIRECTED;
		return LightType::DIRECTIONAL;
	}
	if (has_position && (light.flags & ODIN_LIGHT_HAS_SPOT) && (light.flags & ODIN_LIGHT_HAS_CONE))
		return LightType::SPOT;
	if (has_position)
		return LightType::POSITIONAL;
	return LightType::DIRECTIONAL;
}

/* Twice the area of a triangle on the screen, signed by its winding */
float
twice_area(const float (&at)[3][2])
{
	return (at[1][0] - at[0][0]) * (at[2][1] - at[0][1]) - (at[1][1] - at[0][1]) * (at[2][0] - at[0][0]);
}

/* A mesh flat to the screen: each of its instances at one depth, through
 * its first modelview. The HUD of VON4 is such meshes, with gaps of a
 * fraction of a pixel between them that no pixel centre of the hardware's
 * screen meets (0.23 between the digits of its timer and its panel).
 * `unturned`: none of its triangles turned over with its corners on that
 * screen's pixels, where Snap 2D puts them (a fold would open a hole). */
bool
flat_on_screen(const odin_display_list_t &list, const odin_mesh_t &mesh, bool unturned)
{
	if (mesh.mv_index == NONE || mesh.vp_index >= list.num_vps || mesh.mv_index + mesh.num_instances > list.num_mvs)
		return false;

	float projection[4][4];

	view_projection(list.vp_list[mesh.vp_index], projection);
	for (uint32_t i = 0; i < mesh.num_instances; i++) {
		const float (&m)[4][4] = list.mv_list[mesh.mv_index + i].mtx;
		float depth = 0.0f;

		for (uint32_t t = 0; t < mesh.num_tris; t++) {
			float at[3][2], grid[3][2];

			for (uint32_t k = 0; k < 3; k++) {
				const odin_vertex_body_t &v = list.verts.all[list.indices.all[mesh.first_index + t * 3 + k]];
				float eye[4] = { 0.0f, 0.0f, 0.0f, 1.0f }, clip[4];

				if (v.second_mv != 0)
					return false;
				for (unsigned r = 0; r < 3; r++)
					eye[r] = m[0][r] * v.position[0] + m[1][r] * v.position[1] + m[2][r] * v.position[2] + m[3][r];
				if (t == 0 && k == 0)
					depth = eye[2];
				else if (std::fabs(eye[2] - depth) > 1e-5f * std::fabs(depth))
					return false;
				if (!unturned)
					continue;
				for (unsigned r = 0; r < 4; r++)
					clip[r] = projection[0][r] * eye[0] + projection[1][r] * eye[1] +
					          projection[2][r] * eye[2] + projection[3][r] * eye[3];
				if (!(clip[3] > 0.0f))
					return false;

				/* On the screen, and on the grid as mesh.vert puts it */
				at[k][0] = (clip[0] / clip[3] * 0.5f + 0.5f) * static_cast<float>(SCREEN_WIDTH);
				at[k][1] = (clip[1] / clip[3] * 0.5f + 0.5f) * static_cast<float>(SCREEN_HEIGHT);
				for (unsigned c = 0; c < 2; c++)
					grid[k][c] = std::ceil(std::floor(at[k][c] * 256.0f + 0.5f) / 256.0f - 0.5f);
			}
			if (!unturned)
				continue;

			const float before = twice_area(at), after = twice_area(grid);

			if ((before > 0.0f && after < 0.0f) || (before < 0.0f && after > 0.0f))
				return false;
		}
	}
	return true;
}

} /* namespace */

/* The texture each texhead of the list was drawn with: a list drawn again
 * takes them back, see check_last_list. */
std::vector<HostTexture> &
Renderer::tex_ids()
{
	for (ListTextures &textures : m_list_textures) {
		if (!textures.list)
			textures.list = m_list;
		if (textures.list == m_list) {
			if (textures.ids.size() < m_list->num_texs)
				textures.ids.resize(m_list->num_texs);
			return textures.ids;
		}
	}
	fatal("a fourth display list");
}

/* A depth queue without a colour (621 bit 17) fades the polygons that do not
 * blend into what is behind them (HEURISTIC, read from the PHARRIER cabinet:
 * its demo play loses the far scenery and keeps the mountains, sent as
 * translucent polygons). */
bool
Renderer::is_faded_by_depth(const odin_mesh_t &mesh) const
{
	if (mesh.vp_index >= m_list->num_vps)
		return false;
	const odin_viewport_t &vp = m_list->vp_list[mesh.vp_index];

	return vp.depth.q_behind && vp.depth.q_type == 0 &&
	       vp.depth.inv_range > 0.0f && vp.depth.near_by_edge > 0.0f &&
	       mesh.polytype != ODIN_POLYTYPE_BACKGROUND &&
	       mesh.polytype != ODIN_POLYTYPE_TRANSLUCENT &&
	       !m_debug[DEBUG_NO_FOG];
}

MeshVariant
Renderer::variant_of(const odin_mesh_t &mesh) const
{
	MeshVariant variant;

	variant.collect = m_collect;
	variant.families = m_families;
	/* 0 and 16 are every pixel */
	variant.has_pattern = (mesh.pattern & 15) != 0;

	if (mesh.mat_index == NONE) {
		repeats::error("no material.");
		return variant;
	}
	if (mesh.tex_index == NONE) {
		repeats::error("no texhead.");
		return variant;
	}
	if (mesh.ls_index == NONE) {
		repeats::error("no lightset.");
		return variant;
	}
	const odin_material_t &mat = m_list->mat_list[mesh.mat_index];
	const odin_lightset_t &ls = m_list->ls_list[mesh.ls_index];
	const odin_viewport_t &vp = m_list->vp_list[mesh.vp_index];

	variant.has_texture = odin_mat_has_texture(&mat) && !m_debug[DEBUG_NO_TEXTURES];
	variant.has_lighting = odin_lightset_mask(&ls) != 0xF &&
	                       odin_mat_shading_mode(&mat) != 0 &&
	                       !m_debug[DEBUG_NO_LIGHTING];

	/* The X360 port picks the fogged vo3d variant on the 881 z bit alone
	 * (shader option 11, digit 1). */
	variant.has_fog = odin_mat_depth_blend(&mat) && !vp.depth.q_behind &&
	                  !m_debug[DEBUG_NO_FOG];
	variant.has_cue = is_faded_by_depth(mesh);

	/* The alpha mode and the texel mode apply to unlit materials too
	 * (vo3d.x1xxxxx variants). */
	variant.alpha_mode = odin_mat_has_alpha(&mat) != 0;

	/* The 2C1 bits 31:29 pick how the texel combines with the material
	 * colours, numbered as the port's shader variants (8252E368 byte 6,
	 * 825B4340): values 1, 2, 4 and 5 select modes 1 to 4; 3, 6 and 7
	 * keep the previous mode there, plain texturing here (HEURISTIC). */
	if (variant.has_texture) {
		static const uint8_t modes[8] = { 0, 1, 2, 0, 3, 4, 0, 0 };

		variant.tex_mode = modes[odin_tex_texmode(&m_list->tex_list[mesh.tex_index])];
	}

	if (!variant.has_lighting)
		return variant;

	variant.shading_mode = odin_mat_shading_mode(&mat);
	for (unsigned i = 0; i < 4; i++) {
		variant.lights[i].on = !(odin_lightset_mask(&ls) & (1u << i));
		variant.lights[i].type = type_of(ls.lights[i]);
		variant.lights[i].attenuation = attenuation_of(ls.lights[i]);
	}
	return variant;
}

/* The texture of the mesh and its sampling. The texture of a texhead and
 * its sampling stay from one mesh to the next. */
void
Renderer::choose_texture(const odin_mesh_t &mesh)
{
	if (m_bound_tex_index == mesh.tex_index)
		return;
	m_bound_tex_index = mesh.tex_index;

	const odin_texhead_t &th = m_list->tex_list[mesh.tex_index];
	HostTexture texture;

	/* A list drawn again takes the textures it had: the texture RAM may
	 * have changed since. */
	if (m_again) {
		texture = tex_ids()[mesh.tex_index];
	} else {
		/* A texture that is decoded is a part of its own in the
		 * times of the drawing. */
		const bool decoded = m_times.on() && !m_textures.holds(th);
		const DrawTimes::Part part = decoded ? m_times.part(DrawTimes::PART_TEXTURES) : DrawTimes::NUM_PARTS;

		texture = m_textures.get(th);
		tex_ids()[mesh.tex_index] = texture;
		if (decoded)
			m_times.part(part);
	}
	m_draw.texture = texture;
	/* The sampling belongs to the texhead: two texheads may read the
	 * same texture with another wrap or filter. */
	m_draw.sampler = texture.id ? m_textures.sampler(th) : HostSampler{};
}

/* The mesh with its program, its blend (set_material_blend or
 * set_opaque_blend) and its texture. In a finer picture, a mesh flat to the
 * screen is drawn as the hardware's screen has it, if the user asks
 * (DisplayUpscaleMode): Snap 2D puts its corners on the pixels of that
 * screen, Native 2D has it cover the pixels it covers there
 * (glsl/mesh.geom); the gaps of a fraction of a pixel between such meshes
 * stay shut. A piece made of a flat mesh and one a little tilted comes
 * apart: the lock-on of PHARRIER. */
void
Renderer::draw_mesh(const odin_mesh_t &mesh)
{
	if (logging())
		log_mesh(mesh);

	m_draw.variant = variant_of(mesh);
	if (m_draw.variant.has_texture)
		choose_texture(mesh);
	m_draw.snap = false;
	if (m_scale > 1 && m_upscale_mode != DisplayUpscaleMode::RAW) {
		const bool native = m_upscale_mode == DisplayUpscaleMode::NATIVE && m_card->draws_native();

		if (flat_on_screen(*m_list, mesh, !native)) {
			m_draw.variant.native = native;
			m_draw.snap = !native;
		}
	}
	m_card->draw_mesh(mesh, m_draw);
	m_times.mesh(mesh.num_tris * mesh.num_instances);
}

/* The blend of the material of the mesh (MeshBlend) */
void
Renderer::set_material_blend(const odin_mesh_t &mesh)
{
	unsigned mode = 0;

	if (mesh.mat_index < m_list->num_mats)
		mode = odin_mat_blending_mode(&m_list->mat_list[mesh.mat_index]);
	m_draw.blend_mode = mode;
	m_draw.weigh_by_alpha = weighs_by_alpha(mode);
	m_draw.blend = material_blend(mode);
}

void
Renderer::set_opaque_blend(const odin_mesh_t &mesh)
{
	m_draw.blend = is_faded_by_depth(mesh) ? MeshBlend::FADED : MeshBlend::REPLACE;
}

/* The depth-writing types or the translucent one of a view have meshes
 * whose 101 has bit 26 (depth_bias.hpp) */
bool
Renderer::has_flagged(unsigned vpi) const
{
	static const unsigned types[] = {
		ODIN_POLYTYPE_TRANSPARENT, ODIN_POLYTYPE_OPAQUE, ODIN_POLYTYPE_SHADOW_A,
		ODIN_POLYTYPE_SHADOW_B, ODIN_POLYTYPE_TRANSLUCENT,
	};

	for (const unsigned polytype : types)
		for (unsigned j = 0; j < m_list->num_meshes[vpi][polytype]; j++)
			if (depth_bias::flagged(static_cast<int>(m_list->mesh_list[vpi][polytype][j].depth_bias)))
				return true;
	return false;
}

void
Renderer::draw_polytype(unsigned vpi, unsigned polytype)
{
	const odin_mesh_t *meshes = m_list->mesh_list[vpi][polytype];
	const unsigned num = m_list->num_meshes[vpi][polytype];

	if (num == 0)
		return;
	if (logging())
		log("POLYTYPE %u: %u meshes", polytype, num);

	if (m_debug[DEBUG_SELECT_POLYTYPE] >= 0 &&
	    static_cast<unsigned>(m_debug[DEBUG_SELECT_POLYTYPE]) != polytype)
		return;
	if (logging())
		log(" ==== DRAWING VP %u, POLYTYPE %u ====", vpi, polytype);

	/* The rules of the pass (pass_rules, hikaru_draw.hpp) */
	const PassRules rules = pass_rules(polytype);

	m_card->begin_pass(rules);
	m_families = m_families_view && rules.depth_write;
	m_draw.blend = MeshBlend::REPLACE;
	m_draw.weigh_by_alpha = false;
	switch (polytype) {
	case ODIN_POLYTYPE_TRANSPARENT:
		m_times.part(DrawTimes::PART_PUNCH_THROUGH);
		break;
	case ODIN_POLYTYPE_TRANSLUCENT:
		m_times.part(DrawTimes::PART_COLLECT);
		break;
	case ODIN_POLYTYPE_BACKGROUND:
		m_times.part(DrawTimes::PART_BACKGROUND);
		break;
	default:
		m_times.part(polytype == ODIN_POLYTYPE_OPAQUE ? DrawTimes::PART_OPAQUE : DrawTimes::PART_SHADOWS);
		break;
	}

	if (polytype == ODIN_POLYTYPE_TRANSLUCENT) {
		draw_translucent(vpi, meshes, num);
		return;
	}

	for (unsigned j = 0; j < num; j++) {
		if (rules.material_blend)
			set_material_blend(meshes[j]);
		else
			set_opaque_blend(meshes[j]);
		draw_mesh(meshes[j]);
	}
	m_card->end_pass();
}

void
Renderer::draw_scene()
{
	/* The background first, for the depth queue to fade the polygons
	 * into it, then the pass order of the X360 port (825B3EB0), which
	 * draws it after the opaque types: punch-through, opaque, then the
	 * translucent type. The two shadow types are drawn as opaque. */
	static const unsigned order[] = {
		ODIN_POLYTYPE_BACKGROUND,
		ODIN_POLYTYPE_TRANSPARENT,
		ODIN_POLYTYPE_OPAQUE,
		ODIN_POLYTYPE_SHADOW_A,
		ODIN_POLYTYPE_SHADOW_B,
		ODIN_POLYTYPE_TRANSLUCENT,
	};

	if (m_debug[DEBUG_NO_3D])
		return;

	m_times.part(DrawTimes::PART_VERTICES);
	m_card->begin_scene(*m_list, { logging() ? stdout : nullptr, m_debug[DEBUG_NO_AMBIENT] != 0,
	                               m_debug[DEBUG_NO_DIFFUSE] != 0 });

	/* Each viewport starts from an empty depth buffer, each family of the
	 * 101 its own in a view that has both (depth_bias.hpp). */
	for (unsigned vpi = 0; vpi < ODIN_VIEWPORTS; vpi++) {
		m_times.part(DrawTimes::PART_VERTICES);
		m_card->clear_depth();
		m_families_view = has_flagged(vpi);
		if (m_families_view)
			m_card->begin_families();
		for (const unsigned polytype : order)
			draw_polytype(vpi, polytype);
	}
	m_families_view = m_families = false;
	m_card->end_scene();
}

} /* namespace odin */
