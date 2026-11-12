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

#include "odin/machine/cpu/sh4/sh4_host.hpp"

#include "odin/machine/cpu/sh4/sh4.hpp"


namespace odin {

#if defined(ODIN_HOST_X86_64)

namespace {

constexpr uint32_t SIZE = 32u << 20;

/* rbp points this many bytes into the registers of the CPU: the first 64
 * words are a byte of displacement away. */
constexpr uint32_t MIDDLE = 128;

/* x86: the registers a step names by number, and the registers that hold
 * the tables of pages. */
constexpr uint8_t EAX = 0, ECX = 1;
constexpr uint8_t READS = 5, WRITES = 6;	/* r13, r14 */

/* The register A of the steps, and what x86 calls the steps. */
constexpr uint8_t A = EAX;
constexpr uint8_t ALU[] = { 0, 5, 4, 1, 6, 7 };			/* add, sub, and, or, xor, cmp */
constexpr uint8_t SHIFT[] = { 4, 5, 7 };			/* shl, shr, sar */
constexpr uint8_t SET[] = { 0x94, 0x93, 0x97, 0x9D, 0x9F };	/* sete, setae, seta, setge, setg */

} /* namespace */

Sh4Host::Sh4Host(uint32_t *words, const Memory &memory, const Places &places)
	: HostCode(SIZE), m_words(words), m_memory(memory), m_places(places),
	  m_fused(host_has_fma()), m_enter(nullptr)
{
	if (usable()) {
		clear();
	}
}

/* The way in keeps the CPU in rbx, its registers in rbp and the tables of
 * its memory in r13, r14 and r15, all of which a call leaves alone as it
 * does r12, and jumps to the block; the way back to the run loop undoes it. */
void Sh4Host::clear()
{
	way_in();
	bytes({0x48, 0xBD});				/* mov rbp, the registers */
	quad(reinterpret_cast<uintptr_t>(m_words) + MIDDLE);
	bytes({0x49, 0xBD});				/* mov r13, the pages to read */
	quad(reinterpret_cast<uintptr_t>(m_memory.reads));
	bytes({0x49, 0xBE});				/* mov r14, the pages to write */
	quad(reinterpret_cast<uintptr_t>(m_memory.writes));
	bytes({0x49, 0xBF});				/* mov r15, the epochs of the code */
	quad(reinterpret_cast<uintptr_t>(m_memory.code_epochs));
	way_in_end();
	way_out();
	m_enter = reinterpret_cast<void (*)(Sh4 *, const uint8_t *)>(m_code);
}

uint32_t Sh4Host::begin(const Sh4Op *ops)
{
	const uint32_t place = m_used;

	bytes({0x49, 0xBC});				/* mov r12, ops */
	quad(reinterpret_cast<uintptr_t>(ops));
	return place;
}

void Sh4Host::call(Call fn, uint32_t op)
{
	const uint32_t offset = op * static_cast<uint32_t>(sizeof(Sh4Op));
	const uint8_t lea = static_cast<uint8_t>((ARG[1] << 3) | 4);	/* lea the second argument, [r12 + ...] */

	bytes({0x48, 0x89, static_cast<uint8_t>(0xC0 | (RBX << 3) | ARG[0])});	/* mov the first argument, rbx */
	if (offset < 128) {
		bytes({0x49, 0x8D, static_cast<uint8_t>(0x40 | lea), 0x24, static_cast<uint8_t>(offset)});
	} else {
		bytes({0x49, 0x8D, static_cast<uint8_t>(0x80 | lea), 0x24});
		word(offset);
	}
	call_to(reinterpret_cast<uintptr_t>(fn));
}

void Sh4Host::call_link(Link fn, uint32_t pc)
{
	bytes({0x48, 0x89, static_cast<uint8_t>(0xC0 | (RBX << 3) | ARG[0])});	/* mov the first argument, rbx */
	byte(static_cast<uint8_t>(0xB8 | ARG[1]));	/* mov the second argument, pc */
	word(pc);
	call_to(reinterpret_cast<uintptr_t>(fn));
}

/* The call does it all. */
void Sh4Host::link_fast(uint32_t) {}
void Sh4Host::leave_fast(uint32_t, int32_t) {}
void Sh4Host::enter_fast(bool, uint32_t, int32_t) {}

void Sh4Host::loop_if_told()
{
	bytes({0x48, 0x85, 0xC0, 0x0F, 0x85});		/* test rax, rax / jnz leave */
	jump_to(m_leave);
}

Sh4Host::Jump Sh4Host::jump_if_told()
{
	bytes({0x48, 0x85, 0xC0});			/* test rax, rax */
	return jump_if(JNZ);
}

void Sh4Host::go_where_told()
{
	bytes({0x48, 0x83, 0xF8, 0x01, 0x0F, 0x84});	/* cmp rax, 1 / je leave */
	jump_to(m_leave);
	bytes({0xFF, 0xE0});				/* jmp rax */
}

/* The word of the registers of the CPU as the operand of an instruction
 * that names `reg` too: [rbp + displacement]. */
void Sh4Host::operand(uint8_t reg, uint32_t rank)
{
	const int32_t away = static_cast<int32_t>(4 * rank) - static_cast<int32_t>(MIDDLE);

	if (away == static_cast<int8_t>(away)) {
		bytes({static_cast<uint8_t>(0x45 | (reg << 3)), static_cast<uint8_t>(away)});
	} else {
		byte(static_cast<uint8_t>(0x85 | (reg << 3)));
		word(static_cast<uint32_t>(away));
	}
}

void Sh4Host::load(uint32_t rank)
{
	byte(0x8B);					/* mov A, [the word] */
	operand(A, rank);
}

void Sh4Host::store(uint32_t rank)
{
	byte(0x89);					/* mov [the word], A */
	operand(A, rank);
}

void Sh4Host::store_value(uint32_t rank, uint32_t value)
{
	byte(0xC7);					/* mov dword [the word], value */
	operand(0, rank);
	word(value);
}

void Sh4Host::alu(Alu op, uint32_t rank)
{
	byte(static_cast<uint8_t>(ALU[static_cast<uint8_t>(op)] * 8 + 3));	/* op A, [the word] */
	operand(A, rank);
}

void Sh4Host::alu_value(Alu op, uint32_t value)
{
	const uint8_t which = static_cast<uint8_t>(0xC0 | (ALU[static_cast<uint8_t>(op)] << 3) | A);

	if (static_cast<int32_t>(value) == static_cast<int8_t>(value)) {
		bytes({0x83, which, static_cast<uint8_t>(value)});	/* op A, a byte with its sign */
	} else {
		bytes({0x81, which});					/* op A, value */
		word(value);
	}
}

void Sh4Host::shift(Shift kind, uint32_t count)
{
	bytes({0xC1, static_cast<uint8_t>(0xC0 | (SHIFT[static_cast<uint8_t>(kind)] << 3) | A),
	       static_cast<uint8_t>(count)});
}

void Sh4Host::flag(If found, uint32_t rank)
{
	bytes({0x0F, SET[static_cast<uint8_t>(found)], 0xC0});	/* set<found> al */
	bytes({0x0F, 0xB6, 0xC0});				/* movzx eax, al */
	store(rank);
}

Sh4Host::Jump Sh4Host::jump_if_word(uint32_t rank, bool set)
{
	byte(0x83);					/* cmp dword [the word], 0 */
	operand(7, rank);
	byte(0);
	return jump_if(set ? JNZ : JZ);
}

Sh4Host::Jump Sh4Host::jump_if_bits(uint32_t rank, uint32_t mask, bool set)
{
	byte(0xF7);					/* test dword [the word], mask */
	operand(0, rank);
	word(mask);
	return jump_if(set ? JNZ : JZ);
}

void Sh4Host::float_load(uint32_t f, uint32_t rank)
{
	bytes({0xF3, 0x0F, 0x10});			/* movss xmm<f>, [the word] */
	operand(static_cast<uint8_t>(f), rank);
}

void Sh4Host::float_store(uint32_t rank, uint32_t f)
{
	bytes({0xF3, 0x0F, 0x11});			/* movss [the word], xmm<f> */
	operand(static_cast<uint8_t>(f), rank);
}

void Sh4Host::float_op(Float op, uint32_t f, uint32_t rank)
{
	static constexpr uint8_t CODE[] = { 0x58, 0x5C, 0x59 };	/* addss, subss, mulss */

	bytes({0xF3, 0x0F, CODE[static_cast<uint8_t>(op)]});
	operand(static_cast<uint8_t>(f), rank);
}

void Sh4Host::float_compare(uint32_t f, uint32_t rank)
{
	bytes({0x0F, 0x2E});				/* ucomiss xmm<f>, [the word] */
	operand(static_cast<uint8_t>(f), rank);
}

void Sh4Host::float_from(uint32_t f, uint32_t rank)
{
	bytes({0xF3, 0x0F, 0x2A});			/* cvtsi2ss xmm<f>, [the word] */
	operand(static_cast<uint8_t>(f), rank);
}

void Sh4Host::float_to(uint32_t rank)
{
	bytes({0xF3, 0x0F, 0x2C});			/* cvttss2si eax, [the word] */
	operand(EAX, rank);
}

void Sh4Host::float_fused()
{
	bytes({0xC4, 0xE2, 0x71, 0x99, 0xC2});		/* vfmadd132ss xmm0, xmm1, xmm2 */
}

bool Sh4Host::vectors() const { return false; }
void Sh4Host::float_ipr(uint32_t, uint32_t) {}
void Sh4Host::float_trv(uint32_t, uint32_t) {}
bool Sh4Host::whole(Whole, uint32_t, uint32_t, uint32_t, uint32_t) { return false; }

void Sh4Host::load_pair(uint32_t rank)
{
	bytes({0x48, 0x8B});				/* mov rax, [the words] */
	operand(EAX, rank);
}

void Sh4Host::store_pair(uint32_t rank)
{
	bytes({0x48, 0x89});				/* mov [the words], rax */
	operand(EAX, rank);
}

Sh4Host::Jump Sh4Host::jump_if_odd()
{
	bytes({0xA8, 0x01});				/* test al, 1 */
	return jump_if(JNZ);
}

void Sh4Host::add_remaining(int32_t value)
{
	bytes({0x81, 0x83});				/* add dword [rbx + remaining], value */
	word(static_cast<uint32_t>(m_places.remaining));
	word(static_cast<uint32_t>(value));
}

void Sh4Host::bind(const Slow &slow)
{
	for (uint32_t k = 0; k < slow.count; k++) {
		bind(slow.jumps[k]);
	}
}

void Sh4Host::bind(Jump jump) { HostCode::bind(jump); }
uint32_t Sh4Host::join() { return here(); }

/* rdx = the page of the address in A in the table of r13 or r14; the long
 * way when there is none or the address is not aligned. ecx keeps the
 * address over the bits of the page. */
void Sh4Host::page(uint8_t table, uint32_t size, Slow &slow)
{
	bytes({0x89, 0xC1});				/* mov ecx, eax */
	bytes({0xC1, 0xE9, static_cast<uint8_t>(m_memory.page_bits)});	/* shr ecx, page_bits */
	bytes({0x49, 0x8B, 0x54, static_cast<uint8_t>(0xC8 | table), 0x00});	/* mov rdx, [table + rcx * 8] */
	bytes({0x48, 0x85, 0xD2});			/* test rdx, rdx */
	slow.jumps[slow.count++] = jump_if(JZ);
	if (size > 1) {
		bytes({0xA8, static_cast<uint8_t>(size - 1)});	/* test al, size - 1 */
		slow.jumps[slow.count++] = jump_if(JNZ);
	}
	byte(0x25);					/* and eax, the offset in the page */
	word((1u << m_memory.page_bits) - 1);
}

void Sh4Host::read(uint32_t size, Slow &slow)
{
	page(READS, size, slow);
	switch (size) {
	case 1:
		bytes({0x0F, 0xBE, 0x04, 0x02});	/* movsx eax, byte [rdx + rax] */
		break;
	case 2:
		bytes({0x0F, 0xBF, 0x04, 0x02});	/* movsx eax, word [rdx + rax] */
		break;
	case 4:
		bytes({0x8B, 0x04, 0x02});		/* mov eax, [rdx + rax] */
		break;
	default:
		bytes({0x48, 0x8B, 0x04, 0x02});	/* mov rax, [rdx + rax] */
		break;
	}
}

void Sh4Host::write(uint32_t size, uint32_t rank, Slow &slow)
{
	page(WRITES, size, slow);
	bytes({0x81, 0xE1});				/* and ecx, the physical pages */
	word(m_memory.physical_pages - 1);
	bytes({0x45, 0x8B, 0x44, 0x8F, 0x00});		/* mov r8d, [r15 + rcx * 4] */
	bytes({0x44, 0x3B, 0x83});			/* cmp r8d, [rbx + epoch] */
	word(static_cast<uint32_t>(m_memory.epoch));
	slow.jumps[slow.count++] = jump_if(JZ);
	bytes({0xC6, 0x83});				/* mov byte [rbx + touched], 1 */
	word(static_cast<uint32_t>(m_memory.touched));
	byte(1);
	if (size == 8) {
		byte(0x48);				/* mov rcx, [the words] */
	}
	byte(0x8B);					/* mov ecx, [the word] */
	operand(ECX, rank);
	switch (size) {
	case 1:
		bytes({0x88, 0x0C, 0x02});		/* mov [rdx + rax], cl */
		break;
	case 2:
		bytes({0x66, 0x89, 0x0C, 0x02});	/* mov [rdx + rax], cx */
		break;
	case 4:
		bytes({0x89, 0x0C, 0x02});		/* mov [rdx + rax], ecx */
		break;
	default:
		bytes({0x48, 0x89, 0x0C, 0x02});	/* mov [rdx + rax], rcx */
		break;
	}
}

void Sh4Host::read_at(const uint8_t *at, uint32_t size)
{
	bytes({0x48, 0xB8});				/* mov rax, at */
	quad(reinterpret_cast<uintptr_t>(at));
	switch (size) {
	case 1:
		bytes({0x0F, 0xBE, 0x00});		/* movsx eax, byte [rax] */
		break;
	case 2:
		bytes({0x0F, 0xBF, 0x00});		/* movsx eax, word [rax] */
		break;
	default:
		bytes({0x8B, 0x00});			/* mov eax, [rax] */
		break;
	}
}

#elif !defined(ODIN_HOST_AARCH64) /* a host no file writes code for */

Sh4Host::Sh4Host(uint32_t *words, const Memory &memory, const Places &places)
	: HostCode(0), m_words(words), m_memory(memory), m_places(places), m_fused(false), m_enter(nullptr)
{
}

void Sh4Host::clear() {}
uint32_t Sh4Host::begin(const Sh4Op *) { return 0; }
void Sh4Host::call(Call, uint32_t) {}
void Sh4Host::call_link(Link, uint32_t) {}
void Sh4Host::link_fast(uint32_t) {}
void Sh4Host::leave_fast(uint32_t, int32_t) {}
void Sh4Host::enter_fast(bool, uint32_t, int32_t) {}
Sh4Host::Jump Sh4Host::jump_if_word(uint32_t, bool) { return Jump{0}; }
Sh4Host::Jump Sh4Host::jump_if_odd() { return Jump{0}; }
Sh4Host::Jump Sh4Host::jump_if_bits(uint32_t, uint32_t, bool) { return Jump{0}; }
void Sh4Host::load_pair(uint32_t) {}
void Sh4Host::float_load(uint32_t, uint32_t) {}
void Sh4Host::float_store(uint32_t, uint32_t) {}
void Sh4Host::float_op(Float, uint32_t, uint32_t) {}
void Sh4Host::float_compare(uint32_t, uint32_t) {}
void Sh4Host::float_from(uint32_t, uint32_t) {}
void Sh4Host::float_to(uint32_t) {}
void Sh4Host::float_fused() {}
bool Sh4Host::vectors() const { return false; }
void Sh4Host::float_ipr(uint32_t, uint32_t) {}
void Sh4Host::float_trv(uint32_t, uint32_t) {}
bool Sh4Host::whole(Whole, uint32_t, uint32_t, uint32_t, uint32_t) { return false; }
void Sh4Host::store_pair(uint32_t) {}
void Sh4Host::add_remaining(int32_t) {}
void Sh4Host::loop_if_told() {}
Sh4Host::Jump Sh4Host::jump_if_told() { return Jump{0}; }
void Sh4Host::go_where_told() {}
void Sh4Host::operand(uint8_t, uint32_t) {}
void Sh4Host::load(uint32_t) {}
void Sh4Host::store(uint32_t) {}
void Sh4Host::store_value(uint32_t, uint32_t) {}
void Sh4Host::alu(Alu, uint32_t) {}
void Sh4Host::alu_value(Alu, uint32_t) {}
void Sh4Host::shift(Shift, uint32_t) {}
void Sh4Host::flag(If, uint32_t) {}
void Sh4Host::bind(const Slow &) {}
void Sh4Host::bind(Jump) {}
uint32_t Sh4Host::join() { return 0; }
void Sh4Host::page(uint8_t, uint32_t, Slow &) {}
void Sh4Host::read(uint32_t, Slow &) {}
void Sh4Host::write(uint32_t, uint32_t, Slow &) {}
void Sh4Host::read_at(const uint8_t *, uint32_t) {}

#endif

} /* namespace odin */
