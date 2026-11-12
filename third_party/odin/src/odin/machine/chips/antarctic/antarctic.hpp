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

#ifndef ODIN_ANTARCTIC_HPP
#define ODIN_ANTARCTIC_HPP

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

#include "odin/machine/chips/antarctic/antarctic_list.hpp"

namespace odin {

/* Everything the command processor holds from one frame to the next.
 * Plain data. */
struct AntarcticState {
	static constexpr uint32_t VIEWPORTS = ODIN_VIEWPORTS;
	/* Base plus index on 16 bits (X360 8252EE98); von4 sets bases above
	 * 0x5000. */
	static constexpr uint32_t MATERIALS = 65536;
	static constexpr uint32_t TEXHEADS = 65536;
	static constexpr uint32_t LIGHTS = 1024;
	static constexpr uint32_t LIGHTSETS = 256;
	static constexpr int32_t VIEWPORT_STACK = 32;	/* viewports pushed */

	uint32_t pc;
	uint32_t sp[2];
	uint32_t patterns;	/* of the callers, see 501 */
	uint32_t running;
	uint32_t in_mesh;

	int16_t depth_bias;
	uint16_t pattern;	/* 501 */
	int16_t texoffset[4][2];	/* 0D1 UV offsets in 1/16 texel, by vertex selector */
	odin_fb_config_t fb;

	struct {
		float center[3];
		float value;
		float radius;
		uint32_t cond;
		uint32_t compared;	/* by a 095, since the last 055 */
		uint32_t branch_id;
	} lod;

	struct {
		uint32_t type;
		float alpha;
		float static_mesh_precision;
	} poly;

	struct {
		odin_viewport_t table[VIEWPORTS];
		odin_viewport_t scratch;
		odin_viewport_t current;
		odin_viewport_t stack[VIEWPORT_STACK];
		int32_t depth;
	} vp;

	struct {
		odin_modelview_t modelview;
		odin_modelview_t local;		/* 161 with bit 13 */
		uint32_t has_local;
		odin_modelview_t second;	/* 161 with bit 18 */
	} mv;

	struct {
		odin_material_t table[MATERIALS];
		odin_material_t scratch;
		uint32_t base;
	} mat;

	struct {
		odin_texhead_t table[TEXHEADS];
		odin_texhead_t scratch;
		uint32_t base;
	} tex;

	/* A lightset in its table: the numbers of four lights. */
	struct Lightset {
		uint16_t lights[4];
		uint16_t committed;
	};

	struct {
		Lightset sets[LIGHTSETS];
		odin_lightset_t scratchset;
		odin_light_t table[LIGHTS];
		odin_light_t scratch;
		uint32_t base;
	} lit;

	uint32_t alpha_table[0x40];		/* 154 */
	uint32_t light_table[4][0x20];		/* 194 */

	uint32_t regs[0x100 / 4];		/* 15000000 - 150000FF */
	uint32_t transfer_left;			/* texture transfer: entries still to copy */
};

/* The command processor of the Hikaru, 315-6083 "Antarctic".
 *
 * It executes the instructions the game leaves in the command RAM
 * (48000000 on the bus) or in the RAM of the slave SH-4 (40000000): 32-bit
 * words, 1, 2, 4 or 8 a instruction, the opcode in the lower 9 bits of the
 * first word and the size in its bits 5:4. It can call subroutines, with a
 * stack in the command RAM.
 *
 * It manipulates six kinds of objects: viewports, modelviews, materials,
 * textures/texheads (that's what they are called in PHARRIER, and here),
 * lights/lightsets (a lightset is a set of four lights), and meshes. It has
 * a table for each object type, except meshes:
 *
 *  viewports	8
 *  materials	65536 (looked up from a base)
 *  texheads	65536 (looked up from a base)
 *  lights	1024
 *  lightsets	256
 *
 * Instructions do not manipulate the objects in the tables directly.
 * Instead, they work on a special object, the "scratch" or "active" object:
 * "recall" instructions load an object from the object table into the
 * scratch object, "set" instructions set the properties of the scratch
 * objects, and "commit" instructions store the scratch object back into the
 * table. When a mesh is drawn, the current scratch objects affect its
 * rendering.
 *
 * What comes out is the display list of the frame (antarctic_list.hpp).
 *
 * The chip also moves the textures the game leaves on the bus into the
 * texture RAM. Its registers are at 15000000 for the master SH-4: see
 * antarctic_io.cpp. */
class Antarctic {
public:
	/* A bank of the texture RAM: texels of 16 bits, and the bytes of a line */
	static constexpr uint32_t TEXRAM_WIDTH = 2048, TEXRAM_HEIGHT = 1024, TEXRAM_PITCH = 2 * TEXRAM_WIDTH;
	/* The interrupt causes of 15000088 */
	enum : uint32_t {
		IRQ_TRANSFER_END = 0x01,	/* texture transfer done */
		IRQ_ANALYSIS_END = 0x04,	/* the list has been read */
		IRQ_DISPLAY = 0x80		/* a cause of the display chip */
	};
	/* What the board has to do after a write to a register */
	enum class Written {
		UNKNOWN,	/* not a register the chip is known to have */
		DONE,
		CAUSES,		/* the interrupt causes or their mask changed */
		CONTROL		/* 15000058 was written */
	};

	/* cmdram: the 4 MB of the command RAM; ram_s: the 32 MB of the RAM
	 * of the slave; texram: the two banks of 4 MB of the texture RAM. */
	Antarctic(std::span<uint8_t> cmdram, std::span<uint8_t> ram_s,
	          std::span<uint8_t> texram0, std::span<uint8_t> texram1);

	/* Power-on: every object cleared, nothing running. */
	void reset();

	/* The registers. read_register is false for a register the chip is
	 * not known to have. */
	bool read_register(uint32_t addr, uint32_t &value) const;
	Written write_register(uint32_t addr, uint32_t value);

	/* Interrupt causes, the display chip being one of them; the line to
	 * the board is held while a cause is not masked. */
	void raise(uint32_t causes) { reg(0x88) |= causes; }
	void set_display_irq(bool on);
	bool irq_line() const { return (reg(0x88) & ~reg(0x84) & 0xFF) != 0; }

	/* 15000058: both low bits set ask for a list */
	bool list_asked() const { return reg(0x58) == 3; }
	/* Starts the list the registers describe (15000070, 15000074,
	 * 15000078). */
	void start_list() { start(reg(0x70), reg(0x74), reg(0x78)); }
	/* The list has ended: the two bits go off. */
	void list_ended() { reg(0x58) &= ~3u; }
	/* Starts a list at pc, with the two stack pointers; stacks and
	 * bases are cleared. */
	void start(uint32_t pc, uint32_t sp0, uint32_t sp1);
	bool running() const { return m_s.running != 0; }
	/* Executes one instruction a unit of budget while it lasts, an
	 * instruction that cannot be read included. True when the list
	 * ended during the call, budget then holding what was left. */
	bool run(uint32_t &budget);
	/* Closes the mesh left open when a frame ends in the middle of
	 * it. */
	void end_open_mesh();

	/* The list of the frame that begins. The lists of the last two
	 * frames stay as they are: the renderer may still draw them. */
	odin_display_list_t *begin_list();
	const odin_fb_config_t &fb_config() const { return m_s.fb; }
	uint32_t pc() const { return m_s.pc; }

	/* Texture transfer: copies one entry of the table. True when the
	 * transfer ended at this call. */
	bool step_texture_transfer();
	/* 1500008C: true when the bus writes the texture RAM as it is, false
	 * when each word goes where its bank twiddles it (Hikaru::texram_write) */
	bool texture_ram_twiddled() const { return reg(0x8C) == 0x02020202; }
	/* Called with each rectangle of texels of a bank a transfer is about
	 * to write. */
	using TexelsWritten = void (*)(void *ctx, unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
	void set_before_texture_write(TexelsWritten fn, void *ctx)
	{
		m_before_texture_write = fn;
		m_before_texture_write_ctx = ctx;
	}

	const AntarcticState &state() const { return m_s; }
	void load(const AntarcticState &in);

	/* Debug: each instruction printed there as it is executed (GPU_LOG_CP) */
	void set_disasm(FILE *out) { m_disasm = out; }
	/* Debug: each texture transfer told there (GPU_LOG_IDMA) */
	void set_transfer_log(FILE *out) { m_transfer_log = out; }
	/* What the chip cannot do is told there, stderr; the tests silence it */
	void set_log(FILE *out) { m_log = out; }

private:
	/* antarctic.cpp: the program (its table of instructions in
	 * antarctic_ops.cpp) */

	/* What an instruction does to the program counter and to the mesh
	 * being pushed */
	enum : uint32_t {
		SETS_PC = 1 << 0,	/* moves the program counter itself */
		OPENS_MESH = 1 << 1,	/* opens a mesh if none is open */
		KEEPS_MESH = 1 << 2,	/* leaves the open mesh open */
		PUSHES = OPENS_MESH | KEEPS_MESH
	};
	using Op = void (Antarctic::*)(const uint32_t *inst);
	struct Insn {
		Op handler;	/* nullptr: no such instruction */
		uint32_t flags;
	};
	static const std::array<Insn, 0x200> &insns();

	[[noreturn]] void fatal(const char *what, uint32_t value) const;
	void error(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));
	/* Whether `index` is in a table of `size` entries; if not, the error
	 * `fmt` tells both */
	bool in_table(uint32_t index, uint32_t size, const char *fmt) const __attribute__((format(printf, 4, 0)))
	{
		if (index < size)
			return true;
		error(fmt, index, size);
		return false;
	}

	uint32_t &reg(uint32_t offs) { return m_s.regs[(offs & 0xFF) / 4]; }
	uint32_t reg(uint32_t offs) const { return m_s.regs[(offs & 0xFF) / 4]; }

	bool fetch(uint32_t inst[8]) const;
	void push_pc();
	void pop_pc();
	uint32_t stack_offset() const;

	/* antarctic_ops.cpp: the instructions */

	void op_nop(const uint32_t *inst);
	void op_jump(const uint32_t *inst);
	bool call_condition(const uint32_t *inst) const;
	void op_call(const uint32_t *inst);
	void op_return(const uint32_t *inst);
	void op_end(const uint32_t *inst);

	void op_lod_test_sphere(const uint32_t *inst);
	void op_lod_radius(const uint32_t *inst);
	void op_lod_compare(const uint32_t *inst);

	void op_viewport_set(const uint32_t *inst);
	void op_viewport_ambient(const uint32_t *inst);
	void op_viewport_clear(const uint32_t *inst);
	void op_viewport_commit(const uint32_t *inst);
	void op_viewport_recall(const uint32_t *inst);

	void op_vector(const uint32_t *inst);
	void vector_matrix_row(const uint32_t *inst);
	void vector_lod_center(const uint32_t *inst);
	void vector_light(const uint32_t *inst);

	void op_material_color(const uint32_t *inst);
	void op_material_flags(const uint32_t *inst);
	void op_material_commit(const uint32_t *inst);
	void op_material_recall(const uint32_t *inst);

	void op_texhead_set(const uint32_t *inst);
	void op_texcoord_offset(const uint32_t *inst);
	void op_texhead_commit(const uint32_t *inst);
	void op_texhead_recall(const uint32_t *inst);

	void op_light_attenuation(const uint32_t *inst);
	void op_light_color(const uint32_t *inst);
	void op_light_commit(const uint32_t *inst);
	void op_lightset_commit(const uint32_t *inst);
	void op_lightset_recall(const uint32_t *inst);

	void op_mesh_set(const uint32_t *inst);
	void op_polygon_type(const uint32_t *inst);
	void op_push_position_static(const uint32_t *inst);
	void op_push_position(const uint32_t *inst);
	void op_push_all(const uint32_t *inst);
	void op_push_texcoords3(const uint32_t *inst);
	void op_push_texcoords(const uint32_t *inst);

	void op_framebuffer(const uint32_t *inst);
	void op_alpha_table(const uint32_t *inst);
	void op_light_table(const uint32_t *inst);

	float get_lod_size() const;
	bool is_lod_sphere_visible() const;
	/* The modelview the vertices go through: the local matrix, if any,
	 * then the modelview. */
	void get_modelview(odin_modelview_t &out) const;

	/* antarctic_mesh.cpp: from vertices to the meshes of the list */

	/* The header word of the instructions that push vertices */
	struct VertexInfo {
		uint32_t full;

		bool winding() const { return (full & 0x00000001) != 0; }
		bool ppivot() const { return (full & 0x00000002) != 0; }
		uint32_t tricap() const { return (full >> 9) & 7; }
		/* the vertex follows the second modelview */
		bool second_mv() const { return (full & 0x00001000) != 0; }
		/* it takes the 3x3 of its modelview */
		bool rotate() const { return (full & 0x00002000) != 0; }
		/* it takes the last row of its modelview */
		bool translate() const { return (full & 0x00004000) != 0; }
		/* selector of the UV offset table (X360 port, 8252D590) */
		uint32_t uv_offset() const { return (full >> 18) & 3; }
		bool nocull() const { return (full & 0x00800000) != 0; }
		uint32_t alpha() const { return full >> 24; }
	};
	/* Where a vertex is in the vertices of the list: as it is, and with
	 * its normals reversed for the back of a triangle drawn on both
	 * faces. NOT_LISTED until it is, and again once it changes. */
	static constexpr uint32_t NOT_LISTED = ~0u;
	struct Vertex {
		VertexInfo info;
		odin_vertex_body_t body;
		uint32_t listed[2];

		void changed() { listed[0] = listed[1] = NOT_LISTED; }
	};
	/* What a push instruction brings */
	enum : uint32_t {
		PUSH_POS = 1 << 0,
		PUSH_NRM = 1 << 1,
		PUSH_TXC = 1 << 2
	};

	/* The buffers behind a display list. They grow as a frame asks (the
	 * board reads its commands as they come, with no such limit; Brave
	 * after its first boss lists more than Valkyrie's 4096 texheads) and
	 * keep their size from frame to frame. */
	struct List {
		List();
		void clear();
		/* Room for the objects of one more mesh, two modelviews; for
		 * one more mesh of that viewport and type */
		void room();
		void room_for_mesh(unsigned vp, unsigned type);

		odin_display_list_t view{};
		std::vector<odin_viewport_t> vps;
		std::vector<odin_modelview_t> mvs;
		std::vector<odin_material_t> mats;
		std::vector<odin_texhead_t> texs;
		std::vector<odin_lightset_t> lss;
		/* The 194 table of the four lights of each lightset */
		std::vector<uint16_t> ls_tables;
		/* The 194 words of the lightset listed last */
		uint32_t tables[4][8];
		/* The modelview listed last for a mesh */
		uint32_t mv_index;
		std::array<std::array<std::vector<odin_mesh_t>, 8>, 8> meshes;
		std::vector<odin_vertex_body_t> verts;
		std::vector<uint32_t> indices;
	};

	void begin_mesh(uint32_t addr);
	void end_mesh(uint32_t addr);
	void list_state(odin_mesh_t &mesh);
	void copy_material(Vertex &dst);
	void copy_colors(Vertex &dst, const Vertex &src);
	void copy_texcoords(Vertex &dst, const Vertex &src) const;
	uint32_t list_vertex(Vertex &v, unsigned back);
	void add_triangle();
	void push_vertices(const Vertex *v, uint32_t flags, uint32_t num);

	/* antarctic_io.cpp: the registers and the texture transfer */

	void transfer_texture(const uint32_t entry[4]);
	uint32_t copy_texture(uint32_t bus_addr, uint32_t size, const odin_texhead_t &texhead);
	void copy_level(std::span<const uint8_t> src, std::span<uint8_t> texram, uint32_t bus_addr,
	                uint32_t x0, uint32_t y0, uint32_t w, uint32_t h);

	std::span<uint8_t> m_cmdram, m_ram_s;
	std::span<uint8_t> m_texram[2];
	AntarcticState m_s;

	/* The vertices on their way to triangles: the last three, and the
	 * number of triangles of the mesh being built, which go straight to
	 * the list. Cleared when a list starts. The three stay in their slots
	 * of the ring: `at` tells which is which, the oldest first. */
	struct {
		uint32_t num_verts, num_tris;
		Vertex ring[3];
		uint8_t at[3];
		/* The 691 colour last made a vertex colour, and what it made */
		uint16_t ambient_of[3];
		float ambient[3];
	} m_push;
	Vertex &tmp(unsigned i) { return m_push.ring[m_push.at[i]]; }
	odin_mesh_t *m_mesh;	/* being built, or nullptr */

	/* The list being built is one of three, taken in turn */
	std::array<std::unique_ptr<List>, 3> m_lists;
	unsigned m_building;
	List *m_list;

	TexelsWritten m_before_texture_write;
	void *m_before_texture_write_ctx;

	FILE *m_disasm, *m_log, *m_transfer_log;
	static constexpr uint32_t TYPES_NEVER_MET = 0x8D;	/* polygon types 0, 2, 3 and 7 */
	uint32_t m_types_told;	/* polygon types never met, told once each */
};

} /* namespace odin */

#endif /* ODIN_ANTARCTIC_HPP */
