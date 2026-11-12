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

/* The display list as text and as a fingerprint, to look at what the
 * command processor made and to compare two builds. */

#include "odin/machine/chips/antarctic/antarctic_list.hpp"
#include "odin/common/bytes.hpp"
#include "odin/common/repeats.hpp"

#include <cinttypes>
#include <cstdio>

namespace {

/* FNV-1a, 64 bits */
struct Hash {
	uint64_t value = odin::FNV_START;

	void bytes(const void *data, size_t size) { value = odin::fnv1a(value, data, size); }
	template <typename T> void add(const T &field) { bytes(&field, sizeof(field)); }
};

uint64_t
hash_viewport(const odin_viewport_t &vp)
{
	Hash h;

	h.add(vp.clip.l); h.add(vp.clip.r); h.add(vp.clip.b); h.add(vp.clip.t);
	h.add(vp.clip.f); h.add(vp.clip.n); h.add(vp.clip.f2);
	h.add(vp.offset.x); h.add(vp.offset.y);
	h.add(vp.depth.max); h.add(vp.depth.min);
	h.add(vp.depth.inv_range); h.add(vp.depth.near_by_edge);
	h.add(vp.depth.mask);
	h.add(vp.depth.func); h.add(vp.depth.q_type);
	h.add(vp.depth.q_behind); h.add(vp.depth.q_unknown);
	h.add(vp.color.clear);
	h.add(vp.color.ambient);
	h.add(vp.flags);
	return h.value;
}

uint64_t
hash_material(const odin_material_t &m)
{
	Hash h;

	h.add(m.diffuse); h.add(m.ambient); h.add(m.specular); h.add(m.unknown);
	h.add(m.w081); h.add(m.w881); h.add(m.wA81); h.add(m.wC81);
	h.add(m.flags);
	return h.value;
}

uint64_t
hash_texhead(const odin_texhead_t &t)
{
	Hash h;

	h.add(t.w0C1); h.add(t.w2C1); h.add(t.w4C1); h.add(t.flags);
	return h.value;
}

uint64_t
hash_lightset(const odin_lightset_t &ls, const uint16_t table[4][16])
{
	Hash h;

	for (const odin_light_t &l : ls.lights) {
		h.add(l.position); h.add(l.direction); h.add(l.attenuation);
		h.add(l.diffuse); h.add(l.spot);
		h.add(l.cos_outer); h.add(l.cone_scale);
		h.add(l.flags);
	}
	h.add(ls.flags);
	h.bytes(table, 4 * 16 * sizeof(uint16_t));
	return h.value;
}

} /* namespace */

uint64_t
odin_display_list_fingerprint(const odin_display_list_t *list,
                              const odin_fb_config_t *fb,
                              uint32_t *meshes, uint32_t *tris, FILE *out)
{
	Hash all;

	*meshes = 0;
	*tris = 0;
	for (uint32_t vp = 0; vp < 8; vp++)
	for (uint32_t type = 0; type < 8; type++)
	for (uint32_t i = 0; i < list->num_meshes[vp][type]; i++) {
		const odin_mesh_t &m = list->mesh_list[vp][type][i];
		Hash mesh, modelviews, vertices;

		mesh.add(vp); mesh.add(type);
		mesh.add(m.addr[0]); mesh.add(m.addr[1]);
		mesh.add(m.num_tris); mesh.add(m.num_instances);
		mesh.add(m.depth_bias); mesh.add(m.pattern); mesh.add(m.poly_alpha);
		mesh.add(m.polytype); mesh.add(m.num);

		for (uint32_t j = 0; j < m.num_instances; j++)
			modelviews.add(list->mv_list[m.mv_index + j].mtx);
		modelviews.add(list->mv_list[m.mv2_index].mtx);

		/* The vertices of each triangle in turn, as they are drawn */
		for (uint32_t k = 0; k < m.num_tris * 3; k++)
			vertices.add(list->verts.all[list->indices.all[m.first_index + k]]);

		const uint64_t viewport = hash_viewport(list->vp_list[m.vp_index]);
		const uint64_t material = hash_material(list->mat_list[m.mat_index]);
		const uint64_t texhead = hash_texhead(list->tex_list[m.tex_index]);
		const uint64_t lightset = hash_lightset(list->ls_list[m.ls_index],
		                                        list->ls_tables[m.ls_index]);

		all.add(mesh.value); all.add(viewport); all.add(modelviews.value);
		all.add(material); all.add(texhead); all.add(lightset);
		all.add(vertices.value);

		*meshes += 1;
		*tris += m.num_tris;
		if (out)
			std::fprintf(out, "LISTMESH %u %u.%u.%u @%08X-%08X tris=%u m=%016" PRIx64 " vp=%016" PRIx64
			             " mv=%016" PRIx64 " mat=%016" PRIx64 " tex=%016" PRIx64 " ls=%016" PRIx64
			             " v=%016" PRIx64 "\n",
			             m.num, vp, type, i, m.addr[0], m.addr[1], m.num_tris,
			             mesh.value, viewport, modelviews.value, material,
			             texhead, lightset, vertices.value);
	}
	all.add(fb->clear);
	all.add(fb->w181);
	all.add(fb->w781);
	return all.value;
}

void
odin_tex_coords(const odin_texhead_t *th, uint32_t *x, uint32_t *y)
{
	const unsigned slotx = odin_tex_slotx(th), sloty = odin_tex_sloty(th);

	if (slotx < 0x80 || sloty < 0xC0) {
		/*
		 * This case is triggered by the following CP code, used by
		 * the BOOTROM:
		 *
		 * GPU CMD 480008B0: Texhead: Set Unknown [000100C1]
		 * GPU CMD 480008B4: Texhead: Set Format/Size [C08002C1]
		 * GPU CMD 480008B8: Texhead: Set Slot [0000C4C1]
		 * GPU CMD 480008BC: Commit Texhead [000010C4] n=0 e=1
		 * GPU CMD 480008C0: Recall Texhead [000010C3] () n=0 e=1
		 *
		 * Note how the params to 2C1 and 4C1 are swapped.
		 */
		odin::repeats::print(stderr, "ERROR: GPU: invalid slot %X,%X\n", slotx, sloty);
		*x = 0;
		*y = 0;
	} else {
		*x = (slotx - 0x80) * 16;
		*y = (sloty - 0xC0) * 16;
	}
}

const char *
odin_viewport_str(const odin_viewport_t *vp)
{
	static char out[512];

	std::snprintf(out, sizeof(out), "clip=(%6.3f %6.3f %6.3f %6.3f %6.3f %6.3f) offs=(%6.3f %6.3f) depth=(%u %6.3f %6.3f) dq=(behind=%u type=%u unk=%u mask=%02X%02X%02X%02X P=%g Q=%g)",
	              vp->clip.l, vp->clip.r,
	              vp->clip.b, vp->clip.t,
	              vp->clip.f, vp->clip.n,
	              vp->offset.x, vp->offset.y,
	              vp->depth.func & 7u,
	              vp->depth.min, vp->depth.max,
	              vp->depth.q_behind, vp->depth.q_type, vp->depth.q_unknown,
	              vp->depth.mask[3], vp->depth.mask[2],
	              vp->depth.mask[1], vp->depth.mask[0],
	              vp->depth.inv_range, vp->depth.near_by_edge);
	return out;
}

const char *
odin_modelview_str(const odin_modelview_t *mv)
{
	static char out[512];

	std::snprintf(out, sizeof(out),
	              "|%f %f %f %f| |%f %f %f %f| |%f %f %f %f| |%f %f %f %f|",
	              mv->mtx[0][0], mv->mtx[0][1], mv->mtx[0][2], mv->mtx[0][3],
	              mv->mtx[1][0], mv->mtx[1][1], mv->mtx[1][2], mv->mtx[1][3],
	              mv->mtx[2][0], mv->mtx[2][1], mv->mtx[2][2], mv->mtx[2][3],
	              mv->mtx[3][0], mv->mtx[3][1], mv->mtx[3][2], mv->mtx[3][3]);
	return out;
}

const char *
odin_material_str(const odin_material_t *mat)
{
	static char out[512];

	std::snprintf(out, sizeof(out), "#%02X%02X%02X #%02X%02X%02X #%02X%02X%02X,%02X #%04X,%04X,%04X 081=%08X 881=%08X A81=%08X C81=%08X",
	              mat->diffuse[0], mat->diffuse[1], mat->diffuse[2],
	              mat->ambient[0], mat->ambient[1], mat->ambient[2],
	              mat->specular[0], mat->specular[1], mat->specular[2], mat->specular[3],
	              mat->unknown[0], mat->unknown[1], mat->unknown[2],
	              mat->w081, mat->w881, mat->wA81, mat->wC81);
	return out;
}

const char *
odin_texhead_str(const odin_texhead_t *th)
{
	static const char *name[8] = {
		"ABGR1555",
		"ABGR4444",
		"I4      ",
		"???3??? ",
		"LA8     ",
		"???5??? ",
		"???6??? ",
		"???7??? "
	};
	static char out[512];
	uint32_t basex, basey;

	odin_tex_coords(th, &basex, &basey);
	std::snprintf(out, sizeof(out), "[bank=%X slot=(%2X,%2X) pos=(%3X,%3X) -> offs=%08X] [size=%3ux%3u format=%s] 0C1=%08X 2C1=%08X mode=%u",
	              odin_tex_bank(th), odin_tex_slotx(th), odin_tex_sloty(th),
	              basex, basey, basey * 4096 + basex * 2,
	              16u << odin_tex_logw(th), 16u << odin_tex_logh(th),
	              name[odin_tex_format(th)], th->w0C1, th->w2C1, odin_tex_texmode(th));
	return out;
}

static const char *
light_str(const odin_light_t *lit)
{
	static char out[512];

	std::snprintf(out, sizeof(out), "%u (%+10.3f %+10.3f) dir=%u (%+10.3f %+10.3f %+10.3f) pos=%u (%+10.3f %+10.3f %+10.3f) [%u %03X %03X %03X] spot=%u (%+10.3f %+10.3f %+10.3f) cone=%u (%.3f %.3f)",
	              odin_light_att_type(lit), lit->attenuation[0], lit->attenuation[1],
	              (lit->flags & ODIN_LIGHT_HAS_DIRECTION) ? 1u : 0u,
	              lit->direction[0], lit->direction[1], lit->direction[2],
	              (lit->flags & ODIN_LIGHT_HAS_POSITION) ? 1u : 0u,
	              lit->position[0], lit->position[1], lit->position[2],
	              odin_light_ambient(lit), lit->diffuse[0], lit->diffuse[1], lit->diffuse[2],
	              (lit->flags & ODIN_LIGHT_HAS_SPOT) ? 1u : 0u,
	              lit->spot[0], lit->spot[1], lit->spot[2],
	              (lit->flags & ODIN_LIGHT_HAS_CONE) ? 1u : 0u,
	              lit->cos_outer, lit->cone_scale);
	return out;
}

/* Four lines of light_str, less than 512 characters each, and their
 * newlines */
const char *
odin_lightset_str(const odin_lightset_t *ls)
{
	static char out[4 * (512 + 1)];
	char *tmp = out;

	out[0] = '\0';
	for (unsigned i = 0; i < 4; i++) {
		if (odin_lightset_mask(ls) & (1u << i))
			continue;
		tmp += std::sprintf(tmp, "%s\n", light_str(&ls->lights[i]));
	}
	return out;
}
