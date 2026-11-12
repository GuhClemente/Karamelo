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

#ifndef ODIN_BOARD_DRAWING_VK_HPP
#define ODIN_BOARD_DRAWING_VK_HPP

/* The board drawing with Vulkan (drawing.hpp), for its own files
 * only: drawing_vk.cpp (made; the textures and their samplers),
 * pictures_vk.cpp (the pictures and what goes from one to the next),
 * scene_vk.cpp (the meshes of a list, their pipelines),
 * translucent_vk.cpp (their translucent fragments). The pictures
 * keep the lines in the order OpenGL gives them, the bottom one first:
 * drawn with the same coordinates, they hold the same bytes. */

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "odin/host/renderer/hikaru/drawing.hpp"
#include "odin/host/video/picture_filter_vk.hpp"
#include "odin/host/renderer/hikaru/renderer_programs.hpp"
#include "odin/host/gfx/vk_common.hpp"

namespace odin {

/* The 16-bit pixels of the board (ABGR1555), a little-endian word each with
 * the alpha on top and the red at the bottom: Vulkan's A1R5G5B5 with its red
 * and blue swapped */
inline constexpr VkComponentMapping ABGR1555_SWIZZLE = { VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G,
	                                                 VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_A };

class VkDrawing : public Drawing {
public:
	explicit VkDrawing(vk::Context &card);
	~VkDrawing() override;
	VkDrawing(const VkDrawing &) = delete;
	VkDrawing &operator=(const VkDrawing &) = delete;

	void set_scale(unsigned scale) override;
	bool draws_native() const override { return m_card.geometry_shaders(); }
	bool sorts_per_pixel() const override { return m_card.geometry_shaders(); }

	void begin_scene(const odin_display_list_t &list, const SceneDebug &debug) override;
	void clear_depth() override;
	void begin_pass(const PassRules &rules) override;
	void draw_mesh(const odin_mesh_t &mesh, const MeshDraw &draw) override;
	void end_pass() override {}
	void end_scene() override;
	void begin_families() override;

	void begin_translucent() override;
	void begin_collect(unsigned view, unsigned slot) override;
	void end_collect() override;
	void keep_count(unsigned view, unsigned slot) override;
	uint32_t count_now(unsigned view, unsigned slot) override;
	void end_translucent(bool blend) override;
	uint32_t counted_fragments(unsigned slot, uint32_t views) override;
	void forget_counts(unsigned slot) override { m_counted[slot] = 0; }
	void size_fragments(uint32_t max) override;
	uint32_t fragment_limit() const override;

	void keep_programs_in(const std::string &folder) override;
	void list_programs_in(const std::string &path) override { m_program_list.list_in(path); }
	bool holds_program(uint64_t key) const override { return m_variants.contains(key); }
	void begin_preparing() override {}
	void prepare_program(uint64_t key) override;
	void end_preparing() override {}
	unsigned programs_read_back() const override { return m_read_back; }
	unsigned programs_compiled() const override { return m_compiled; }

	bool has_stamps() const override { return m_card.stamps(); }
	uint32_t put_stamp() override;
	bool reached(uint32_t stamp) override;
	uint64_t stamp_time(uint32_t stamp) override;
	void forget_stamp(uint32_t stamp, bool again) override;
	/* The commands of a frame go to the card at its end: its stamps are
	 * read frames later (renderer_times.hpp) */
	void send_commands() override {}

	void scene_to_pipe(unsigned slot) override;
	void blend_scene_to_pipe(unsigned slot, unsigned buffer, uint32_t rate) override;
	void keep_picture(unsigned slot, unsigned buffer) override;

	void set_layer(unsigned i, LayerTexels texels, const void *pixels) override;
	void compose(const Compose &compose, unsigned shown, const bool layers[2]) override;
	void present(PictureRect rect, unsigned window_w, unsigned window_h) override;
	void draw_pmx(const PmxScene &scene, unsigned anisotropy) override;
	void prepare_pmx(const std::vector<uint64_t> &keys,
	                 const std::function<void(unsigned, unsigned)> &progress) override;
	void list_pmx_in(const std::string &path) override;
	HostPicture frame_picture() const override { return { FRAME_PICTURE, width(), height() }; }
	void set_filter(const std::string &preset) override;
	void draw_loading(std::span<const LoadingRect> rects, unsigned window_w, unsigned window_h) override;

	void read_kept(std::span<uint8_t> pictures) override;
	void write_kept(std::span<const uint8_t> pictures) override;

	HostTexture make_texture(Texels texels, std::span<const TexelRect> levels) override;
	void forget_textures(std::span<const HostTexture> textures) override;
	HostSampler make_sampler(const TextureSampling &sampling, unsigned anisotropy) override;
	void forget_sampler(HostSampler sampler) override;

private:
	struct Pmx;
	std::shared_ptr<Pmx> m_pmx;
	/* Descriptor sets of one layout, each kept until it is freed, from
	 * pools made as they fill */
	struct KeptSets {
		VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLER;
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		std::vector<VkDescriptorPool> pools;
	};
	struct KeptSet {
		VkDescriptorPool pool = VK_NULL_HANDLE;
		VkDescriptorSet set = VK_NULL_HANDLE;
	};
	/* A texture, a sampler, each with the set the mesh shaders read it
	 * through */
	struct Texture {
		vk::Image image;
		KeptSet set;
	};
	struct Sampler {
		VkSampler sampler;
		KeptSet set;
	};
	/* What the mesh shaders are pushed (glsl/mesh.vert) */
	struct MeshPush {
		uint32_t view, mv, mv2, lightset;
		float depth_scale, alpha_ref, weigh_by_alpha;
		uint32_t pattern;
		uint32_t order, blend_mode;
		float sort_scale;
		uint32_t snap;
		float scale;
		float family;
	};

	/* The number of the picture of the frame, as the window reads it;
	 * the format of the pictures */
	static constexpr uint32_t FRAME_PICTURE = 1;
	static constexpr VkFormat PICTURE_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

	unsigned width() const { return SCREEN_WIDTH * m_scale; }
	unsigned height() const { return SCREEN_HEIGHT * m_scale; }
	vk::Image &kept(unsigned i) { return m_kept[i]; }
	void build_pictures();
	void destroy_pictures(bool kept_too);
	vk::Image make_picture(unsigned w, unsigned h);
	void clear(VkCommandBuffer cb, vk::Image &image, float alpha);
	/* A copy, each pixel the nearest */
	void copy_picture(VkCommandBuffer cb, vk::Image &from, vk::Image &to);
	/* All of a picture of the scale into another as it is: its colour, or
	 * its depth */
	void copy_whole(VkCommandBuffer cb, vk::Image &from, vk::Image &to,
	                VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
	void clear_window(unsigned window_w, unsigned window_h);

	/* drawing_vk.cpp */
	void make_sets(KeptSets &sets, VkDescriptorType type, VkShaderStageFlags stages);
	KeptSet keep_set(KeptSets &sets);
	void free_set(const KeptSet &set);
	void destroy_sets(KeptSets &sets);
	KeptSet image_set(const vk::Image &image);

	/* scene_vk.cpp */
	void build_meshes();
	void make_mesh_shaders();
	void make_mesh_layout();
	void destroy_meshes();
	void open_scene(bool clear, bool clear_depth = false);
	void open_family(unsigned family);
	void set_scene_state(VkCommandBuffer cb, VkDescriptorSet set3);
	void close_scene();
	void use_depth();
	void log(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));
	VkPipeline pipeline(const MeshVariant &variant, MeshBlend blend);
	/* The first time one is asked for: made, and kept by `index` */
	VkPipeline make_pipeline(const MeshVariant &variant, MeshBlend blend, uint64_t key, uint64_t index);
	void read_cache(const std::string &path);
	void write_cache();
	void write_view(unsigned index);
	void write_lightset(unsigned index);
	void set_view(const odin_mesh_t &mesh);
	void bind_texture(const MeshDraw &draw);

	/* translucent_vk.cpp */
	void build_lists();
	void destroy_lists();
	void build_list_pictures();

	vk::Context &m_card;
	const vk::Functions &m_fn;
	unsigned m_scale = 1;
	/* Where a list is drawn; the KEPT pictures (the two 3D buffers, then
	 * the pipe of the last three lists); the picture of the frame; one
	 * of the size of the screen of the hardware for the save states */
	vk::Image m_scene, m_kept[KEPT], m_frame, m_state;
	/* The 2D layers, kept from frame to frame, and what they hold; what a
	 * layer not shown reads, as OpenGL's texture 0: (0, 0, 0, 1) */
	vk::Image m_layers[2];
	std::optional<LayerTexels> m_layer_texels[2];
	vk::Image m_none;
	VkSampler m_nearest = VK_NULL_HANDLE;
	/* The passes: the blender of the 3D buffers, the composition, the
	 * picture brought to the size of the window */
	vk::Pass m_blender, m_compose, m_presenter;
	/* The filter of the picture, its preset, the frames it has drawn */
	std::unique_ptr<VkPictureFilter> m_filter;
	std::string m_filter_preset;
	unsigned m_filter_frames = 0;

	/* The textures and the samplers, by their number; the sets of
	 * both; the texture and the sampler of the meshes without one */
	std::unordered_map<uint32_t, Texture> m_textures;
	std::unordered_map<uint32_t, Sampler> m_samplers;
	uint32_t m_next_texture = 1, m_next_sampler = 1;
	KeptSets m_texture_sets, m_sampler_sets;
	KeptSet m_none_set, m_nearest_set;

	/* The stamps of the clock of the card (Debug: ODIN_DRAW_TIMES): their
	 * queries, made when first put, and those free to be put; a stamp is
	 * its query plus one, 0 none */
	VkQueryPool m_stamps = VK_NULL_HANDLE;
	std::vector<uint32_t> m_free_stamps;

	/* The meshes: their depth, shaders (the fragment shader of the
	 * translucent ones apart), layout (the set of the list, then those of
	 * the texture, the sampler and the translucent fragments), pipelines
	 * by variant and blend, the variants that have one, the cache of the
	 * pipelines (its folder, the start of the names of the caches of the
	 * card, its file), the list of the programs of the game */
	vk::Image m_depth;
	VkShaderModule m_mesh_vert = VK_NULL_HANDLE, m_mesh_frag = VK_NULL_HANDLE, m_mesh_collect = VK_NULL_HANDLE;
	VkShaderModule m_mesh_families = VK_NULL_HANDLE;
	VkShaderModule m_mesh_geom = VK_NULL_HANDLE;	/* Native 2D */
	VkDescriptorSetLayout m_list_layout = VK_NULL_HANDLE, m_collect_layout = VK_NULL_HANDLE;
	VkPipelineLayout m_mesh_layout = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, VkPipeline> m_pipelines;
	std::unordered_set<uint64_t> m_variants;
	VkPipelineCache m_cache = VK_NULL_HANDLE;
	std::string m_cache_folder, m_cache_card, m_cache_path;
	ProgramList m_program_list;
	/* The pipelines draw_pmx makes after prepare_pmx */
	ProgramList m_pmx_list;
	unsigned m_read_back = 0, m_compiled = 0;
	bool m_preparing = false;

	/* The list being drawn: its buffers (the views and the lightsets
	 * written when first used) and their set, what is bound and pushed */
	const odin_display_list_t *m_list = nullptr;
	SceneDebug m_debug{};
	vk::Slice m_vertices{}, m_indices{}, m_views{}, m_lightsets{};
	VkDescriptorSet m_list_set = VK_NULL_HANDLE;
	uint32_t m_identity = 0;	/* the modelview of none */
	std::vector<bool> m_view_written, m_lightset_written;
	VkPipeline m_bound_pipeline = VK_NULL_HANDLE;
	uint32_t m_bound_texture = 0, m_bound_sampler = 0;
	uint32_t m_scissor_vp_index = ~0u;
	MeshPush m_push{};
	/* Drawing into the scene: in a rendering of Vulkan now (copies and
	 * barriers wait outside), between begin_scene and end_scene */
	bool m_open = false;
	/* The depth: not all far (or undefined), to be emptied before its next
	 * use, stored by the open rendering; the translucent views to come,
	 * which copy it (begin_translucent) */
	bool m_depth_used = false, m_depth_wanted = false, m_depth_kept = true;
	unsigned m_translucent_left = 0;
	/* How the meshes of the polygon type being drawn are drawn */
	struct {
		float alpha_ref;	/* fragments at or below it are dropped */
		bool depth_bias;	/* the 101 bias moves the mesh */
		bool depth_write;
		uint32_t order;		/* triangles sent so far, while collecting */
	} m_pass{};

	/* The translucent fragments, see translucent_vk.cpp: being
	 * collected, and their set (the rest of them after the families) */
	bool m_collecting = false;
	VkDescriptorSet m_collect_set = VK_NULL_HANDLE;
	/* A view with both families of the 101 (begin_families, as
	 * scene_gl.cpp): the eye depth and the scale of the mesh that keeps each
	 * pixel in each family, the depth of the flagged one, the set each
	 * family reads the other's eye depths through, the family drawn into
	 * now (NO_FAMILY: the scene) */
	static constexpr VkFormat EYE_FORMAT = VK_FORMAT_R32G32_SFLOAT;
	vk::Image m_eye[2], m_flag_depth;
	VkDescriptorSet m_family_set[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
	bool m_families_view = false;
	unsigned m_family = NO_FAMILY;
	/* The translucent fragments again: the depth the other types left, the
	 * last fragment of each pixel, the colour under them; the fragments,
	 * the count of each view (each `m_count_stride` bytes), the counts the
	 * processor reads, and for each list of the pipe the sending its counts
	 * go with (0: none); the pass that blends them */
	vk::Image m_solid_depth, m_heads, m_below;
	vk::Buffer m_nodes, m_counts, m_readback;
	VkDeviceSize m_count_stride = 4;
	uint64_t m_counted[PIPE] = {};
	vk::Pass m_sorter;
	bool m_said_no_lists = false;
};

} /* namespace odin */

#endif /* ODIN_BOARD_DRAWING_VK_HPP */
