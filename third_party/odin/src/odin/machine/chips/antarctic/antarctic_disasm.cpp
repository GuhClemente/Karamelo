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

/* Debug (GPU_LOG_CP): the text of each instruction of the command
 * processor. See antarctic_ops.cpp for what they do. */

#include "odin/machine/chips/antarctic/antarctic_disasm.hpp"
#include "odin/machine/chips/antarctic/antarctic_decode.hpp"
#include "odin/common/bytes.hpp"

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace odin {

namespace {

/* What a text function fills */
struct Out {
	uint32_t pc;
	char text[192];
	bool unhandled;

	void print(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
	{
		va_list args;

		va_start(args, fmt);
		std::vsnprintf(text, sizeof(text), fmt, args);
		va_end(args);
	}
	/* A form of the instruction that is not known */
	void unknown()
	{
		print("unknown form");
		unhandled = true;
	}
};

/* The condition of a call and of a return, bits 12-15 */
const char *const CALL_COND[16] = {
	"",		"?1?",		"?2?",		"?3?",
	"!COND",	"?5?",		"?6?",		"?7?",
	"COND",		"?9?",		"?A?",		"?B?",
	"?C?",		"?D?",		"?E?",		"?F?",
};

void
text_nop(Out &o, const uint32_t *inst)
{
	o.print("nop");

	o.unhandled |= (inst[0] != 0);
}

void
text_jump(Out &o, const uint32_t *inst)
{
	static const char *cond[16] = {
		"",		"NEQ BID",	"?2?",		"?3?",
		"?4",		"COND",		"!COND",	"COND",
		"?8?",		"!COND",	"?A?",		"?B?",
		"?C?",		"EQ BID",	"?E?",		"?F?"
	};
	uint32_t addr = jump_address(o.pc, inst);

	o.unhandled |= !!(inst[0] & 0x00000600);

	o.print("jump %s @%08X [BID=%04X]",
	        cond[(inst[0] >> 12) & 0xF], addr, inst[0] >> 16);
}

void
text_call(Out &o, const uint32_t *inst)
{
	uint32_t addr = jump_address(o.pc, inst);

	o.unhandled |= !!(inst[0] & 0xFFFF3600);

	o.print("call %s @%08X", CALL_COND[(inst[0] >> 12) & 0xF], addr);
}

void
text_return(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFFF3E00);

	o.print("ret %s", CALL_COND[(inst[0] >> 12) & 0xF]);
}

void
text_end(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFFFFE00);

	o.print("kill");
}

void
text_lod_test_sphere(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0x00000E00);

	o.print("lod: test sphere [%f]", f32(inst[0] & 0xFFFFF000));
}

void
text_lod_radius(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFFFFE00);

	o.print("lod: set threshold [%f]", f32(inst[1]));
}

void
text_lod_compare(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFFF3E00);

	o.print("lod: compare size [%04X %04X]",
	        inst[1] >> 16, inst[1] & 0xFFFF);
}

void
text_viewport_set(Out &o, const uint32_t *inst)
{
	switch ((inst[0] >> 8) & 7) {
	case 0:
		o.unhandled |= !!(inst[0] & 0xFFFFF800);

		o.print("vp: set clip Z [f=%f f2=%f n=%f]",
		        f32(inst[1]), f32(inst[2]), f32(inst[3]));
		break;

	case 2:
		o.unhandled |= !!(inst[0] & 0xFFFFF800);
	
		o.print("vp: set clip XY [clipxy=(%f %f %f %f) offs=(%f,%f)]",
		        s16(inst[2]), s16(inst[3]),
		        s16(inst[2] >> 16), s16(inst[3] >> 16),
		        (float) (int16_t) (inst[1] & 0xFFFF),
		        (float) (int16_t) (inst[1] >> 16));
		break;

	case 4:
		o.unhandled |= !!(inst[0] & 0xFFFFF800);
		o.unhandled |= !!(inst[3] & 0x1FFFFFFF);

		o.print("vp: set depth [func=%u range=(%f,%f)]",
		        inst[3] >> 29, f32(inst[1]), f32(inst[2]));
		break;

	case 6:
		o.unhandled |= !!(inst[0] & 0xFFF0F800);
		/* Without a colour only the linear type is understood. */
		o.unhandled |= (inst[0] & 0x00020000) && (inst[0] & 0x000C0000);
	
		o.print("vp: set depth queue [type=%u behind=%u unk=%u mask=(%08X) P=%g Q=%g]",
		        (inst[0] >> 18) & 7, (inst[0] >> 17) & 1,
		        (inst[0] >> 16) & 1, inst[1],
		        f32(inst[2]), f32(inst[3]));
		break;
	}
}

void
text_viewport_ambient(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0x0000F600);
	o.unhandled |= !(inst[0] & 0x00000800);

	o.print("vp: set ambient [%X %X %X]",
	        inst[0] >> 16, inst[1] & 0xFFFF, inst[1] >> 16);
}

void
text_viewport_clear(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFFFF600);
	o.unhandled |= !(inst[0] & 0x00000800);
	o.unhandled |= !!(inst[0] & 0xFE000000);

	o.print("vp: set clear [%X]", inst[1]);
}

void
text_viewport_commit(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFF8FE00);

	o.print("vp: commit @%u", ((inst[0] >> 16) & 7));
}

void
text_viewport_recall(Out &o, const uint32_t *inst)
{
	const char *op;

	switch ((inst[0] >> 12) & 0xF) {
	case 0:
		op = "";
		break;
	case 2:
		op = "push";
		break;
	case 4:
		op = "pop";
		break;
	default:
		op = "unknown";
		o.unhandled = true;
		break;
	}

	o.unhandled |= !!(inst[0] & 0xFFF89E00);

	o.print("vp: recall @%u %s", ((inst[0] >> 16) & 7), op);
}

void
text_vector(Out &o, const uint32_t *inst)
{
	uint32_t push, elem;

	switch ((inst[0] >> 8) & 0xF) {

	case 1:
		push = (inst[0] >> 18) & 1;
		elem = (inst[0] >> 16) & 3;

		o.unhandled |= !!(inst[0] & 0xFFF0F000);

		o.print("mtx: set vector [%c %u (%f %f %f)]",
		        push ? 'P' : ' ', elem,
		        f32(inst[1]), f32(inst[2]), f32(inst[3]));
		break;
	case 5:
		o.unhandled |= !!(inst[0] & 0xFFFC0000);

		o.print("lod: set vector [%f %f %f]",
		        f32(inst[1]), f32(inst[2]), f32(inst[3]));
		break;
	case 9:
		o.unhandled |= !!(inst[0] & 0xFFFE0000);

		o.print("lit: set vector 9 [%f %f %f]",
		        f32(inst[1]), f32(inst[2]), f32(inst[3]));
		break;
	case 0xB:
		o.unhandled |= !!(inst[0] & 0xFFFF0000);

		o.print("lit: set vector B [%f %f %f]",
		        f32(inst[1]), f32(inst[2]), f32(inst[3]));
		break;
	default:
		o.unknown();
		break;
	}
}

void
text_material_flags(Out &o, const uint32_t *inst)
{
	switch ((inst[0] >> 8) & 0xF) {
	case 0:
		o.unhandled |= !!(inst[0] & 0xFFF0E000);

		o.print("mat: set unknown");
		break;
	case 8:
		o.unhandled |= !!(inst[0] & 0x0000F000);

		o.print("mat: set flags [mode=%u zblend=%u tex=%u alpha=%u highl=%u y=%u x=%u X=%02X]",
		        (inst[0] >> 16) & 3,
		        (inst[0] >> 18) & 1,
		        (inst[0] >> 19) & 1,
		        (inst[0] >> 20) & 1,
		        (inst[0] >> 21) & 1,
		        (inst[0] >> 22) & 1,
		        (inst[0] >> 23) & 1,
		        inst[0] >> 24);
		break;
	case 0xA:
		o.unhandled |= !!(inst[0] & 0xFFFCF000);

		o.print("mat: set blending mode [mode=%u]", (inst[0] >> 16) & 3);
		break;
	case 0xC:
		o.unhandled |= !!(inst[0] & 0xFFC0F000);

		o.print("mat: set unknown [%x %X]",
		        (inst[0] >> 21) & 1, (inst[0] >> 16) & 0x1F);
		break;
	default:
		o.unknown();
		break;
	}
}

void
text_material_color(Out &o, const uint32_t *inst)
{
	switch ((inst[0] >> 8) & 15) {
	case 0:
		o.unhandled |= !!(inst[0] & 0xFFFFF800);
	
		o.print("mat: set diffuse [%08X]", inst[1]);
		break;
	case 2:
		o.unhandled |= !!(inst[0] & 0xFFFFF800);
	
		o.print("mat: set ambient [%08X]", inst[1]);
		break;
	case 4:
		o.unhandled |= !!(inst[0] & 0xFFFFF800);
	
		o.print("mat: set specular [%X]", inst[1]);
		break;

	case 6:
		o.unhandled |= !!(inst[0] & 0x0000F800);
	
		o.print("mat: set unknown [%X %X %X]",
		        inst[0] >> 16, inst[1] >> 16, inst[1] & 0xFFFF);
		break;
	case 0xA:
		o.unhandled |= !!(inst[0] & 0xFFFCF000);

		o.print("mat: set blending mode [mode=%u], second word dropped [%08X]",
		        (inst[0] >> 16) & 3, inst[1]);
		break;
	default:
		o.unknown();
		break;
	}
}

void
text_material_commit(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFF00E000);
	o.unhandled |= !(inst[0] & 0x1000);

	o.print("mat: commit @base+%u", (inst[0] >> 16));
}

void
text_material_recall(Out &o, const uint32_t *inst)
{
	if (!(inst[0] & 0x1000)) {
		o.unhandled |= !!(inst[0] & 0x0000E000);

		o.print("mat: set base %u", (inst[0] >> 16));
	} else {
		o.unhandled |= !!(inst[0] & 0xFF00E000);

		o.print("mat: recall @base+%u", (inst[0] >> 16));
	}
}

void
text_texhead_set(Out &o, const uint32_t *inst)
{
	switch ((inst[0] >> 8) & 7) {
	case 0:
		o.unhandled |= !!(inst[0] & 0xF00CF800);

		o.print("tex: set bias [mode=%u %X]",
		        (inst[0] >> 16) & 0xF, (inst[0] >> 20) & 0xFF);
		break;

	case 2:
		o.unhandled |= !!(inst[0] & 0x00003800);

		o.print("tex: set format [%ux%u fmt=%u mode=%u wrap=(%u %u|%u %u) unk=%X]",
		        16 << ((inst[0] >> 16) & 7),
		        16 << ((inst[0] >> 19) & 7),
		        (inst[0] >> 26) & 7,
		        (inst[0] >> 29) & 7,
		        (inst[0] >> 22) & 1,
		        (inst[0] >> 23) & 1,
		        (inst[0] >> 24) & 1,
		        (inst[0] >> 25) & 1,
		        (inst[0] >> 14) & 3);
		break;
	case 4:
		o.print("tex: set slot [bank=%u (%X,%X)]",
		        (inst[0] >> 12) & 1,(inst[0] >> 16) & 0xFF, inst[0] >> 24);

		o.unhandled |= !!(inst[0] & 0x0000E000);
		break;
	default:
		o.unknown();
		break;
	}
}

void
text_texcoord_offset(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFFCF000);

	o.print("tex: set panning [%X %+d %+d]",
	        (inst[0] >> 16) & 3, (int16_t) inst[1], (int16_t) (inst[1] >> 16));
}

void
text_texhead_commit(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFF00E000);
	o.unhandled |= ((inst[0] & 0x1000) != 0x1000);

	o.print("tex: commit @base+%u", (inst[0] >> 16));

}

void
text_texhead_recall(Out &o, const uint32_t *inst)
{
	if (!(inst[0] & 0x1000)) {
		o.unhandled |= !!(inst[0] & 0x0000E000);

		o.print("tex: set base %u", (inst[0] >> 16));
	} else {
		o.unhandled |= !!(inst[0] & 0xFF00E000);

		o.print("tex: recall @base+%u", (inst[0] >> 16));
	}
}

void
text_light_attenuation(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFFCF000);

	o.print("lit: set attenuation [%u p=%f q=%f]",
	        (inst[0] >> 16) & 3, f32(inst[1]), f32(inst[2]));
}

void
text_light_color(Out &o, const uint32_t *inst)
{
	switch ((inst[0] >> 8) & 7) {
	case 0:
		o.unhandled |= !!(inst[0] & 0xFF70F000);
		o.unhandled |= !!(inst[1] & 0xC0000000);

		o.print("lit: set diffuse [%X amb=%u]", inst[1], (inst[0] >> 16) & 0xF);
		break;
	case 4:
		o.unhandled |= !!(inst[0] & 0xFEFFF000);
		
		o.print("lit: set cone [%s cos_out=%.3f w=%02X]", ((inst[0] >> 24) & 1) ? "none" : "on", static_cast<float>(inst[1] & 0xFFFF) / 32768.0f, (inst[1] >> 16) & 0xFF);
		break;
	default:
		o.unknown();
		break;
	}
}

void
text_unknown_006(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFFFF000);

	o.print("lit: unknown");
}

void
text_unknown_046(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFFEF000);

	o.print("lit: unknown [%u]", (inst[0] >> 16) & 1);
}

void
text_light_commit(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFC00F000);

	o.print("lit: commit @%u", ((inst[0] >> 16) & 0x3FF));
}

void
text_lightset_commit(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFF00E000);
	o.unhandled |= ((inst[0] & 0x1000) != 0x1000);
	o.unhandled |= !!(inst[1] & 0xFC00FC00);
	o.unhandled |= !!(inst[2] & 0xFC00FC00);

	o.print("lit: commit set @base+%u [%u %u %u %u]",
	        ((inst[0] >> 16) & 0xFF),
	        inst[1] & 0x3FF, (inst[1] >> 16) & 0x3FF,
	        inst[2] & 0x3FF, (inst[2] >> 16) & 0x3FF);
}

void
text_lightset_recall(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xF000E000);

	if (!(inst[0] & 0x1000))
		o.print("lit: set set base %u", ((inst[0] >> 16) & 0xFF));
	else
		o.print("lit: recall set @base+%u [mask=%X]",
		        ((inst[0] >> 16) & 0xFF), (inst[0] >> 24) & 0xFF);
}

void
text_mesh_set(Out &o, const uint32_t *inst)
{
	uint32_t log;
	float precision;

	switch ((inst[0] >> 8) & 0xF) {
	case 1:
		o.unhandled |= !!(inst[0] & 0xF800F000);

		o.print("mesh: set depth bias [%d]",
		        (int) ((inst[0] >> 16) & 0x7FF) - (int) ((inst[0] >> 15) & 0x800));
		break;
	case 3:
		o.unhandled |= !!(inst[0] & 0xFF00F000);

		o.print("mesh: set unknown [%u]", (inst[0] >> 16) & 0xFF);
		break;
	case 5:	
		o.unhandled |= !!(inst[0] & 0xFFE0F000);

		o.print("mesh: set pattern [%X]", (inst[0] >> 16) & 0x1F);
		break;
	case 9:
		log = (inst[0] >> 16) & 0xFF;
		precision = std::ldexp(1.0f, (int) log - 0x8D);

		o.unhandled |= !!(inst[0] & 0xFF00F000);

		o.print("mesh: set precision s [%u %f]", log, precision);	
		break;
	default:
		o.unknown();
		break;
	}

}

void
text_polygon_type(Out &o, const uint32_t *inst)
{
	static const char *poly_type_name[8] = {
		"invalid 0",
		"opaque",
		"shadow A",
		"shadow B",
		"transparent",
		"background",
		"translucent",
		"invalid 7"
	};
	const uint32_t type = (inst[0] >> 9) & 7;
	const float alpha = static_cast<float>(inst[0] >> 24) * (1.0f / 255.0f);

	o.unhandled |= !!(inst[0] & 0x00FFF000);

	o.print("mesh: set poly type [%s alpha=%f]", poly_type_name[type], alpha);
}

void
text_push_position_static(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0x007F0000);

	o.print("mesh: push position s");
}

void
text_push_position(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0x007F0000);

	o.print("mesh: push position d");
}

void
text_push_all(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0x00770000);

	o.print("mesh: push all d");
}

void
text_push_texcoords3(Out &o, const uint32_t *inst)
{

	o.unhandled |= !!(inst[0] & 0x0000F000);

	o.print("mesh: push texcoords 3 [texhead @base+%u]", inst[0] >> 16);
}

void
text_push_texcoords(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFF7FF000);

	o.print("mesh: push texcoords 1");
}

void
text_framebuffer(Out &o, const uint32_t *inst)
{
	switch ((inst[0] >> 8) & 7) {
	case 1:
		o.unhandled |= !!(inst[0] & 0xFE00F800);

		o.print("fb: set blending (%u %X)",
		        (inst[0] >> 24) & 1, (inst[0] >> 16) & 0xFF);
		break;
	case 7:
		o.unhandled |= !!(inst[0] & 0xF8F8F800);

		o.print("fb: set combiner (%X %X)",
		        (inst[0] >> 24) & 7, (inst[0] >> 16) & 7);
		break;
	default:
		o.unknown();
		break;
	}
}

void
text_unknown_088(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFF7FF000);

	o.print("unk: unknown");
}

void
text_alpha_table(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFC0F000);

	o.print("mat: set alpha thresh [%u (%X %X)]",
	        (inst[0] >> 16) & 0x3F, inst[1] & 0xFF, inst[1] >> 8);
}

void
text_light_table(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFCE0F000);

	o.print("light: set table [%u:%u lo=%X hi=%X]",
	        (inst[0] >> 24) & 3, (inst[0] >> 16) & 0x1F,
	        inst[1] >> 16, inst[1] & 0xFFFF);
}

void
text_unknown_1A1(Out &o, const uint32_t *inst)
{
	o.unhandled |= !!(inst[0] & 0xFFFFF000);
	o.unhandled |= (inst[3] != 0);

	o.print("unk: set address [%08X %08X]", inst[1], inst[2]);
}

using Text = void (*)(Out &o, const uint32_t *inst);

const std::array<Text, 0x200> &
texts()
{
	static const struct {
		uint32_t first, last;
		Text text;
	} list[] = {
		{ 0x000, 0x000, text_nop },
		{ 0x003, 0x003, text_viewport_recall },
		{ 0x004, 0x004, text_viewport_commit },
		{ 0x005, 0x005, text_lod_test_sphere },
		{ 0x006, 0x006, text_unknown_006 },
		{ 0x011, 0x011, text_viewport_ambient },
		{ 0x012, 0x012, text_jump },
		{ 0x021, 0x021, text_viewport_set },
		{ 0x043, 0x043, text_lightset_recall },
		{ 0x046, 0x046, text_unknown_046 },
		{ 0x051, 0x051, text_light_color },
		{ 0x052, 0x052, text_call },
		{ 0x055, 0x055, text_lod_radius },
		{ 0x061, 0x061, text_light_attenuation },
		{ 0x064, 0x064, text_lightset_commit },
		{ 0x081, 0x081, text_material_flags },
		{ 0x082, 0x082, text_return },
		{ 0x083, 0x083, text_material_recall },
		{ 0x084, 0x084, text_material_commit },
		{ 0x088, 0x088, text_unknown_088 },
		{ 0x091, 0x091, text_material_color },
		{ 0x095, 0x095, text_lod_compare },
		{ 0x0C1, 0x0C1, text_texhead_set },
		{ 0x0C3, 0x0C3, text_texhead_recall },
		{ 0x0C4, 0x0C4, text_texhead_commit },
		{ 0x0D1, 0x0D1, text_texcoord_offset },
		{ 0x0E8, 0x0E9, text_push_texcoords3 },
		{ 0x101, 0x101, text_mesh_set },
		{ 0x103, 0x103, text_polygon_type },
		{ 0x104, 0x104, text_light_commit },
		{ 0x113, 0x113, text_polygon_type },
		{ 0x12C, 0x12F, text_push_position_static },
		{ 0x154, 0x154, text_alpha_table },
		{ 0x158, 0x15B, text_push_texcoords },
		{ 0x161, 0x161, text_vector },
		{ 0x181, 0x181, text_framebuffer },
		{ 0x191, 0x191, text_viewport_clear },
		{ 0x194, 0x194, text_light_table },
		{ 0x1A1, 0x1A1, text_unknown_1A1 },
		{ 0x1AC, 0x1AF, text_push_position },
		{ 0x1B8, 0x1BF, text_push_all },
		{ 0x1C2, 0x1C2, text_end },
	};
	static const std::array<Text, 0x200> table = [] {
		std::array<Text, 0x200> t{};

		for (const auto &e : list)
			for (uint32_t op = e.first; op <= e.last; op++)
				t[op] = e.text;
		return t;
	}();

	return table;
}

} /* namespace */

std::string
antarctic_disassemble(uint32_t pc, const uint32_t inst[8], bool in_mesh,
                      bool &unhandled)
{
	const Text text = texts()[inst[0] & 0x1FF];
	const uint32_t words = insn_size(inst[0]) / 4;
	char line[512], *tmp = line;
	Out o{};

	o.pc = pc;
	if (text)
		text(o, inst);
	else
		o.unknown();
	unhandled = o.unhandled;

	tmp += std::sprintf(tmp, "CP @%08X : ", pc);
	for (uint32_t i = 0; i < 8; i++) {
		if (i < words)
			tmp += std::sprintf(tmp, "%08X ", inst[i]);
		else
			tmp += std::sprintf(tmp, "........ ");
	}
	std::sprintf(tmp, "%c %c %s", o.unhandled ? '!' : ' ', in_mesh ? 'M' : ' ', o.text);
	return line;
}

} /* namespace odin */
