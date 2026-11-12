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

#include "odin/machine/cpu/arm7/arm7_host.hpp"

#include <cstring>

namespace odin {

#if defined(ODIN_HOST_X86_64)

namespace {

constexpr uint32_t SIZE = 8u << 20;
/* The CPSR: the bits of the flags, of the masks of FIQ (F) and IRQ (I) */
constexpr unsigned BIT_N = 31, BIT_Z = 30, BIT_C = 29, BIT_V = 28, BIT_I = 7, BIT_F = 6;
constexpr uint32_t FLAG_N = 1u << BIT_N, FLAG_Z = 1u << BIT_Z, FLAG_C = 1u << BIT_C, FLAG_V = 1u << BIT_V;

} /* namespace */

Arm7Host::Arm7Host(const Places &places, Link link)
	: HostCode(SIZE), m_places(places), m_link(link), m_follow(0), m_block(0), m_enter(nullptr)
{
	if (usable()) {
		clear();
	}
}

/* The way in keeps the CPU in rbx, the cycles spent in r12d, the cycles
 * to spend in r14d, the memory of the ARM7 in r13 and the instructions run
 * in r15, which a call leaves alone, and jumps to the block; the way back
 * adds those instructions to the CPU and returns the cycles spent. Then
 * the way on to the block at PC, for every block to jump to. */
void Arm7Host::clear()
{
	way_in();
	move32(R12, ARG[2]);
	move32(R14, ARG[3]);
	byte(0x4C);					/* mov r13, [the memory] */
	field(0x8B, R13 & 7, m_places.ram_base);
	bytes({0x45, 0x31, 0xFF});			/* xor r15d, r15d */
	way_in_end();
	byte(0x4C);					/* add [instructions], r15 */
	field(0x01, R15 & 7, m_places.instructions);
	move32(RAX, R12);
	way_out();

	/* The instructions run so far in the CPU, for the link to count */
	m_follow = m_used;
	byte(0x4C);					/* add [instructions], r15 */
	field(0x01, R15 & 7, m_places.instructions);
	bytes({0x45, 0x31, 0xFF});			/* xor r15d, r15d */
	bytes({0x48, 0x89, static_cast<uint8_t>(0xC0 | (RBX << 3) | ARG[0])});	/* mov the first argument, rbx */
	call_to(reinterpret_cast<uintptr_t>(m_link));
	bytes({0x48, 0x85, 0xC0, 0x0F, JZ});		/* test rax, rax / jz back */
	jump_to(m_leave);
	bytes({0xFF, 0xE0});				/* jmp rax */

	m_enter = reinterpret_cast<uint32_t (*)(Arm7 *, const uint8_t *, uint32_t, uint32_t)>(m_code);
}

uint8_t Arm7Host::host(Reg reg)
{
	static constexpr uint8_t REGS[] = { RAX, RCX, RDX, R8 };

	return REGS[static_cast<uint8_t>(reg)];
}

void Arm7Host::field(uint8_t opcode, uint8_t reg, int32_t place)
{
	if (reg >= 8) {
		byte(0x44);
	}
	bytes({opcode, static_cast<uint8_t>(0x83 | ((reg & 7) << 3))});	/* [rbx + place] */
	word(static_cast<uint32_t>(place));
}

void Arm7Host::store_value(int32_t place, uint32_t value)
{
	field(0xC7, 0, place);				/* mov dword [place], value */
	word(value);
}

void Arm7Host::count()
{
	bytes({0x49, 0xFF, 0xC7});			/* inc r15 */
}

/* Back to C++ at pc + 4 when the cycles are all spent */
void Arm7Host::cycles_spent(uint32_t pc)
{
	bytes({0x45, 0x39, 0xF4});			/* cmp r12d, r14d / jae out */
	m_exits.push_back({jump_if(JNC), pc + 4});
}

uint32_t Arm7Host::begin(uint32_t pc)
{
	m_block = pc;
	m_stale.clear();
	m_exits.clear();
	return m_used;
}

void Arm7Host::check(uint32_t at, uint32_t pc, uint32_t op)
{
	bytes({0x41, 0x81, 0xBD});			/* cmp dword [r13 + at], op */
	word(at);
	word(op);
	m_stale.push_back({jump_if(JNZ), pc});
}

HostCode::Jump Arm7Host::unless(uint32_t, uint16_t pass)
{
	field(0x8B, RAX, m_places.cpsr);		/* mov eax, [cpsr] */
	shift_ri(5, RAX, BIT_V);				/* shr eax, 28 */
	mov_ri(RCX, pass);
	bytes({0x0F, 0xA3, 0xC1});			/* bt ecx, eax */
	return jump_if(JNC);
}

void Arm7Host::call(uint32_t pc, uint32_t op, Run does)
{
	const Places &p = m_places;

	store_value(p.cycles, 0);
	store_value(p.op_pc, pc);
	store_value(p.pc, pc + 4);
	bytes({0x48, 0x89, static_cast<uint8_t>(0xC0 | (RBX << 3) | ARG[0])});	/* mov the first argument, rbx */
	mov_ri(ARG[1], op);
	call_to(reinterpret_cast<uintptr_t>(does));
	field(0x03, R12, p.cycles);			/* add r12d, [cycles] */
	count();

	/* An interrupt to take: FIQ or IRQ raised and not masked */
	bytes({0x0F});					/* movzx eax, byte [fiq_line] */
	field(0xB6, RAX, p.fiq_line);
	bytes({0x0F});					/* movzx ecx, byte [irq_line] */
	field(0xB6, RCX, p.irq_line);
	shift_ri(4, RAX, BIT_F);
	shift_ri(4, RCX, BIT_I);
	op_rr(0x09, RAX, RCX);
	field(0x8B, RCX, p.cpsr);
	not_r(RCX);
	op_rr(0x85, RAX, RCX);
	bytes({0x0F, JNZ});
	jump_to(m_leave);

	bytes({0x45, 0x39, 0xF4, 0x0F, JNC});		/* cmp r12d, r14d / jae back */
	jump_to(m_leave);
	field(0x81, 7, p.pc);				/* cmp dword [pc], pc + 4 */
	word(pc + 4);
	bytes({0x0F, JNZ});
	jump_to(m_follow);
}

void Arm7Host::spent(uint32_t pc, uint32_t cycles)
{
	op_ri(0, R12, cycles);
	count();
	cycles_spent(pc);
}

void Arm7Host::went(uint32_t cycles)
{
	op_ri(0, R12, cycles);
	count();
	bytes({0x45, 0x39, 0xF4, 0x0F, JNC});		/* cmp r12d, r14d / jae back */
	jump_to(m_leave);
	jump_back(m_follow);
}

uint32_t Arm7Host::branch(uint32_t pc, uint32_t target, bool link, uint32_t direct)
{
	if (link) {
		store_value(m_places.r + 4 * 14, pc + 4);
	}
	op_ri(0, R12, 3);
	count();
	store_value(m_places.pc, target);
	bytes({0x45, 0x39, 0xF4, 0x0F, JNC});		/* cmp r12d, r14d / jae back */
	jump_to(m_leave);
	field(0x80, 7, m_places.kept);			/* cmp byte [kept], 0 / jnz follow */
	byte(0);
	bytes({0x0F, JNZ});
	jump_to(m_follow);
	/* The way on until the block of the target is written */
	const uint32_t at = m_used + 1;
	jump_back(direct ? direct : m_follow);
	return direct ? 0 : at;
}

void Arm7Host::link_to(uint32_t at, uint32_t place)
{
	const uint32_t distance = place - (at + 4);

	std::memcpy(m_code + at, &distance, 4);
}

void Arm7Host::failed(Jump skip, uint32_t pc)
{
	const Jump on = skip_ahead();			/* jmp on */

	bind(skip);
	op_ri(0, R12, 1);
	byte(0x48);					/* inc qword [failed] */
	field(0xFF, 0, m_places.failed);
	cycles_spent(pc);
	bind(on);
}

uint32_t Arm7Host::end(uint32_t next, uint32_t direct)
{
	store_value(m_places.pc, next);
	field(0x80, 7, m_places.kept);			/* cmp byte [kept], 0 / jnz follow */
	byte(0);
	bytes({0x0F, JNZ});
	jump_to(m_follow);
	const uint32_t at = m_used + 1;
	jump_back(direct ? direct : m_follow);
	for (const Out &out : m_stale) {
		bind(out.jump);
		store_value(m_places.pc, out.pc);
		store_value(m_places.stale_block, m_block);
		field(0xC6, 0, m_places.stale);		/* mov byte [stale], 1 */
		byte(1);
		jump_back(m_leave);
	}
	for (const Out &out : m_exits) {
		bind(out.jump);
		store_value(m_places.pc, out.pc);
		jump_back(m_leave);
	}
	return direct ? 0 : at;
}

void Arm7Host::load(Reg reg, uint32_t n)
{
	field(0x8B, host(reg), m_places.r + static_cast<int32_t>(4 * n));
}

void Arm7Host::load_value(Reg reg, uint32_t value)
{
	mov_ri(host(reg), value);
}

void Arm7Host::store(uint32_t n, Reg reg)
{
	field(0x89, host(reg), m_places.r + static_cast<int32_t>(4 * n));
}

void Arm7Host::store_pc(Reg reg)
{
	field(0x89, host(reg), m_places.pc);
}

void Arm7Host::copy(Reg dst, Reg src)
{
	op_rr(0x89, host(dst), host(src));
}

void Arm7Host::alu(Alu op, Reg dst, Reg src)
{
	static constexpr uint8_t CODE[] = { 0x01, 0x11, 0x29, 0x19, 0x21, 0x09, 0x31 };

	op_rr(CODE[static_cast<uint8_t>(op)], host(dst), host(src));
}

void Arm7Host::invert(Reg reg)
{
	not_r(host(reg));
}

void Arm7Host::shift(Shift kind, Reg reg, uint32_t amount)
{
	static constexpr uint8_t EXT[] = { 4, 5, 7, 1 };	/* shl, shr, sar, ror */

	shift_ri(EXT[static_cast<uint8_t>(kind)], host(reg), static_cast<uint8_t>(amount));
}

void Arm7Host::rotate_through_carry(Reg reg)
{
	shift_ri(3, host(reg), 1);			/* rcr reg, 1 */
}

void Arm7Host::carry_from_c(bool inverted)
{
	bytes({0x0F});					/* bt dword [cpsr], 29 */
	field(0xBA, 4, m_places.cpsr);
	byte(BIT_C);
	if (inverted) {
		byte(0xF5);				/* cmc */
	}
}

void Arm7Host::keep_carry(Reg reg)
{
	flag_r(2, host(reg));
}

void Arm7Host::flags_arithmetic(Reg result, bool borrow)
{
	const uint8_t res = host(result);

	flag_r(2, RDX);					/* C */
	flag_r(0, R9);					/* V */
	if (borrow) {
		op_ri(6, RDX, 1);
	}
	shift_ri(4, RDX, BIT_C);
	shift_ri(4, R9, BIT_V);
	op_rr(0x09, RDX, R9);
	op_rr(0x89, R9, res);				/* N */
	op_ri(4, R9, FLAG_N);
	op_rr(0x09, RDX, R9);
	op_rr(0x85, res, res);				/* Z */
	flag_r(4, R9);
	shift_ri(4, R9, BIT_Z);
	op_rr(0x09, RDX, R9);
	field(0x8B, R10, m_places.cpsr);
	op_ri(4, R10, ~(FLAG_N | FLAG_Z | FLAG_C | FLAG_V));
	op_rr(0x09, R10, RDX);
	field(0x89, R10, m_places.cpsr);
}

void Arm7Host::flags_logical(Reg result, const Reg *carry)
{
	const uint8_t res = host(result);

	op_rr(0x89, RDX, res);				/* N */
	op_ri(4, RDX, FLAG_N);
	op_rr(0x85, res, res);				/* Z */
	flag_r(4, R9);
	shift_ri(4, R9, BIT_Z);
	op_rr(0x09, RDX, R9);
	if (carry) {
		op_rr(0x89, R9, host(*carry));
		shift_ri(4, R9, BIT_C);
		op_rr(0x09, RDX, R9);
	}
	field(0x8B, R10, m_places.cpsr);
	op_ri(4, R10, carry ? ~(FLAG_N | FLAG_Z | FLAG_C) : ~(FLAG_N | FLAG_Z));
	op_rr(0x09, R10, RDX);
	field(0x89, R10, m_places.cpsr);
}

void Arm7Host::flags_logical_value(Reg result, bool carry)
{
	const uint8_t res = host(result);

	op_rr(0x89, RDX, res);				/* N */
	op_ri(4, RDX, FLAG_N);
	op_rr(0x85, res, res);				/* Z */
	flag_r(4, R9);
	shift_ri(4, R9, BIT_Z);
	op_rr(0x09, RDX, R9);
	if (carry) {
		op_ri(1, RDX, FLAG_C);
	}
	field(0x8B, R10, m_places.cpsr);
	op_ri(4, R10, ~(FLAG_N | FLAG_Z | FLAG_C));
	op_rr(0x09, R10, RDX);
	field(0x89, R10, m_places.cpsr);
}

void Arm7Host::mask(Reg reg, uint32_t value)
{
	op_ri(4, host(reg), value);
}

void Arm7Host::add_value(Reg reg, uint32_t value)
{
	op_ri(0, host(reg), value);
}

HostCode::Jump Arm7Host::unless_below(Reg reg, uint32_t limit)
{
	op_ri(7, host(reg), limit);			/* cmp reg, limit / jae */
	return jump_if(JNC);
}

/* [r13 + r8] */
void Arm7Host::ram_load(Reg dst, bool one_byte)
{
	const uint8_t reg = static_cast<uint8_t>(host(dst) & 7);

	if (one_byte) {
		bytes({0x43, 0x0F, 0xB6});		/* movzx reg, byte [r13 + r8] */
	} else {
		bytes({0x43, 0x8B});			/* mov reg, [r13 + r8] */
	}
	bytes({static_cast<uint8_t>(0x44 | (reg << 3)), 0x05, 0x00});
}

void Arm7Host::ram_store(Reg src, bool one_byte)
{
	const uint8_t reg = static_cast<uint8_t>(host(src) & 7);

	bytes({0x43, static_cast<uint8_t>(one_byte ? 0x88 : 0x89)});	/* mov [r13 + r8], reg */
	bytes({static_cast<uint8_t>(0x44 | (reg << 3)), 0x05, 0x00});
}

void Arm7Host::rotate_by_b(Reg reg)
{
	const uint8_t r = host(reg);

	if (r >= 8) {
		byte(0x41);
	}
	bytes({0xD3, static_cast<uint8_t>(0xC8 | (r & 7))});	/* ror reg, cl */
}

HostCode::Jump Arm7Host::skip_ahead()
{
	byte(0xE9);
	m_used += 4;
	return Jump{m_used - 4};
}

#elif !defined(ODIN_HOST_AARCH64) /* a host no file writes code for */

Arm7Host::Arm7Host(const Places &places, Link link)
	: HostCode(0), m_places(places), m_link(link), m_follow(0), m_block(0), m_enter(nullptr)
{
}

void Arm7Host::clear() {}
uint8_t Arm7Host::host(Reg) { return 0; }
void Arm7Host::field(uint8_t, uint8_t, int32_t) {}
void Arm7Host::store_value(int32_t, uint32_t) {}
void Arm7Host::count() {}
void Arm7Host::cycles_spent(uint32_t) {}
uint32_t Arm7Host::begin(uint32_t) { return 0; }
void Arm7Host::check(uint32_t, uint32_t, uint32_t) {}
HostCode::Jump Arm7Host::unless(uint32_t, uint16_t) { return Jump{0}; }
void Arm7Host::call(uint32_t, uint32_t, Run) {}
void Arm7Host::spent(uint32_t, uint32_t) {}
void Arm7Host::went(uint32_t) {}
uint32_t Arm7Host::branch(uint32_t, uint32_t, bool, uint32_t) { return 0; }
void Arm7Host::link_to(uint32_t, uint32_t) {}
void Arm7Host::failed(Jump, uint32_t) {}
uint32_t Arm7Host::end(uint32_t, uint32_t) { return 0; }
void Arm7Host::load(Reg, uint32_t) {}
void Arm7Host::load_value(Reg, uint32_t) {}
void Arm7Host::store(uint32_t, Reg) {}
void Arm7Host::store_pc(Reg) {}
void Arm7Host::copy(Reg, Reg) {}
void Arm7Host::alu(Alu, Reg, Reg) {}
void Arm7Host::invert(Reg) {}
void Arm7Host::shift(Shift, Reg, uint32_t) {}
void Arm7Host::rotate_through_carry(Reg) {}
void Arm7Host::carry_from_c(bool) {}
void Arm7Host::keep_carry(Reg) {}
void Arm7Host::flags_arithmetic(Reg, bool) {}
void Arm7Host::flags_logical(Reg, const Reg *) {}
void Arm7Host::flags_logical_value(Reg, bool) {}
void Arm7Host::mask(Reg, uint32_t) {}
void Arm7Host::add_value(Reg, uint32_t) {}
HostCode::Jump Arm7Host::unless_below(Reg, uint32_t) { return Jump{0}; }
void Arm7Host::ram_load(Reg, bool) {}
void Arm7Host::ram_store(Reg, bool) {}
void Arm7Host::rotate_by_b(Reg) {}
HostCode::Jump Arm7Host::skip_ahead() { return Jump{0}; }

#endif

} /* namespace odin */
