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

#include "odin/machine/core/host_code.hpp"

#include <cstring>

#if defined(ODIN_HOST_X86_64) || defined(ODIN_HOST_AARCH64)
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#if defined(__APPLE__) && defined(ODIN_HOST_AARCH64)
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif
#endif

namespace odin {

#if defined(ODIN_HOST_X86_64) || defined(ODIN_HOST_AARCH64)

namespace {

/* How far a direct call reaches */
#if defined(ODIN_HOST_X86_64)
constexpr uintptr_t REACH = 2048u << 20;
#else
constexpr uintptr_t REACH = 128u << 20;
#endif

uint8_t *take(uintptr_t at, uint32_t size)
{
#if defined(_WIN32)
	return static_cast<uint8_t *>(VirtualAlloc(reinterpret_cast<void *>(at), size, MEM_RESERVE | MEM_COMMIT,
	                                           PAGE_EXECUTE_READWRITE));
#else
#if defined(MAP_FIXED_NOREPLACE)
	const int fixed = MAP_FIXED_NOREPLACE;
#else
	const int fixed = 0;
#endif
#if defined(__APPLE__) && defined(ODIN_HOST_AARCH64)
	const int jit = MAP_JIT;
#else
	const int jit = 0;
#endif
	void *want = reinterpret_cast<void *>(at);
	void *got = mmap(want, size, PROT_READ | PROT_WRITE | PROT_EXEC,
	                 MAP_PRIVATE | MAP_ANONYMOUS | jit | (at ? fixed : 0), -1, 0);

	if (got == MAP_FAILED) {
		return nullptr;
	}
	if (at && got != want) {
		munmap(got, size);
		return nullptr;
	}
	return static_cast<uint8_t *>(got);
#endif
}

void give_back(uint8_t *code, uint32_t size)
{
#if defined(_WIN32)
	(void)size;
	VirtualFree(code, 0, MEM_RELEASE);
#else
	munmap(code, size);
#endif
}

/* Memory the host may run, within reach of a direct call from the code of
 * the program when there is some free. */
uint8_t *take_near(uint32_t size)
{
	const uintptr_t step = REACH / 32;
	const uintptr_t here = reinterpret_cast<uintptr_t>(&take) & ~(step - 1);

	for (uintptr_t n = 1; n < 24; n++) {
		if (uint8_t *code = take(here + n * step, size)) {
			return code;
		}
		if (here > n * step) {
			if (uint8_t *code = take(here - n * step, size)) {
				return code;
			}
		}
	}
	return take(0, size);
}

} /* namespace */

HostCode::HostCode(uint32_t size)
	:
#if defined(ODIN_HOST_AARCH64)
	  m_written(0),
#endif
	  m_code(take_near(size)), m_size(size), m_used(0), m_leave(0), m_far_calls(0)
{
}

HostCode::~HostCode()
{
	if (m_code) {
		give_back(m_code, m_size);
	}
}

#if defined(__APPLE__) && defined(ODIN_HOST_AARCH64)
namespace {
thread_local bool g_writing = false;
}
#endif

void HostCode::writable()
{
#if defined(__APPLE__) && defined(ODIN_HOST_AARCH64)
	if (!g_writing) {
		pthread_jit_write_protect_np(0);
		g_writing = true;
	}
#endif
}

void HostCode::ready() const
{
#if defined(ODIN_HOST_AARCH64)
#if defined(__APPLE__)
	if (g_writing) {
		pthread_jit_write_protect_np(1);
		g_writing = false;
	}
#endif
	if (m_written < m_used) {
#if defined(__APPLE__)
		sys_icache_invalidate(m_code + m_written, m_used - m_written);
#elif defined(_WIN32)
		FlushInstructionCache(GetCurrentProcess(), m_code + m_written, m_used - m_written);
#else
		__builtin___clear_cache(reinterpret_cast<char *>(m_code + m_written),
		                        reinterpret_cast<char *>(m_code + m_used));
#endif
		m_written = m_used;
	}
#endif
}

#endif

#if defined(ODIN_HOST_X86_64)

namespace {

/* The bytes a call needs above it on the stack, with what keeps the stack
 * aligned on 16 */
#if defined(_WIN32)
constexpr uint8_t FRAME = 40;
#else
constexpr uint8_t FRAME = 8;
#endif

} /* namespace */

void HostCode::bytes(std::initializer_list<uint8_t> values)
{
	for (uint8_t value : values) {
		byte(value);
	}
}

void HostCode::word(uint32_t value)
{
	std::memcpy(m_code + m_used, &value, 4);
	m_used += 4;
}

void HostCode::quad(uint64_t value)
{
	std::memcpy(m_code + m_used, &value, 8);
	m_used += 8;
}

void HostCode::jump_to(uint32_t place)
{
	word(place - (m_used + 4));
}

HostCode::Jump HostCode::jump_if(uint8_t condition)
{
	bytes({0x0F, condition});			/* j<condition> ... */
	m_used += 4;
	return Jump{m_used - 4};
}

void HostCode::bind(Jump jump)
{
	const uint32_t distance = m_used - (jump.from + 4);

	std::memcpy(m_code + jump.from, &distance, 4);
}

void HostCode::jump_back(uint32_t place)
{
	byte(0xE9);					/* jmp place */
	jump_to(place);
}

void HostCode::call_to(uintptr_t target)
{
	const int64_t distance = static_cast<int64_t>(target - (reinterpret_cast<uintptr_t>(m_code) + m_used + 5));

	if (distance == static_cast<int32_t>(distance)) {
		byte(0xE8);				/* call target */
		word(static_cast<uint32_t>(distance));
	} else {
		bytes({0x48, 0xB8});			/* mov rax, target / call rax */
		quad(target);
		bytes({0xFF, 0xD0});
		m_far_calls++;
	}
}

void HostCode::move32(uint8_t dst, uint8_t src)
{
	const uint8_t rex = static_cast<uint8_t>(0x40 | (src >= 8 ? 4 : 0) | (dst >= 8 ? 1 : 0));

	if (rex != 0x40) {
		byte(rex);
	}
	bytes({0x89, static_cast<uint8_t>(0xC0 | ((src & 7) << 3) | (dst & 7))});	/* mov dst, src */
}

namespace {

/* The REX prefix of a step naming `reg` in ModRM.reg and `rm` in ModRM.rm,
 * 0x40 when none is needed */
uint8_t rex(uint8_t reg, uint8_t rm)
{
	return static_cast<uint8_t>(0x40 | (reg >= 8 ? 4 : 0) | (rm >= 8 ? 1 : 0));
}

} /* namespace */

void HostCode::op_rr(uint8_t opcode, uint8_t dst, uint8_t src)
{
	if (rex(src, dst) != 0x40) {
		byte(rex(src, dst));
	}
	bytes({opcode, static_cast<uint8_t>(0xC0 | ((src & 7) << 3) | (dst & 7))});
}

void HostCode::op_ri(uint8_t ext, uint8_t dst, uint32_t value)
{
	if (dst >= 8) {
		byte(0x41);
	}
	const uint8_t modrm = static_cast<uint8_t>(0xC0 | (ext << 3) | (dst & 7));
	if (static_cast<int32_t>(value) == static_cast<int8_t>(value)) {
		bytes({0x83, modrm, static_cast<uint8_t>(value)});
	} else {
		bytes({0x81, modrm});
		word(value);
	}
}

void HostCode::shift_ri(uint8_t ext, uint8_t dst, uint8_t count)
{
	if (dst >= 8) {
		byte(0x41);
	}
	bytes({0xC1, static_cast<uint8_t>(0xC0 | (ext << 3) | (dst & 7)), count});
}

void HostCode::not_r(uint8_t dst)
{
	if (dst >= 8) {
		byte(0x41);
	}
	bytes({0xF7, static_cast<uint8_t>(0xD0 | (dst & 7))});
}

void HostCode::mov_ri(uint8_t dst, uint32_t value)
{
	if (dst >= 8) {
		byte(0x41);
	}
	byte(static_cast<uint8_t>(0xB8 | (dst & 7)));
	word(value);
}

void HostCode::flag_r(uint8_t cc, uint8_t dst)
{
	/* set<cc> dst8: a register past the fourth needs a prefix */
	if (dst >= 4) {
		byte(dst >= 8 ? 0x41 : 0x40);
	}
	bytes({0x0F, static_cast<uint8_t>(0x90 | cc), static_cast<uint8_t>(0xC0 | (dst & 7))});
	/* movzx dst, dst8 */
	if (dst >= 4) {
		byte(dst >= 8 ? 0x45 : 0x40);
	}
	bytes({0x0F, 0xB6, static_cast<uint8_t>(0xC0 | ((dst & 7) << 3) | (dst & 7))});
}

void HostCode::way_in()
{
	m_used = 0;
	bytes({0x53, 0x41, 0x54, 0x55});		/* push rbx / push r12 / push rbp */
	bytes({0x41, 0x55, 0x41, 0x56, 0x41, 0x57});	/* push r13 / push r14 / push r15 */
	bytes({0x48, 0x83, 0xEC, FRAME});		/* sub rsp, FRAME */
	bytes({0x48, 0x89, static_cast<uint8_t>(0xC0 | (ARG[0] << 3) | RBX)});	/* mov rbx, the first argument */
}

void HostCode::way_in_end()
{
	bytes({0xFF, static_cast<uint8_t>(0xE0 | ARG[1])});	/* jmp the second argument */
	m_leave = m_used;
}

void HostCode::way_out()
{
	bytes({0x48, 0x83, 0xC4, FRAME});		/* add rsp, FRAME */
	bytes({0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D});	/* pop r15 / pop r14 / pop r13 */
	bytes({0x5D, 0x41, 0x5C, 0x5B, 0xC3});		/* pop rbp / pop r12 / pop rbx / ret */
}

#endif

#if !defined(ODIN_HOST_X86_64) && !defined(ODIN_HOST_AARCH64) /* a host this file does not write code for */

HostCode::HostCode(uint32_t size) : m_code(nullptr), m_size(size), m_used(0), m_leave(0), m_far_calls(0) {}
HostCode::~HostCode() = default;
void HostCode::bind(Jump) {}
void HostCode::jump_back(uint32_t) {}
void HostCode::writable() {}
void HostCode::ready() const {}

#endif

} /* namespace odin */
