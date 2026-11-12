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

/* The instructions of the command processor, one function each, by family:
 * control flow, level of detail, viewports, matrices and vectors, materials,
 * texheads, lights, meshes, framebuffer and tables. The table at the end
 * gives each opcode its function. */

#include "odin/machine/chips/antarctic/antarctic.hpp"
#include "odin/machine/chips/antarctic/antarctic_decode.hpp"
#include "odin/common/bytes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace odin {

/* Puts the low `width` bits of value in the field of a word at `shift` */
static void
set_field(uint32_t &word, uint32_t shift, uint32_t width, uint32_t value)
{
	const uint32_t mask = ((1u << width) - 1) << shift;

	word = (word & ~mask) | ((value << shift) & mask);
}

/* A byte or a half of a word, the lowest being 0 */
static uint8_t
byte(uint32_t word, unsigned n)
{
	return static_cast<uint8_t>(word >> (8 * n));
}

static uint16_t
half(uint32_t word, unsigned n)
{
	return static_cast<uint16_t>(word >> (16 * n));
}

/* The two texture coordinates of a word, in sixteenths */
static void
read_texcoords(float uv[2], uint32_t word)
{
	uv[0] = s16(word) / 16.0f;
	uv[1] = s16(word >> 16) / 16.0f;
}

static void
set_flag(uint32_t &word, uint32_t flag, bool on)
{
	word = on ? (word | flag) : (word & ~flag);
}

/*
 * Control Flow
 * ============
 *
 * The CP supports jumps and subroutine calls, including conditional calls.
 *
 * HEURISTIC: the call stack is in the command RAM, from the addresses the
 * registers 1500007{4,8} give (push_pc, pop_pc). The games run right with
 * it; a stack out of the command RAM stops Odin.
 */

/* 000	Nop
 *
 *	-------- -------- -------o oooooooo
 */

void
Antarctic::op_nop(const uint32_t *)
{
}


/* 012	Jump
 *
 *	IIIIIIII IIIIIIII CCCCR--o oooooooo
 *	AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *
 * I = Branch identifier, or a size on the screen in 1/16 of a pixel
 * C = Condition
 * R = Relative
 * A = Address or Offset in 32-bit words.
 *
 * Conditions 5 and 9 compare the bounding sphere of the model (561, 055)
 * with I: 5 jumps when its width on the screen is at least I, 9 when it
 * is smaller. PHARRIER picks one of three models of each object this way;
 * its sizes are whole pixels (2, 3, 4, 7, 8, 10, 15, 20, 30, 100, 120, 150
 * and 200).
 *
 * A jump to itself keeps the processor there until the next launch. The
 * first swap of BRAVEFF leaves one in its entry at 48000100 (@0C0D3F64);
 * ending the list there instead changes nothing to its loop.
 *
 * Conditions 1 and D test the word a 095 left. Straight after a 055, D
 * jumps like 5 (HEURISTIC: the SGNASCAR cabinet shows the rows of
 * spectators near the camera, I = 444; far away they are too small to
 * tell. 1 is never used this way).
 */

/* Width of the bounding sphere on the screen, in 1/16 of a pixel
 * (HEURISTIC: diameter * projection distance / distance to the centre.
 * The PHARRIER cabinet shows the finest player model at 2.8 units; with
 * the radius, the SGNASCAR stands turn to their far model too early). A
 * view turned over (021, F below 0) sizes them as the right way: sizes
 * below 0 left the track out of the rear-view mirror of SGNASCAR, which
 * Demul shows. */
float
Antarctic::get_lod_size() const
{
	return 32.0f * m_s.lod.radius * std::fabs(m_s.vp.current.clip.f) / m_s.lod.value;
}

void
Antarctic::op_jump(const uint32_t *inst)
{
	uint32_t addr = jump_address(m_s.pc, inst);
	uint32_t branch_id = inst[0] >> 16;
	bool jump;

	switch ((inst[0] >> 12) & 0xF) {
	case 0x0:
		jump = true;
		break;
	case 0x1:
		jump = m_s.lod.branch_id != branch_id;
		break;
	case 0x5:
		jump = get_lod_size() >= static_cast<float>(branch_id);
		break;
	case 0x6:
		jump = !m_s.lod.cond;
		break;
	case 0x7:
		jump = m_s.lod.cond != 0;
		break;
	case 0x9:
		jump = get_lod_size() < static_cast<float>(branch_id);
		break;
	case 0xD:
		jump = m_s.lod.compared ? m_s.lod.branch_id == branch_id :
		       get_lod_size() >= static_cast<float>(branch_id);
		break;
	default:
		fatal("condition not known", inst[0]);
	}

	if (jump)
		m_s.pc = addr;
	else
		m_s.pc += 8;
}


/* 052	Call
 *
 *	-------- -------- CCCCR--o oooooooo
 *	AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA
 *
 * C = Condition
 * R = Relative
 * A = Address or Offset in 32-bit words.
 */

/* The condition of a call or a return: always, or on the condition the
 * last test of the level of detail left */
bool
Antarctic::call_condition(const uint32_t *inst) const
{
	switch ((inst[0] >> 12) & 0xF) {
	case 0x0:
		return true;
	case 0x4:
		return !m_s.lod.cond;
	case 0x8:
		return m_s.lod.cond != 0;
	default:
		fatal("condition not known", inst[0]);
	}
}

void
Antarctic::op_call(const uint32_t *inst)
{
	uint32_t addr = jump_address(m_s.pc, inst);
	if (call_condition(inst)) {
		/* See 501 */
		m_s.patterns = ((m_s.patterns << 5) | m_s.pattern) & 0x3FFFFFFF;
		push_pc();
		m_s.pc = addr;
	} else
		m_s.pc += 8;
}


/* 082	Return
 *
 *	-------- -------- CCCC---o oooooooo
 *
 * C = Condition
 */

void
Antarctic::op_return(const uint32_t *inst)
{
	if (call_condition(inst)) {
		m_s.pattern = m_s.patterns & 0x1F;
		m_s.patterns >>= 5;
		pop_pc();
	} else
		m_s.pc += 4;
}


/* 1C2	Kill
 *
 *	-------- -------- -------o oooooooo
 */

void
Antarctic::op_end(const uint32_t *)
{
	m_s.running = 0;
}


/* 005	LOD: Test The Bounding Sphere
 *
 *	TTTTTTTT TTTTTTTT TTTT---o oooooooo
 *
 * T = Truncated floating-point radius.
 *
 * Tells whether the sphere around the point of the 561 touches the view
 * volume of the current viewport. SGNASCAR recalls a taller viewport for
 * every 005 (y from -80 to 560) and pops it right after; then it returns
 * (082 with the condition 4) or calls the model (052 with the condition 8).
 * Its cabinet shows the pit crews, spheres of 2 to 2.5 units, from hundreds
 * of units away.
 */

bool
Antarctic::is_lod_sphere_visible() const
{
	const odin_viewport_t *vp = &m_s.vp.current;
	const float f = vp->clip.f, g = vp->clip.f2, r = m_s.lod.radius;
	const float x = m_s.lod.center[0], y = m_s.lod.center[1], z = -m_s.lod.center[2];
	/* Edges of the window on the projection plane, y up. */
	const float l = vp->clip.l - vp->offset.x, u = vp->offset.y - vp->clip.b;
	const float e = vp->clip.r - vp->offset.x, d = vp->offset.y - vp->clip.t;

	if (z + r < vp->clip.n)
		return false;
	if (x * f - z * l < -r * std::hypot(f, l) ||
	    z * e - x * f < -r * std::hypot(f, e))
		return false;
	if (y * g - z * d < -r * std::hypot(g, d) ||
	    z * u - y * g < -r * std::hypot(g, u))
		return false;
	return true;
}

void
Antarctic::op_lod_test_sphere(const uint32_t *inst)
{
	m_s.lod.radius = f32(inst[0] & 0xFFFFF000);
	m_s.lod.cond = is_lod_sphere_visible();
}


/* 055	LOD: Set Threshold
 *
 *	-------- -------- -------o oooooooo
 *	TTTTTTTT TTTTTTTT TTTTTTTT TTTTTTTT
 *
 * T = Floating-point threshold
 *
 *	The radius of the bounding sphere of the model, around the point of
 *	the 561: in PHARRIER it is the distance of the farthest vertex.
 */

void
Antarctic::op_lod_radius(const uint32_t *inst)
{
	m_s.lod.radius = f32(inst[1]);
	m_s.lod.compared = false;
}


/* 095	LOD: Compare The Size
 *
 *	-------- -------- CCCC---o oooooooo
 *	HHHHHHHH HHHHHHHH LLLLLLLL LLLLLLLL
 *
 * C = Form
 * H, L = Sizes on the screen in 1/16 of a pixel, H above L
 *
 * Compares the bounding sphere of the model with the two sizes. It leaves
 * a flag for the jumps with the conditions 6 and 7, and one of the two
 * words for those with the conditions 1 and D.
 *
 * SGNASCAR draws each piece of its scenery twice. The near model has the
 * form 8: drawn from H up, skipped below L, drawn after a 501 in between.
 * The far model has the form 4: drawn below L, skipped from H up, drawn
 * after a 501 in between. The sizes are 32 * radius * 554.256 / distance
 * for 300, 400 and 500 units, 554.256 being a view 60 degrees wide.
 */

void
Antarctic::op_lod_compare(const uint32_t *inst)
{
	const float size = get_lod_size();
	const uint32_t hi = inst[1] >> 16;
	const uint32_t lo = inst[1] & 0xFFFF;

	m_s.lod.compared = true;

	switch ((inst[0] >> 12) & 0xF) {
	case 0x4:
		m_s.lod.cond = size >= static_cast<float>(lo);
		m_s.lod.branch_id = (size >= static_cast<float>(hi)) ? lo : hi;
		break;
	case 0x8:
		m_s.lod.cond = size >= static_cast<float>(hi);
		m_s.lod.branch_id = (size >= static_cast<float>(lo)) ? hi : lo;
		break;
	default:
		fatal("instruction form not known", inst[0]);
	}
}


/*
 * Viewports
 * =========
 *
 * These specify an on-screen rectangle (a subregion of the framebuffer,
 * presumably), a projection matrix, the depth-buffer and depth queue
 * configuration, ambient lighting and clear color. The exact meaning of the
 * various fields are still partially unknown.
 */

static uint32_t
viewport_index(const uint32_t *inst)
{
	return (inst[0] >> 16) & (AntarcticState::VIEWPORTS - 1);
}

/* 021	Viewport: Set Z Clipping
 *
 *	-------- -------- -------o oooooooo
 *	FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF
 *	ffffffff ffffffff ffffffff ffffffff
 *	NNNNNNNN NNNNNNNN NNNNNNNN NNNNNNNN
 *
 * F = Distance of the projection plane for x, in pixels
 * f = Distance of the projection plane for y, in pixels
 * N = Near clipping plane
 *
 *	The two distances are equal nearly everywhere. SGNASCAR sends
 *	735.958 and 755.566 for its main view: its cabinet shows the 3D
 *	picture 2.7% taller than with one distance for both. Its rear-view
 *	mirror has F = -277.149 for f = 305.024: the view turned over left
 *	to right, the word before it 00000021 as for any other view.
 *
 * See PH:@0C01587C, PH:@0C0158A4, PH:@0C0158E8.
 *
 *
 * 221	Viewport: Set XY Clipping and Offset
 *
 *	-------- -------- -------o oooooooo
 *	YYYYYYYY YYYYYYYY XXXXXXXX XXXXXXXX
 *	--BBBBBB BBBBBBBB -LLLLLLL LLLLLLLL
 *	--TTTTTT TTTTTTTT -RRRRRRR RRRRRRRR
 *
 * T, B, L, R = Clipping planes
 * X, Y = Viewport offset, signed; von4 centres the view of its lever panel
 *        on (960, -85), off the screen. No game sends a negative X: its
 *        sign is assumed (HEURISTIC).
 *
 * See PH:@0C015924
 *
 *
 * 421	Viewport: Set Depth Range
 *
 *	-------- -------- -------o oooooooo
 *	mmmmmmmm mmmmmmmm mmmmmmmm mmmmmmmm
 *	MMMMMMMM MMMMMMMM MMMMMMMM MMMMMMMM
 *	FFF----- -------- -------- --------
 *
 * m = Minimum
 * M = Maximum
 * F = Number of the viewport. Those of a lower number are drawn first,
 *     each over what is there and with a depth buffer of its own
 *     (HEURISTIC: the games use the eight values; what the board does
 *     with them is not known).
 *
 * See PH:@0C015AA6
 *
 *
 * 621	Viewport: Set Depth Queue
 *
 *	-------- ---TTTDW -------o oooooooo
 *	AAAAAAAA BBBBBBBB GGGGGGGG RRRRRRRR
 *	PPPPPPPP PPPPPPPP PPPPPPPP PPPPPPPP
 *	QQQQQQQQ QQQQQQQQ QQQQQQQQ QQQQQQQQ
 *
 * W = Unknown; the flag argument of the builders that take a colour
 * D = No colour: the polygons fade into what is behind them
 * T = Type; 0 is linear, 2, 4 and 6 send a density as P (not understood)
 * R, G, B, A = Fog color; not written when D is set
 * P = 1 / (end - start)
 * Q = near / end; near / start when D is set
 *
 *	The von4 library (0C716320) computes P and Q from a fog start and
 *	end in eye depth units and sets W from its flag argument. The X360
 *	port applies a linear fog on the eye depth from start to end to the
 *	materials with the 881 z bit, and ignores W and T.
 *
 *	The PHARRIER library has two more builders, without a colour, that
 *	set D: 0C015A3E (start, end) and 0C015A72 (type, density, start).
 *	The viewport menu of the game calls their values "Depth Queue Type",
 *	"Start", "End" and "Dns".
 *
 * See PH:@0C0159C4, PH:@0C015A02, PH:@0C015A3E.
 */

void
Antarctic::op_viewport_set(const uint32_t *inst)
{
	odin_viewport_t *vp = &m_s.vp.scratch;

	switch ((inst[0] >> 8) & 7) {
	case 0:
		vp->clip.f = f32(inst[1]);
		vp->clip.f2 = f32(inst[2]);
		vp->clip.n = f32(inst[3]);
		vp->flags |= ODIN_VP_HAS_021;
		break;
	case 2:
		vp->offset.x = s16(inst[1]);
		vp->offset.y = s16(inst[1] >> 16);
		vp->clip.l = s16(inst[2]);
		vp->clip.r = s16(inst[3]);
		vp->clip.b = s16(inst[2] >> 16);
		vp->clip.t = s16(inst[3] >> 16);
		vp->flags |= ODIN_VP_HAS_221;
		break;
	case 4:
		vp->depth.min = f32(inst[1]);
		vp->depth.max = f32(inst[2]);
		vp->depth.func = inst[3] >> 29;
		vp->flags |= ODIN_VP_HAS_421;
		break;
	case 6:
		vp->depth.q_type	= (inst[0] >> 18) & 7;
		vp->depth.q_behind	= (inst[0] >> 17) & 1;
		vp->depth.q_unknown	= (inst[0] >> 16) & 1;
		vp->depth.mask[0]	= byte(inst[1], 0);
		vp->depth.mask[1]	= byte(inst[1], 1);
		vp->depth.mask[2]	= byte(inst[1], 2);
		vp->depth.mask[3]	= byte(inst[1], 3);
		vp->depth.inv_range	= f32(inst[2]);
		vp->depth.near_by_edge	= f32(inst[3]);
		vp->flags |= ODIN_VP_HAS_621;
		break;
	default:
		fatal("instruction form not known", inst[0]);
	}
}


/* 011	Viewport: Set Ambient Color
 *
 *	rrrrrrrr rrrrrrrr ----1--o oooooooo
 *	bbbbbbbb bbbbbbbb gggggggg gggggggg
 *
 * r,g,b = color
 *
 * See PH:@0C037840.
 */

void
Antarctic::op_viewport_ambient(const uint32_t *inst)
{
	odin_viewport_t *vp = &m_s.vp.scratch;

	vp->color.ambient[0] = half(inst[0], 1);
	vp->color.ambient[1] = half(inst[1], 0);
	vp->color.ambient[2] = half(inst[1], 1);

	vp->flags |= ODIN_VP_HAS_011;
}


/* 191	Viewport: Set Clear Color
 *
 *	-------- -------- ----1--o oooooooo
 *	-------a bbbbbbbb gggggggg rrrrrrrr
 *
 * a,r,g,b = color. The libraries send one bit of their alpha, the top one
 * (PH:@0C016368, PH:@0C016396).
 *
 *	The viewport editor of PHARRIER names the second byte "Blue" and the
 *	third "Green" (its table at PH:@0C1D7430, sent by PH:@0C037760): its
 *	labels are crossed. The game clears its sky with 01CC2D0F, and the
 *	cabinet shows it blue.
 *
 * See PH:@0C016368, PH:@0C016396, PH:@0C037760.
 */

void
Antarctic::op_viewport_clear(const uint32_t *inst)
{
	odin_viewport_t *vp = &m_s.vp.scratch;

	vp->color.clear[0] = byte(inst[1], 0);
	vp->color.clear[1] = byte(inst[1], 1);
	vp->color.clear[2] = byte(inst[1], 2);
	vp->color.clear[3] = ((inst[1] >> 24) & 1) ? 0xFF : 0;
	std::copy(std::begin(vp->color.clear), std::end(vp->color.clear), m_s.fb.clear);

	vp->flags |= ODIN_VP_HAS_191;
}


/* 004	Commit Viewport
 *
 *	-------- -----iii -------o oooooooo
 *
 * i = Index
 *
 * See PH:@0C015AD0.
 */

void
Antarctic::op_viewport_commit(const uint32_t *inst)
{
	odin_viewport_t *vp = &m_s.vp.table[viewport_index(inst)];

	*vp = m_s.vp.scratch;
}


/* 003	Recall Viewport
 *
 *	-------- -----iii -pP----o oooooooo
 *
 * i = Index
 *
 * P = Push
 *
 *	Pushes current viewport on the stack and uses the one specified by i.
 *
 * p = Pop
 *
 *	Pops the viewport on the stack and uses it.
 *
 * Information kindly provided by DreamZzz.
 *
 * The recall picks the viewport the polygons use and leaves the viewport
 * registers alone, as in the X360 port (commit 825393E8 copies its whole
 * staging block into the slot, recall 825394F8 only reads the slot). A
 * viewport committed without its own 011 or 621 gets the ones executed
 * last, even after a recall: the AIRTRIX skater is lit by the ambient the
 * frame before set for the scenery.
 *
 * See PH:@0C015AF6, PH:@0C015B12, PH:@0C015B32.
 */

void
Antarctic::op_viewport_recall(const uint32_t *inst)
{
	odin_viewport_t *vp = &m_s.vp.current;

	switch ((inst[0] >> 12) & 0xF) {
	case 0:
		*vp = m_s.vp.table[viewport_index(inst)];
		break;
	case 2:
		m_s.vp.stack[m_s.vp.depth] = *vp;
		m_s.vp.depth++;
		if (m_s.vp.depth >= AntarcticState::VIEWPORT_STACK)
			fatal("viewport stack full", inst[0]);

		*vp = m_s.vp.table[viewport_index(inst)];
		break;
	case 4:
		m_s.vp.depth--;
		if (m_s.vp.depth < 0)
			fatal("viewport stack empty", inst[0]);
		*vp = m_s.vp.stack[m_s.vp.depth];
		break;
	default:
		fatal("instruction form not known", inst[0]);
	}
}


/*
 * Modelview Matrix
 * ================
 *
 * The CP uses command 161 to upload each (row) vector of the modelview matrix
 * separately.  The CP can also perform instanced drawing, and instructed to do
 * so through command 161 (HYPOTHESIS: no source says so, and no bit of 161 is
 * known for it; each mesh is drawn once).
 *
 * The other commands here set various vectors used for lighting (i.e. light
 * position and direction) but are not well understood.
 */

/* res = v through m, vectors as rows */
static void
transform(float res[4], const float m[4][4], const float v[4])
{
	for (unsigned i = 0; i < 4; i++)
		res[i] = m[0][i] * v[0] + m[1][i] * v[1] + m[2][i] * v[2] + m[3][i] * v[3];
}

/* The modelview the geometry goes through: the local matrix first, if one
 * was sent since the last row of the modelview. HEURISTIC: whether a second
 * local matrix adds to the first, and what it does to the second modelview,
 * is not known. */
void
Antarctic::get_modelview(odin_modelview_t &out) const
{
	const odin_modelview_t *m = &m_s.mv.modelview, *l = &m_s.mv.local;

	if (!m_s.mv.has_local) {
		out = *m;
		return;
	}

	for (unsigned i = 0; i < 4; i++) {
		for (unsigned j = 0; j < 3; j++)
			out.mtx[i][j] = m->mtx[0][j] * l->mtx[i][0] +
			                m->mtx[1][j] * l->mtx[i][1] +
			                m->mtx[2][j] * l->mtx[i][2] +
			                ((i == 3) ? m->mtx[3][j] : 0.0f);
		out.mtx[i][3] = (i == 3) ? 1.0f : 0.0f;
	}
}

static float
length3(const float v[4])
{
	return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

/* 161	Set Matrix Vector
 *
 *	-------- ----MPNN -EL----o oooooooo
 *	XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX
 *	YYYYYYYY YYYYYYYY YYYYYYYY YYYYYYYY
 *	ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ
 *
 * M = Unknown. The libraries set it on the last row of a local matrix.
 *
 * P = Second modelview
 *
 *	The row goes to the second modelview instead of the current one. A
 *	vertex with bit 12 of its header follows that matrix: each segment
 *	of the AIRTRIX trails is a quad with one edge on each, and the
 *	joints of its skater are skinned the same way.
 *
 * L = Local matrix
 *
 *	The row belongs to a matrix that the geometry goes through before
 *	the modelview; E marks its last row. The libraries send rows 0 to 2
 *	as 4 to 6 and the last one as F (PHARRIER 0C016076, a translation).
 *	PHARRIER moves the second half of a door with it, from the modelview
 *	of the first (0C0DEA90), and turns the second stroke of its star
 *	sparkles a quarter of a turn; the cabinet shows the stars. The
 *	models of RACER set it to the identity.
 *
 * N = Column index
 *
 *
 * 561	LOD: Set Vector
 *
 *	-------- ------NN -WW----o oooooooo
 *	XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX
 *	YYYYYYYY YYYYYYYY YYYYYYYY YYYYYYYY
 *	ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ
 *
 * N = Always 11b
 *
 * W = Always 11b
 *
 * Uploads a vector used for LOD computations: the centre of the bounding
 * sphere of the model.
 *
 *
 * 961	Light: Set Vector 9
 *
 *	-------- -------T TWW----o oooooooo
 *	XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX
 *	YYYYYYYY YYYYYYYY YYYYYYYY YYYYYYYY
 *	ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ
 *
 * T = Direction/Position/etc.
 * W = Unknown. 3 in the position of the light of the SEGA init program,
 *     (100, 100, 100) under an identity modelview, which a list of
 *     BRAVEFF sends too; 0 everywhere else in the six attract loops.
 *
 *
 * B61	Light: Set Vector B
 *
 *	-------- -------- TWW----o oooooooo
 *	XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX
 *	YYYYYYYY YYYYYYYY YYYYYYYY YYYYYYYY
 *	ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ ZZZZZZZZ
 *
 * T = Direction/Position/etc.
 * W = Unknown
 */

void
Antarctic::op_vector(const uint32_t *inst)
{
	switch ((inst[0] >> 8) & 0xF) {
	case 0x1:
		vector_matrix_row(inst);
		break;
	case 0x5:
		vector_lod_center(inst);
		break;
	case 0x9:
	case 0xB:
		vector_light(inst);
		break;
	default:
		fatal("instruction form not known", inst[0]);
	}
}

/* The three floats of the instruction */
static void
read_vector(float v[3], const uint32_t *inst)
{
	v[0] = f32(inst[1]);
	v[1] = f32(inst[2]);
	v[2] = f32(inst[3]);
}

/* A row of a matrix; the other rows stay as they are. */
static void
set_row(odin_modelview_t &mv, uint32_t row, const uint32_t *inst)
{
	read_vector(mv.mtx[row], inst);
	mv.mtx[row][3] = (row == 3) ? 1.0f : 0.0f;
}

/* 161: a row of the modelview, of the second modelview or of the local
 * matrix */
void
Antarctic::vector_matrix_row(const uint32_t *inst)
{
	const bool second = ((inst[0] >> 18) & 1) != 0;
	const uint32_t row = (inst[0] >> 16) & 3;

	/* A row of the local matrix; bit 14 is on its last one. */
	if ((inst[0] & 0x0000B000) == 0x00002000) {
		set_row(m_s.mv.local, row, inst);
		if (inst[0] & 0x00004000)
			m_s.mv.has_local = 1;
		return;
	}

	/* Bit 19 alone and bits 15, 14 and 12 are not understood. */
	if (inst[0] & 0x0008F000) {
		error("@%08X: unhandled modelview bits %08X", m_s.pc, inst[0]);
		if (inst[0] & 0x0000F000)
			return;
	}

	if (second) {
		set_row(m_s.mv.second, row, inst);
		return;
	}

	/* A row alone leaves the others as they are: PHARRIER sends
	 * the four rows with the first character of a "+50", scaled
	 * by 0.1875, then the last row only for the next ones.
	 *
	 * We store the columns as rows to facilitate the
	 * translation to GL column-major convertion in the
	 * renderer. */
	set_row(m_s.mv.modelview, row, inst);
	m_s.mv.has_local = 0;
}

/* 561: the centre of the bounding sphere of the model */
void
Antarctic::vector_lod_center(const uint32_t *inst)
{
	odin_modelview_t eye;
	float v[4], w[4];

	read_vector(v, inst);
	v[3] = 1.0f;

	/* The point in eye space and its distance. */
	get_modelview(eye);
	transform(w, eye.mtx, v);
	m_s.lod.center[0] = w[0];
	m_s.lod.center[1] = w[1];
	m_s.lod.center[2] = w[2];
	m_s.lod.value = length3(w);
}

/* 961, B61: the direction, the position or the spot axis of the light */
void
Antarctic::vector_light(const uint32_t *inst)
{
	odin_light_t &lit = m_s.lit.scratch;

	/* The light vectors arrive in eye space: the von4 library
	 * transforms them by XMTRX on the SH-4 before emitting them
	 * (0C715E00 directions, 0C715F20 positions) and the X360 port
	 * does the same in its API (8253B5B8, 8253B530); the GPU does
	 * not apply the modelview to them. */
	switch (inst[0] & 0x000FF000) {
	case 0x00008000: /* Direction */
		if (((inst[0] >> 8) & 0xF) == 0xB) {
			/* B61: spot axis, from the light to its target
			 * (von4 library 0C715FA0). */
			read_vector(lit.spot, inst);
			lit.flags |= ODIN_LIGHT_HAS_SPOT;
		} else {
			read_vector(lit.direction, inst);
			lit.flags |= ODIN_LIGHT_HAS_DIRECTION;
		}
		break;
	case 0x00010000: /* Position */
	case 0x00016000: /* Position, W = 3 */
		read_vector(lit.position, inst);
		lit.flags |= ODIN_LIGHT_HAS_POSITION;
		break;
	default:
		error("CP @%08X: unhandled light 161 param: %08X", m_s.pc, inst[0]);
		break;
	}
}


/*
 * Materials
 * =========
 *
 * A material holds the colours of a polygon, its shading mode, its blending
 * mode and its alpha thresholds.
 */

static uint32_t
material_index(const uint32_t *inst)
{
	return (inst[0] >> 16) & (AntarcticState::MATERIALS - 1);
}

/* 091	Material: Set Diffuse
 *
 *	-------- -------- -------o oooooooo
 *	xxxxxxxx BBBBBBBB GGGGGGGG RRRRRRRR
 *
 * x = Not a field. The libraries of VON4 (@0C716AC0) and of BRAVEFF
 *     (@0C0D69A0) keep each colour of a material in three bytes and load
 *     this one as a word: the byte is the red of the 291 that follows.
 *
 * See PH:@0C0CF742.
 *
 *
 * 291	Material: Set Colour 1 (blended over 091 by the vertex alpha when
 *	881 bit a is set, X360 vo3d.0x00100)
 *
 *	-------- -------- -------o oooooooo
 *	-------- BBBBBBBB GGGGGGGG RRRRRRRR
 *
 * See PH:@0C0CF742.
 *
 *
 * 491	Material: Set Specular/Shininess
 *
 *	-------- -------- -------o oooooooo
 *	SSSSSSSS BBBBBBBB GGGGGGGG RRRRRRRR
 *
 * S = Shininess
 *
 * See PH:@0C0CF798, PH:@0C01782C.
 *
 *
 * 691	Material: Set Lighting Ambient (16 bits per channel, 255 is 1.0;
 *	the X360 port saturates it to 8 and lights with it, 825B4DE8, but
 *	the hardware goes above: PHARRIER sends 600)
 *
 *	RRRRRRRR RRRRRRRR -------o oooooooo
 *	BBBBBBBB BBBBBBBB GGGGGGGG GGGGGGGG
 *
 * See PH:@0C0CF7CC.
 *
 *
 * A91	Material: Set Blending Mode, in two words
 *
 *	-------- ------mm -------o oooooooo
 *	-------- -------- -------- --------
 *
 * BRAVEFF only. Its library makes the words 881, A81 and C81 by adding 0x200
 * to 0x891 and takes the 0x10 off the first alone (@0C0D6A0A; VON4 has it
 * right, @0C716B1A): the blending mode leaves as A91 and the alpha index as
 * a C91 behind it, which the size bits make the second word of the A91. The
 * cabinet shows the fires of the game added to the picture, as mode 2 does:
 * the board takes the mode from the first word, as it does for A81.
 * HEURISTIC: the second word is dropped. The board may run it as a C81
 * instead; the alpha index it would set is not used here.
 */

void
Antarctic::op_material_color(const uint32_t *inst)
{
	odin_material_t *mat = &m_s.mat.scratch;

	switch ((inst[0] >> 8) & 15) {
	case 0:
		mat->diffuse[0] = byte(inst[1], 0);
		mat->diffuse[1] = byte(inst[1], 1);
		mat->diffuse[2] = byte(inst[1], 2);
		mat->flags |= ODIN_MAT_HAS_091;
		break;
	case 2:
		mat->ambient[0] = byte(inst[1], 0);
		mat->ambient[1] = byte(inst[1], 1);
		mat->ambient[2] = byte(inst[1], 2);
		mat->flags |= ODIN_MAT_HAS_291;
		break;
	case 4:
		mat->specular[0] = byte(inst[1], 0);
		mat->specular[1] = byte(inst[1], 1);
		mat->specular[2] = byte(inst[1], 2);
		mat->specular[3] = byte(inst[1], 3);
		mat->flags |= ODIN_MAT_HAS_491;
		break;
	case 6:
		mat->unknown[0] = half(inst[0], 1);
		mat->unknown[1] = half(inst[1], 0);
		mat->unknown[2] = half(inst[1], 1);
		mat->flags |= ODIN_MAT_HAS_691;
		break;
	case 0xA:
		op_material_flags(inst);
		break;
	default:
		fatal("instruction form not known", inst[0]);
	}
}


/* 081	Material: Set Unknown
 *
 *	-------- ----mmmm ---n---o oooooooo
 *
 * n, m = Unknown
 *
 *
 * 881	Material: Set Flags
 *
 *	XXXXXXXX xyhatzSS -------o oooooooo
 *
 * X = Copies of x: the libraries load the flags of the game as a signed
 *     byte (VON4 @0C716B16).
 *
 * x, y = Unknown. Used in BRAVEFF, not in its attract loop.
 *
 * S = Shading mode
 *
 *	0 = Unlit.
 *	1 = Gouraud.
 *	2 = Flat? (used for FX and some geometry in PHARRIER).
 *
 * z = Depth blend (fog)
 *
 *	Selects the fogged shader variant in the X360 port (vo3d digit 1).
 *
 * t = Textured
 *
 * a = Alpha mode
 *
 *	Apparently only used for the skate in AIRTRIX.
 *
 * h = Highlight mode
 *
 *	Used for the player character in PHARRIER.
 *
 * See PH:@0C0CF700.
 *
 *
 * A81	Material: Set Blending Mode
 *
 *	-------- ------mm -------o oooooooo
 *
 * m = Blending Mode
 *
 *	Apparently almost always zero. It is 2 only for lights and star patches
 *	in AIRTRIX.
 *
 * See PH:@0C0CF7FA.
 *
 *
 * C81	Material: Set Alpha Test
 *
 *	-------- --IIIIII -------o oooooooo
 *
 * I = Index intop the alpha threshold table (see instruction 154).
 *
 * See PH:@0C0CF868-@0C0CF876.
 */

void
Antarctic::op_material_flags(const uint32_t *inst)
{
	odin_material_t *mat = &m_s.mat.scratch;

	switch ((inst[0] >> 8) & 0xF) {
	case 0x0:
		mat->w081 = inst[0];
		mat->flags |= ODIN_MAT_HAS_081;
		break;
	case 0x8:
		mat->w881 = inst[0];
		mat->flags |= ODIN_MAT_HAS_881;
		break;
	case 0xA:
		mat->wA81 = inst[0];
		mat->flags |= ODIN_MAT_HAS_A81;
		break;
	case 0xC:
		mat->wC81 = inst[0];
		mat->flags |= ODIN_MAT_HAS_C81;
		break;
	default:
		fatal("instruction form not known", inst[0]);
	}
}


/* 084	Commit Material
 *
 *	------nn nnnnnnnn ---1---o oooooooo
 *
 * n = Index
 *
 * See PH:@0C0153D4, PH:@0C0CF878.
 */

void
Antarctic::op_material_commit(const uint32_t *inst)
{
	uint32_t index = m_s.mat.base + material_index(inst);

	if (!in_table(index, AntarcticState::MATERIALS, "CP: material commit index exceeds MAX (%u >= %u), skipping"))
		return;

	m_s.mat.table[index] = m_s.mat.scratch;
}


/* 083	Recall Material
 *
 *	nnnnnnnn nnnnnnnn ---A---o oooooooo
 *
 * n = Index
 * A = Active
 *
 * See @0C00657C, PH:@0C0CF882.
 */

void
Antarctic::op_material_recall(const uint32_t *inst)
{
	uint32_t index = material_index(inst);

	if (!(inst[0] & 0x1000))
		m_s.mat.base = index;
	else {
		index += m_s.mat.base;
		if (!in_table(index, AntarcticState::MATERIALS, "CP: material recall index exceeds MAX (%u >= %u), skipping"))
			return;

		m_s.mat.scratch = m_s.mat.table[index];
	}
}


/*
 * Texheads
 * ========
 *
 * Textures used for 3D rendering are stored (through the GPU IDMA) in the two
 * available TEXRAM banks.
 */

static uint32_t
texhead_index(const uint32_t *inst)
{
	return (inst[0] >> 16) & (AntarcticState::TEXHEADS - 1);
}

/* 0C1	Texhead: Set Bias
 *
 *	----BBBB AAAA--MM -------o oooooooo
 *
 * A, B = Level bias, signed: a byte of the texture record of the game
 *        (von4 0C716B60), 0, E0, D0, F0, D8, B0, F8... with the filter 1,
 *        10, 20 and 7F too in PHARRIER. The X360 port decodes it and never
 *        reads it. Taken in eighths of a level (HEURISTIC, see the
 *        renderer).
 * M = Filter: 0 nearest, 1 trilinear, 2 bilinear without mipmaps, 3 left
 *     at the sampler defaults (X360 825C6650).
 *
 *	Mode 0 is used frequently with values 0 and FF.
 *
 *	Mode 1 is used with a variety of values.
 *
 *	Mode 2 is only used in BRAVEFF.
 *
 *
 * 2C1	Texhead: Set Format/Size
 *
 *	UUUFFFrr wwHHHWWW ba-----o oooooooo
 *
 * a = UVs in whole texels, else 12.4 (X360 8252D710). In SGNASCAR it is 1
 *     for hi-res LODs, 0 for lo-res LODs.
 *
 * b = Extensively used in PHARRIER.
 *
 * F = Format
 *
 * r = Mirror (bit 24 for U, bit 25 for V); wins over repeat.
 *
 * w = Repeat (bit 22 for U, bit 23 for V); else clamp to edge.
 *
 *	Read in the X360 port (825C6C98); the PH reading below
 *	had U and V swapped.
 *
 * H = log16 of Height
 * W = log16 of Width
 *
 * U = How the texel combines with the material colours in the X360 port's
 *     shaders (8252E368 byte 6, 825B4340, vo3d vertex shaders): 0 texel,
 *     1 texel over 091 by its alpha, 2 texel times 291, 4 091 with the
 *     texel alpha, 5 291 over 091 by the texel; 3, 6 and 7 keep the
 *     previous mode there.
 *
 * See PH:@0C015BCC.
 *
 *
 * 4C1	Texhead: Set Slot
 *
 *	nnnnnnnn mmmmmmmm ---b---o oooooooo
 *
 * n = Slot Y
 * m = Slot X
 * b = TEXRAM bank
 *
 * NOTE: for some reason the BOOTROM uploads a couple of 2C1/4C1 instructions
 * with their parameters swapped. This hasn't been seen in any game so far.
 *
 * See PH:@0C015BA0.
 */

void
Antarctic::op_texhead_set(const uint32_t *inst)
{
	odin_texhead_t *th = &m_s.tex.scratch;

	switch ((inst[0] >> 8) & 7) {
	case 0:
		th->w0C1 = inst[0];
		th->flags |= ODIN_TEX_HAS_0C1;
		break;
	case 2:
		th->w2C1 = inst[0];
		th->flags |= ODIN_TEX_HAS_2C1;
		break;
	case 4:
		th->w4C1 = inst[0];
		th->flags |= ODIN_TEX_HAS_4C1;
		break;
	default:
		fatal("instruction form not known", inst[0]);
	}
}


/* 0D1	Texhead: Set Panning
 *
 *	-------- ------MM -------o oooooooo
 *	YYYYYYYY YYYYYYYY XXXXXXXX XXXXXXXX
 *
 * M = Offset slot 1..3, picked per vertex by bits 19:18 of the vertex
 *     header; slot 0 is stored but never applied (X360 82538E08, 82539020).
 * X, Y = Signed amounts in 1/16 texel.
 */

void
Antarctic::op_texcoord_offset(const uint32_t *inst)
{
	unsigned m = (inst[0] >> 16) & 3;

	m_s.texoffset[m][0] = static_cast<int16_t>(inst[1]);
	m_s.texoffset[m][1] = static_cast<int16_t>(inst[1] >> 16);
}


/* 0C4	Commit Texhead
 *
 *	------nn nnnnnnnn ---uoooo oooooooo
 *
 * n = Index, added to the base
 * u = Always 1 in the attract loops
 *
 * See PH:@0C01545C.
 */

void
Antarctic::op_texhead_commit(const uint32_t *inst)
{
	uint32_t index = m_s.tex.base + texhead_index(inst);

	if (!in_table(index, AntarcticState::TEXHEADS, "CP: texhead commit index exceeds MAX (%u >= %u), skipping"))
		return;

	m_s.tex.table[index] = m_s.tex.scratch;
}


/* 0C3	Recall Texhead
 *
 *	nnnnnnnn nnnnnnnn ---M---o oooooooo
 *
 * n = Index
 * M = Modifier: 0 = set base only, 1 = recall for real
 *
 *	The index is added to the base. The attract loops set bases of up to
 *	7AC3 (VON4) and recall indices of up to 16D (PHARRIER); how many bits
 *	of the sum the board keeps is not known, 16 here.
 */

void
Antarctic::op_texhead_recall(const uint32_t *inst)
{
	uint32_t index = texhead_index(inst);

	if (!(inst[0] & 0x1000))
		m_s.tex.base = index;
	else {
		index += m_s.tex.base;
		if (!in_table(index, AntarcticState::TEXHEADS, "CP: texhead recall index exceeds MAX (%u >= %u), skipping"))
			return;

		m_s.tex.scratch = m_s.tex.table[index];
	}
}


/*
 * Lights
 * ======
 *
 * According to the system16.com specs, the hardware supports 1024 lights per
 * scene, and 4 lights per polygon. It supports several light types (ambient,
 * spot, etc.) and several emission types (constant, infinite linear, square,
 * reciprocal, and reciprocal squared.)
 *
 * AFAICS, the hardware supports two light-related objects: lights and
 * lightsets. A light specifies the position, direction, emission properties
 * and more of a single light. A lightset specifies a set of (up to) four
 * lights that insist on the mesh being rendered. This setup is consistent with
 * the system16 specs.
 */

static uint32_t
light_index(const uint32_t *inst)
{
	return (inst[0] >> 16) & (AntarcticState::LIGHTS - 1);
}

static uint32_t
lightset_index(const uint32_t *inst)
{
	return (inst[0] >> 16) & (AntarcticState::LIGHTSETS - 1);
}

/* 061	Light: Set Attenuation
 *
 *	-------- ------TT -------o oooooooo
 *	PPPPPPPP PPPPPPPP PPPPPPPP PPPPPPPP
 *	QQQQQQQQ QQQQQQQQ QQQQQQQQ QQQQQQQQ
 *	-------- -------- -------- --------
 *
 * T = Attenuation type
 *
 *	0 = Linear (or infinite if P = Q = 1)
 *	1 = Square
 *	2 = Inverse linear
 *	3 = Inverse square
 *
 * P, Q = Attenuation parameters
 *
 * For attenuation type 0, P and Q are:
 *
 *  P = 1.0f / (FR4 - FR5)
 *  Q = -FR5
 *
 * For attenuation type 1, P and Q are:
 *
 *  P = 1.0f / (FR4**2 - FR5**2)
 *  Q = -FR5**2
 *
 * For attenuation type 2, P and Q are:
 *
 *  P = (FR4 * FR5) / (FR4 - FR5)
 *  Q = -1.0f / |FR5|
 *
 * For attenuation type 3, P and Q are:
 *
 *  P = (FR4**2 * FR5**2) / (FR5**2 - FR4**2)
 *  Q = -1.0 / |FR5**2|
 */

void
Antarctic::op_light_attenuation(const uint32_t *inst)
{
	odin_light_t *lit = &m_s.lit.scratch;

	set_field(lit->flags, ODIN_LIGHT_ATT_TYPE_SHIFT, 2, inst[0] >> 16);
	lit->attenuation[0] = f32(inst[1]);
	lit->attenuation[1] = f32(inst[2]);

	lit->flags |= ODIN_LIGHT_HAS_061;
	lit->flags &= ~uint32_t{ODIN_LIGHT_HAS_POSITION};
	lit->flags &= ~uint32_t{ODIN_LIGHT_HAS_DIRECTION};
}


/* 051	Light: Set Diffuse
 *
 *	-------- D---AAAA -------o oooooooo
 *	--BBBBBB BBBBGGGG GGGGGGRR RRRRRRRR
 *
 * D = Unknown. BRAVEFF sets it on the light of its aiming reticles, which
 *     its cabinet shows lit: it does not switch the light off.
 * A = Ambient level, 0 to 15
 *
 *	The light adds A / 16 of its colour to every polygon it reaches,
 *	facing it or not. The light editor of PHARRIER shows the field as
 *	"amb", 0 to 15, next to the colour (PH:@0C1D7048); SGNASCAR sends
 *	(int) (16 * level), 15 at most (@0C062200). von4 leaves it at 0 and
 *	the X360 port ignores it.
 *
 * See PH:@0C0178C6; for a,b,c computation see PH:@0C03DC66.
 *
 *
 * 451	Light: Set Spot Cone
 *
 *	-------D -------- -------o oooooooo
 *	------f- WWWWWWWW CCCCCCCC CCCCCCCC
 *
 * D = No cone (the library sets it for every light without a cone)
 * C = cos (outer angle), 1.15 fixed point
 * W = Scale of the fade, an 8-bit float: 4 bits of exponent, 4 of mantissa
 * f = Unknown flag (dl_emit_spotlight argument)
 *
 *	Without a cone the second word is no field: the library writes the
 *	first word again (PH:@0C017C58, 01000451 01000451) or the constant
 *	it builds it from (PH:@0C017D64, 01000451 01000400).
 *
 *	The light editor of PHARRIER names four types of light, INFINITE,
 *	POINT, SPOT and CONSTANT (table at PH:@0C16E664), and sends them
 *	(PH:@0C03DCD0) as a direction through the view matrix, a position, a
 *	position with a cone, and a direction left as it is (0C017D64). Its
 *	attenuations are NO, LINEAR, RECIPROCAL, RECIPROCAL2 and SQUARE.
 *
 *	Emitted by the von4 library dl_emit_spotlight (0C715FA0) after the
 *	B61 vector: it halves its two angles, sorts their cosines and sends
 *	the smaller one as C. The X360 port fades the light with sat ((L .
 *	axis - cos outer) / (cos inner - cos outer)), axis towards the light.
 *	The library sends W = (bits (1.0f) - bits (cos inner - cos outer))
 *	>> 19, the top bits of an approximate reciprocal: the scale is taken
 *	as (1 + mantissa / 16) * 2^exponent, up to 12 % above the exact
 *	1 / (cos inner - cos outer).
 *
 * See PH:@0C017A7C, PH:@0C017B6C, PH:@0C017C58, PH:@0C017CD4, PH:@0C017D64.
 */

void
Antarctic::op_light_color(const uint32_t *inst)
{
	odin_light_t *lit = &m_s.lit.scratch;

	switch ((inst[0] >> 8) & 7) {
	case 0:
		set_flag(lit->flags, ODIN_LIGHT_051_BIT, (inst[0] >> 23) & 1);
		set_field(lit->flags, ODIN_LIGHT_AMBIENT_SHIFT, 4, inst[0] >> 16);
		lit->diffuse[0] = inst[1] & 0x3FF;
		lit->diffuse[1] = (inst[1] >> 10) & 0x3FF;
		lit->diffuse[2] = (inst[1] >> 20) & 0x3FF;
		lit->flags |= ODIN_LIGHT_HAS_051;
		break;
	case 4:
		{
			const float cos_outer = static_cast<float>(inst[1] & 0xFFFF) * (1.0f / 32768.0f);
			const uint32_t w = (inst[1] >> 16) & 0xFF;

			set_flag(lit->flags, ODIN_LIGHT_HAS_CONE, !((inst[0] >> 24) & 1));
			lit->cos_outer = cos_outer;
			lit->cone_scale = std::ldexp(1.0f + static_cast<float>(w & 15) / 16.0f, static_cast<int>(w >> 4));
		}
		lit->flags |= ODIN_LIGHT_HAS_451;
		break;
	default:
		fatal("instruction form not known", inst[0]);
	}
}


/* 006	Unknown
 *
 *	-------- -------- -------o oooooooo
 *
 * Every game sends 006 then 046 with n set at the end of the list that
 * resets the GPU (von4 library 0C711F90), after an identity modelview.
 * RACER alone sends them later: seven lists written by hand in its
 * program, each one flat quad that opens with 006 and an identity local
 * matrix and closes with 046, n clear. The 400 stars behind its opening
 * text are such lists, and they match the cabinet with both ignored (the
 * table runs op_nop). No library emits them, and the decoder of the X360
 * port skips every word of this type (8252E5C8).
 */


/* 046	Unknown
 *
 *	-------- -------n ----oooo oooooooo
 *
 * n = Unknown
 *
 * See 006.
 */


/* 104	Commit Light
 *
 *	------nn nnnnnnnn ----oooo oooooooo
 *
 * n = Index
 */

void
Antarctic::op_light_commit(const uint32_t *inst)
{
	uint32_t index = light_index(inst);

	m_s.lit.table[index] = m_s.lit.scratch;
}


/* 064  Commit Lightset
 *
 *      -------- nnnnnnnn ---M---o oooooooo
 *      ------bb bbbbbbbb ------aa aaaaaaaa
 *      ------dd dddddddd ------cc cccccccc
 *	-------- -------- -------- --------
 *
 * M = Unknown (0 in the BOOTROM, 1 elsewhere)
 * n = Lightset index
 * a,b,c,d = Indices of four lights
 *
 *	The set keeps the four numbers, as in the X360 port (825C51D0): the
 *	lights are read when it is recalled.
 *
 * See PH:@0C017DF0.
 */

void
Antarctic::op_lightset_commit(const uint32_t *inst)
{
	uint32_t index = m_s.lit.base + lightset_index(inst);

	if (!in_table(index, AntarcticState::LIGHTSETS, "CP: lightset commit index exceeds MAX (%u >= %u), skipping"))
		return;

	AntarcticState::Lightset &set = m_s.lit.sets[index];

	set.lights[0] = inst[1] & (AntarcticState::LIGHTS - 1);
	set.lights[1] = (inst[1] >> 16) & (AntarcticState::LIGHTS - 1);
	set.lights[2] = inst[2] & (AntarcticState::LIGHTS - 1);
	set.lights[3] = (inst[2] >> 16) & (AntarcticState::LIGHTS - 1);
	set.committed = 1;
}


/* 043	Recall Lightset
 *
 *	----DDDD nnnnnnnn ---A---o oooooooo
 *
 * A = Clear, the index is the new base of the lightsets
 * D = Disabled lights mask
 * n = Index
 *
 *	Without A the set is recalled all the same (HEURISTIC, as Valkyrie
 *	had it, where the 083 and the 0C3 set their base and no more): the
 *	games send 00000043, and 0E000043 at start, and the six attract loops
 *	draw the same pictures either way.
 *
 *	The four lights are taken as they are now, as in the X360 port
 *	(825C6090): a light committed after its set counts, and a set recalled
 *	before it is committed again gives the lights its old numbers name
 *	today. BRAVEFF recalls its set 3 one frame before it commits it, when
 *	the control room takes fire, and the room flashes on its cabinet. A
 *	set never committed has no light, as in the port.
 */

void
Antarctic::op_lightset_recall(const uint32_t *inst)
{
	uint32_t index = lightset_index(inst);

	if (!(inst[0] & 0x1000))
		m_s.lit.base = index;

	index += m_s.lit.base;
	if (!in_table(index, AntarcticState::LIGHTSETS, "CP: lightset recall index exceeds MAX (%u >= %u), skipping"))
		return;

	const AntarcticState::Lightset &set = m_s.lit.sets[index];
	odin_lightset_t &ls = m_s.lit.scratchset;

	if (!set.committed)
		error("CP: lightset %u recalled before it is committed", index);
	for (uint32_t i = 0; i < 4; i++)
		ls.lights[i] = set.committed ? m_s.lit.table[set.lights[i]] : odin_light_t{};
	ls.flags = set.committed ? ODIN_LIGHTSET_SET : 0;
	set_field(ls.flags, ODIN_LIGHTSET_MASK_SHIFT, 4, inst[0] >> 24);
}


/*
 * Meshes
 * ======
 *
 * This class of instructions pushes (or otherwise deals with) vertex
 * data to the transformation/rasterization pipeline.
 */

/* 101	Mesh: Set Depth Bias
 *
 *	-----FBB BBBBBBBB -------o oooooooo
 *
 * F = Flag, B = Bias. Held as the 11 bits F:B signed; the renderer reads
 *     them one way against the other polygon types and another among the
 *     translucent ones (depth_bias.hpp, HEURISTIC).
 *
 *	The X360 port decodes F apart and B as one unsigned number
 *	(8252E428), then never uses them. SGNASCAR sends only values with F:
 *	its track and walls at 0x400 to 0x403, more layers at 0x500 to 0x503
 *	and 0x600, the shadows of its cars at 0x700. PHARRIER sends its
 *	stars at 0x400, the halos of its planets at 0x500, and 0x406, 0x1F4
 *	and 0x3FF; AIRTRIX its decals, glows and shadows at 10 to 0x3D6,
 *	BRAVEFF its decals at 20 to 102, VON4 2 and 10, SWRACER none.
 *
 * See @0C008040, PH:@0C016418, PH:@0C016446.
 *
 *
 * 301	Mesh: Set Unknown
 *
 *	-------- UNNNNNNN -------o oooooooo
 *
 * U, N = Unknown. Five games send it once a frame, always 0 in their
 *        attract loops: the VON4 library has it at the head of a fixed
 *        list, with a 101 of 0 behind it (@0C71234C).
 *
 *
 * 501	Mesh: Set Pattern
 *
 *	-------- ---PPPPP -------o oooooooo
 *
 * P = Pattern of 4 x 4 pixels of the screen: the polygons that follow are
 *     drawn on its pixels only, whatever their type.
 *
 *	The low four bits go from every pixel (0) down to none (8), then up
 *	again with the pixels the first half leaves out (8 + n is what n
 *	does not draw); bit 4 mirrors the pattern left to right. The X360
 *	port holds the 32 patterns (823000D0) and kills the pixels with
 *	them in every shader (825C6608, vo3d variants xxxxxx1).
 *
 *	The AIRTRIX library keeps the codes of nine levels at four places
 *	(AT:@0C0C841C, AT:@0C049BA6) and wraps a model between a 501 and a
 *	501 0 (AT:@0C049D08): its cabinet shows the combo labels come and
 *	go in dots. PHARRIER draws the shadow of the player with C and 4 in
 *	turn, BRAVEFF its smoke: both cabinets show what lies behind them.
 *
 *	SWRACER sends -7 to -1 on 16 bits for the far scenery it brings in:
 *	the low five bits are taken (HEURISTIC), 1/8 to 7/8 of the pixels.
 *
 *	A subroutine returns with the pattern it was called with (HEURISTIC:
 *	what saves it is not known). A model of the BRAVEFF burning hall
 *	sets C in its own list and never clears it, and the cabinet shows
 *	the firemen and the furniture that follow whole; the port starts
 *	every model without a pattern (82530198). A pattern holds across
 *	the material definitions: the frame of the AIRTRIX messages, drawn
 *	after some, goes away in dots on the cabinet.
 *
 *
 * 901	Mesh: Set Precision (Static)
 *
 *	-------- pppppppp ----100o oooooooo
 *
 * Information kindly provided by CaH4e3.
 */

void
Antarctic::op_mesh_set(const uint32_t *inst)
{
	uint32_t log;

	switch ((inst[0] >> 8) & 0xF) {
	case 1:
		m_s.depth_bias = static_cast<int16_t>(static_cast<int>((inst[0] >> 16) & 0x7FF) -
		                                      static_cast<int>((inst[0] >> 15) & 0x800));
		break;
	case 3:
		break;
	case 5:
		m_s.pattern = (inst[0] >> 16) & 0x1F;
		break;
	case 9:
		/* 2^(log - 0x8D): the sky dome of SWRACER has 0x8E. */
		log = (inst[0] >> 16) & 0xFF;
		m_s.poly.static_mesh_precision = std::ldexp(1.0f, static_cast<int>(log) - 0x8D);
		break;
	default:
		fatal("instruction form not known", inst[0]);
	}
}


/* 103	Set Poly Type
 * 113	Set Poly Type
 *
 *	AAAAAAAA -------- -------o oooxoooo
 *
 * A = Base mesh alpha value
 * x = Size bit: the 113 has a second word. PHARRIER, the only game to
 *     send it, repeats the first word there.
 *
 *  3: Opaque. Alpha is ignored.
 *  9: Punch-through. Alpha is ignored.
 *  D: Translucent.
 *
 * Information kindly contributed by DreamZzz.
 *
 *	The six attract loops send the types 1, 4, 5 and 6 only. The types 2
 *	and 3, the shadows, are drawn as opaque, as in the X360 port; 0 and 7
 *	are not drawn.
 *
 * See AT:@0C049CDA, PH:@0C0173CA, AT:@0C69A220.
 */

void
Antarctic::op_polygon_type(const uint32_t *inst)
{
	const uint32_t type = (inst[0] >> 9) & 7;

	if (((TYPES_NEVER_MET >> type) & 1) && !((m_types_told >> type) & 1)) {
		m_types_told |= 1u << type;
		error("CP @%08X : polygon type %u, never met", m_s.pc, type);
	}
	m_s.poly.type  = type;
	m_s.poly.alpha = static_cast<float>(inst[0] >> 24) * (1.0f / 255.0f);
}


/* 12x	Mesh: Push Position (Static)
 * 1Ax	Mesh: Push Position (Dynamic)
 * 1Bx	Mesh: Push All (Position, Normal, Texcoords) (Dynamic)
 *
 *
 * They appear to have a common 32-bit header:
 *
 *	AAAAAAAA C---OO-- uPRSTTTo oooootpW
 *
 * A = Vertex alpha
 *
 * C = Don't Cull
 *
 *     Disables face culling for this mesh.
 *
 * O = Texcoord offset
 *
 *     Which of the three offsets of the 0D1 the texcoords take; 0 is none.
 *
 * u = Unknown
 *
 *     Set on every vertex but the three that close the list.
 *
 * P = Position
 *
 *     Add the last row of the modelview.
 *
 * R = Rotation
 *
 *     Apply the 3x3 of the modelview.
 *
 *     Nearly every vertex has both. The 2D quads of BRAVEFF have P only
 *     and get just the last row before each quad. The cloud floor of
 *     PHARRIER has R only. The trails of its lock-on lasers have none:
 *     they are built in eye space with XMTRX on the SH-4 (PH:@0C0EE7A0)
 *     and no matrix is sent for them.
 *
 * S = Second modelview
 *
 *     The vertex goes through the matrix a 161 with bit 18 loaded.
 *
 * T = Triangle
 *
 *     The only observed values so far are 0 and 7.
 *
 *     If 0, the vertex is pushed to the GPU vertex buffer. If 7, the vertex is
 *     pushed and defines a triangle along with the two previously pushed
 *     vertices.
 *
 * t = Texcoord pivot?
 *
 *     Apparently only used by 1Bx, which includes texcoord info.
 *
 * p = Position pivot
 *
 *     When 0, the vertex is linked to the previous two according to the
 *     winding bit. When 1, the vertex at offset -2 is kept unchanged in the
 *     vertex buffer, and acts as a pivot for building a triangle fan.
 *
 * W = Winding
 *
 *     Triangle winding specifies whether the vertices composing the
 *     to-be-drawn triangle are to be in the order (0, -1, -2), and W is 0
 *     in this case, or (0, -2, -1), and W is 1 in this case.
 *
 *     If vertex in position -2 is a pivot, it is treated as if it wasn't. [?]
 *
 * For 12x, the rest of the instruction looks like:
 *
 *	-------- -------- -------- --------
 *	xxxxxxxx xxxxxxxx ??????uu uuuuuuuu
 *	yyyyyyyy yyyyyyyy ??????vv vvvvvvvv
 *	zzzzzzzz zzzzzzzz ??????ww wwwwwwww
 *
 * x,y,z = Position
 * u,v,w = Normal
 *
 * For 1AX, the rest of the instruction looks like:
 *
 *	-------- -------- -------- --------
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *
 * x,y,z = Position
 *
 * For 1BX, the rest of the instruction looks like:
 *
 *	-------- -------- -------- --------
 *	xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *	yyyyyyyy yyyyyyyy yyyyyyyy yyyyyyyy
 *	zzzzzzzz zzzzzzzz zzzzzzzz zzzzzzzz
 *	ssssssss ssssssss tttttttt tttttttt
 *	uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu
 *	vvvvvvvv vvvvvvvv vvvvvvvv vvvvvvvv
 *	wwwwwwww wwwwwwww wwwwwwww wwwwwwww
 *
 * x,y,z = Position
 * u,v,w = Normal
 * s,t = Texcoords
 *
 * Meshes come in two flavors: dynamic and static. Dynamic meshes are specified
 * with normal IEEE 754 floating-point values. Static meshes are specified with
 * variable fixed-point values; the fixed-point precision is defined by command
 * 901.
 *
 * Information on the existence of static/dynamic mesh varieties, and info on
 * the fixed-point decoding kindly provided by CaH4e3.
 */

void
Antarctic::op_push_position_static(const uint32_t *inst)
{
	const float scale = m_s.poly.static_mesh_precision / 64.0f;
	Vertex v{};

	v.info.full = inst[0];

	for (unsigned i = 0; i < 3; i++) {
		const uint32_t word = inst[1 + i];

		v.body.position[i] = static_cast<float>(static_cast<int32_t>(word) >> 10) * scale;
		v.body.normal[i] = s16((word & 0x3FF) << 6) / 16384.0f;
	}

	push_vertices(&v, PUSH_POS | PUSH_NRM, 1);
}


void
Antarctic::op_push_position(const uint32_t *inst)
{
	Vertex v{};

	v.info.full = inst[0];

	read_vector(v.body.position, inst);

	push_vertices(&v, PUSH_POS, 1);
}


void
Antarctic::op_push_all(const uint32_t *inst)
{
	Vertex v{};

	v.info.full = inst[0];

	read_vector(v.body.position, inst);

	read_vector(v.body.normal, inst + 4);

	read_texcoords(v.body.texcoords, inst[4]);

	push_vertices(&v, PUSH_POS | PUSH_NRM | PUSH_TXC, 1);
}


/* 0E8	Mesh: Push Texcoords 3
 *
 *	TTTTTTTT TTTTTTTT ----WWWo oooooooC
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *
 * The interaction of U, u, P, W, with the ones specified by the push position/
 * push all instructions is still unknown.
 *
 * T = Texhead of the triangle, from the base. The BOOTROM commits two
 *     texheads and recalls the first only: its text has 0 here, the grid
 *     of its C.R.T. test (lists at ROM 9BE0 and 9C60) has 1, the 16x16
 *     tile that repeats, with texcoords in whole texels.
 * u, v = Texcoords for three points.
 */

void
Antarctic::op_push_texcoords3(const uint32_t *inst)
{
	const uint32_t index = m_s.tex.base + (inst[0] >> 16);
	Vertex vs[3]{};

	/* Another texhead: the three vertices go on in a new mesh. */
	if (index < AntarcticState::TEXHEADS &&
	    std::memcmp(&m_s.tex.scratch, &m_s.tex.table[index], sizeof(m_s.tex.scratch))) {
		end_mesh(m_s.pc);
		m_s.tex.scratch = m_s.tex.table[index];
		begin_mesh(m_s.pc);
	}

	for (unsigned i = 0; i < 3; i++) {
		vs[i].info.full = inst[0] & 0xFFFF;

		read_texcoords(vs[i].body.texcoords, inst[i + 1]);
	}

	push_vertices(&vs[0], PUSH_TXC, 3);
}


/* 158	Mesh: Push Texcoords 1
 *
 *	-------- ?------- ----???o ooooo??C
 *	vvvvvvvv vvvvvvvv uuuuuuuu uuuuuuuu
 *
 * u, v = Texcoords for one point.
 */

void
Antarctic::op_push_texcoords(const uint32_t *inst)
{
	Vertex v{};

	v.info.full = inst[0];

	read_texcoords(v.body.texcoords, inst[1]);

	push_vertices(&v, PUSH_TXC, 1);
}


/*
 * Framebuffer and Tables
 * ======================
 */

/* 181	FB: Set Blending
 *
 *	-------E AAAAAAAA -------o oooooooo
 *
 * E = Enable blending
 * A = Blending factor
 *
 *
 * 781	FB: Set Combiner
 *
 *	-----ENN -----enn -------o oooooooo
 *
 * E = Buffer A, Enable (?)
 * N = Buffer A, Select first buffer
 *
 * e = Buffer B, Enable (?)
 * n = Buffer B, Select second buffer
 *
 * Determines how to linearly combine the framebuffers to obtain the 3D scene.
 * The equation is given by:
 *
 *    result = factor * A + (1 - factor) * B
 *
 * Here A and B can be either the front/back buffer or a 2D layer. The numering
 * should be the same as that of registers 1A000180-1A00019C.
 *
 * The values uploaded here depend on the state of 1A00001C bits 23:24 and
 * 1A000020 bit 0. See @0C0065D6, PH:@0C016336, PH:@0C038952, PH:@0C015B50.
 */

void
Antarctic::op_framebuffer(const uint32_t *inst)
{
	switch ((inst[0] >> 8) & 7) {
	case 1:
		m_s.fb.w181 = inst[0];
		break;
	case 7:
		m_s.fb.w781 = inst[0];
		break;
	default:
		fatal("instruction form not known", inst[0]);
	}
}


/* 088	Flush
 *
 *	-------- U------- ----xxxo oooooooo
 *
 * U = Unknown
 * x = Unknown
 *
 * Always comes as the last instruction. Perhaps some kind of 'flush all'
 * or 'raise IRQ' command or something like that. If it is a 'flush all'
 * command, it may set some GPU ports not set by 1C2 (1A000024 perhaps.)
 * Ignored: the table runs op_nop.
 */


/* 154	Mat: Set Alpha Threshold
 *
 *	-------- --IIIIII -------o oooooooo
 *	HHHHHHHH HHHHHHHH HHHHHHHH LLLLLLLL
 *
 * I = Index
 * L = Alpha low threshold
 * H = Alpha high threshold
 *
 * See PH:@0C017798, PH:@0C0CF868. Used by instruction C81.
 */

void
Antarctic::op_alpha_table(const uint32_t *inst)
{
	m_s.alpha_table[(inst[0] >> 16) & 0x3F] = inst[1];
}


/* 194	Light: Set Table
 *
 *	------NN ---MMMMM -------o oooooooo
 *	LLLLLLLL LLLLLLLL HHHHHHHH HHHHHHHH
 *
 * N, M = Indices
 * L = Data, Lo
 * H = Data, Hi
 *
 * NOTE: definitely related to lighting; possibly spotlight angles.
 *
 * See PH:@0C017A3E.
 */

void
Antarctic::op_light_table(const uint32_t *inst)
{
	uint32_t index1 = (inst[0] >> 24) & 3;
	uint32_t index2 = (inst[0] >> 16) & 0x1F;

	m_s.light_table[index1][index2] = inst[1];
}


/* 3A1	Set Lo Addresses
 *
 *	-------- -------- -----01o oooooooo
 *	llllllll llllllll llllllll llllllll
 *	LLLLLLLL LLLLLLLL LLLLLLLL LLLLLLLL
 *      -------- -------- -------- --------
 *
 *
 * 5A1	Set Hi Addresses
 *
 *	-------- -------- -----10o oooooooo
 *	uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu
 *	UUUUUUUU UUUUUUUU UUUUUUUU UUUUUUUU
 *      -------- -------- -------- --------
 *
 * l,L and u,U = where the two 3D buffers of unit 0 and of unit 1 lie in the
 * FB (the values of 1A000180, 1A000188 and of 1A000200, 1A000208).
 * See PH:@0C016308 for both. Ignored: the table runs op_nop.
 */

/****************************************************************************
 Opcodes
****************************************************************************/

const std::array<Antarctic::Insn, 0x200> &
Antarctic::insns()
{
	static const struct {
		uint32_t first, last;	/* opcodes, the low bits being a variant */
		Op handler;
		uint32_t flags;
	} list[] = {
		{ 0x000, 0x000, &Antarctic::op_nop,			KEEPS_MESH },
		{ 0x003, 0x003, &Antarctic::op_viewport_recall,		0 },
		{ 0x004, 0x004, &Antarctic::op_viewport_commit,		0 },
		{ 0x005, 0x005, &Antarctic::op_lod_test_sphere,		0 },
		{ 0x006, 0x006, &Antarctic::op_nop,			0 },
		{ 0x011, 0x011, &Antarctic::op_viewport_ambient,	0 },
		{ 0x012, 0x012, &Antarctic::op_jump,			SETS_PC },
		{ 0x021, 0x021, &Antarctic::op_viewport_set,		0 },
		{ 0x043, 0x043, &Antarctic::op_lightset_recall,		0 },
		{ 0x046, 0x046, &Antarctic::op_nop,			0 },
		{ 0x051, 0x051, &Antarctic::op_light_color,		0 },
		{ 0x052, 0x052, &Antarctic::op_call,			SETS_PC },
		{ 0x055, 0x055, &Antarctic::op_lod_radius,		0 },
		{ 0x061, 0x061, &Antarctic::op_light_attenuation,	0 },
		{ 0x064, 0x064, &Antarctic::op_lightset_commit,		0 },
		{ 0x081, 0x081, &Antarctic::op_material_flags,		KEEPS_MESH },
		{ 0x082, 0x082, &Antarctic::op_return,			SETS_PC },
		/* A recall (083, 0C3) ends the mesh: the polygons that
		 * follow take the new material and texhead. PHARRIER recalls
		 * both between the quads of one model, with no other
		 * instruction in between. */
		{ 0x083, 0x083, &Antarctic::op_material_recall,		0 },
		{ 0x084, 0x084, &Antarctic::op_material_commit,		0 },
		{ 0x088, 0x088, &Antarctic::op_nop,			0 },
		{ 0x091, 0x091, &Antarctic::op_material_color,		KEEPS_MESH },
		{ 0x095, 0x095, &Antarctic::op_lod_compare,		0 },
		{ 0x0C1, 0x0C1, &Antarctic::op_texhead_set,		0 },
		{ 0x0C3, 0x0C3, &Antarctic::op_texhead_recall,		0 },
		{ 0x0C4, 0x0C4, &Antarctic::op_texhead_commit,		0 },
		{ 0x0D1, 0x0D1, &Antarctic::op_texcoord_offset,		0 },
		{ 0x0E8, 0x0E9, &Antarctic::op_push_texcoords3,		PUSHES },
		{ 0x101, 0x101, &Antarctic::op_mesh_set,		0 },
		{ 0x103, 0x103, &Antarctic::op_polygon_type,		0 },
		{ 0x104, 0x104, &Antarctic::op_light_commit,		0 },
		{ 0x113, 0x113, &Antarctic::op_polygon_type,		0 },
		{ 0x12C, 0x12F, &Antarctic::op_push_position_static,	PUSHES },
		{ 0x154, 0x154, &Antarctic::op_alpha_table,		0 },
		{ 0x158, 0x15B, &Antarctic::op_push_texcoords,		PUSHES },
		{ 0x161, 0x161, &Antarctic::op_vector,			0 },
		{ 0x181, 0x181, &Antarctic::op_framebuffer,		0 },
		{ 0x191, 0x191, &Antarctic::op_viewport_clear,		0 },
		{ 0x194, 0x194, &Antarctic::op_light_table,		0 },
		{ 0x1A1, 0x1A1, &Antarctic::op_nop,			0 },
		{ 0x1AC, 0x1AF, &Antarctic::op_push_position,		PUSHES },
		{ 0x1B8, 0x1BF, &Antarctic::op_push_all,		PUSHES },
		{ 0x1C2, 0x1C2, &Antarctic::op_end,			SETS_PC },
	};
	static const std::array<Insn, 0x200> table = [] {
		std::array<Insn, 0x200> t{};

		for (const auto &e : list)
			for (uint32_t op = e.first; op <= e.last; op++)
				t[op] = Insn{e.handler, e.flags};
		return t;
	}();

	return table;
}

} /* namespace odin */
