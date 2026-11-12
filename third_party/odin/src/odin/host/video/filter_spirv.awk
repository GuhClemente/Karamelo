#  ██████╗ ██████╗ ██╗███╗   ██╗
# ██╔═══██╗██╔══██╗██║████╗  ██║
# ██║   ██║██║  ██║██║██╔██╗ ██║
# ██║   ██║██║  ██║██║██║╚██╗██║
# ╚██████╔╝██████╔╝██║██║ ╚████║
#  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
#
# Copyright (C) 2026 Réda Chérif-Touil
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# awk -v part=VERTEX|FRAGMENT -f filter_spirv.awk <folder>/*.glsl <pass> : the
# source of a pass of a filter of the picture for Vulkan (CMakeLists.txt), as
# picture_preset.cpp's part() makes it for OpenGL: its version first, then
# its part defined. Each parameter the shaders of its folder declare
# (#pragma parameter) a specialization constant at its first value, where
# OpenGL makes it a constant of the source (picture_filter_gl.cpp): the
# filter gives it the value of its preset (picture_filter_vk.cpp). As
# picture_preset.cpp's declared(), a name or a description longer than 63
# characters, or no description, declares nothing.
FNR == 1 { file++ }
file < ARGC - 1 && /^#pragma parameter/ {
	split($0, quoted, "\"")
	split(quoted[3], after, " ")
	value = after[1]
	if (value !~ /[.eE]/)
		value = value ".0"
	if (length($3) > 63 || length(quoted[2]) < 1 || length(quoted[2]) > 63)
		next
	if (!($3 in id)) {
		id[$3] = count++
		initial[$3] = value
	}
	next
}
file == ARGC - 1 { lines[++n] = $0 }
END {
	for (i = 1; i <= n; i++) {
		line = lines[i]
		if (line ~ /^#version/) {
			version = line
			line = ""
		} else if (line ~ /^#pragma parameter/ || line ~ /^#define texture/) {
			line = ""
		} else if (line ~ /^uniform float [A-Za-z0-9_]+;/) {
			name = line
			sub(/^uniform float /, "", name)
			sub(/;.*/, "", name)
			if (name in id)
				line = "layout(constant_id = " id[name] ") const float " name " = " initial[name] ";"
		}
		lines[i] = line
	}
	print version
	print ""
	print "#extension GL_ARB_shading_language_420pack : enable"
	print "#define " part
	print "#define PARAMETER_UNIFORM"
	print ""
	print "#line 1"
	for (i = 1; i <= n; i++)
		print lines[i]
}
