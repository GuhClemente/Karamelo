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

/* SH-4 instructions (SH-4 software manual, section 9) and their decoding.
 * Every instruction is one function that runs on a decoded operand, then
 * calls the next one of the block as its last act: with that call turned
 * into a jump by the compiler, a block runs without coming back to the
 * loop. Without it the calls only nest, to the length of a block at most.
 *
 * This file is built without contraction of floating-point expressions:
 * a product is fused with a sum only where std::fma says so. */

#include "odin/machine/cpu/sh4/sh4.hpp"
#include "odin/common/bytes.hpp"
#include "odin/common/env.hpp"
#include "odin/common/fma_by_double.hpp"
#include "odin/machine/cpu/sh4/sh4_fsca.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace odin {

#if defined(ODIN_HOST_X86_64)
#if defined(_WIN32)
#include <intrin.h>
bool
host_has_fma()
{
	int info[4];
	__cpuid(info, 1);
	return ((info[2] & (1 << 12)) != 0) && !env_flag("ODIN_SH4_NO_FMA");
}
#else
bool
host_has_fma()
{
	__builtin_cpu_init();
	return __builtin_cpu_supports("fma") != 0 && !env_flag("ODIN_SH4_NO_FMA");
}
#endif
#endif

namespace {

/* The products FMAC, FIPR and FTRV fuse with a sum, rounded once. A build
 * for hosts that have FMA does it in one instruction (std::fma); a build for
 * any x86-64 has these built twice, once for FMA, taken when the host has
 * it, else with fma_by_double: the same results on every host, whatever its
 * library. */
inline float
fma_one(float x, float y, float z)
{
	return std::fma(x, y, z);
}

template <float (*FMA)(float, float, float)>
[[gnu::always_inline]] inline float
fused_mac(uint32_t a, uint32_t b, uint32_t c)
{
	return FMA(f32(a), f32(b), f32(c));
}

template <float (*FMA)(float, float, float)>
[[gnu::always_inline]] inline float
fused_ipr(const uint32_t *a, const uint32_t *b)
{
	float sum = f32(a[1]) * f32(b[1]);

	sum = FMA(f32(a[0]), f32(b[0]), sum);
	sum = FMA(f32(a[2]), f32(b[2]), sum);
	sum = FMA(f32(a[3]), f32(b[3]), sum);
	return sum;
}

/* The vector v by the matrix x, one column of x per component of v */
template <float (*FMA)(float, float, float)>
[[gnu::always_inline]] inline void
fused_trv(uint32_t *v, const uint32_t *x)
{
	const float v0 = f32(v[0]), v1 = f32(v[1]), v2 = f32(v[2]), v3 = f32(v[3]);

	for (uint32_t i = 0; i < 4; i++) {
		float sum = v1 * f32(x[4 + i]);

		sum = FMA(v0, f32(x[i]), sum);
		sum = FMA(v2, f32(x[8 + i]), sum);
		sum = FMA(v3, f32(x[12 + i]), sum);
		v[i] = std::bit_cast<uint32_t>(sum);
	}
}

#if defined(__FMA__) || !(defined(__x86_64__) || defined(_M_X64))
float mac(uint32_t a, uint32_t b, uint32_t c) { return fused_mac<fma_one>(a, b, c); }
float ipr(const uint32_t *a, const uint32_t *b) { return fused_ipr<fma_one>(a, b); }
void trv(uint32_t *v, const uint32_t *x) { fused_trv<fma_one>(v, x); }
#else
[[gnu::target("fma")]] float mac_fma(uint32_t a, uint32_t b, uint32_t c) { return fused_mac<fma_one>(a, b, c); }
[[gnu::target("fma")]] float ipr_fma(const uint32_t *a, const uint32_t *b) { return fused_ipr<fma_one>(a, b); }
[[gnu::target("fma")]] void trv_fma(uint32_t *v, const uint32_t *x) { fused_trv<fma_one>(v, x); }
[[gnu::noinline]] float mac_any(uint32_t a, uint32_t b, uint32_t c) { return fused_mac<fma_by_double>(a, b, c); }
[[gnu::noinline]] float ipr_any(const uint32_t *a, const uint32_t *b) { return fused_ipr<fma_by_double>(a, b); }
[[gnu::noinline]] void trv_any(uint32_t *v, const uint32_t *x) { fused_trv<fma_by_double>(v, x); }

const bool HAS_FMA = host_has_fma();

float mac(uint32_t a, uint32_t b, uint32_t c) { return HAS_FMA ? mac_fma(a, b, c) : mac_any(a, b, c); }
float ipr(const uint32_t *a, const uint32_t *b) { return HAS_FMA ? ipr_fma(a, b) : ipr_any(a, b); }
void trv(uint32_t *v, const uint32_t *x) { HAS_FMA ? trv_fma(v, x) : trv_any(v, x); }
#endif

/* a + b and a * b with a the first operand of the host's instruction, which
 * picks the NaN of two: the compiler may swap the operands otherwise. The
 * reader and the host code thus give the same NaN. HEURISTIC: the SH-4
 * gives its own, 7FBFFFFF or 7FF7FFFF FFFFFFFF, whatever its operands
 * (hardware manual, 6.2.2). */
#if defined(__GNUC__) && defined(__aarch64__)
#define ODIN_FLOAT_OP(name, type, insn, reg) \
	type name(type a, type b) { type r; asm(insn " %" reg "0, %" reg "1, %" reg "2" : "=w"(r) : "w"(a), "w"(b)); return r; }
ODIN_FLOAT_OP(add_first, float, "fadd", "s")
ODIN_FLOAT_OP(mul_first, float, "fmul", "s")
ODIN_FLOAT_OP(add_first, double, "fadd", "d")
ODIN_FLOAT_OP(mul_first, double, "fmul", "d")
#elif defined(__GNUC__) && defined(__x86_64__)
#define ODIN_FLOAT_OP(name, type, insn) \
	type name(type a, type b) { asm(insn " %1, %0" : "+x"(a) : "x"(b)); return a; }
ODIN_FLOAT_OP(add_first, float, "addss")
ODIN_FLOAT_OP(mul_first, float, "mulss")
ODIN_FLOAT_OP(add_first, double, "addsd")
ODIN_FLOAT_OP(mul_first, double, "mulsd")
#else
#define ODIN_FLOAT_OP(name, type, op) \
	type name(type a, type b) { return a op b; }
ODIN_FLOAT_OP(add_first, float, +)
ODIN_FLOAT_OP(mul_first, float, *)
ODIN_FLOAT_OP(add_first, double, +)
ODIN_FLOAT_OP(mul_first, double, *)
#endif
#undef ODIN_FLOAT_OP

} /* namespace */

/* How an instruction hands over to what comes next. The reader does it by
 * a tail call: the instructions of a block follow one another without
 * coming back. */
struct Sh4Threaded {
	using Ret = void;

	/* The next instruction of the block. */
	static void go(Sh4 &c, const Sh4Op *op) { return op[1].run(c, op + 1); }
	/* The block is left after this instruction. */
	static void stop(Sh4 &c, const Sh4Op *op) { return c.stop_after(op); }
	/* The delay slot of a branch that goes, `m` further, then the end
	 * that follows it. */
	static void slot(Sh4 &c, const Sh4Op *op)
	{
		const Sh4Op *s = op + op->m;

		return s->run(c, s);
	}
	/* At the end of a block: the block at `pc`, or the run loop. */
	static void follow(Sh4 &c, uint32_t pc) { return c.chain(pc); }
	/* Back to the run loop. */
	static void loop(Sh4 &) {}
};

/* Host code calls each instruction of a block in turn and is told what
 * comes next, as sh4_host.hpp says */
struct Sh4Called {
	using Ret = uintptr_t;

	static uintptr_t go(Sh4 &, const Sh4Op *) { return 0; }
	static uintptr_t stop(Sh4 &c, const Sh4Op *op) { return c.stopped(op); }
	/* The host code calls the slot itself, then the end that follows
	 * it. */
	static uintptr_t slot(Sh4 &, const Sh4Op *) { return 1; }
	static uintptr_t follow(Sh4 &c, uint32_t pc) { return c.link(pc); }
	static uintptr_t loop(Sh4 &) { return 1; }
};

/* The instructions, written once for every way to hand over. */
template <class Way>
struct Sh4OpsT {
	using Op = Sh4Op;
	using Ret = typename Way::Ret;

	static int32_t s8(uint32_t v) { return static_cast<int8_t>(v); }
	static int32_t s16(uint32_t v) { return static_cast<int16_t>(v); }
	static uint32_t u32(int32_t v) { return static_cast<uint32_t>(v); }
	static uint32_t bits(float v) { return std::bit_cast<uint32_t>(v); }

	/* On to the next instruction of the block. */
	static Ret go(Sh4 &c, const Op *op) { return Way::go(c, op); }
	/* The same after a memory access, unless the block must be left. */
	static Ret next(Sh4 &c, const Op *op, bool plain)
	{
		if (plain) [[likely]] {
			return go(c, op);
		}
		return Way::stop(c, op);
	}

	[[noreturn]] static void fail(Sh4 &c, const Op *op, const char *what)
	{
		if (!(op->flags & Sh4::OP_SLOT)) {
			c.m_r.pc = c.pc_of(op);
		}
		c.fatal(what, c.m_r.pc);
	}
	static void need(Sh4 &c, const Op *op, bool ok, const char *what)
	{
		if (!ok) [[unlikely]] {
			fail(c, op, what);
		}
	}
	static void need_privileged(Sh4 &c, const Op *op)
	{
		need(c, op, (c.m_r.sr & Sh4::SR_MD) != 0, "privileged instruction in user mode");
	}
	static void need_fpu(Sh4 &c, const Op *op)
	{
		need(c, op, (c.m_r.sr & Sh4::SR_FD) == 0, "FPU instruction with the FPU disabled");
	}

	/* A double has its high word in FRn and its low word in FRn+1.
	 * HEURISTIC: an odd n is an undefined code that nobody has measured
	 * on an SH-4; its low bit is dropped, as the operations of the
	 * software manual (section 9, normal_fmul) read DR[n >> 1] and as
	 * MAME sh4.cpp does. */
	static double get_dr(const Sh4 &c, uint32_t n)
	{
		n &= 14;
		return std::bit_cast<double>((static_cast<uint64_t>(c.m_r.fr[n]) << 32) | c.m_r.fr[n + 1]);
	}
	static void set_dr(Sh4 &c, uint32_t n, double v)
	{
		uint64_t u = std::bit_cast<uint64_t>(v);
		n &= 14;
		c.m_r.fr[n] = static_cast<uint32_t>(u >> 32);
		c.m_r.fr[n + 1] = static_cast<uint32_t>(u);
	}
	/* The pair FMOV moves when FPSCR.SZ is set: DRn, or XDn for an odd n. */
	static uint32_t *pair(Sh4 &c, uint32_t n) { return (n & 1) ? &c.m_r.xf[n & 14] : &c.m_r.fr[n & 14]; }
	static uint64_t get_pair(Sh4 &c, uint32_t n)
	{
		const uint32_t *p = pair(c, n);
		return p[0] | (static_cast<uint64_t>(p[1]) << 32);
	}
	static void set_pair(Sh4 &c, uint32_t n, uint64_t v)
	{
		uint32_t *p = pair(c, n);
		p[0] = static_cast<uint32_t>(v);
		p[1] = static_cast<uint32_t>(v >> 32);
	}

	/* A branch with a delay slot. PC takes the target at once, as on the
	 * chip; then the slot runs, and the end that follows it leaves the
	 * block. */
	static Ret jump(Sh4 &c, const Op *op, uint32_t target)
	{
		need(c, op, !(op->flags & Sh4::OP_BAD_SLOT), "instruction not allowed in a delay slot");
		c.m_r.pc = target;
		return Way::slot(c, op);
	}
	static Ret branch(Sh4 &c, const Op *op)
	{
		c.m_r.pc = op->imm;
		c.m_remaining += c.m_blk->count - op->idx - 1;
		return Way::follow(c, op->imm);
	}
	/* Ends a block that stops for its length: the program goes on at
	 * `imm`. */
	static Ret end_next(Sh4 &c, const Op *op)
	{
		c.m_r.pc = op->imm;
		return Way::follow(c, op->imm);
	}
	/* Ends the delay slot of a taken branch: `imm` cycles are given back
	 * for the instructions of the block that did not run, and the program
	 * goes on at PC, through the run loop if the slot disturbed
	 * something. */
	static Ret end_leave(Sh4 &c, const Op *op)
	{
		c.m_remaining += static_cast<int32_t>(op->imm);
		if (!c.m_event) [[likely]] {
			return Way::follow(c, c.m_r.pc);
		}
		return Way::loop(c);
	}

	static Ret invalid(Sh4 &c, const Op *op) { fail(c, op, "invalid instruction"); }

	/* Data transfer */

	static Ret mov(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret movi(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = op->imm;
		return go(c, op);
	}
	static Ret movwi(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, op->imm, v);
		c.m_r.r[op->n] = u32(s16(v));
		return next(c, op, plain);
	}
	static Ret movli(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, op->imm, v);
		c.m_r.r[op->n] = v;
		return next(c, op, plain);
	}
	static Ret movbs(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n], static_cast<uint8_t>(c.m_r.r[op->m])));
	}
	static Ret movws(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n], static_cast<uint16_t>(c.m_r.r[op->m])));
	}
	static Ret movls(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n], c.m_r.r[op->m]));
	}
	static Ret movbl(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = u32(s8(v));
		return next(c, op, plain);
	}
	static Ret movwl(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = u32(s16(v));
		return next(c, op, plain);
	}
	static Ret movll(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = v;
		return next(c, op, plain);
	}
	static Ret movbm(Sh4 &c, const Op *op)
	{
		bool plain = c.write(op, c.m_r.r[op->n] - 1, static_cast<uint8_t>(c.m_r.r[op->m]));
		c.m_r.r[op->n] -= 1;
		return next(c, op, plain);
	}
	static Ret movwm(Sh4 &c, const Op *op)
	{
		bool plain = c.write(op, c.m_r.r[op->n] - 2, static_cast<uint16_t>(c.m_r.r[op->m]));
		c.m_r.r[op->n] -= 2;
		return next(c, op, plain);
	}
	static Ret movlm(Sh4 &c, const Op *op)
	{
		bool plain = c.write(op, c.m_r.r[op->n] - 4, c.m_r.r[op->m]);
		c.m_r.r[op->n] -= 4;
		return next(c, op, plain);
	}
	static Ret movbp(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = u32(s8(v));
		if (op->n != op->m) {
			c.m_r.r[op->m] += 1;
		}
		return next(c, op, plain);
	}
	static Ret movwp(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = u32(s16(v));
		if (op->n != op->m) {
			c.m_r.r[op->m] += 2;
		}
		return next(c, op, plain);
	}
	static Ret movlp(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, c.m_r.r[op->m], v);
		c.m_r.r[op->n] = v;
		if (op->n != op->m) {
			c.m_r.r[op->m] += 4;
		}
		return next(c, op, plain);
	}
	static Ret movbs0(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n] + c.m_r.r[0], static_cast<uint8_t>(c.m_r.r[op->m])));
	}
	static Ret movws0(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n] + c.m_r.r[0], static_cast<uint16_t>(c.m_r.r[op->m])));
	}
	static Ret movls0(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n] + c.m_r.r[0], c.m_r.r[op->m]));
	}
	static Ret movbl0(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + c.m_r.r[0], v);
		c.m_r.r[op->n] = u32(s8(v));
		return next(c, op, plain);
	}
	static Ret movwl0(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + c.m_r.r[0], v);
		c.m_r.r[op->n] = u32(s16(v));
		return next(c, op, plain);
	}
	static Ret movll0(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + c.m_r.r[0], v);
		c.m_r.r[op->n] = v;
		return next(c, op, plain);
	}
	static Ret movblg(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.gbr + op->imm, v);
		c.m_r.r[0] = u32(s8(v));
		return next(c, op, plain);
	}
	static Ret movwlg(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, c.m_r.gbr + op->imm, v);
		c.m_r.r[0] = u32(s16(v));
		return next(c, op, plain);
	}
	static Ret movllg(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, c.m_r.gbr + op->imm, v);
		c.m_r.r[0] = v;
		return next(c, op, plain);
	}
	static Ret movbsg(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.gbr + op->imm, static_cast<uint8_t>(c.m_r.r[0])));
	}
	static Ret movwsg(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.gbr + op->imm, static_cast<uint16_t>(c.m_r.r[0])));
	}
	static Ret movlsg(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.gbr + op->imm, c.m_r.r[0]));
	}
	/* With a displacement: the byte and word forms between R0 and
	 * @(disp,Rm); MOV.L @(disp,Rm),Rn and MOV.L Rm,@(disp,Rn) */
	static Ret movbl4(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + op->imm, v);
		c.m_r.r[0] = u32(s8(v));
		return next(c, op, plain);
	}
	static Ret movwl4(Sh4 &c, const Op *op)
	{
		uint16_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + op->imm, v);
		c.m_r.r[0] = u32(s16(v));
		return next(c, op, plain);
	}
	static Ret movll4(Sh4 &c, const Op *op)
	{
		uint32_t v;
		bool plain = c.read(op, c.m_r.r[op->m] + op->imm, v);
		c.m_r.r[op->n] = v;
		return next(c, op, plain);
	}
	static Ret movbs4(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->m] + op->imm, static_cast<uint8_t>(c.m_r.r[0])));
	}
	static Ret movws4(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->m] + op->imm, static_cast<uint16_t>(c.m_r.r[0])));
	}
	static Ret movls4(Sh4 &c, const Op *op)
	{
		return next(c, op, c.write(op, c.m_r.r[op->n] + op->imm, c.m_r.r[op->m]));
	}
	static Ret mova(Sh4 &c, const Op *op)
	{
		c.m_r.r[0] = op->imm;
		return go(c, op);
	}
	static Ret movt(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.t;
		return go(c, op);
	}
	static Ret swapb(Sh4 &c, const Op *op)
	{
		uint32_t m = c.m_r.r[op->m];
		c.m_r.r[op->n] = (m & 0xFFFF0000) | ((m << 8) & 0xFF00) | ((m >> 8) & 0xFF);
		return go(c, op);
	}
	static Ret swapw(Sh4 &c, const Op *op)
	{
		uint32_t m = c.m_r.r[op->m];
		c.m_r.r[op->n] = (m >> 16) | (m << 16);
		return go(c, op);
	}
	static Ret xtrct(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = (c.m_r.r[op->m] << 16) | (c.m_r.r[op->n] >> 16);
		return go(c, op);
	}

	/* Arithmetic */

	static Ret add(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] += c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret addi(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] += op->imm;
		return go(c, op);
	}
	static Ret addc(Sh4 &c, const Op *op)
	{
		uint32_t tmp0 = c.m_r.r[op->n];
		uint32_t tmp1 = tmp0 + c.m_r.r[op->m];
		uint32_t res = tmp1 + c.m_r.t;
		c.m_r.r[op->n] = res;
		c.m_r.t = (tmp0 > tmp1) || (tmp1 > res);
		return go(c, op);
	}
	static Ret addv(Sh4 &c, const Op *op)
	{
		bool d = static_cast<int32_t>(c.m_r.r[op->n]) < 0;
		bool s = static_cast<int32_t>(c.m_r.r[op->m]) < 0;
		c.m_r.r[op->n] += c.m_r.r[op->m];
		bool a = static_cast<int32_t>(c.m_r.r[op->n]) < 0;
		c.m_r.t = (d == s) && (d != a);
		return go(c, op);
	}
	static Ret neg(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = 0u - c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret negc(Sh4 &c, const Op *op)
	{
		uint32_t tmp = 0u - c.m_r.r[op->m];
		uint32_t res = tmp - c.m_r.t;
		c.m_r.r[op->n] = res;
		c.m_r.t = (0 < tmp) || (tmp < res);
		return go(c, op);
	}
	static Ret sub(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] -= c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret subc(Sh4 &c, const Op *op)
	{
		uint32_t tmp0 = c.m_r.r[op->n];
		uint32_t tmp1 = tmp0 - c.m_r.r[op->m];
		uint32_t res = tmp1 - c.m_r.t;
		c.m_r.r[op->n] = res;
		c.m_r.t = (tmp0 < tmp1) || (tmp1 < res);
		return go(c, op);
	}
	static Ret subv(Sh4 &c, const Op *op)
	{
		bool d = static_cast<int32_t>(c.m_r.r[op->n]) < 0;
		bool s = static_cast<int32_t>(c.m_r.r[op->m]) < 0;
		c.m_r.r[op->n] -= c.m_r.r[op->m];
		bool a = static_cast<int32_t>(c.m_r.r[op->n]) < 0;
		c.m_r.t = (s != d) && (d != a);
		return go(c, op);
	}
	static Ret cmpeq(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] == c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret cmpge(Sh4 &c, const Op *op)
	{
		c.m_r.t = static_cast<int32_t>(c.m_r.r[op->n]) >= static_cast<int32_t>(c.m_r.r[op->m]);
		return go(c, op);
	}
	static Ret cmpgt(Sh4 &c, const Op *op)
	{
		c.m_r.t = static_cast<int32_t>(c.m_r.r[op->n]) > static_cast<int32_t>(c.m_r.r[op->m]);
		return go(c, op);
	}
	static Ret cmphi(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] > c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret cmphs(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] >= c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret cmppz(Sh4 &c, const Op *op)
	{
		c.m_r.t = static_cast<int32_t>(c.m_r.r[op->n]) >= 0;
		return go(c, op);
	}
	static Ret cmppl(Sh4 &c, const Op *op)
	{
		c.m_r.t = static_cast<int32_t>(c.m_r.r[op->n]) > 0;
		return go(c, op);
	}
	static Ret cmpim(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[0] == op->imm;
		return go(c, op);
	}
	static Ret cmpstr(Sh4 &c, const Op *op)
	{
		uint32_t tmp = c.m_r.r[op->n] ^ c.m_r.r[op->m];
		c.m_r.t = !(tmp & 0xFF000000) || !(tmp & 0x00FF0000) || !(tmp & 0x0000FF00) || !(tmp & 0x000000FF);
		return go(c, op);
	}
	static Ret div0s(Sh4 &c, const Op *op)
	{
		uint32_t q = c.m_r.r[op->n] >> 31, m = c.m_r.r[op->m] >> 31;
		c.m_r.sr = (c.m_r.sr & ~(Sh4::SR_Q | Sh4::SR_M)) | (q ? Sh4::SR_Q : 0) | (m ? Sh4::SR_M : 0);
		c.m_r.t = q ^ m;
		return go(c, op);
	}
	static Ret div0u(Sh4 &c, const Op *op)
	{
		c.m_r.sr &= ~(Sh4::SR_Q | Sh4::SR_M);
		c.m_r.t = 0;
		return go(c, op);
	}
	/* One step of a division (software manual, DIV1). */
	static Ret div1(Sh4 &c, const Op *op)
	{
		uint32_t old_q = (c.m_r.sr & Sh4::SR_Q) ? 1 : 0;
		uint32_t m = (c.m_r.sr & Sh4::SR_M) ? 1 : 0;
		uint32_t q = c.m_r.r[op->n] >> 31;
		uint32_t rn = (c.m_r.r[op->n] << 1) | c.m_r.t;
		uint32_t tmp0 = rn, tmp2 = c.m_r.r[op->m], tmp1;

		if (old_q == m) {
			rn -= tmp2;
			tmp1 = rn > tmp0;
		} else {
			rn += tmp2;
			tmp1 = rn < tmp0;
		}
		/* The carry or borrow of the step, turned over by the bit shifted
		 * out and, when the divisor is negative, once more. */
		q = (q ^ tmp1 ^ m) & 1;
		c.m_r.r[op->n] = rn;
		c.m_r.sr = (c.m_r.sr & ~Sh4::SR_Q) | (q ? Sh4::SR_Q : 0);
		c.m_r.t = q == m;
		return go(c, op);
	}
	static Ret dmuls(Sh4 &c, const Op *op)
	{
		int64_t res = static_cast<int64_t>(static_cast<int32_t>(c.m_r.r[op->n]))
			* static_cast<int64_t>(static_cast<int32_t>(c.m_r.r[op->m]));
		c.m_r.macl = static_cast<uint32_t>(res);
		c.m_r.mach = static_cast<uint32_t>(static_cast<uint64_t>(res) >> 32);
		return go(c, op);
	}
	static Ret dmulu(Sh4 &c, const Op *op)
	{
		uint64_t res = static_cast<uint64_t>(c.m_r.r[op->n]) * c.m_r.r[op->m];
		c.m_r.macl = static_cast<uint32_t>(res);
		c.m_r.mach = static_cast<uint32_t>(res >> 32);
		return go(c, op);
	}
	static Ret dt(Sh4 &c, const Op *op)
	{
		c.m_r.t = --c.m_r.r[op->n] == 0;
		return go(c, op);
	}
	static Ret extsb(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = u32(s8(c.m_r.r[op->m]));
		return go(c, op);
	}
	static Ret extsw(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = u32(s16(c.m_r.r[op->m]));
		return go(c, op);
	}
	static Ret extub(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.r[op->m] & 0xFF;
		return go(c, op);
	}
	static Ret extuw(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.r[op->m] & 0xFFFF;
		return go(c, op);
	}
	/* MAC.L with S clear: MAC += (Rn) * (Rm), signed, 64 bits. */
	static Ret macl(Sh4 &c, const Op *op)
	{
		uint32_t vn, vm;
		bool plain = c.read(op, c.m_r.r[op->n], vn);
		c.m_r.r[op->n] += 4;
		plain &= c.read(op, c.m_r.r[op->m], vm);
		c.m_r.r[op->m] += 4;
		need(c, op, !(c.m_r.sr & Sh4::SR_S), "MAC.L with S set is not implemented");
		int64_t prod = static_cast<int64_t>(static_cast<int32_t>(vn)) * static_cast<int64_t>(static_cast<int32_t>(vm));
		uint64_t mac = (static_cast<uint64_t>(c.m_r.mach) << 32) | c.m_r.macl;
		mac += static_cast<uint64_t>(prod);
		c.m_r.macl = static_cast<uint32_t>(mac);
		c.m_r.mach = static_cast<uint32_t>(mac >> 32);
		return next(c, op, plain);
	}
	static Ret macw(Sh4 &c, const Op *op)
	{
		uint16_t vn, vm;
		bool plain = c.read(op, c.m_r.r[op->n], vn);
		c.m_r.r[op->n] += 2;
		plain &= c.read(op, c.m_r.r[op->m], vm);
		c.m_r.r[op->m] += 2;

		uint32_t old = c.m_r.macl;
		int32_t prod = s16(vn) * s16(vm);
		uint32_t dst = static_cast<int32_t>(old) < 0;
		uint32_t src = (prod < 0 ? 1u : 0u) + dst;
		c.m_r.macl = old + u32(prod);
		uint32_t ans = (static_cast<int32_t>(c.m_r.macl) < 0 ? 1u : 0u) + dst;
		if (c.m_r.sr & Sh4::SR_S) {
			if (ans == 1) {
				if (src == 0) {
					c.m_r.macl = 0x7FFFFFFF;
				}
				if (src == 2) {
					c.m_r.macl = 0x80000000;
				}
			}
		} else {
			c.m_r.mach += prod < 0 ? 0xFFFFFFFFu : 0u;
			if (old > c.m_r.macl) {
				c.m_r.mach += 1;
			}
		}
		return next(c, op, plain);
	}
	static Ret mull(Sh4 &c, const Op *op)
	{
		c.m_r.macl = c.m_r.r[op->n] * c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret mulsw(Sh4 &c, const Op *op)
	{
		c.m_r.macl = u32(s16(c.m_r.r[op->n]) * s16(c.m_r.r[op->m]));
		return go(c, op);
	}
	static Ret muluw(Sh4 &c, const Op *op)
	{
		c.m_r.macl = (c.m_r.r[op->n] & 0xFFFF) * (c.m_r.r[op->m] & 0xFFFF);
		return go(c, op);
	}

	/* Logic */

	static Ret and_(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] &= c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret andi(Sh4 &c, const Op *op)
	{
		c.m_r.r[0] &= op->imm;
		return go(c, op);
	}
	static Ret andm(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.gbr + c.m_r.r[0], v);
		plain &= c.write(op, c.m_r.gbr + c.m_r.r[0], static_cast<uint8_t>(v & op->imm));
		return next(c, op, plain);
	}
	static Ret not_(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = ~c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret or_(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] |= c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret ori(Sh4 &c, const Op *op)
	{
		c.m_r.r[0] |= op->imm;
		return go(c, op);
	}
	static Ret orm(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.gbr + c.m_r.r[0], v);
		plain &= c.write(op, c.m_r.gbr + c.m_r.r[0], static_cast<uint8_t>(v | op->imm));
		return next(c, op, plain);
	}
	static Ret tas(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.r[op->n], v);
		c.m_r.t = v == 0;
		plain &= c.write(op, c.m_r.r[op->n], static_cast<uint8_t>(v | 0x80));
		return next(c, op, plain);
	}
	static Ret tst(Sh4 &c, const Op *op)
	{
		c.m_r.t = (c.m_r.r[op->n] & c.m_r.r[op->m]) == 0;
		return go(c, op);
	}
	static Ret tsti(Sh4 &c, const Op *op)
	{
		c.m_r.t = (c.m_r.r[0] & op->imm) == 0;
		return go(c, op);
	}
	static Ret tstm(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.gbr + c.m_r.r[0], v);
		c.m_r.t = (v & op->imm) == 0;
		return next(c, op, plain);
	}
	static Ret xor_(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] ^= c.m_r.r[op->m];
		return go(c, op);
	}
	static Ret xori(Sh4 &c, const Op *op)
	{
		c.m_r.r[0] ^= op->imm;
		return go(c, op);
	}
	static Ret xorm(Sh4 &c, const Op *op)
	{
		uint8_t v;
		bool plain = c.read(op, c.m_r.gbr + c.m_r.r[0], v);
		plain &= c.write(op, c.m_r.gbr + c.m_r.r[0], static_cast<uint8_t>(v ^ op->imm));
		return next(c, op, plain);
	}

	/* Shifts */

	static Ret rotl(Sh4 &c, const Op *op)
	{
		uint32_t v = c.m_r.r[op->n];
		c.m_r.t = v >> 31;
		c.m_r.r[op->n] = (v << 1) | c.m_r.t;
		return go(c, op);
	}
	static Ret rotr(Sh4 &c, const Op *op)
	{
		uint32_t v = c.m_r.r[op->n];
		c.m_r.t = v & 1;
		c.m_r.r[op->n] = (v >> 1) | (c.m_r.t << 31);
		return go(c, op);
	}
	static Ret rotcl(Sh4 &c, const Op *op)
	{
		uint32_t v = c.m_r.r[op->n];
		c.m_r.r[op->n] = (v << 1) | c.m_r.t;
		c.m_r.t = v >> 31;
		return go(c, op);
	}
	static Ret rotcr(Sh4 &c, const Op *op)
	{
		uint32_t v = c.m_r.r[op->n];
		c.m_r.r[op->n] = (v >> 1) | (c.m_r.t << 31);
		c.m_r.t = v & 1;
		return go(c, op);
	}
	static Ret shll(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] >> 31;
		c.m_r.r[op->n] <<= 1;
		return go(c, op);
	}
	static Ret shar(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] & 1;
		c.m_r.r[op->n] = u32(static_cast<int32_t>(c.m_r.r[op->n]) >> 1);
		return go(c, op);
	}
	static Ret shlr(Sh4 &c, const Op *op)
	{
		c.m_r.t = c.m_r.r[op->n] & 1;
		c.m_r.r[op->n] >>= 1;
		return go(c, op);
	}
	static Ret shll2(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] <<= 2;
		return go(c, op);
	}
	static Ret shll8(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] <<= 8;
		return go(c, op);
	}
	static Ret shll16(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] <<= 16;
		return go(c, op);
	}
	static Ret shlr2(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] >>= 2;
		return go(c, op);
	}
	static Ret shlr8(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] >>= 8;
		return go(c, op);
	}
	static Ret shlr16(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] >>= 16;
		return go(c, op);
	}
	static Ret shad(Sh4 &c, const Op *op)
	{
		uint32_t m = c.m_r.r[op->m], n = c.m_r.r[op->n];
		if (!(m >> 31)) {
			n <<= m & 31;
		} else if ((m & 31) == 0) {
			n = (n >> 31) ? 0xFFFFFFFF : 0;
		} else {
			n = u32(static_cast<int32_t>(n) >> ((~m & 31) + 1));
		}
		c.m_r.r[op->n] = n;
		return go(c, op);
	}
	static Ret shld(Sh4 &c, const Op *op)
	{
		uint32_t m = c.m_r.r[op->m], n = c.m_r.r[op->n];
		if (!(m >> 31)) {
			n <<= m & 31;
		} else if ((m & 31) == 0) {
			n = 0;
		} else {
			n >>= (~m & 31) + 1;
		}
		c.m_r.r[op->n] = n;
		return go(c, op);
	}

	/* Branches */

	static Ret bt(Sh4 &c, const Op *op) { return c.m_r.t ? branch(c, op) : go(c, op); }
	static Ret bf(Sh4 &c, const Op *op) { return c.m_r.t ? go(c, op) : branch(c, op); }
	/* BF back to the DT Rn just before it, a delay loop: the turns left
	 * are counted without being run, up to the last of the count or of
	 * the slice. */
	static Ret bf_dt(Sh4 &c, const Op *op)
	{
		if (c.m_r.t) {
			return go(c, op);
		}
		uint32_t left = static_cast<uint32_t>(c.m_remaining) + c.m_blk->count - op->idx;
		uint32_t turns = std::min(c.m_r.r[op->n] - 1, (left - 1) / 2);
		c.m_r.r[op->n] -= turns;
		c.m_remaining -= static_cast<int32_t>(turns * 2);
		c.m_skipped += turns * 2;
		return branch(c, op);
	}
	static Ret bts(Sh4 &c, const Op *op) { return c.m_r.t ? jump(c, op, op->imm) : go(c, op); }
	static Ret bfs(Sh4 &c, const Op *op) { return c.m_r.t ? go(c, op) : jump(c, op, op->imm); }
	static Ret bra(Sh4 &c, const Op *op) { return jump(c, op, op->imm); }
	static Ret braf(Sh4 &c, const Op *op) { return jump(c, op, c.pc_of(op) + c.m_r.r[op->n] + 4); }
	static Ret bsr(Sh4 &c, const Op *op)
	{
		c.m_r.pr = c.pc_of(op) + 4;
		return jump(c, op, op->imm);
	}
	static Ret bsrf(Sh4 &c, const Op *op)
	{
		uint32_t pc = c.pc_of(op);
		c.m_r.pr = pc + 4;
		return jump(c, op, pc + c.m_r.r[op->n] + 4);
	}
	static Ret jmp(Sh4 &c, const Op *op)
	{
		need(c, op, !(c.m_r.r[op->n] & 1), "JMP to an odd address");
		return jump(c, op, c.m_r.r[op->n]);
	}
	static Ret jsr(Sh4 &c, const Op *op)
	{
		uint32_t target = c.m_r.r[op->n];
		c.m_r.pr = c.pc_of(op) + 4;
		return jump(c, op, target);
	}
	static Ret rts(Sh4 &c, const Op *op) { return jump(c, op, c.m_r.pr); }
	static Ret rte(Sh4 &c, const Op *op)
	{
		uint32_t target = c.m_r.spc;
		need(c, op, !(op->flags & Sh4::OP_BAD_SLOT), "instruction not allowed in a delay slot");
		/* The delay slot already runs with the restored SR. */
		c.set_sr(c.m_r.ssr);
		c.m_resumed = true;
		return jump(c, op, target);
	}

	/* System control */

	static Ret clrt(Sh4 &c, const Op *op)
	{
		c.m_r.t = 0;
		return go(c, op);
	}
	static Ret sett(Sh4 &c, const Op *op)
	{
		c.m_r.t = 1;
		return go(c, op);
	}
	static Ret clrs(Sh4 &c, const Op *op)
	{
		c.m_r.sr &= ~Sh4::SR_S;
		return go(c, op);
	}
	static Ret sets(Sh4 &c, const Op *op)
	{
		c.m_r.sr |= Sh4::SR_S;
		return go(c, op);
	}
	static Ret clrmac(Sh4 &c, const Op *op)
	{
		c.m_r.mach = 0;
		c.m_r.macl = 0;
		return go(c, op);
	}
	static Ret nop(Sh4 &c, const Op *op) { return go(c, op); }
	static Ret ldcsr(Sh4 &c, const Op *op)
	{
		c.set_sr(c.m_r.r[op->n]);
		return Way::stop(c, op);
	}
	static Ret ldcgbr(Sh4 &c, const Op *op)
	{
		c.m_r.gbr = c.m_r.r[op->n];
		return go(c, op);
	}
	static Ret ldcvbr(Sh4 &c, const Op *op)
	{
		c.m_r.vbr = c.m_r.r[op->n];
		return go(c, op);
	}
	static Ret ldcssr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.ssr = c.m_r.r[op->n];
		return go(c, op);
	}
	static Ret ldcspc(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.spc = c.m_r.r[op->n];
		return go(c, op);
	}
	static Ret ldcdbr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.dbr = c.m_r.r[op->n];
		return go(c, op);
	}
	static Ret ldcrbank(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.rbank[op->m] = c.m_r.r[op->n];
		return go(c, op);
	}
	/* The bank of R0-R7 may change under the register that is then
	 * incremented, as SR is loaded first. */
	static Ret ldcmsr(Sh4 &c, const Op *op)
	{
		uint32_t v;
		c.read(op, c.m_r.r[op->n], v);
		c.set_sr(v);
		c.m_r.r[op->n] += 4;
		return Way::stop(c, op);
	}
	static Ret ldcmgbr(Sh4 &c, const Op *op) { return pop(c, op, c.m_r.gbr); }
	static Ret ldcmvbr(Sh4 &c, const Op *op) { return pop(c, op, c.m_r.vbr); }
	static Ret ldcmssr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return pop(c, op, c.m_r.ssr);
	}
	static Ret ldcmspc(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return pop(c, op, c.m_r.spc);
	}
	static Ret ldcmdbr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return pop(c, op, c.m_r.dbr);
	}
	static Ret ldcmrbank(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return pop(c, op, c.m_r.rbank[op->m]);
	}
	static Ret ldsmach(Sh4 &c, const Op *op)
	{
		c.m_r.mach = c.m_r.r[op->n];
		return go(c, op);
	}
	static Ret ldsmacl(Sh4 &c, const Op *op)
	{
		c.m_r.macl = c.m_r.r[op->n];
		return go(c, op);
	}
	static Ret ldspr(Sh4 &c, const Op *op)
	{
		c.m_r.pr = c.m_r.r[op->n];
		return go(c, op);
	}
	static Ret ldsmmach(Sh4 &c, const Op *op) { return pop(c, op, c.m_r.mach); }
	static Ret ldsmmacl(Sh4 &c, const Op *op) { return pop(c, op, c.m_r.macl); }
	static Ret ldsmpr(Sh4 &c, const Op *op) { return pop(c, op, c.m_r.pr); }
	static Ret sleep(Sh4 &c, const Op *op)
	{
		c.set_run(Sh4Run::Sleeping);
		return Way::stop(c, op);
	}
	static Ret stcsr(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.sr | c.m_r.t;
		return go(c, op);
	}
	static Ret stcgbr(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.gbr;
		return go(c, op);
	}
	static Ret stcvbr(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.vbr;
		return go(c, op);
	}
	static Ret stcssr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.r[op->n] = c.m_r.ssr;
		return go(c, op);
	}
	static Ret stcspc(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.r[op->n] = c.m_r.spc;
		return go(c, op);
	}
	static Ret stcsgr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.r[op->n] = c.m_r.sgr;
		return go(c, op);
	}
	static Ret stcdbr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.r[op->n] = c.m_r.dbr;
		return go(c, op);
	}
	static Ret stcrbank(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		c.m_r.r[op->n] = c.m_r.rbank[op->m];
		return go(c, op);
	}
	/* Rn - 4 -> Rn, then the value to (Rn) */
	static Ret push(Sh4 &c, const Op *op, uint32_t value)
	{
		c.m_r.r[op->n] -= 4;
		return next(c, op, c.write(op, c.m_r.r[op->n], value));
	}
	/* The value at (Rn) to `to`, then Rn + 4 -> Rn */
	static Ret pop(Sh4 &c, const Op *op, uint32_t &to)
	{
		const bool plain = c.read(op, c.m_r.r[op->n], to);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static Ret stcmsr(Sh4 &c, const Op *op) { return push(c, op, c.m_r.sr | c.m_r.t); }
	static Ret stcmgbr(Sh4 &c, const Op *op) { return push(c, op, c.m_r.gbr); }
	static Ret stcmvbr(Sh4 &c, const Op *op) { return push(c, op, c.m_r.vbr); }
	static Ret stcmssr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return push(c, op, c.m_r.ssr);
	}
	static Ret stcmspc(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return push(c, op, c.m_r.spc);
	}
	static Ret stcmsgr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return push(c, op, c.m_r.sgr);
	}
	static Ret stcmdbr(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return push(c, op, c.m_r.dbr);
	}
	static Ret stcmrbank(Sh4 &c, const Op *op)
	{
		need_privileged(c, op);
		return push(c, op, c.m_r.rbank[op->m]);
	}
	static Ret stsmach(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.mach;
		return go(c, op);
	}
	static Ret stsmacl(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.macl;
		return go(c, op);
	}
	static Ret stspr(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] = c.m_r.pr;
		return go(c, op);
	}
	static Ret stsmmach(Sh4 &c, const Op *op) { return push(c, op, c.m_r.mach); }
	static Ret stsmmacl(Sh4 &c, const Op *op) { return push(c, op, c.m_r.macl); }
	static Ret stsmpr(Sh4 &c, const Op *op) { return push(c, op, c.m_r.pr); }
	static Ret trapa(Sh4 &c, const Op *op) { fail(c, op, "TRAPA is not implemented"); }

	/* Cache and store queues: there is no cache, and a store queue write
	 * went to its target at once (the HEURISTIC in sh4.cpp), so PREF has
	 * nothing left to send. */
	static Ret movca(Sh4 &c, const Op *op) { return next(c, op, c.write(op, c.m_r.r[op->n], c.m_r.r[0])); }

	/* FPU registers */

	static Ret ldsfpscr(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		c.set_fpscr(c.m_r.r[op->n]);
		return go(c, op);
	}
	static Ret ldsmfpscr(Sh4 &c, const Op *op)
	{
		uint32_t v;
		need_fpu(c, op);
		bool plain = c.read(op, c.m_r.r[op->n], v);
		c.set_fpscr(v);
		c.m_r.r[op->n] += 4;
		return next(c, op, plain);
	}
	static Ret stsfpscr(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		c.m_r.r[op->n] = c.m_r.fpscr;
		return go(c, op);
	}
	static Ret stsmfpscr(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		return push(c, op, c.m_r.fpscr);
	}
	static Ret ldsfpul(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		c.m_r.fpul = c.m_r.r[op->n];
		return go(c, op);
	}
	static Ret ldsmfpul(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		return pop(c, op, c.m_r.fpul);
	}
	static Ret stsfpul(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		c.m_r.r[op->n] = c.m_r.fpul;
		return go(c, op);
	}
	static Ret stsmfpul(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		return push(c, op, c.m_r.fpul);
	}
	static Ret fschg(Sh4 &c, const Op *op)
	{
		c.set_fpscr(c.m_r.fpscr ^ Sh4::FPSCR_SZ);
		return go(c, op);
	}
	static Ret frchg(Sh4 &c, const Op *op)
	{
		c.set_fpscr(c.m_r.fpscr ^ Sh4::FPSCR_FR);
		return go(c, op);
	}
	static Ret fldi0(Sh4 &c, const Op *op)
	{
		need(c, op, !(c.m_r.fpscr & Sh4::FPSCR_PR), "FLDI0 in double precision");
		c.m_r.fr[op->n] = 0x00000000;
		return go(c, op);
	}
	static Ret fldi1(Sh4 &c, const Op *op)
	{
		need(c, op, !(c.m_r.fpscr & Sh4::FPSCR_PR), "FLDI1 in double precision");
		c.m_r.fr[op->n] = 0x3F800000;
		return go(c, op);
	}
	static Ret fsts(Sh4 &c, const Op *op)
	{
		c.m_r.fr[op->n] = c.m_r.fpul;
		return go(c, op);
	}
	static Ret flds(Sh4 &c, const Op *op)
	{
		c.m_r.fpul = c.m_r.fr[op->n];
		return go(c, op);
	}

	/* FMOV: one register, or a pair when FPSCR.SZ is set */

	static Ret fmov(Sh4 &c, const Op *op)
	{
		if (c.m_r.fpscr & Sh4::FPSCR_SZ) {
			set_pair(c, op->n, get_pair(c, op->m));
		} else {
			c.m_r.fr[op->n] = c.m_r.fr[op->m];
		}
		return go(c, op);
	}
	static Ret fmov_from(Sh4 &c, const Op *op, uint32_t addr)
	{
		bool plain;
		if (c.m_r.fpscr & Sh4::FPSCR_SZ) {
			uint64_t v;
			plain = c.read(op, addr, v);
			set_pair(c, op->n, v);
		} else {
			plain = c.read(op, addr, c.m_r.fr[op->n]);
		}
		return next(c, op, plain);
	}
	static Ret fmov_to(Sh4 &c, const Op *op, uint32_t addr)
	{
		bool plain;
		if (c.m_r.fpscr & Sh4::FPSCR_SZ) {
			plain = c.write(op, addr, get_pair(c, op->m));
		} else {
			plain = c.write(op, addr, c.m_r.fr[op->m]);
		}
		return next(c, op, plain);
	}
	static Ret fmov_load(Sh4 &c, const Op *op) { return fmov_from(c, op, c.m_r.r[op->m]); }
	static Ret fmov_store(Sh4 &c, const Op *op) { return fmov_to(c, op, c.m_r.r[op->n]); }
	static Ret fmov_index_load(Sh4 &c, const Op *op) { return fmov_from(c, op, c.m_r.r[0] + c.m_r.r[op->m]); }
	static Ret fmov_index_store(Sh4 &c, const Op *op) { return fmov_to(c, op, c.m_r.r[0] + c.m_r.r[op->n]); }
	static Ret fmov_restore(Sh4 &c, const Op *op)
	{
		uint32_t addr = c.m_r.r[op->m];
		c.m_r.r[op->m] += (c.m_r.fpscr & Sh4::FPSCR_SZ) ? 8 : 4;
		return fmov_from(c, op, addr);
	}
	static Ret fmov_save(Sh4 &c, const Op *op)
	{
		c.m_r.r[op->n] -= (c.m_r.fpscr & Sh4::FPSCR_SZ) ? 8 : 4;
		return fmov_to(c, op, c.m_r.r[op->n]);
	}

	/* FPU arithmetic. HEURISTIC: computed by the host and rounded to the
	 * nearest whatever FPSCR.RM and FPSCR.DN say; a NaN comes out as the
	 * host makes it; no FPU exception is raised. */

	static Ret flt(Sh4 &c, const Op *op)
	{
		int32_t v = static_cast<int32_t>(c.m_r.fpul);
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(static_cast<float>(v));
		} else {
			set_dr(c, op->n, static_cast<double>(v));
		}
		return go(c, op);
	}
	/* HEURISTIC: what the host gives out of range, 0x80000000; the SH-4
	 * saturates. */
	static Ret ftrc(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			float v = f32(c.m_r.fr[op->n]);
			c.m_r.fpul = (v >= -2147483648.0f && v < 2147483648.0f) ? u32(static_cast<int32_t>(v)) : 0x80000000;
		} else {
			double v = get_dr(c, op->n);
			c.m_r.fpul = (v > -2147483649.0 && v < 2147483648.0) ? u32(static_cast<int32_t>(v)) : 0x80000000;
		}
		return go(c, op);
	}
	static Ret fcnvsd(Sh4 &c, const Op *op)
	{
		set_dr(c, op->n, static_cast<double>(f32(c.m_r.fpul)));
		return go(c, op);
	}
	static Ret fcnvds(Sh4 &c, const Op *op)
	{
		c.m_r.fpul = bits(static_cast<float>(get_dr(c, op->n)));
		return go(c, op);
	}
	/* The sign of a double is in FRn. FNEG and FABS work on FRn whatever
	 * the precision, an odd n too (software manual, 9.25 and 9.40; FNEG
	 * measured on the SH-4 of a Dreamcast, Flycast issue 2139). */
	static Ret fneg(Sh4 &c, const Op *op)
	{
		c.m_r.fr[op->n] ^= 0x80000000;
		return go(c, op);
	}
	static Ret fabs_(Sh4 &c, const Op *op)
	{
		c.m_r.fr[op->n] &= 0x7FFFFFFF;
		return go(c, op);
	}
	static Ret fadd(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(add_first(f32(c.m_r.fr[op->m]), f32(c.m_r.fr[op->n])));
		} else {
			set_dr(c, op->n, add_first(get_dr(c, op->m), get_dr(c, op->n)));
		}
		return go(c, op);
	}
	static Ret fsub(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(f32(c.m_r.fr[op->n]) - f32(c.m_r.fr[op->m]));
		} else {
			set_dr(c, op->n, get_dr(c, op->n) - get_dr(c, op->m));
		}
		return go(c, op);
	}
	static Ret fmul(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(mul_first(f32(c.m_r.fr[op->m]), f32(c.m_r.fr[op->n])));
		} else {
			set_dr(c, op->n, mul_first(get_dr(c, op->m), get_dr(c, op->n)));
		}
		return go(c, op);
	}
	static Ret fdiv(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(f32(c.m_r.fr[op->n]) / f32(c.m_r.fr[op->m]));
		} else {
			set_dr(c, op->n, get_dr(c, op->n) / get_dr(c, op->m));
		}
		return go(c, op);
	}
	/* FR0 * FRm + FRn, rounded once (software manual, 6.3.1). */
	static Ret fmac(Sh4 &c, const Op *op)
	{
		need(c, op, !(c.m_r.fpscr & Sh4::FPSCR_PR), "FMAC in double precision");
		c.m_r.fr[op->n] = bits(mac(c.m_r.fr[op->m], c.m_r.fr[0], c.m_r.fr[op->n]));
		return go(c, op);
	}
	static Ret fsqrt(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.fr[op->n] = bits(std::sqrt(f32(c.m_r.fr[op->n])));
		} else {
			set_dr(c, op->n, std::sqrt(get_dr(c, op->n)));
		}
		return go(c, op);
	}
	/* With PR set, FSRRA, FSCA, FIPR and FTRV do nothing: no write, FPSCR
	 * as it was (measured on the SH-4 of a Dreamcast, shrike4-rtl
	 * model/fipr/hw_specials). */
	/* HEURISTIC: FSRRA, FIPR and FTRV are computed by the host, where the
	 * SH-4 has its own tables and error bounds. */
	static Ret fsrra(Sh4 &c, const Op *op)
	{
		if (c.m_r.fpscr & Sh4::FPSCR_PR)
			return go(c, op);

		float v = f32(c.m_r.fr[op->n]);
		if (!(v < 0)) {
			c.m_r.fr[op->n] = bits(1.0f / std::sqrt(v));
		}
		return go(c, op);
	}
	/* The angle is the low 16 bits of FPUL, a full turn in 65536. The
	 * sine and the cosine come from a table that no host decides, frozen
	 * for now: see sh4_fsca.cpp. */
	static Ret fsca(Sh4 &c, const Op *op)
	{
		if (c.m_r.fpscr & Sh4::FPSCR_PR)
			return go(c, op);

		const uint32_t *const pair = sh4_fsca_table() + 2 * (c.m_r.fpul & 0xFFFF);

		c.m_r.fr[op->n] = pair[0];
		c.m_r.fr[op->n + 1] = pair[1];
		return go(c, op);
	}
	static Ret fipr(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR))
			c.m_r.fr[op->n + 3] = bits(ipr(&c.m_r.fr[op->n], &c.m_r.fr[op->m]));
		return go(c, op);
	}
	/* The vector FRn..FRn+3 by the matrix in XF0..XF15, one column of the
	 * matrix per component of the vector. */
	static Ret ftrv(Sh4 &c, const Op *op)
	{
		need_fpu(c, op);
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR))
			trv(&c.m_r.fr[op->n], c.m_r.xf);
		return go(c, op);
	}
	static Ret fcmpeq(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.t = f32(c.m_r.fr[op->n]) == f32(c.m_r.fr[op->m]);
		} else {
			c.m_r.t = get_dr(c, op->n) == get_dr(c, op->m);
		}
		return go(c, op);
	}
	static Ret fcmpgt(Sh4 &c, const Op *op)
	{
		if (!(c.m_r.fpscr & Sh4::FPSCR_PR)) {
			c.m_r.t = f32(c.m_r.fr[op->n]) > f32(c.m_r.fr[op->m]);
		} else {
			c.m_r.t = get_dr(c, op->n) > get_dr(c, op->m);
		}
		return go(c, op);
	}

	/* Decoding */

	enum class Form : uint8_t {
		None,
		N,		/* register in bits 8-11 */
		NM,		/* and one in bits 4-7 */
		NImm,		/* register and signed 8-bit immediate */
		Imm,		/* unsigned 8-bit immediate */
		ImmSigned,
		MDisp0, MDisp1,	/* register in bits 4-7 and 4-bit displacement, scaled */
		NMDisp2,
		Gbr0, Gbr1, Gbr2,	/* 8-bit displacement from GBR, scaled */
		PcWord, PcLong, PcAddr,	/* 8-bit displacement from PC */
		Branch8, Branch12,
		NBank,		/* register and bank register in bits 4-6 */
		Vectors,	/* FVn in bits 10-11, FVm in bits 8-9 */
		Vector,		/* FVn in bits 10-11 */
		Pair		/* FRn and FRn+1, bits 9-11 */
	};
	using F = Form;
	using K = Sh4::Kind;
	using H = Sh4::Inline;
	struct Entry {
		uint16_t mask, match;
		Ret (*run)(Sh4 &, const Op *);
		Form form;
		Sh4::Kind kind;
		Sh4::Inline host = Sh4::Inline::No;	/* what host code does in place of a call */
	};
	static const Entry table[];
	static const uint32_t table_size;
	static const std::array<uint8_t, 65536> &index();
};

/* Software manual, section 9, and tables 7.3 to 7.12 for the codes. FSCA
 * and FSRRA are in the SH7091 only (MAME sh4.cpp). */
template <class Way>
const typename Sh4OpsT<Way>::Entry Sh4OpsT<Way>::table[] = {
	{0xF00F, 0x6003, mov, F::NM, K::Plain, H::Mov},
	{0xF00F, 0x2000, movbs, F::NM, K::Plain, H::MovBS},
	{0xF00F, 0x2001, movws, F::NM, K::Plain, H::MovWS},
	{0xF00F, 0x2002, movls, F::NM, K::Plain, H::MovLS},
	{0xF00F, 0x6000, movbl, F::NM, K::Plain, H::MovBL},
	{0xF00F, 0x6001, movwl, F::NM, K::Plain, H::MovWL},
	{0xF00F, 0x6002, movll, F::NM, K::Plain, H::MovLL},
	{0xF00F, 0x2004, movbm, F::NM, K::Plain, H::MovBM},
	{0xF00F, 0x2005, movwm, F::NM, K::Plain, H::MovWM},
	{0xF00F, 0x2006, movlm, F::NM, K::Plain, H::MovLM},
	{0xF00F, 0x6004, movbp, F::NM, K::Plain, H::MovBP},
	{0xF00F, 0x6005, movwp, F::NM, K::Plain, H::MovWP},
	{0xF00F, 0x6006, movlp, F::NM, K::Plain, H::MovLP},
	{0xF00F, 0x0004, movbs0, F::NM, K::Plain, H::MovBS0},
	{0xF00F, 0x0005, movws0, F::NM, K::Plain, H::MovWS0},
	{0xF00F, 0x0006, movls0, F::NM, K::Plain, H::MovLS0},
	{0xF00F, 0x000C, movbl0, F::NM, K::Plain, H::MovBL0},
	{0xF00F, 0x000D, movwl0, F::NM, K::Plain, H::MovWL0},
	{0xF00F, 0x000E, movll0, F::NM, K::Plain, H::MovLL0},
	{0xF000, 0xE000, movi, F::NImm, K::Plain, H::MovI},
	{0xF000, 0x9000, movwi, F::PcWord, K::PcRelative, H::MovWI},
	{0xF000, 0xD000, movli, F::PcLong, K::PcRelative, H::MovLI},
	{0xFF00, 0xC400, movblg, F::Gbr0, K::Plain, H::MovBLG},
	{0xFF00, 0xC500, movwlg, F::Gbr1, K::Plain, H::MovWLG},
	{0xFF00, 0xC600, movllg, F::Gbr2, K::Plain, H::MovLLG},
	{0xFF00, 0xC000, movbsg, F::Gbr0, K::Plain, H::MovBSG},
	{0xFF00, 0xC100, movwsg, F::Gbr1, K::Plain, H::MovWSG},
	{0xFF00, 0xC200, movlsg, F::Gbr2, K::Plain, H::MovLSG},
	{0xFF00, 0x8000, movbs4, F::MDisp0, K::Plain, H::MovBS4},
	{0xFF00, 0x8100, movws4, F::MDisp1, K::Plain, H::MovWS4},
	{0xF000, 0x1000, movls4, F::NMDisp2, K::Plain, H::MovLS4},
	{0xFF00, 0x8400, movbl4, F::MDisp0, K::Plain, H::MovBL4},
	{0xFF00, 0x8500, movwl4, F::MDisp1, K::Plain, H::MovWL4},
	{0xF000, 0x5000, movll4, F::NMDisp2, K::Plain, H::MovLL4},
	{0xFF00, 0xC700, mova, F::PcAddr, K::PcRelative, H::MovA},
	{0xF0FF, 0x0029, movt, F::N, K::Plain, H::MovT},
	{0xF00F, 0x6008, swapb, F::NM, K::Plain, H::SwapB},
	{0xF00F, 0x6009, swapw, F::NM, K::Plain, H::SwapW},
	{0xF00F, 0x200D, xtrct, F::NM, K::Plain, H::Xtrct},

	{0xF00F, 0x300C, add, F::NM, K::Plain, H::Add},
	{0xF000, 0x7000, addi, F::NImm, K::Plain, H::AddI},
	{0xF00F, 0x300E, addc, F::NM, K::Plain, H::AddC},
	{0xF00F, 0x300F, addv, F::NM, K::Plain},
	{0xFF00, 0x8800, cmpim, F::ImmSigned, K::Plain, H::CmpIm},
	{0xF00F, 0x3000, cmpeq, F::NM, K::Plain, H::CmpEq},
	{0xF00F, 0x3002, cmphs, F::NM, K::Plain, H::CmpHs},
	{0xF00F, 0x3003, cmpge, F::NM, K::Plain, H::CmpGe},
	{0xF00F, 0x3006, cmphi, F::NM, K::Plain, H::CmpHi},
	{0xF00F, 0x3007, cmpgt, F::NM, K::Plain, H::CmpGt},
	{0xF0FF, 0x4011, cmppz, F::N, K::Plain, H::CmpPz},
	{0xF0FF, 0x4015, cmppl, F::N, K::Plain, H::CmpPl},
	{0xF00F, 0x200C, cmpstr, F::NM, K::Plain},
	{0xF00F, 0x3004, div1, F::NM, K::Plain},
	{0xF00F, 0x2007, div0s, F::NM, K::Plain},
	{0xFFFF, 0x0019, div0u, F::None, K::Plain},
	{0xF00F, 0x300D, dmuls, F::NM, K::Plain, H::DMulS},
	{0xF00F, 0x3005, dmulu, F::NM, K::Plain, H::DMulU},
	{0xF0FF, 0x4010, dt, F::N, K::Plain, H::Dt},
	{0xF00F, 0x600E, extsb, F::NM, K::Plain, H::ExtSB},
	{0xF00F, 0x600F, extsw, F::NM, K::Plain, H::ExtSW},
	{0xF00F, 0x600C, extub, F::NM, K::Plain, H::ExtUB},
	{0xF00F, 0x600D, extuw, F::NM, K::Plain, H::ExtUW},
	{0xF00F, 0x000F, macl, F::NM, K::Plain},
	{0xF00F, 0x400F, macw, F::NM, K::Plain},
	{0xF00F, 0x0007, mull, F::NM, K::Plain, H::MulL},
	{0xF00F, 0x200F, mulsw, F::NM, K::Plain, H::MulSW},
	{0xF00F, 0x200E, muluw, F::NM, K::Plain, H::MulUW},
	{0xF00F, 0x600B, neg, F::NM, K::Plain, H::Neg},
	{0xF00F, 0x600A, negc, F::NM, K::Plain, H::NegC},
	{0xF00F, 0x3008, sub, F::NM, K::Plain, H::Sub},
	{0xF00F, 0x300A, subc, F::NM, K::Plain, H::SubC},
	{0xF00F, 0x300B, subv, F::NM, K::Plain},

	{0xF00F, 0x2009, and_, F::NM, K::Plain, H::And},
	{0xFF00, 0xC900, andi, F::Imm, K::Plain, H::AndI},
	{0xFF00, 0xCD00, andm, F::Imm, K::Plain},
	{0xF00F, 0x6007, not_, F::NM, K::Plain, H::Not},
	{0xF00F, 0x200B, or_, F::NM, K::Plain, H::Or},
	{0xFF00, 0xCB00, ori, F::Imm, K::Plain, H::OrI},
	{0xFF00, 0xCF00, orm, F::Imm, K::Plain},
	{0xF0FF, 0x401B, tas, F::N, K::Plain},
	{0xF00F, 0x2008, tst, F::NM, K::Plain, H::Tst},
	{0xFF00, 0xC800, tsti, F::Imm, K::Plain, H::TstI},
	{0xFF00, 0xCC00, tstm, F::Imm, K::Plain},
	{0xF00F, 0x200A, xor_, F::NM, K::Plain, H::Xor},
	{0xFF00, 0xCA00, xori, F::Imm, K::Plain, H::XorI},
	{0xFF00, 0xCE00, xorm, F::Imm, K::Plain},

	{0xF0FF, 0x4004, rotl, F::N, K::Plain, H::RotL},
	{0xF0FF, 0x4005, rotr, F::N, K::Plain, H::RotR},
	{0xF0FF, 0x4024, rotcl, F::N, K::Plain, H::RotCL},
	{0xF0FF, 0x4025, rotcr, F::N, K::Plain, H::RotCR},
	{0xF0FF, 0x4020, shll, F::N, K::Plain, H::ShLL},	/* SHAL: the same result */
	{0xF0FF, 0x4021, shar, F::N, K::Plain, H::ShAR},
	{0xF0FF, 0x4000, shll, F::N, K::Plain, H::ShLL},
	{0xF0FF, 0x4001, shlr, F::N, K::Plain, H::ShLR},
	{0xF0FF, 0x4008, shll2, F::N, K::Plain, H::ShLL2},
	{0xF0FF, 0x4009, shlr2, F::N, K::Plain, H::ShLR2},
	{0xF0FF, 0x4018, shll8, F::N, K::Plain, H::ShLL8},
	{0xF0FF, 0x4019, shlr8, F::N, K::Plain, H::ShLR8},
	{0xF0FF, 0x4028, shll16, F::N, K::Plain, H::ShLL16},
	{0xF0FF, 0x4029, shlr16, F::N, K::Plain, H::ShLR16},
	{0xF00F, 0x400C, shad, F::NM, K::Plain, H::ShAd},
	{0xF00F, 0x400D, shld, F::NM, K::Plain, H::ShLd},

	{0xFF00, 0x8B00, bf, F::Branch8, K::Branch, H::Bf},
	{0xFF00, 0x8F00, bfs, F::Branch8, K::BranchDelayed, H::BfS},
	{0xFF00, 0x8900, bt, F::Branch8, K::Branch, H::Bt},
	{0xFF00, 0x8D00, bts, F::Branch8, K::BranchDelayed, H::BtS},
	{0xF000, 0xA000, bra, F::Branch12, K::Jump, H::Bra},
	{0xF0FF, 0x0023, braf, F::N, K::Jump, H::BraF},
	{0xF000, 0xB000, bsr, F::Branch12, K::Jump, H::Bsr},
	{0xF0FF, 0x0003, bsrf, F::N, K::Jump, H::BsrF},
	{0xF0FF, 0x402B, jmp, F::N, K::Jump, H::Jmp},
	{0xF0FF, 0x400B, jsr, F::N, K::Jump, H::Jsr},
	{0xFFFF, 0x000B, rts, F::None, K::Jump, H::Rts},
	{0xFFFF, 0x002B, rte, F::None, K::Jump},

	{0xFFFF, 0x0008, clrt, F::None, K::Plain, H::ClrT},
	{0xFFFF, 0x0018, sett, F::None, K::Plain, H::SetT},
	{0xFFFF, 0x0048, clrs, F::None, K::Plain},
	{0xFFFF, 0x0058, sets, F::None, K::Plain},
	{0xFFFF, 0x0028, clrmac, F::None, K::Plain, H::ClrMac},
	{0xFFFF, 0x0009, nop, F::None, K::Plain, H::Nop},
	{0xFFFF, 0x001B, sleep, F::None, K::End},
	{0xFF00, 0xC300, trapa, F::Imm, K::End},
	{0xF0FF, 0x400E, ldcsr, F::N, K::End},
	{0xF0FF, 0x401E, ldcgbr, F::N, K::Plain, H::LdcGbr},
	{0xF0FF, 0x402E, ldcvbr, F::N, K::Plain},
	{0xF0FF, 0x403E, ldcssr, F::N, K::Plain},
	{0xF0FF, 0x404E, ldcspc, F::N, K::Plain},
	{0xF0FF, 0x40FA, ldcdbr, F::N, K::Plain},
	{0xF08F, 0x408E, ldcrbank, F::NBank, K::Plain},
	{0xF0FF, 0x4007, ldcmsr, F::N, K::End},
	{0xF0FF, 0x4017, ldcmgbr, F::N, K::Plain},
	{0xF0FF, 0x4027, ldcmvbr, F::N, K::Plain},
	{0xF0FF, 0x4037, ldcmssr, F::N, K::Plain},
	{0xF0FF, 0x4047, ldcmspc, F::N, K::Plain},
	{0xF0FF, 0x40F6, ldcmdbr, F::N, K::Plain},
	{0xF08F, 0x4087, ldcmrbank, F::NBank, K::Plain},
	{0xF0FF, 0x400A, ldsmach, F::N, K::Plain, H::LdsMacH},
	{0xF0FF, 0x401A, ldsmacl, F::N, K::Plain, H::LdsMacL},
	{0xF0FF, 0x402A, ldspr, F::N, K::Plain, H::LdsPr},
	{0xF0FF, 0x4006, ldsmmach, F::N, K::Plain},
	{0xF0FF, 0x4016, ldsmmacl, F::N, K::Plain},
	{0xF0FF, 0x4026, ldsmpr, F::N, K::Plain, H::LdsMPr},
	{0xF0FF, 0x0002, stcsr, F::N, K::Plain},
	{0xF0FF, 0x0012, stcgbr, F::N, K::Plain, H::StcGbr},
	{0xF0FF, 0x0022, stcvbr, F::N, K::Plain},
	{0xF0FF, 0x0032, stcssr, F::N, K::Plain},
	{0xF0FF, 0x0042, stcspc, F::N, K::Plain},
	{0xF0FF, 0x003A, stcsgr, F::N, K::Plain},
	{0xF0FF, 0x00FA, stcdbr, F::N, K::Plain},
	{0xF08F, 0x0082, stcrbank, F::NBank, K::Plain},
	{0xF0FF, 0x4003, stcmsr, F::N, K::Plain},
	{0xF0FF, 0x4013, stcmgbr, F::N, K::Plain},
	{0xF0FF, 0x4023, stcmvbr, F::N, K::Plain},
	{0xF0FF, 0x4033, stcmssr, F::N, K::Plain},
	{0xF0FF, 0x4043, stcmspc, F::N, K::Plain},
	{0xF0FF, 0x4032, stcmsgr, F::N, K::Plain},
	{0xF0FF, 0x40F2, stcmdbr, F::N, K::Plain},
	{0xF08F, 0x4083, stcmrbank, F::NBank, K::Plain},
	{0xF0FF, 0x000A, stsmach, F::N, K::Plain, H::StsMacH},
	{0xF0FF, 0x001A, stsmacl, F::N, K::Plain, H::StsMacL},
	{0xF0FF, 0x002A, stspr, F::N, K::Plain, H::StsPr},
	{0xF0FF, 0x4002, stsmmach, F::N, K::Plain},
	{0xF0FF, 0x4012, stsmmacl, F::N, K::Plain},
	{0xF0FF, 0x4022, stsmpr, F::N, K::Plain, H::StsMPr},

	/* No TLB and no cache: LDTLB, PREF, OCBI, OCBP and OCBWB do nothing. */
	{0xFFFF, 0x0038, nop, F::None, K::Plain, H::Nop},
	{0xF0FF, 0x0083, nop, F::N, K::Plain, H::Nop},
	{0xF0FF, 0x0093, nop, F::N, K::Plain, H::Nop},
	{0xF0FF, 0x00A3, nop, F::N, K::Plain, H::Nop},
	{0xF0FF, 0x00B3, nop, F::N, K::Plain, H::Nop},
	{0xF0FF, 0x00C3, movca, F::N, K::Plain, H::MovCA},

	{0xF0FF, 0x405A, ldsfpul, F::N, K::Plain, H::LdsFpul},
	{0xF0FF, 0x406A, ldsfpscr, F::N, K::Plain},
	{0xF0FF, 0x4056, ldsmfpul, F::N, K::Plain},
	{0xF0FF, 0x4066, ldsmfpscr, F::N, K::Plain},
	{0xF0FF, 0x005A, stsfpul, F::N, K::Plain, H::StsFpul},
	{0xF0FF, 0x006A, stsfpscr, F::N, K::Plain},
	{0xF0FF, 0x4052, stsmfpul, F::N, K::Plain},
	{0xF0FF, 0x4062, stsmfpscr, F::N, K::Plain},
	{0xFFFF, 0xF3FD, fschg, F::None, K::Plain, H::FSChg},
	{0xFFFF, 0xFBFD, frchg, F::None, K::Plain},
	{0xF0FF, 0xF00D, fsts, F::N, K::Plain, H::FSts},
	{0xF0FF, 0xF01D, flds, F::N, K::Plain, H::FLds},
	{0xF0FF, 0xF02D, flt, F::N, K::Plain, H::FLt},
	{0xF0FF, 0xF03D, ftrc, F::N, K::Plain, H::FTrc},
	{0xF0FF, 0xF04D, fneg, F::N, K::Plain, H::FNeg},
	{0xF0FF, 0xF05D, fabs_, F::N, K::Plain, H::FAbs},
	{0xF0FF, 0xF06D, fsqrt, F::N, K::Plain},
	{0xF0FF, 0xF07D, fsrra, F::N, K::Plain},
	{0xF0FF, 0xF08D, fldi0, F::N, K::Plain, H::FLdi0},
	{0xF0FF, 0xF09D, fldi1, F::N, K::Plain, H::FLdi1},
	{0xF0FF, 0xF0AD, fcnvsd, F::N, K::Plain},
	{0xF0FF, 0xF0BD, fcnvds, F::N, K::Plain},
	{0xF0FF, 0xF0ED, fipr, F::Vectors, K::Plain, H::FIpr},
	{0xF1FF, 0xF0FD, fsca, F::Pair, K::Plain},
	{0xF3FF, 0xF1FD, ftrv, F::Vector, K::Plain, H::FTrv},
	{0xF00F, 0xF000, fadd, F::NM, K::Plain, H::FAdd},
	{0xF00F, 0xF001, fsub, F::NM, K::Plain, H::FSub},
	{0xF00F, 0xF002, fmul, F::NM, K::Plain, H::FMul},
	{0xF00F, 0xF003, fdiv, F::NM, K::Plain},
	{0xF00F, 0xF004, fcmpeq, F::NM, K::Plain},
	{0xF00F, 0xF005, fcmpgt, F::NM, K::Plain, H::FCmpGt},
	{0xF00F, 0xF006, fmov_index_load, F::NM, K::Plain, H::FMovIndexLoad},
	{0xF00F, 0xF007, fmov_index_store, F::NM, K::Plain, H::FMovIndexStore},
	{0xF00F, 0xF008, fmov_load, F::NM, K::Plain, H::FMovLoad},
	{0xF00F, 0xF009, fmov_restore, F::NM, K::Plain, H::FMovRestore},
	{0xF00F, 0xF00A, fmov_store, F::NM, K::Plain, H::FMovStore},
	{0xF00F, 0xF00B, fmov_save, F::NM, K::Plain, H::FMovSave},
	{0xF00F, 0xF00C, fmov, F::NM, K::Plain, H::FMov},
	{0xF00F, 0xF00E, fmac, F::NM, K::Plain, H::FMac},
};
template <class Way>
const uint32_t Sh4OpsT<Way>::table_size = sizeof(Sh4OpsT<Way>::table) / sizeof(Sh4OpsT<Way>::table[0]);

/* The rank + 1 in the table for each of the 65536 codes, 0 for a code that
 * is no instruction. */
template <class Way>
const std::array<uint8_t, 65536> &Sh4OpsT<Way>::index()
{
	static const std::array<uint8_t, 65536> built = [] {
		std::array<uint8_t, 65536> t{};
		for (uint32_t inst = 0; inst < 65536; inst++) {
			for (uint32_t i = 0; i < table_size; i++) {
				if ((inst & table[i].mask) == table[i].match) {
					t[inst] = static_cast<uint8_t>(i + 1);
					break;
				}
			}
		}
		return t;
	}();
	return built;
}

using Sh4Ops = Sh4OpsT<Sh4Threaded>;

Sh4::Kind Sh4::decode(uint16_t inst, uint32_t pc, Sh4Op &op)
{
	uint32_t rank = Sh4Ops::index()[inst];
	uint32_t n = (inst >> 8) & 15, m = (inst >> 4) & 15;
	uint32_t imm8 = inst & 0xFF, disp4 = inst & 15;
	uint32_t simm8 = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(inst)));
	using F = Sh4Ops::Form;

	op = {};
	if (rank == 0) {
		op.run = Sh4Ops::invalid;
		op.imm = inst;
		return Kind::End;
	}

	const Sh4Ops::Entry &e = Sh4Ops::table[rank - 1];
	op.run = e.run;
	switch (e.form) {
	case F::None:
		break;
	case F::N:
		op.n = static_cast<uint8_t>(n);
		break;
	case F::NM:
		op.n = static_cast<uint8_t>(n);
		op.m = static_cast<uint8_t>(m);
		break;
	case F::NImm:
		op.n = static_cast<uint8_t>(n);
		op.imm = simm8;
		break;
	case F::Imm:
		op.imm = imm8;
		break;
	case F::ImmSigned:
		op.imm = simm8;
		break;
	case F::MDisp0:
		op.m = static_cast<uint8_t>(m);
		op.imm = disp4;
		break;
	case F::MDisp1:
		op.m = static_cast<uint8_t>(m);
		op.imm = disp4 << 1;
		break;
	case F::NMDisp2:
		op.n = static_cast<uint8_t>(n);
		op.m = static_cast<uint8_t>(m);
		op.imm = disp4 << 2;
		break;
	case F::Gbr0:
		op.imm = imm8;
		break;
	case F::Gbr1:
		op.imm = imm8 << 1;
		break;
	case F::Gbr2:
		op.imm = imm8 << 2;
		break;
	case F::PcWord:
		op.n = static_cast<uint8_t>(n);
		op.imm = pc + 4 + (imm8 << 1);
		break;
	case F::PcLong:
		op.n = static_cast<uint8_t>(n);
		op.imm = ((pc + 4) & ~3u) + (imm8 << 2);
		break;
	case F::PcAddr:
		op.imm = ((pc + 4) & ~3u) + (imm8 << 2);
		break;
	case F::Branch8:
		op.imm = pc + (simm8 << 1) + 4;
		break;
	case F::Branch12:
		op.imm = pc + ((((inst & 0xFFFu) ^ 0x800u) - 0x800u) << 1) + 4;
		break;
	case F::NBank:
		op.n = static_cast<uint8_t>(n);
		op.m = static_cast<uint8_t>(m & 7);
		break;
	case F::Vectors:
		op.n = static_cast<uint8_t>(n & 12);
		op.m = static_cast<uint8_t>((n & 3) << 2);
		break;
	case F::Vector:
		op.n = static_cast<uint8_t>(n & 12);
		break;
	case F::Pair:
		op.n = static_cast<uint8_t>(n & 14);
		break;
	}
	return e.kind;
}

/* The instruction, or the end of a block, as host code calls it. */
Sh4Host::Call Sh4::called(void (*run)(Sh4 &, const Sh4Op *))
{
	using Calls = Sh4OpsT<Sh4Called>;
	static const std::unordered_map<void (*)(Sh4 &, const Sh4Op *), Sh4Host::Call> both = [] {
		std::unordered_map<void (*)(Sh4 &, const Sh4Op *), Sh4Host::Call> m;

		for (uint32_t i = 0; i < Sh4Ops::table_size; i++) {
			m.emplace(Sh4Ops::table[i].run, Calls::table[i].run);
		}
		m.emplace(Sh4Ops::invalid, Calls::invalid);
		m.emplace(Sh4Ops::bf_dt, Calls::bf_dt);
		m.emplace(&Sh4::op_next, Calls::end_next);
		m.emplace(&Sh4::op_leave, Calls::end_leave);
		return m;
	}();

	return both.at(run);
}

/* The registers the forms that loops waiting on a timer are made of read
 * and write, as the instructions above use them (see sh4_clock.cpp); those
 * of the FPU with FPSCR.PR and SZ clear, which a turn is checked for. */
bool Sh4::uses(const Sh4Op &op, Uses &out)
{
	enum : uint32_t {
		RN_R = 1u << 0, RN_W = 1u << 1, RM_R = 1u << 2, RM_W = 1u << 3, R0_R = 1u << 4, R0_W = 1u << 5,
		T_R = 1u << 6, T_W = 1u << 7, PR_R = 1u << 8, PR_W = 1u << 9, FPUL_R = 1u << 10, FPUL_W = 1u << 11,
		FN_R = 1u << 12, FN_W = 1u << 13, FM_R = 1u << 14,
		MEM = 1u << 15, TO_RN = 1u << 16, TO_PR = 1u << 17, TO_FN = 1u << 18,	/* where a load goes */
		CHOICE = 1u << 19, TARGET = 1u << 20, ON_T = 1u << 21, STILL = 1u << 22
	};
	using S = Sh4Ops;
	constexpr uint32_t ALU = RN_R | RM_R | RN_W, CMP = RN_R | RM_R | T_W, ONE = RM_R | RN_W;
	constexpr uint32_t LOAD = RM_R | RN_W | MEM | TO_RN, STORE = RM_R | RN_R | MEM;
	constexpr uint32_t FALU = FN_R | FM_R | FN_W;
	using O = Order;
	struct Form {
		uint32_t f;
		Order order = O::None;
		uint8_t shift = 0;
	};
	static const std::unordered_map<void (*)(Sh4 &, const Sh4Op *), Form> forms = {
		{S::mov, {ONE, O::Same}}, {S::movi, {RN_W}}, {S::mova, {R0_W}}, {S::movt, {T_R | RN_W}},
		{S::movli, {RN_W | MEM | TO_RN}}, {S::movwi, {RN_W | MEM | TO_RN}},
		{S::movll, {LOAD}}, {S::movll4, {LOAD}}, {S::movll0, {LOAD | R0_R}}, {S::movlp, {LOAD | RM_W}},
		{S::movls, {STORE, O::Store}}, {S::movls4, {STORE, O::Store}}, {S::movls0, {STORE | R0_R}}, {S::movlm, {STORE | RN_W}},
		{S::stsmpr, {PR_R | RN_R | RN_W | MEM}}, {S::ldsmpr, {RN_R | RN_W | PR_W | MEM | TO_PR}},
		{S::add, {ALU, O::Add}}, {S::addi, {RN_R | RN_W, O::Add}}, {S::sub, {ALU, O::Sub}},
		{S::and_, {ALU}}, {S::or_, {ALU}}, {S::xor_, {ALU}},
		{S::andi, {R0_R | R0_W}}, {S::ori, {R0_R | R0_W}}, {S::xori, {R0_R | R0_W}},
		{S::not_, {ONE, O::Not}}, {S::neg, {ONE, O::Neg}}, {S::extsb, {ONE}}, {S::extsw, {ONE}}, {S::extub, {ONE}},
		{S::extuw, {ONE}},
		{S::shll, {RN_R | RN_W | T_W}}, {S::shlr, {RN_R | RN_W | T_W}}, {S::shar, {RN_R | RN_W | T_W}},
		{S::shll2, {RN_R | RN_W, O::ShiftLeft, 2}}, {S::shlr2, {RN_R | RN_W, O::ShiftRight, 2}},
		{S::shll8, {RN_R | RN_W, O::ShiftLeft, 8}}, {S::shlr8, {RN_R | RN_W, O::ShiftRight, 8}},
		{S::shll16, {RN_R | RN_W, O::ShiftLeft, 16}}, {S::shlr16, {RN_R | RN_W, O::ShiftRight, 16}},
		{S::shld, {ALU}}, {S::shad, {ALU}}, {S::dt, {RN_R | RN_W | T_W}},
		{S::cmpeq, {CMP}}, {S::cmphs, {CMP, O::AtLeast}}, {S::cmpge, {CMP, O::GreaterEq}},
		{S::cmphi, {CMP, O::Above}}, {S::cmpgt, {CMP, O::Greater}},
		{S::cmppz, {RN_R | T_W, O::NotNegative}}, {S::cmppl, {RN_R | T_W, O::Positive}}, {S::cmpim, {R0_R | T_W}},
		{S::tst, {CMP}}, {S::tsti, {R0_R | T_W}}, {S::clrt, {T_W}}, {S::sett, {T_W}}, {S::nop, {0}},
		{S::bt, {T_R | CHOICE | ON_T}}, {S::bf, {T_R | CHOICE}}, {S::bts, {T_R | CHOICE | ON_T}},
		{S::bfs, {T_R | CHOICE}},
		{S::bra, {0}}, {S::bsr, {PR_W}}, {S::jmp, {RN_R | TARGET}}, {S::jsr, {RN_R | PR_W | TARGET}},
		{S::rts, {PR_R | TARGET}},
		{S::ldsfpul, {RN_R | FPUL_W, O::Same}}, {S::stsfpul, {FPUL_R | RN_W, O::Same}},
		{S::flt, {FPUL_R | FN_W, O::ToFloat}}, {S::ftrc, {FN_R | FPUL_W, O::ToInt}},
		{S::fsts, {FPUL_R | FN_W, O::Same}}, {S::flds, {FN_R | FPUL_W, O::Same}},
		{S::fmov, {FM_R | FN_W, O::Same}}, {S::fmov_load, {RM_R | FN_W | MEM | TO_FN}},
		{S::fmov_store, {FM_R | RN_R | MEM | STILL, O::Store}},
		{S::fldi0, {FN_W}}, {S::fldi1, {FN_W}}, {S::fneg, {FN_R | FN_W, O::FNeg}}, {S::fabs_, {FN_R | FN_W}},
		{S::fadd, {FALU, O::FAdd}}, {S::fsub, {FALU, O::FSub}}, {S::fmul, {FALU, O::FMul}},
		{S::fdiv, {FALU, O::FDiv}},
		{S::fcmpeq, {FN_R | FM_R | T_W}}, {S::fcmpgt, {FN_R | FM_R | T_W, O::FGreater}},
	};
	const auto found = forms.find(op.run);

	if (found == forms.end()) {
		return false;
	}
	const uint32_t f = found->second.f;
	const uint64_t rn = uint64_t{1} << op.n, rm = uint64_t{1} << op.m, r0 = 1, t = uint64_t{1} << USE_T;
	const uint64_t pr = uint64_t{1} << USE_PR, fpul = uint64_t{1} << USE_FPUL;
	const uint64_t fn = uint64_t{1} << (USE_FR + op.n), fm = uint64_t{1} << (USE_FR + op.m);

	out = {};
	out.reads = ((f & RN_R) ? rn : 0) | ((f & RM_R) ? rm : 0) | ((f & R0_R) ? r0 : 0) | ((f & T_R) ? t : 0) |
	            ((f & PR_R) ? pr : 0) | ((f & FPUL_R) ? fpul : 0) | ((f & FN_R) ? fn : 0) | ((f & FM_R) ? fm : 0);
	out.writes = ((f & RN_W) ? rn : 0) | ((f & RM_W) ? rm : 0) | ((f & R0_W) ? r0 : 0) | ((f & T_W) ? t : 0) |
	             ((f & PR_W) ? pr : 0) | ((f & FPUL_W) ? fpul : 0) | ((f & FN_W) ? fn : 0);
	out.loaded = ((f & TO_RN) ? rn : 0) | ((f & TO_PR) ? pr : 0) | ((f & TO_FN) ? fn : 0);
	out.memory = (f & MEM) != 0;
	out.choice = (f & CHOICE) != 0;
	out.on_t = (f & ON_T) != 0;
	out.target = (f & TARGET) != 0;
	out.twice = op.n == op.m && (((f & RN_R) && (f & RM_R)) || ((f & FN_R) && (f & FM_R)));
	out.still = (f & STILL) != 0;
	out.order = found->second.order;
	out.shift = found->second.shift;
	return true;
}

/* What host code does itself in place of a call to the instruction. */
Sh4::Inline Sh4::inlined(void (*run)(Sh4 &, const Sh4Op *))
{
	static const std::unordered_map<void (*)(Sh4 &, const Sh4Op *), Inline> forms = [] {
		std::unordered_map<void (*)(Sh4 &, const Sh4Op *), Inline> m;

		for (uint32_t i = 0; i < Sh4Ops::table_size; i++) {
			m.emplace(Sh4Ops::table[i].run, Sh4Ops::table[i].host);
		}
		return m;
	}();
	const auto found = forms.find(run);

	return found == forms.end() ? Inline::No : found->second;
}

/* The two ends of a block, see Sh4OpsT. */
void Sh4::op_next(Sh4 &c, const Sh4Op *op)
{
	return Sh4Ops::end_next(c, op);
}

void Sh4::op_leave(Sh4 &c, const Sh4Op *op)
{
	return Sh4Ops::end_leave(c, op);
}

/* Whether a conditional branch with a delay slot goes, for this T. */
bool Sh4::taken(const Sh4Op &op, uint32_t t)
{
	return (op.run == Sh4Ops::bts) == (t != 0);
}

/* A delay loop, DT Rn then BF back to it: the BF counts the turns. */
void Sh4::pair_delay_loop(const Sh4Op &before, uint32_t before_pc, Sh4Op &op)
{
	if (before.run == Sh4Ops::dt && op.run == Sh4Ops::bf && op.imm == before_pc) {
		op.run = Sh4Ops::bf_dt;
		op.n = before.n;
	}
}

} /* namespace odin */
