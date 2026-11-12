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

#include "odin/machine/chips/pmx1/pmx1_host.hpp"
#include "odin/machine/chips/pmx1/pmx1_core.hpp"

#include <cstring>

namespace odin {

#if defined(ODIN_HOST_X86_64)

namespace {

constexpr uint32_t SIZE = 4u << 20;
/* jump if above (unsigned), the second byte of 0F 87 */
constexpr uint8_t JA = 0x87;

} /* namespace */

Pmx1Host::Pmx1Host(const Places &places, Link link)
	: HostCode(SIZE), m_places(places), m_link(link), m_follow(0), m_rank(0), m_enter(nullptr)
{
	if (usable())
		clear();
}

/* The way in keeps the core in rbx, the ops run in r12d, the most to run
 * in r14d and video RAM in r13, which a call leaves alone, and jumps to
 * the block; the way back returns the ops run. Then the way on to the
 * block at the next address, for every block to jump to. */
void Pmx1Host::clear()
{
	way_in();
	move32(R14, ARG[2]);
	/* mov r13, the fourth argument */
	bytes({static_cast<uint8_t>(0x49 | (ARG[3] >= 8 ? 4 : 0)), 0x89,
	       static_cast<uint8_t>(0xC0 | ((ARG[3] & 7) << 3) | (R13 & 7))});
	bytes({0x45, 0x31, 0xE4});			/* xor r12d, r12d */
	way_in_end();
	move32(RAX, R12);
	way_out();

	m_follow = m_used;
	bytes({0x48, 0x89, static_cast<uint8_t>(0xC0 | (RBX << 3) | ARG[0])});	/* mov the first argument, rbx */
	call_to(reinterpret_cast<uintptr_t>(m_link));
	bytes({0x48, 0x85, 0xC0, 0x0F, JZ});		/* test rax, rax / jz back */
	jump_to(m_leave);
	bytes({0xFF, 0xE0});				/* jmp rax */

	m_enter = reinterpret_cast<uint32_t (*)(Pmx1Core *, const uint8_t *, uint32_t, const uint8_t *)>(m_code);
}

uint32_t Pmx1Host::run(Pmx1Core &core, uint32_t place, uint32_t cycles, const uint8_t *vram) const
{
	ready();
	return m_enter(&core, m_code + place, cycles, vram);
}

void Pmx1Host::field(uint8_t opcode, uint8_t reg, int32_t place)
{
	if (reg >= 8)
		byte(0x44);
	bytes({opcode, static_cast<uint8_t>(0x83 | ((reg & 7) << 3))});	/* [rbx + place] */
	word(static_cast<uint32_t>(place));
}

/* Back to C++ when fewer ops are left to run than the block has: the next
 * address is already set */
uint32_t Pmx1Host::begin(uint32_t rank, uint32_t count)
{
	const uint32_t place = m_used;

	m_rank = rank;
	m_stale.clear();
	m_done.clear();
	m_slow.clear();
	move32(RAX, R14);
	op_rr(0x29, RAX, R12);				/* sub eax, r12d */
	op_ri(7, RAX, count);				/* cmp eax, count / jb back */
	bytes({0x0F, JC});
	jump_to(m_leave);
	return place;
}

void Pmx1Host::check(uint32_t offset, uint32_t value, uint32_t k)
{
	bytes({0x41, 0x81, 0xBD});			/* cmp dword [r13 + offset], value */
	word(offset);
	word(value);
	m_stale.push_back({jump_if(JNZ), k});
}

void Pmx1Host::call(uintptr_t does, const void *op, uint32_t k)
{
	bytes({0x48, 0x89, static_cast<uint8_t>(0xC0 | (RBX << 3) | ARG[0])});	/* mov the first argument, rbx */
	/* mov the second, op */
	bytes({static_cast<uint8_t>(0x48 | (ARG[1] >= 8 ? 1 : 0)), static_cast<uint8_t>(0xB8 | (ARG[1] & 7))});
	quad(reinterpret_cast<uintptr_t>(op));
	op_rr(0x31, ARG[2], ARG[2]);			/* the third, thread 0 */
	call_to(does);
	op_rr(0x85, RAX, RAX);				/* test eax, eax / jnz back */
	m_done.push_back({jump_if(JNZ), k});
}

HostCode::Jump Pmx1Host::unless(uint16_t pass)
{
	field(0x8B, RAX, m_places.flags);		/* mov eax, [flags] */
	mov_ri(RCX, pass);
	bytes({0x0F, 0xA3, 0xC1});			/* bt ecx, eax */
	return jump_if(JNC);
}

void Pmx1Host::load(int32_t place)
{
	field(0x8B, RAX, place);
}

void Pmx1Host::load_operand(int32_t place)
{
	field(0x8B, RCX, place);
}

void Pmx1Host::store(int32_t place)
{
	field(0x89, RAX, place);
}

void Pmx1Host::store_value(int32_t place, uint32_t value)
{
	field(0xC7, 0, place);				/* mov dword [place], value */
	word(value);
}

void Pmx1Host::operate(uint32_t op, uint32_t value)
{
	static constexpr uint8_t EXT[] = { 0, 5, 4, 1, 6, 4, 5 };	/* add sub and or xor, shl shr */

	if (op >= 5)
		shift_ri(EXT[op], RAX, static_cast<uint8_t>(value));
	else
		op_ri(EXT[op], RAX, value);
}

void Pmx1Host::operate_regs(uint32_t op)
{
	static constexpr uint8_t CODE[] = { 0x01, 0x29, 0x21, 0x09, 0x31 };	/* add sub and or xor */

	if (op >= 5)
		bytes({0xD3, static_cast<uint8_t>(op == 5 ? 0xE0 : 0xE8)});	/* shl or shr eax, cl */
	else
		op_rr(CODE[op], RAX, RCX);
}

void Pmx1Host::compare(uint32_t op, uint32_t value)
{
	if (op == 1) {
		op_ri(7, RAX, value);			/* cmp eax, value */
	} else {
		byte(0xA9);				/* test eax, value */
		word(value);
	}
}

void Pmx1Host::compare_regs(uint32_t op)
{
	op_rr(op == 1 ? 0x39 : 0x85, RAX, RCX);		/* cmp or test eax, ecx */
}

/* The core's flags: N 8, Z 4, C 2, V 1 */
void Pmx1Host::flags(uint32_t op)
{
	if (op >= 5)
		op_rr(0x85, RAX, RAX);			/* test eax, eax: a shift by 0 sets none */
	flag_r(8, R9);					/* N */
	flag_r(4, R10);					/* Z */
	if (op <= 1) {
		flag_r(2, R8);				/* C, or the borrow */
		flag_r(0, RDX);				/* V */
		if (op == 1)
			op_ri(6, R8, 1);
		bytes({0x42, 0x8D, 0x14, 0x42});	/* lea edx, [rdx + r8 * 2] */
	} else if (op <= 4) {
		field(0x8B, RDX, m_places.flags);	/* C and V kept */
		op_ri(4, RDX, 3);
	} else {
		op_rr(0x31, RDX, RDX);
	}
	bytes({0x42, 0x8D, 0x14, 0x92});		/* lea edx, [rdx + r10 * 4] */
	bytes({0x42, 0x8D, 0x14, 0xCA});		/* lea edx, [rdx + r9 * 8] */
	field(0x89, RDX, m_places.flags);
}

void Pmx1Host::swap(int32_t a, int32_t b)
{
	field(0x8B, RAX, a);
	field(0x8B, RCX, b);
	field(0x89, RCX, a);
	field(0x89, RAX, b);
}

HostCode::Jump Pmx1Host::unless_count()
{
	op_ri(7, RCX, 31);				/* cmp ecx, 31 / ja */
	return jump_if(JA);
}

void Pmx1Host::otherwise(Jump odd, uintptr_t does, const void *op, uint32_t k)
{
	m_slow.push_back({{odd}, does, op, k, m_used});
}

void Pmx1Host::field_indexed(uint8_t opcode, uint8_t reg, uint8_t index, uint8_t scale, int32_t place)
{
	static constexpr uint8_t SCALE[] = { 0, 0, 1, 0, 2, 0, 0, 0, 3 };

	if (reg >= 8)
		byte(0x44);
	bytes({opcode, static_cast<uint8_t>(0x84 | ((reg & 7) << 3)),
	       static_cast<uint8_t>(SCALE[scale] << 6 | (index & 7) << 3 | RBX)});	/* [rbx + index * scale + place] */
	word(static_cast<uint32_t>(place));
}

HostCode::Jump Pmx1Host::jump_ahead()
{
	byte(0xE9);
	m_used += 4;
	return Jump{m_used - 4};
}

/* As Pmx1Core::frame_buffer(): the top 6 bits FB_OFFSET, aligned, inside */
void Pmx1Host::in_vram(std::vector<Jump> &jumps, uint32_t room)
{
	move32(RDX, RCX);
	shift_ri(5, RDX, 26);
	field(0x8B, R8, m_places.fb_offset);
	op_ri(4, R8, 0x3F);
	op_rr(0x39, RDX, R8);				/* cmp edx, r8d */
	jumps.push_back(jump_if(JNZ));
	bytes({0xF7, 0xC1});				/* test ecx, 3 */
	word(3);
	jumps.push_back(jump_if(JNZ));
	op_ri(4, RCX, 0x03FFFFFF);
	op_ri(7, RCX, m_places.vram_size - room);	/* cmp ecx, last / ja */
	jumps.push_back(jump_if(JA));
}

void Pmx1Host::wimp(int32_t value, uintptr_t does, const void *op, uint32_t k)
{
	std::vector<Jump> odd;

	field(0x8B, RAX, value);
	move32(RDX, RAX);				/* edx = the register, bits 25-16 */
	shift_ri(5, RDX, 16);
	op_ri(4, RDX, 0x3FF);
	move32(RCX, RAX);
	shift_ri(5, RCX, 30);
	op_ri(7, RCX, 1);				/* a write: bits 31-30 = 01 */
	const Jump read = jump_if(JNZ);

	field_indexed(0x80, 7, RDX, 1, m_places.plain);	/* cmp byte [plain + edx], 0 */
	byte(0);
	odd.push_back(jump_if(JZ));
	bytes({0x0F, 0xB7, 0xC0});			/* movzx eax, ax */
	field_indexed(0x89, RAX, RDX, 4, m_places.implicit);
	const Jump done = jump_ahead();

	bind(read);
	byte(0xA9);					/* test eax, 0xFC000000: a read, bits 31-26 clear */
	word(0xFC000000);
	odd.push_back(jump_if(JNZ));
	op_ri(4, RAX, 0xFFFF0000);
	field_indexed(0x0B, RAX, RDX, 4, m_places.implicit);	/* or eax, [implicit + edx * 4] */
	field(0x89, RAX, m_places.answer);
	bind(done);
	m_slow.push_back({odd, does, op, k, m_used});
}

void Pmx1Host::store_word(int32_t address, uint32_t add, bool back, int32_t value, uintptr_t does, const void *op,
                          uint32_t k)
{
	std::vector<Jump> odd;

	field(0x8B, RCX, address);
	if (add)
		op_ri(0, RCX, add);
	move32(R11, RCX);				/* r11d = the address, ecx its offset */
	in_vram(odd, 4);
	move32(RDX, RCX);
	shift_ri(5, RDX, 12);
	bytes({0x49, 0xB9});				/* mov r9, pages */
	quad(reinterpret_cast<uintptr_t>(m_places.pages));
	bytes({0x41, 0x80, 0x3C, 0x11, 0x00});		/* cmp byte [r9 + rdx], 0 */
	odd.push_back(jump_if(JZ));
	move32(RDX, RCX);
	shift_ri(5, RDX, 6);
	bytes({0x49, 0xBA});				/* mov r10, lines */
	quad(reinterpret_cast<uintptr_t>(m_places.lines));
	bytes({0x41, 0x80, 0x3C, 0x12, 0x00});		/* cmp byte [r10 + rdx], 0 */
	odd.push_back(jump_if(JNZ));
	if (back)
		field(0x89, R11, address);
	field(0x8B, RAX, value);
	bytes({0x41, 0x89, 0x44, 0x0D, 0x00});		/* mov [r13 + rcx], eax */
	m_slow.push_back({odd, does, op, k, m_used});
}

void Pmx1Host::getd(int32_t address, uint32_t add, bool back, int32_t to, uintptr_t does, const void *op, uint32_t k)
{
	std::vector<Jump> odd;

	field(0x83, 7, m_places.queue_size);		/* cmp dword [size], QUEUE / jae */
	byte(static_cast<uint8_t>(Pmx1Core::QUEUE));
	odd.push_back(jump_if(JNC));
	field(0x8B, RCX, address);
	if (add)
		op_ri(0, RCX, add);
	move32(R11, RCX);
	in_vram(odd, 8);
	if (back)
		field(0x89, R11, address);
	/* Queued after the others, then the oldest taken: the queue as long */
	field(0x8B, RDX, m_places.queue_first);
	field(0x03, RDX, m_places.queue_size);
	op_ri(4, RDX, Pmx1Core::QUEUE - 1);
	bytes({0x45, 0x8B, 0x44, 0x0D, 0x00});		/* mov r8d, [r13 + rcx] */
	field_indexed(0x89, R8, RDX, 8, m_places.queued);
	bytes({0x45, 0x8B, 0x44, 0x0D, 0x04});		/* mov r8d, [r13 + rcx + 4] */
	field_indexed(0x89, R8, RDX, 8, m_places.queued + 4);
	field(0x8B, RAX, m_places.queue_first);
	field_indexed(0x8B, RDX, RAX, 8, m_places.queued);
	op_ri(0, RAX, 1);
	op_ri(4, RAX, Pmx1Core::QUEUE - 1);
	field(0x89, RAX, m_places.queue_first);
	field(0x89, RDX, to);
	m_slow.push_back({odd, does, op, k, m_used});
}

void Pmx1Host::take(int32_t to, uint16_t pass, uintptr_t does, const void *op, uint32_t k)
{
	std::vector<Jump> odd;

	field(0x8B, RCX, m_places.queue_size);
	op_rr(0x85, RCX, RCX);
	odd.push_back(jump_if(JZ));
	field(0x8B, RAX, m_places.queue_first);
	field_indexed(0x8B, RDX, RAX, 8, m_places.queued);	/* edx = its first word */
	op_ri(0, RAX, 1);
	op_ri(4, RAX, Pmx1Core::QUEUE - 1);
	field(0x89, RAX, m_places.queue_first);
	op_ri(5, RCX, 1);
	field(0x89, RCX, m_places.queue_size);
	/* It pops even when its condition fails */
	const Jump skip = pass != 0xFFFF ? unless(pass) : Jump{0};

	field(0x89, RDX, to);
	if (pass != 0xFFFF)
		bind(skip);
	m_slow.push_back({odd, does, op, k, m_used});
}

void Pmx1Host::queue_read(uintptr_t does, const void *op, uint32_t k)
{
	std::vector<Jump> odd;

	field(0x83, 7, m_places.queue_size);		/* cmp dword [size], QUEUE / jae */
	byte(static_cast<uint8_t>(Pmx1Core::QUEUE));
	odd.push_back(jump_if(JNC));
	move32(RCX, RAX);
	in_vram(odd, 8);
	field(0x8B, RDX, m_places.queue_first);
	field(0x03, RDX, m_places.queue_size);		/* add edx, [size] */
	op_ri(4, RDX, Pmx1Core::QUEUE - 1);
	bytes({0x45, 0x8B, 0x44, 0x0D, 0x00});		/* mov r8d, [r13 + rcx] */
	field_indexed(0x89, R8, RDX, 8, m_places.queued);
	bytes({0x45, 0x8B, 0x44, 0x0D, 0x04});		/* mov r8d, [r13 + rcx + 4] */
	field_indexed(0x89, R8, RDX, 8, m_places.queued + 4);
	field(0x83, 0, m_places.queue_size);		/* add dword [size], 1 */
	byte(1);
	m_slow.push_back({odd, does, op, k, m_used});
}

void Pmx1Host::branch(uint32_t target, uint32_t slots, uintptr_t does, const void *op, uint32_t k)
{
	field(0x83, 7, m_places.slots);			/* cmp dword [slots], 0 / jnz the op called */
	byte(0);
	const Jump busy = jump_if(JNZ);

	store_value(m_places.slots, slots);
	store_value(m_places.target, target);
	otherwise(busy, does, op, k);
}

void Pmx1Host::jump_on(uint32_t direct, uint32_t &at)
{
	at = direct ? 0 : m_used + 1;
	jump_back(direct ? direct : m_follow);
}

/* Then the ways out: the ops called on their odd paths, and back to C++
 * with the op left at, in ecx */
void Pmx1Host::end(uint32_t count, bool jumps, uint32_t slots, bool known, bool always, uint32_t taken,
                   uint32_t taken_direct, uint32_t next, uint32_t next_direct, uint32_t &taken_at, uint32_t &next_at)
{
	op_ri(0, R12, count);				/* add r12d, count */
	taken_at = next_at = 0;
	if (jumps) {
		Jump fall{0};

		if (!always) {
			field(0x83, 7, m_places.slots);	/* cmp dword [slots], 0 / jz fall */
			byte(0);
			fall = jump_if(JZ);
		}
		store_value(m_places.slots, slots);
		if (slots) {
			store_value(m_places.next, next);
			jump_back(m_leave);
		} else if (known) {
			store_value(m_places.next, taken);
			jump_on(taken_direct, taken_at);
		} else {
			field(0x8B, RAX, m_places.target);
			field(0x89, RAX, m_places.next);
			jump_back(m_follow);
		}
		if (!always)
			bind(fall);
	}
	if (!jumps || !always) {
		store_value(m_places.next, next);
		jump_on(next_direct, next_at);
	}

	for (const Slow &slow : m_slow) {
		for (const Jump jump : slow.jumps)
			bind(jump);
		call(slow.does, slow.op, slow.k);
		jump_back(slow.resume);
	}
	const uint32_t stale = m_used;

	store_value(m_places.exit_block, m_rank);
	store_value(m_places.exit_stale, 1);
	field(0x89, RCX, m_places.exit_op);
	jump_back(m_leave);

	const uint32_t done = m_used;

	store_value(m_places.exit_block, m_rank);
	field(0x89, RAX, m_places.exit_done);
	field(0x89, RCX, m_places.exit_op);
	jump_back(m_leave);
	for (const Out &out : m_stale) {
		bind(out.jump);
		mov_ri(RCX, out.k);
		jump_back(stale);
	}
	for (const Out &out : m_done) {
		bind(out.jump);
		mov_ri(RCX, out.k);
		jump_back(done);
	}
}

void Pmx1Host::link_to(uint32_t at, uint32_t place)
{
	const uint32_t distance = place - (at + 4);

	std::memcpy(m_code + at, &distance, 4);
}

#else /* a host this file does not write code for */

Pmx1Host::Pmx1Host(const Places &, Link) : HostCode(0) {}
void Pmx1Host::clear() {}
uint32_t Pmx1Host::run(Pmx1Core &, uint32_t, uint32_t, const uint8_t *) const { return 0; }
uint32_t Pmx1Host::begin(uint32_t, uint32_t) { return 0; }
void Pmx1Host::check(uint32_t, uint32_t, uint32_t) {}
void Pmx1Host::call(uintptr_t, const void *, uint32_t) {}
HostCode::Jump Pmx1Host::unless(uint16_t) { return Jump{0}; }
void Pmx1Host::load(int32_t) {}
void Pmx1Host::load_operand(int32_t) {}
void Pmx1Host::store(int32_t) {}
void Pmx1Host::store_value(int32_t, uint32_t) {}
void Pmx1Host::operate(uint32_t, uint32_t) {}
void Pmx1Host::operate_regs(uint32_t) {}
void Pmx1Host::compare(uint32_t, uint32_t) {}
void Pmx1Host::compare_regs(uint32_t) {}
void Pmx1Host::flags(uint32_t) {}
void Pmx1Host::swap(int32_t, int32_t) {}
HostCode::Jump Pmx1Host::unless_count() { return Jump{0}; }
void Pmx1Host::otherwise(Jump, uintptr_t, const void *, uint32_t) {}
void Pmx1Host::branch(uint32_t, uint32_t, uintptr_t, const void *, uint32_t) {}
void Pmx1Host::wimp(int32_t, uintptr_t, const void *, uint32_t) {}
void Pmx1Host::store_word(int32_t, uint32_t, bool, int32_t, uintptr_t, const void *, uint32_t) {}
void Pmx1Host::getd(int32_t, uint32_t, bool, int32_t, uintptr_t, const void *, uint32_t) {}
void Pmx1Host::take(int32_t, uint16_t, uintptr_t, const void *, uint32_t) {}
void Pmx1Host::queue_read(uintptr_t, const void *, uint32_t) {}
void Pmx1Host::end(uint32_t, bool, uint32_t, bool, bool, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t &taken_at,
                   uint32_t &next_at)
{
	taken_at = next_at = 0;
}
void Pmx1Host::link_to(uint32_t, uint32_t) {}

#endif

} /* namespace odin */
