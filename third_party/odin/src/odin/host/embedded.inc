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

/* Section, symbol names and separators of the *.S files for each system */

#if defined(_WIN32)
#define READ_ONLY .section .rdata, "dr"
#elif defined(__APPLE__)
#define READ_ONLY .section __TEXT,__const
#else
#define READ_ONLY .section .rodata
#endif

#if defined(__APPLE__)
#define C_NAME(name) _##name
#else
#define C_NAME(name) name
#endif

#if defined(__APPLE__) && defined(__aarch64__)
#define NEXT %%
#else
#define NEXT ;
#endif

#if defined(_WIN32) || defined(__APPLE__)
#define STACK_NOTE
#else
#define STACK_NOTE .section .note.GNU-stack, "", @progbits
#endif
