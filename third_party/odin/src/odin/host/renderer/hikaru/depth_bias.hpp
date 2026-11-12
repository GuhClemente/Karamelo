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

#ifndef ODIN_DEPTH_BIAS_HPP
#define ODIN_DEPTH_BIAS_HPP

/* The 101 of a mesh (Antarctic::op_mesh_set) as the renderer applies it:
 * the eye depth is multiplied by a scale, the pixel stays where it is.
 * The patents of the hardware compare a pixel twice, against the other
 * polygon types and among the translucent ones (US 6 320 580): the 101
 * reads one way for each, and the first only within a family (flagged)
 * (HEURISTIC, all read from the cabinets). */
namespace odin::depth_bias {

/* Against the other types, a unit of bits 25:16 brings the mesh nearer
 * by 5.6 / 65536 of its depth: the shadow of the AIRTRIX board needs 5.5
 * to 5.75, the car shadows of SGNASCAR, as much as 6 % behind its banked
 * track, 5.38 or more. */
constexpr float UNIT = 5.6f / 65536.0f;

/* Against the other types: bits 25:16, unsigned, bit 26 left out. `bias`
 * is the 101 as the list holds it, bits 26:16 signed. */
constexpr float test_scale(int bias)
{
	return 1.0f - static_cast<float>(bias & 0x3FF) * UNIT;
}

/* Bit 26 puts the meshes in two families, and the 101 moves a mesh
 * against those of its own family only; against the other family the
 * depths meet as they are (HEURISTIC, read from the SGNASCAR cabinet). In
 * the pit, the lane (0x500) covers the ground it lies on (0x400), yet the
 * shadows of the crew (0) and the air hose (0), 0.35 % and 0.5 % before
 * the lane, show over it; on the track, the painted lines (0x500) show
 * though they lie as much as 1.5 % behind it (0x400); the wall (0x401)
 * stays before the underside of the stands (0), which lies 0.6 % behind
 * it. No one move for each value gives them all. The card keeps the
 * polygon of each pixel (US 6 239 809): it can weigh the two polygons
 * that meet. */
constexpr bool flagged(int bias)
{
	return (bias & 0x400) != 0;
}

/* What a translucent fragment of a mesh tells the sort of the families:
 * 0 in a view without both, -1 flagged, 1 not */
constexpr float family_sign(bool both, int bias)
{
	return !both ? 0.0f : flagged(bias) ? -1.0f : 1.0f;
}

/* Whether a pixel of a mesh at the eye depth `w` keeps its place against
 * one at `other_w`, as the depth tests weigh them (equal depths keep it) */
constexpr bool before(float w, int bias, float other_w, int other)
{
	if (flagged(bias) != flagged(other))
		return w <= other_w;
	return w * test_scale(bias) <= other_w * test_scale(other);
}

/* Among the translucent fragments: bits 26:16 signed, 1 / 65536 a unit.
 * The halo of a PHARRIER planet (0x500, -768) goes behind the planet it
 * lies 0.02 % before; the glow of an AIRTRIX street lamp (+511) before
 * the glass it lies 0.27 % behind. */
constexpr float sort_scale(int bias)
{
	return 1.0f - static_cast<float>(bias) / 65536.0f;
}

} /* namespace odin::depth_bias */

#endif /* ODIN_DEPTH_BIAS_HPP */
