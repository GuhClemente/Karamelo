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

/* The processor a build of Odin was made for, checked before anything else
 * of Odin runs (a constructor of the first priority, built for any x86-64):
 * a build for x86-64-v3 or v2 stops with a message on a processor that lacks
 * what it needs, rather than on an instruction the processor does not know.
 * The builds Odin is given out as: x86-64-v3, and an "old" one for
 * x86-64-v2. */

#include <cstdio>
#include <cstdlib>

#include <SDL3/SDL.h>

#if defined(__x86_64__) && defined(__SSE4_2__)

#if defined(__AVX512F__)
#define ODIN_CPU_LEVEL "x86-64-v4"
#define ODIN_CPU_NEEDS "This build of Odin needs a processor with AVX-512 (x86-64-v4). On this computer, use the " \
	"main build of Odin, or the \"old\" one."
#elif defined(__AVX2__)
#define ODIN_CPU_LEVEL "x86-64-v3"
#define ODIN_CPU_NEEDS "This build of Odin needs a processor with AVX2 and FMA (x86-64-v3): a 4th-generation Intel Core " \
	"(2013) or later, or an AMD Ryzen. On this computer, use the \"old\" build of Odin."
#else
#define ODIN_CPU_LEVEL "x86-64-v2"
#define ODIN_CPU_NEEDS "This build of Odin needs a processor with SSE4.2 and POPCNT (x86-64-v2), from 2009 or later."
#endif

namespace {

__attribute__((constructor(101), target("arch=x86-64"))) void
check_processor()
{
#ifndef _WIN32
	__builtin_cpu_init();
	if (__builtin_cpu_supports(ODIN_CPU_LEVEL))
		return;
	/* Not fprintf: where it is fortified (_FORTIFY_SOURCE, Ubuntu), it
	 * is inlined, built for the processor of the build */
	std::fputs("Odin: " ODIN_CPU_NEEDS "\n", stderr);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Odin", ODIN_CPU_NEEDS, nullptr);
	std::_Exit(1);
#endif
}

} /* namespace */

#endif
