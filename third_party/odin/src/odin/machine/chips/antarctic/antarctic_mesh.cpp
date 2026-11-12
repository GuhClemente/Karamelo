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

/* From the vertices the instructions push to the meshes of the display
 * list: the triangles are assembled here, and each mesh takes a copy of the
 * objects that are current when its first vertex comes. */

#include "odin/machine/chips/antarctic/antarctic.hpp"
#include "odin/common/bytes.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace odin {

static constexpr float INV255 = 1.0f / 255.0f;

namespace {

/* At least `need` of them: twice as many as before when it grows */
template <typename T>
void
grow(std::vector<T> &list, size_t need)
{
	if (list.size() < need)
		list.resize(std::max({ need, list.size() * 2, size_t{64} }));
}

/* Lists an object, unless it is the one listed last: the meshes that follow
 * one another with the same object point to the same one, and the renderer
 * sends it once. Returns where it is. */
template <typename T>
uint32_t
list_object(T *list, uint32_t &num, const T &object)
{
	if (num == 0 || !same_bytes(list[num - 1], object))
		std::memcpy(&list[num++], &object, sizeof(T));
	return num - 1;
}

} /* namespace */

Antarctic::List::List()
{
	clear();
	room();
	for (unsigned vp = 0; vp < 8; vp++)
		for (unsigned type = 0; type < 8; type++)
			room_for_mesh(vp, type);
}

void
Antarctic::List::room()
{
	grow(vps, view.num_vps + 1);
	grow(mvs, view.num_mvs + 2);
	grow(mats, view.num_mats + 1);
	grow(texs, view.num_texs + 1);
	grow(lss, view.num_lss + 1);
	grow(ls_tables, lss.size() * 4 * 16);
	view.vp_list = vps.data();
	view.mv_list = mvs.data();
	view.mat_list = mats.data();
	view.tex_list = texs.data();
	view.ls_list = lss.data();
	view.ls_tables = reinterpret_cast<uint16_t (*)[4][16]>(ls_tables.data());
}

void
Antarctic::List::room_for_mesh(unsigned vp, unsigned type)
{
	grow(meshes[vp][type], view.num_meshes[vp][type] + 1);
	view.mesh_list[vp][type] = meshes[vp][type].data();
}

void
Antarctic::List::clear()
{
	view.num_vps = 0;
	view.num_mvs = 0;
	view.mv2_index = ~0u;
	mv_index = ~0u;
	view.num_mats = 0;
	view.num_texs = 0;
	view.num_lss = 0;
	for (auto &by_type : view.num_meshes)
		for (uint32_t &num : by_type)
			num = 0;
	view.total_meshes = 0;
	verts.clear();
	indices.clear();
	view.verts.all = verts.data();
	view.verts.num = 0;
	view.indices.all = indices.data();
	view.indices.num = 0;
}

odin_display_list_t *
Antarctic::begin_list()
{
	m_building = (m_building + 1) % static_cast<unsigned>(m_lists.size());
	m_list = m_lists[m_building].get();
	m_list->clear();
	/* A pivot may outlive the list it was listed in. */
	for (Vertex &v : m_push.ring)
		v.changed();
	return &m_list->view;
}

/* Copies the objects that are current into the list and points the mesh
 * to them. */
void
Antarctic::list_state(odin_mesh_t &mesh)
{
	odin_display_list_t &l = m_list->view;

	std::memset(&mesh, 0, sizeof(mesh));
	m_list->room();

	mesh.vp_index = list_object(l.vp_list, l.num_vps, m_s.vp.current);
	mesh.mat_index = list_object(l.mat_list, l.num_mats, m_s.mat.scratch);
	mesh.tex_index = list_object(l.tex_list, l.num_texs, m_s.tex.scratch);

	/* A lightset is its lights and the 194 table of each. A word of a
	 * table has two points, the even one in its upper half; the first
	 * of the four tables of a light is taken (HEURISTIC: what picks one
	 * is not known, the games load the same ramp in all of them). */
	uint32_t tables[4][8];

	for (unsigned i = 0; i < 4; i++)
		std::memcpy(tables[i], m_s.light_table[i], sizeof(tables[i]));
	if (l.num_lss == 0 || !same_bytes(l.ls_list[l.num_lss - 1], m_s.lit.scratchset) ||
	    !same_bytes(m_list->tables, tables)) {
		for (unsigned i = 0; i < 4; i++) {
			for (unsigned j = 0; j < 8; j++) {
				l.ls_tables[l.num_lss][i][j * 2] = static_cast<uint16_t>(tables[i][j] >> 16);
				l.ls_tables[l.num_lss][i][j * 2 + 1] = static_cast<uint16_t>(tables[i][j]);
			}
		}
		std::memcpy(m_list->tables, tables, sizeof(tables));
		std::memcpy(&l.ls_list[l.num_lss++], &m_s.lit.scratchset, sizeof(odin_lightset_t));
	}
	mesh.ls_index = l.num_lss - 1;

	/* The modelview as well, when it changes; second modelviews may be
	 * listed in between. */
	odin_modelview_t mv;

	get_modelview(mv);
	if (m_list->mv_index >= l.num_mvs || !same_bytes(l.mv_list[m_list->mv_index], mv)) {
		m_list->mv_index = l.num_mvs;
		l.mv_list[l.num_mvs++] = mv;
	}
	mesh.mv_index = m_list->mv_index;
	mesh.num_instances = 1;

	/* The second modelview (161 with bit 18) is listed when it changes. */
	if (l.mv2_index >= l.num_mvs ||
	    !same_bytes(l.mv_list[l.mv2_index], m_s.mv.second)) {
		l.mv2_index = l.num_mvs;
		l.mv_list[l.num_mvs++] = m_s.mv.second;
	}
	mesh.mv2_index = l.mv2_index;

	mesh.depth_bias = m_s.depth_bias;
	mesh.pattern = m_s.pattern;

	mesh.num = l.total_meshes++;
}

void
Antarctic::begin_mesh(uint32_t addr)
{
	odin_display_list_t &l = m_list->view;
	/* The meshes are kept by viewport, which the top bits of the 421
	 * number, and by polygon type. */
	const uint32_t vp = m_s.vp.current.depth.func;
	const uint32_t type = m_s.poly.type;
	const uint32_t index = l.num_meshes[vp][type];

	if (m_mesh)
		fatal("a mesh is open already", addr);
	m_list->room_for_mesh(vp, type);
	m_mesh = &l.mesh_list[vp][type][index];
	l.num_meshes[vp][type] = index + 1;

	list_state(*m_mesh);
	m_mesh->poly_alpha = m_s.poly.alpha;
	m_mesh->polytype = type;
	m_mesh->addr[0] = addr;
	m_mesh->first_index = static_cast<uint32_t>(m_list->indices.size());

	/* The last vertices stay and take the new material: a strip goes
	 * on through a recall. The colour bars of the BOOTROM C.R.T. test
	 * are one strip, with a material recalled before the two vertices
	 * that close each bar. */
	if (m_push.num_verts > 3)
		m_push.num_verts = 3;
	for (uint32_t i = 3 - m_push.num_verts; i < 3; i++)
		copy_material(tmp(i));
	m_push.num_tris = 0;
}

void
Antarctic::end_mesh(uint32_t addr)
{
	List &list = *m_list;

	if (!m_mesh)
		fatal("no mesh is open", addr);

	m_mesh->num_tris = m_push.num_tris;
	list.view.verts.all = list.verts.data();
	list.view.verts.num = static_cast<uint32_t>(list.verts.size());
	list.view.indices.all = list.indices.data();
	list.view.indices.num = static_cast<uint32_t>(list.indices.size());

	m_mesh->addr[1] = addr;
	m_mesh = nullptr;
}


void
Antarctic::copy_material(Vertex &dst)
{
	const odin_material_t &mat = m_s.mat.scratch;

	dst.changed();
	std::memcpy(dst.body.diffuse, mat.diffuse, sizeof(dst.body.diffuse));

	/* The 691 colour is the ambient of the shading mode 1 and the
	 * emission of the mode 2, 255 for 1.0. The port saturates it there
	 * (825B4DE8), the hardware does not: PHARRIER sends 600 in mode 2
	 * for the syringe of its nurse and 736 in mode 1 for the selection
	 * frame of its shop, and the cabinet shows both that much brighter. */
	if (std::memcmp(m_push.ambient_of, mat.unknown, sizeof(mat.unknown)) != 0) {
		std::memcpy(m_push.ambient_of, mat.unknown, sizeof(mat.unknown));
		for (unsigned i = 0; i < 3; i++)
			m_push.ambient[i] = mat.unknown[i] / 255.0f;
	}
	std::memcpy(dst.body.ambient, m_push.ambient, sizeof(dst.body.ambient));

	/* The 491 colour is in 1/64: PHARRIER sets 0x40 and SGNASCAR goes up
	 * to 0x7F. The port multiplies it by 4 and wraps to 8 bits (825B4DE8),
	 * the same thing below 0x40; the shader scales it. */
	std::memcpy(dst.body.specular, mat.specular, sizeof(dst.body.specular));
	std::memcpy(dst.body.color1, mat.ambient, sizeof(dst.body.color1));
}

void
Antarctic::copy_colors(Vertex &dst, const Vertex &src)
{
	copy_material(dst);

	/* Vertex alpha times polygon alpha for every type (X360 8252D858);
	 * the render pass decides what it means. */
	const float alpha = std::clamp(m_s.poly.alpha * (static_cast<float>(src.info.alpha()) * INV255), 0.0f, 1.0f);
	dst.body.alpha = static_cast<uint8_t>(alpha * 255.0f);
}

void
Antarctic::copy_texcoords(Vertex &dst, const Vertex &src) const
{
	const odin_texhead_t *th = &m_s.tex.scratch;
	/* The texture is 16 << log texels a side, twice as many in I4: the
	 * coordinates are divided by a power of two, the same as multiplied
	 * by its inverse, which is exact. */
	const unsigned i4 = odin_tex_format(th) == ODIN_FORMAT_I4 ? 1 : 0;
	const auto inverse = [](unsigned log) { return std::bit_cast<float>((127u - log) << 23); };
	const float inv_w = inverse(4 + odin_tex_logw(th) + i4);
	const float inv_h = inverse(4 + odin_tex_logh(th) + i4);
	/* Vertex UVs are 12.4 unless the texhead says whole texels; the 0D1
	 * offset slot picked by the vertex is added in 1/16 texel (X360
	 * 8252D710, 82538E70). */
	const float k = odin_tex_uvtexels(th) ? 16.0f : 1.0f;
	float u = src.body.texcoords[0] * k;
	float v = src.body.texcoords[1] * k;
	const uint32_t sel = src.info.uv_offset();

	if (sel) {
		u += m_s.texoffset[sel][0] * (1.0f / 16.0f);
		v += m_s.texoffset[sel][1] * (1.0f / 16.0f);
	}

	dst.body.texcoords[0] = u * inv_w;
	dst.body.texcoords[1] = v * inv_h;
	dst.changed();
}

/* The index of a vertex in the list, which takes it the first time it is
 * used as it is now. */
uint32_t
Antarctic::list_vertex(Vertex &v, unsigned back)
{
	std::vector<odin_vertex_body_t> &verts = m_list->verts;

	if (v.listed[back] == NOT_LISTED) {
		v.listed[back] = static_cast<uint32_t>(verts.size());
		verts.push_back(v.body);
		if (back)
			for (unsigned j = 0; j < 3; j++)
				verts.back().normal[j] = -verts.back().normal[j];
	}
	return v.listed[back];
}

/* The last three vertices make a triangle of the mesh. */
void
Antarctic::add_triangle()
{
	if (m_push.num_verts < 3)
		return;

	const bool both_faces = tmp(2).info.nocull();
	std::vector<uint32_t> &indices = m_list->indices;
	Vertex *const va = &tmp(0);
	Vertex *vb = &tmp(1), *vc = &tmp(2);
	const odin_vertex_body_t *a = &va->body;
	const odin_vertex_body_t *b = &vb->body;
	const odin_vertex_body_t *c = &vc->body;
	float e1[3], e2[3], n[3];

	/* Degenerate triangles are dropped, as in the X360 port
	 * (825B6100); not when the vertices follow different
	 * modelviews, the matrices give them their shape then (the
	 * AIRTRIX trails). */
	for (unsigned i = 0; i < 3; i++) {
		e1[i] = b->position[i] - a->position[i];
		e2[i] = c->position[i] - a->position[i];
	}
	n[0] = e1[1] * e2[2] - e1[2] * e2[1];
	n[1] = e1[2] * e2[0] - e1[0] * e2[2];
	n[2] = e1[0] * e2[1] - e1[1] * e2[0];
	if (a->second_mv == b->second_mv && b->second_mv == c->second_mv &&
	    (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) <= 0.0f)
		return;

	/* The winding is the vertex order, reversed by the winding bit
	 * of the closing vertex. The port winds each triangle after its
	 * vertex normals instead (825B6218), but the SGNASCAR cabinet
	 * shows quads whose normals point away from the viewer (the
	 * dark panels of its ENTRY DRIVERS screen). */
	if (tmp(2).info.winding())
		std::swap(vb, vc);

	indices.push_back(list_vertex(*va, 0));
	indices.push_back(list_vertex(*vb, 0));
	indices.push_back(list_vertex(*vc, 0));
	m_push.num_tris += 1;

	/* Both faces (bit 23): the triangle is emitted twice, the
	 * second time for its back, lit with the reversed normals. */
	if (both_faces) {
		indices.push_back(list_vertex(*va, 1));
		indices.push_back(list_vertex(*vc, 1));
		indices.push_back(list_vertex(*vb, 1));
		m_push.num_tris += 1;
	}
}

/* Takes what a push instruction brings: one vertex, or the texcoords of
 * the last three. A header with the triangle bits closes a triangle. */
void
Antarctic::push_vertices(const Vertex *v, uint32_t flags, uint32_t num)
{
	if (v->info.tricap() != 0 && v->info.tricap() != 7)
		fatal("triangle bits not known", v->info.full);

	if (num == 1) {
		/* Note that tmp(2) always points to the last pushed vertex,
		 * which for instructions 12x, 1Ax and 1Bx means the vertex
		 * pushed by the instruction itself, and for instructions 0Ex
		 * and 15x the vertex pushed by the previous "push"
		 * instruction. */

		/* If the incoming vertex includes the position, it becomes
		 * the last one, in the slot of the vertex it leaves out: the
		 * oldest, or the one before the last when the p(osition)pivot
		 * bit keeps the oldest. */
		if (flags & PUSH_POS) {
			uint8_t *const at = m_push.at;

			if (!v->info.ppivot()) {
				const uint8_t out = at[0];

				at[0] = at[1];
				at[1] = at[2];
				at[2] = out;
			} else {
				std::swap(at[1], at[2]);
			}
			Vertex &last = tmp(2);

			last.info.full = 0;
			/* Push all writes every field below. */
			if ((flags & (PUSH_NRM | PUSH_TXC)) != (PUSH_NRM | PUSH_TXC))
				last.body = odin_vertex_body_t{};

			for (unsigned i = 0; i < 3; i++)
				last.body.position[i] = v->body.position[i];
			copy_colors(last, *v);

			m_push.num_verts += 1;
		}

		if (flags & PUSH_NRM)
			for (unsigned i = 0; i < 3; i++)
				tmp(2).body.normal[i] = v->body.normal[i];

		/* Bit 12 of the header: the vertex follows the second
		 * modelview (161 with bit 18). Bits 13 and 14: it takes the
		 * 3x3 and the last row of its modelview. The vertex shader
		 * picks the matrix. */
		if (flags & PUSH_POS)
			tmp(2).body.second_mv = static_cast<uint8_t>(
				(v->info.second_mv() ? 1 : 0) +
				(v->info.rotate() ? 0 : 64) +
				(v->info.translate() ? 0 : 128));

		if (flags & PUSH_TXC)
			copy_texcoords(tmp(2), *v);

		/* Whatever came, the last vertex is not the one listed. */
		tmp(2).changed();
	} else {
		if (m_push.num_verts < 3)
			return;

		for (unsigned i = 0; i < 3; i++)
			copy_texcoords(tmp(2 - i), v[i]);
	}

	/* A vertex whose tricap is 7 closes its triangle */
	if (v[0].info.tricap() == 7) {
		tmp(2).info.full = v[0].info.full;
		add_triangle();
	}
}

} /* namespace odin */
