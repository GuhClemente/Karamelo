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

/* What the command processor of the Hikaru (315-6083, "Antarctic") hands
 * over for a frame: the objects its instructions set and the triangles it
 * assembled. Plain data, read by the renderer. */

#ifndef ODIN_ANTARCTIC_LIST_HPP
#define ODIN_ANTARCTIC_LIST_HPP

#include <cstdint>
#include <cstdio>

/* Used for both texheads and layers */
enum {
	ODIN_FORMAT_ABGR1555 = 0,
	ODIN_FORMAT_ABGR4444 = 1,
	ODIN_FORMAT_I4 = 2,	/* 4 bits a texel, 2 x 2 texels a word */
	ODIN_FORMAT_LA8 = 4,
	ODIN_FORMAT_A2BGR10 = 8
};

/* PHARRIER lists the following polygon types:
 *
 * opaque, shadow A, shadow B, transparent, background, translucent.
 *
 * Each of them is associated to a GPU HW performance counter, so the
 * hardware very likely distinguishes between them too. */
enum {
	ODIN_POLYTYPE_OPAQUE = 1,
	ODIN_POLYTYPE_SHADOW_A = 2,
	ODIN_POLYTYPE_SHADOW_B = 3,
	ODIN_POLYTYPE_TRANSPARENT = 4,	/* DreamZzz calls it punchthrough */
	ODIN_POLYTYPE_BACKGROUND = 5,
	ODIN_POLYTYPE_TRANSLUCENT = 6,
	ODIN_POLYTYPES = 8		/* types of three bits */
};

/* The viewports of a list, each drawn from an empty depth buffer */
enum { ODIN_VIEWPORTS = 8 };

/* Which instructions an object has received (these and the other
 * ODIN_*_HAS_* below): no code tests them, but they are among the bytes
 * compared to tell two objects apart (list_object, the texhead of 0E8) */
enum {
	ODIN_VP_HAS_021 = 1 << 0,
	ODIN_VP_HAS_221 = 1 << 1,
	ODIN_VP_HAS_421 = 1 << 2,
	ODIN_VP_HAS_621 = 1 << 3,
	ODIN_VP_HAS_011 = 1 << 4,
	ODIN_VP_HAS_191 = 1 << 5
};

typedef struct {
	struct {
		float l, r;
		float b, t;
		float f, n, f2;
	} clip;
	struct {
		float x, y;
	} offset;
	struct {
		float max, min;
		float inv_range, near_by_edge;
		uint8_t mask[4];
		uint32_t func;
		uint32_t q_type;
		uint32_t q_behind;
		uint32_t q_unknown;
	} depth;
	struct {
		uint8_t clear[4];
		uint16_t ambient[3];
	} color;
	uint32_t flags;
} odin_viewport_t;

typedef struct {
	float mtx[4][4];
} odin_modelview_t;

enum {
	ODIN_MAT_HAS_091 = 1 << 0,
	ODIN_MAT_HAS_291 = 1 << 1,
	ODIN_MAT_HAS_491 = 1 << 2,
	ODIN_MAT_HAS_691 = 1 << 3,
	ODIN_MAT_HAS_081 = 1 << 4,
	ODIN_MAT_HAS_881 = 1 << 5,
	ODIN_MAT_HAS_A81 = 1 << 6,
	ODIN_MAT_HAS_C81 = 1 << 7
};

typedef struct {
	uint8_t diffuse[3];	/* 091: base colour of untextured polygons (X360 vo3d) */
	uint8_t ambient[3];	/* 291: colour 1, blended over 091 by the vertex alpha (881 bit a) */
	uint8_t specular[4];	/* 491: specular, times 4 in the port, and exponent */
	uint16_t unknown[3];	/* 691: ambient, 16 bits per channel, 255 is 1.0 */
	uint32_t w081;
	uint32_t w881;
	uint32_t wA81;
	uint32_t wC81;
	uint32_t flags;
} odin_material_t;

/* 881 */
static inline unsigned odin_mat_shading_mode (const odin_material_t *m) { return (m->w881 >> 16) & 3; }
static inline unsigned odin_mat_depth_blend (const odin_material_t *m) { return (m->w881 >> 18) & 1; }
static inline unsigned odin_mat_has_texture (const odin_material_t *m) { return (m->w881 >> 19) & 1; }
static inline unsigned odin_mat_has_alpha (const odin_material_t *m) { return (m->w881 >> 20) & 1; }
/* A81 */
static inline unsigned odin_mat_blending_mode (const odin_material_t *m) { return (m->wA81 >> 16) & 3; }

enum {
	ODIN_TEX_HAS_0C1 = 1 << 0,
	ODIN_TEX_HAS_2C1 = 1 << 1,
	ODIN_TEX_HAS_4C1 = 1 << 2
};

typedef struct {
	uint32_t w0C1;
	uint32_t w2C1;
	uint32_t w4C1;
	uint32_t flags;
} odin_texhead_t;

/* 0C1: bits 17:16, X360 825C6650 */
static inline unsigned odin_tex_filter (const odin_texhead_t *t) { return (t->w0C1 >> 16) & 3; }
/* 0C1: bits 27:20, signed */
static inline int odin_tex_bias (const odin_texhead_t *t) { return (int) (int8_t) ((t->w0C1 >> 20) & 0xFF); }
/* 2C1: bit 14, UVs in whole texels, else 12.4 (X360 8252D710) */
static inline unsigned odin_tex_uvtexels (const odin_texhead_t *t) { return (t->w2C1 >> 14) & 1; }
static inline unsigned odin_tex_logw (const odin_texhead_t *t) { return (t->w2C1 >> 16) & 7; }
static inline unsigned odin_tex_logh (const odin_texhead_t *t) { return (t->w2C1 >> 19) & 7; }
/* 2C1: bits 22 and 23, else clamp to edge; bits 24 and 25 win over them */
static inline unsigned odin_tex_repeatu (const odin_texhead_t *t) { return (t->w2C1 >> 22) & 1; }
static inline unsigned odin_tex_repeatv (const odin_texhead_t *t) { return (t->w2C1 >> 23) & 1; }
static inline unsigned odin_tex_mirroru (const odin_texhead_t *t) { return (t->w2C1 >> 24) & 1; }
static inline unsigned odin_tex_mirrorv (const odin_texhead_t *t) { return (t->w2C1 >> 25) & 1; }
static inline unsigned odin_tex_format (const odin_texhead_t *t) { return (t->w2C1 >> 26) & 7; }
/* 2C1: bits 31:29, how the texel combines with 091 and 291 (X360 8252E368, 825B4340) */
static inline unsigned odin_tex_texmode (const odin_texhead_t *t) { return (t->w2C1 >> 29) & 7; }
/* 4C1 */
static inline unsigned odin_tex_bank (const odin_texhead_t *t) { return (t->w4C1 >> 12) & 1; }
static inline unsigned odin_tex_slotx (const odin_texhead_t *t) { return (t->w4C1 >> 16) & 0xFF; }
static inline unsigned odin_tex_sloty (const odin_texhead_t *t) { return (t->w4C1 >> 24) & 0xFF; }

/* A texhead with these fields and nothing else */
static inline odin_texhead_t
odin_tex_make (unsigned bank, unsigned slotx, unsigned sloty,
               unsigned logw, unsigned logh, unsigned format)
{
	odin_texhead_t t;

	t.w0C1 = 0;
	t.w2C1 = ((logw & 7) << 16) | ((logh & 7) << 19) | ((format & 7) << 26);
	t.w4C1 = ((bank & 1) << 12) | ((slotx & 0xFF) << 16) | ((sloty & 0xFF) << 24);
	t.flags = 0;
	return t;
}

enum {
	ODIN_LIGHT_HAS_061 = 1 << 0,
	ODIN_LIGHT_HAS_051 = 1 << 1,
	ODIN_LIGHT_HAS_451 = 1 << 2,
	ODIN_LIGHT_HAS_POSITION = 1 << 3,
	ODIN_LIGHT_HAS_DIRECTION = 1 << 4,
	ODIN_LIGHT_ATT_TYPE_SHIFT = 5,		/* 2 bits */
	ODIN_LIGHT_AMBIENT_SHIFT = 7,		/* 4 bits; 051: constant share of the colour, in 1/16 */
	ODIN_LIGHT_051_BIT = 1 << 11,
	ODIN_LIGHT_HAS_SPOT = 1 << 12,
	ODIN_LIGHT_HAS_CONE = 1 << 13
};

typedef struct {
	float position[3];
	float direction[3];
	float attenuation[2];
	uint16_t diffuse[3];
	float spot[3];		/* B61: towards the target, unnormalized */
	float cos_outer;	/* 451: cone edge */
	float cone_scale;	/* 451: 1 / (cos inner - cos outer) */
	uint32_t flags;
} odin_light_t;

static inline unsigned odin_light_att_type (const odin_light_t *l) { return (l->flags >> ODIN_LIGHT_ATT_TYPE_SHIFT) & 3; }
static inline unsigned odin_light_ambient (const odin_light_t *l) { return (l->flags >> ODIN_LIGHT_AMBIENT_SHIFT) & 0xF; }

enum {
	ODIN_LIGHTSET_SET = 1 << 0,
	ODIN_LIGHTSET_MASK_SHIFT = 1		/* 4 bits */
};

typedef struct {
	odin_light_t lights[4];
	uint32_t flags;
} odin_lightset_t;

static inline unsigned odin_lightset_mask (const odin_lightset_t *ls) { return (ls->flags >> ODIN_LIGHTSET_MASK_SHIFT) & 0xF; }

/* 56 bytes, no padding: the renderer uploads the array as it is. A vertex
 * the triangles of a strip or a fan share is there once, as in the X360
 * port (825B77E8). */
typedef struct {
	float position[3];	/* 0x00 */
	uint8_t diffuse[3];	/* 0x0C */
	uint8_t alpha;		/* 0x0F */
	float normal[3];	/* 0x10 */
	float ambient[3];	/* 0x1C, the 691 colour over 255 */
	float texcoords[2];	/* 0x28 */
	uint8_t specular[4];	/* 0x30 */
	uint8_t color1[3];	/* 0x34 */
	uint8_t second_mv;	/* 0x37, 1: second modelview, 64: no 3x3, 128: no last row */
} odin_vertex_body_t;

typedef struct {
	uint32_t first_index;	/* of its first triangle, in the indices of the frame */
	uint32_t num_tris;
	uint32_t addr[2];
	uint32_t vp_index;
	uint32_t mv_index;
	uint32_t mv2_index;	/* second modelview */
	uint32_t num_instances;	/* always 1: no instancing is known (161) */
	uint32_t mat_index;
	uint32_t tex_index;
	uint32_t ls_index;
	float depth_bias;
	uint32_t pattern;	/* 501 */
	float poly_alpha;	/* 103 alpha, logged */
	uint32_t num;
	uint32_t polytype;
} odin_mesh_t;

/* The objects of a frame, in the order the meshes took them, and the
 * meshes by viewport and polygon type. */
typedef struct {
	odin_viewport_t		*vp_list;
	uint32_t		 num_vps;

	odin_modelview_t	*mv_list;
	uint32_t		 num_mvs;
	uint32_t		 mv2_index;	/* last second modelview listed this frame */

	odin_material_t		*mat_list;
	uint32_t		 num_mats;

	odin_texhead_t		*tex_list;
	uint32_t		 num_texs;

	odin_lightset_t		*ls_list;
	uint32_t		 num_lss;
	/* The 194 table of the four lights of each lightset */
	uint16_t		(*ls_tables)[4][16];

	odin_mesh_t		*mesh_list[ODIN_VIEWPORTS][ODIN_POLYTYPES];
	uint32_t		 num_meshes[ODIN_VIEWPORTS][ODIN_POLYTYPES];
	uint32_t		 total_meshes;

	/* The vertices of all the meshes, and their triangles: three
	 * indices each, in the order they are drawn */
	struct {
		odin_vertex_body_t	*all;
		uint32_t		num;
	} verts;
	struct {
		uint32_t		*all;
		uint32_t		num;
	} indices;
} odin_display_list_t;

/* 181, 781 and 191: the framebuffer blender and the clear colour */
typedef struct {
	uint32_t w181;
	uint32_t w781;
	uint8_t clear[4];
} odin_fb_config_t;

static inline unsigned odin_fb_alpha (const odin_fb_config_t *f) { return (f->w181 >> 16) & 0xFF; }
static inline unsigned odin_fb_blend (const odin_fb_config_t *f) { return (f->w181 >> 24) & 1; }
static inline unsigned odin_fb_num_1 (const odin_fb_config_t *f) { return (f->w781 >> 16) & 3; }
static inline unsigned odin_fb_num_2 (const odin_fb_config_t *f) { return (f->w781 >> 24) & 3; }

/* antarctic_list.cpp */

/* Where a texhead sits in its bank of the texture RAM, in texels. The slots
 * are 16 x 16 and start at (80, C0); (0, 0) and a message for a slot before
 * them. */
void odin_tex_coords(const odin_texhead_t *th, uint32_t *x, uint32_t *y);

/* One line for an object, in a buffer of the function */
const char *odin_viewport_str(const odin_viewport_t *vp);
const char *odin_modelview_str(const odin_modelview_t *mv);
const char *odin_material_str(const odin_material_t *mat);
const char *odin_texhead_str(const odin_texhead_t *th);
const char *odin_lightset_str(const odin_lightset_t *ls);

/* Fingerprint of a list: FNV-1a 64 over the fields of its meshes, by value,
 * in the order of the mesh lists, then the framebuffer words. dump, if not
 * NULL, takes one line a mesh. */
uint64_t odin_display_list_fingerprint(const odin_display_list_t *list,
                                       const odin_fb_config_t *fb,
                                       uint32_t *meshes, uint32_t *tris,
                                       FILE *dump);

#endif /* ODIN_ANTARCTIC_LIST_HPP */
