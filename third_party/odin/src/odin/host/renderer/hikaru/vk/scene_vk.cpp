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

/* The meshes of a display list with Vulkan (drawing.hpp), as
 * scene_gl.cpp draws them with OpenGL: the same rules of the board
 * (hikaru_draw.hpp), the data of the list in buffers the mesh shaders read
 * (glsl/mesh.*), a pipeline for each variant and blend, kept on disk by
 * Vulkan's cache of pipelines. */

#include "odin/host/renderer/hikaru/vk/drawing_vk.hpp"
#include "odin/host/renderer/hikaru/depth_bias.hpp"
#include "odin/host/gfx/program_cache.hpp"
#include "odin/common/bytes.hpp"
#include "odin/common/repeats.hpp"
#include "odin/host/gfx/spirv.hpp"
#include "odin/host/app/user_files.hpp"

#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace odin {

namespace {

constexpr uint32_t NONE = ~0u;

/* A view and a lightset as the mesh shaders read them (std430) */
struct ViewData {
	float projection[4][4];
	float fog[2], cue[2];
	float fog_color[3], pad0;
	float ambient[3], pad1;
};
static_assert(sizeof(ViewData) == 112);

struct LightData {
	float position[3], pad0;
	float direction[3], pad1;
	float color[3], ambient;
	float extents[2], pad2[2];
	float spot[3], pad3;
	float cone[2];
	float table[16];
	float pad4[2];
};
static_assert(sizeof(LightData) == 160 && offsetof(LightData, table) == 88);

struct LightsetData {
	LightData lights[4];
};

/* The constants of a variant, by their constant_id in glsl/mesh.* */
struct Constants {
	int32_t gl_clip_space;
	int32_t has_texture, has_lighting, shading_mode, alpha_mode, tex_mode;
	int32_t has_light[4], light_type[4], light_att_type[4];
	int32_t has_fog, has_cue, has_pattern, native_2d;
};
constexpr uint32_t NUM_CONSTANTS = 22;
static_assert(sizeof(Constants) == NUM_CONSTANTS * sizeof(int32_t));

/* The blend factors of each MeshBlend, as scene_gl.cpp has them:
 * colour source and destination, alpha source and destination */
constexpr VkBlendFactor BLENDS[6][4] = {
	/* REPLACE */     { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO },
	/* FADED */       { VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ZERO,
	                    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
	/* ALPHA_OVER */  { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ZERO,
	                    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
	/* PRODUCT */     { VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO },
	/* ALPHA_ADDED */ { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
	                    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
	/* OVER */        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ZERO,
	                    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
};

/* Whether the bytes of a cache of pipelines were made by this card and its
 * driver, as their header says */
bool
cache_fits(const std::vector<uint8_t> &bytes, const VkPhysicalDeviceProperties &card)
{
	VkPipelineCacheHeaderVersionOne header;

	if (bytes.size() < sizeof(header))
		return false;
	std::memcpy(&header, bytes.data(), sizeof(header));
	return header.headerSize >= sizeof(header) && header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
	       header.vendorID == card.vendorID && header.deviceID == card.deviceID &&
	       std::memcmp(header.pipelineCacheUUID, card.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

/* The constants of a variant (Constants) */
Constants
constants_of(const MeshVariant &variant, bool gl_clip_space)
{
	Constants c{};

	c.gl_clip_space = gl_clip_space;
	c.has_texture = variant.has_texture;
	c.has_lighting = variant.has_lighting;
	c.shading_mode = static_cast<int32_t>(variant.shading_mode);
	c.alpha_mode = variant.alpha_mode;
	c.tex_mode = static_cast<int32_t>(variant.tex_mode);
	for (unsigned i = 0; i < 4; i++) {
		c.has_light[i] = variant.lights[i].on;
		c.light_type[i] = static_cast<int32_t>(variant.lights[i].type);
		c.light_att_type[i] = static_cast<int32_t>(variant.lights[i].attenuation);
	}
	c.has_fog = variant.has_fog;
	c.has_cue = variant.has_cue;
	c.has_pattern = variant.has_pattern;
	c.native_2d = variant.native;
	return c;
}

/* How the colours of a mesh are blended (BLENDS), nothing written by a
 * collecting one. The families of the 101 leave their eye depths too, not
 * blended; on a card where the blend is the same for all, the alpha of 1
 * of those fragments keeps them with the blends of the depth-writing types
 * (scene_gl.cpp) */
std::array<VkPipelineColorBlendAttachmentState, 2>
mix_of(MeshBlend blend, bool collect, bool independent_blend)
{
	const VkBlendFactor *const f = BLENDS[static_cast<unsigned>(blend)];
	const VkColorComponentFlags rgba = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
	                                   VK_COLOR_COMPONENT_A_BIT;
	std::array<VkPipelineColorBlendAttachmentState, 2> mix = { {
		{ VK_TRUE, f[0], f[1], VK_BLEND_OP_ADD, f[2], f[3], VK_BLEND_OP_ADD, collect ? 0u : rgba },
		{ VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE,
		  VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT },
	} };

	if (!independent_blend)
		mix[1] = mix[0];
	return mix;
}

} /* namespace */

void
VkDrawing::log(const char *fmt, ...) const
{
	va_list args;

	va_start(args, fmt);
	log_line(m_debug.log, fmt, args);
	va_end(args);
}

/* The shaders (the one of the translucent fragments where the card gives
 * gl_PrimitiveID), the layout of the meshes, the sets of the texture and
 * the sampler of the meshes without one (made after m_none and m_nearest),
 * an empty cache */
void
VkDrawing::build_meshes()
{
	make_mesh_shaders();
	make_mesh_layout();

	const VkDevice device = m_card.device();

	m_none_set = image_set(m_none);
	m_nearest_set = keep_set(m_sampler_sets);
	const VkDescriptorImageInfo nearest{ m_nearest, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
	const VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_nearest_set.set, 0, 0, 1,
		                          VK_DESCRIPTOR_TYPE_SAMPLER, &nearest, nullptr, nullptr };
	m_fn.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

	const VkPipelineCacheCreateInfo cache{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, 0, nullptr };
	m_card.check(m_fn.vkCreatePipelineCache(device, &cache, nullptr, &m_cache), "a pipeline cache");
}

/* The shaders of the meshes: those the card can run */
void
VkDrawing::make_mesh_shaders()
{
	const VkDevice device = m_card.device();
	vk::Spirv code[5] = { ODIN_SPIRV_OF(mesh_vert), ODIN_SPIRV_OF(mesh_frag), ODIN_SPIRV_OF(mesh_collect_frag),
		              ODIN_SPIRV_OF(mesh_geom), ODIN_SPIRV_OF(mesh_families_frag) };
	VkShaderModule *const modules[5] = { &m_mesh_vert, &m_mesh_frag, &m_mesh_collect, &m_mesh_geom,
		                             &m_mesh_families };
	const bool made[5] = { true, true, m_card.primitive_ids(), m_card.geometry_shaders(), true };
	std::vector<uint32_t> collecting;

	/* gl_PrimitiveID declared through Tessellation, not Geometry (MoltenVK) */
	if (m_card.primitive_ids_tessellation()) {
		constexpr uint32_t CAPABILITY = 2u << 16 | 17u, GEOMETRY = 2, TESSELLATION = 3;

		collecting.assign(code[2].code, code[2].code + code[2].bytes / sizeof(uint32_t));
		for (size_t i = 5; i < collecting.size() && collecting[i] >> 16; i += collecting[i] >> 16)
			if (collecting[i] == CAPABILITY && i + 1 < collecting.size() && collecting[i + 1] == GEOMETRY)
				collecting[i + 1] = TESSELLATION;
		code[2] = { collecting.data(), collecting.size() * sizeof(uint32_t) };
	}

	for (unsigned i = 0; i < 5; i++) {
		if (!made[i])
			continue;
		const VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, code[i].bytes,
		                                     code[i].code };
		m_card.check(m_fn.vkCreateShaderModule(device, &info, nullptr, modules[i]), "a mesh shader");
	}
}

/* The sets of a list, of the textures, of the samplers, of the translucent
 * fragments, and the layout of the meshes that takes them */
void
VkDrawing::make_mesh_layout()
{
	const VkDevice device = m_card.device();

	/* The views, the modelviews, the lightsets */
	const VkDescriptorSetLayoutBinding list[3] = {
		{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		  nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	const VkDescriptorSetLayoutCreateInfo set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3,
	                                           list };
	m_card.check(m_fn.vkCreateDescriptorSetLayout(device, &set, nullptr, &m_list_layout), "the set of a list");
	make_sets(m_texture_sets, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT);
	make_sets(m_sampler_sets, VK_DESCRIPTOR_TYPE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);

	/* The fragments, the last of each pixel, the count of the view, the
	 * depth the other types left, what each family of the 101 left
	 * (glsl/fragment_lists.glsl, mesh.frag); a family meets the other one
	 * through the first of those */
	const VkDescriptorSetLayoutBinding lists[6] = {
		{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	const VkDescriptorSetLayoutCreateInfo collect{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 6,
	                                               lists };
	m_card.check(m_fn.vkCreateDescriptorSetLayout(device, &collect, nullptr, &m_collect_layout),
	             "the set of the translucent fragments");

	const VkDescriptorSetLayout sets[4] = { m_list_layout, m_texture_sets.layout, m_sampler_sets.layout,
		                                m_collect_layout };
	const VkPushConstantRange push{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPush) };
	const VkPipelineLayoutCreateInfo layout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 4, sets, 1,
	                                         &push };
	m_card.check(m_fn.vkCreatePipelineLayout(device, &layout, nullptr, &m_mesh_layout), "the layout of the meshes");
}

/* The card is done with them: the frames were waited for. */
void
VkDrawing::destroy_meshes()
{
	const VkDevice device = m_card.device();

	write_cache();
	for (const auto &[key, pipeline] : m_pipelines)
		m_fn.vkDestroyPipeline(device, pipeline, nullptr);
	m_pipelines.clear();
	m_fn.vkDestroyPipelineCache(device, m_cache, nullptr);
	for (auto &[id, texture] : m_textures)
		m_card.destroy_image(texture.image);
	m_textures.clear();
	for (const auto &[id, sampler] : m_samplers)
		m_fn.vkDestroySampler(device, sampler.sampler, nullptr);
	m_samplers.clear();
	destroy_sets(m_texture_sets);
	destroy_sets(m_sampler_sets);
	m_fn.vkDestroyPipelineLayout(device, m_mesh_layout, nullptr);
	m_fn.vkDestroyDescriptorSetLayout(device, m_list_layout, nullptr);
	m_fn.vkDestroyDescriptorSetLayout(device, m_collect_layout, nullptr);
	m_fn.vkDestroyShaderModule(device, m_mesh_vert, nullptr);
	m_fn.vkDestroyShaderModule(device, m_mesh_frag, nullptr);
	m_fn.vkDestroyShaderModule(device, m_mesh_collect, nullptr);
	m_fn.vkDestroyShaderModule(device, m_mesh_families, nullptr);
	m_fn.vkDestroyShaderModule(device, m_mesh_geom, nullptr);
}

/* The cache of this card and of these mesh shaders in the folder, taken
 * when its driver made it; what the cache held so far kept with it. Its name
 * has the card and the fingerprint of the shaders (FNV-1a). */
void
VkDrawing::keep_programs_in(const std::string &folder)
{
	const VkPhysicalDeviceProperties &card = m_card.properties();
	uint64_t shaders = FNV_START;
	char name[64];

	if (folder.empty())
		return;
	for (const vk::Spirv &code : { ODIN_SPIRV_OF(mesh_vert), ODIN_SPIRV_OF(mesh_frag), ODIN_SPIRV_OF(mesh_geom),
	                                ODIN_SPIRV_OF(mesh_families_frag) })
		shaders = fnv1a(shaders, code.code, code.bytes);
	std::snprintf(name, sizeof(name), "vulkan-%08" PRIx32 "-%08" PRIx32 "-", card.vendorID, card.deviceID);
	m_cache_card = name;
	std::snprintf(name, sizeof(name), "%016" PRIx64 ".bin", shaders);
	m_cache_folder = folder;
	m_cache_path = folder + "/" + m_cache_card + name;
	read_cache(m_cache_path);
}

void
VkDrawing::read_cache(const std::string &path)
{
	const std::optional<std::vector<uint8_t>> file = read_file(path);

	if (!file || !cache_fits(*file, m_card.properties()))
		return;
	const VkPipelineCacheCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr, 0, file->size(),
	                                      file->data() };
	VkPipelineCache read;

	if (m_fn.vkCreatePipelineCache(m_card.device(), &info, nullptr, &read) != VK_SUCCESS)
		return;
	m_card.check(m_fn.vkMergePipelineCaches(m_card.device(), read, 1, &m_cache), "a pipeline cache");
	m_fn.vkDestroyPipelineCache(m_card.device(), m_cache, nullptr);
	m_cache = read;
}

/* When pipelines were compiled: with what the file holds now (another Odin
 * may have written it since), then written whole. The caches of this card
 * made with other mesh shaders go: no pipeline of theirs would be taken
 * again. */
void
VkDrawing::write_cache()
{
	size_t size = 0;

	if (m_cache_path.empty() || m_compiled == 0)
		return;
	read_cache(m_cache_path);
	if (m_fn.vkGetPipelineCacheData(m_card.device(), m_cache, &size, nullptr) != VK_SUCCESS || size == 0)
		return;
	std::vector<uint8_t> bytes(size);
	if (m_fn.vkGetPipelineCacheData(m_card.device(), m_cache, &size, bytes.data()) != VK_SUCCESS)
		return;
	bytes.resize(size);
	if (!odin::write_kept(m_cache_path, bytes))
		return;

	const std::string ours = std::filesystem::path(m_cache_path).filename().string();
	std::error_code error;
	for (const auto &entry : std::filesystem::directory_iterator(m_cache_folder, error)) {
		const std::string name = entry.path().filename().string();

		if (name.starts_with(m_cache_card) && name.ends_with(".bin") && name != ours)
			std::filesystem::remove(entry.path(), error);
	}
}

/* The pipeline of the variant and the blend, made the first time. A variant
 * first met while drawing goes to the list of the programs of the game. The
 * translucent fragments go to their lists, not to the colour: their blend
 * is that of sort.frag, the pipeline is one whatever the blend. */
VkPipeline
VkDrawing::pipeline(const MeshVariant &variant, MeshBlend blend)
{
	if (variant.collect)
		blend = MeshBlend::REPLACE;
	const uint64_t key = variant.key();
	const uint64_t index = key * 8 + static_cast<unsigned>(blend);
	const auto found = m_pipelines.find(index);

	return found != m_pipelines.end() ? found->second : make_pipeline(variant, blend, key, index);
}

VkPipeline
VkDrawing::make_pipeline(const MeshVariant &variant, MeshBlend blend, uint64_t key, uint64_t index)
{
	const Constants c = constants_of(variant, m_card.gl_clip_space());
	VkSpecializationMapEntry entries[NUM_CONSTANTS];
	for (uint32_t i = 0; i < NUM_CONSTANTS; i++)
		entries[i] = { i, i * static_cast<uint32_t>(sizeof(int32_t)), sizeof(int32_t) };
	const VkSpecializationInfo constants{ NUM_CONSTANTS, entries, sizeof(c), &c };

	/* Native 2D: the geometry shader between them */
	const VkPipelineShaderStageCreateInfo stages[3] = {
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, m_mesh_vert,
		  "main", &constants },
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
		  variant.collect ? m_mesh_collect : variant.families ? m_mesh_families : m_mesh_frag, "main", &constants },
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_GEOMETRY_BIT, m_mesh_geom,
		  "main", nullptr },
	};
	const uint32_t num_stages = variant.native ? 3 : 2;

	/* The locations of the mesh vertex shader, as scene_gl.cpp
	 * gives them: three bytes of a colour read as four, the shader
	 * taking three (Vulkan does not ask every card for three bytes) */
	const VkVertexInputBindingDescription binding{ 0, sizeof(odin_vertex_body_t), VK_VERTEX_INPUT_RATE_VERTEX };
	const VkVertexInputAttributeDescription attributes[9] = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(odin_vertex_body_t, position) },
		{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(odin_vertex_body_t, normal) },
		{ 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(odin_vertex_body_t, diffuse) },
		{ 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(odin_vertex_body_t, ambient) },
		{ 4, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(odin_vertex_body_t, specular) },
		{ 5, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(odin_vertex_body_t, color1) },
		{ 6, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(odin_vertex_body_t, texcoords) },
		{ 7, 0, VK_FORMAT_R8_UNORM, offsetof(odin_vertex_body_t, alpha) },
		{ 8, 0, VK_FORMAT_R8_UNORM, offsetof(odin_vertex_body_t, second_mv) },
	};
	const VkPipelineVertexInputStateCreateInfo input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	                                                  nullptr, 0, 1, &binding, 9, attributes };
	const VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
	                                                       nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
	/* z clipped from -w to w, as OpenGL does */
	const VkPipelineViewportDepthClipControlCreateInfoEXT clip{
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT, nullptr, VK_TRUE };
	const VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
	                                                  m_card.gl_clip_space() ? &clip : nullptr, 0, 1, nullptr, 1,
	                                                  nullptr };
	VkPipelineRasterizationStateCreateInfo raster{};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_BACK_BIT;
	raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo samples{};
	samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineDepthStencilStateCreateInfo depth{};
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
	depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	const std::array<VkPipelineColorBlendAttachmentState, 2> mix =
		mix_of(blend, variant.collect, m_card.independent_blend());
	VkPipelineColorBlendStateCreateInfo blending{};
	blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blending.attachmentCount = variant.families ? 2 : 1;
	blending.pAttachments = mix.data();
	const VkDynamicState states[7] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
		                           VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE,
		                           VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
		                           VK_DYNAMIC_STATE_DEPTH_COMPARE_OP };
	const VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 7,
	                                                states };
	const VkFormat colours[2] = { PICTURE_FORMAT, EYE_FORMAT };
	VkPipelineCreationFeedback made{}, stage_made[3] = {};
	const VkPipelineCreationFeedbackCreateInfo feedback{ VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO,
		                                             nullptr, &made, num_stages, stage_made };
	const VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		                                       m_card.creation_feedback() ? &feedback : nullptr, 0,
		                                       variant.families ? 2u : 1u, colours, m_card.depth_format(),
		                                       VK_FORMAT_UNDEFINED };
	VkGraphicsPipelineCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.pNext = &rendering;
	info.stageCount = num_stages;
	info.pStages = stages;
	info.pVertexInputState = &input;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &samples;
	info.pDepthStencilState = &depth;
	info.pColorBlendState = &blending;
	info.pDynamicState = &dynamic;
	info.layout = m_mesh_layout;

	VkPipeline pipeline;
	m_card.check(m_fn.vkCreateGraphicsPipelines(m_card.device(), m_cache, 1, &info, nullptr, &pipeline),
	             "a mesh pipeline");
	if ((made.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT) &&
	    (made.flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT))
		m_read_back++;
	else
		m_compiled++;
	m_pipelines.emplace(index, pipeline);
	if (m_variants.insert(key).second && !m_preparing)
		m_program_list.add(key);
	return pipeline;
}

/* With the blend the variant is most often drawn with: faded by the depth
 * queue, else replacing (the others, the background's, when first met). */
void
VkDrawing::prepare_program(uint64_t key)
{
	const std::optional<MeshVariant> variant = MeshVariant::from_key(key);

	if (!variant || (variant->collect && !m_card.primitive_ids()) || (variant->native && !m_card.geometry_shaders()) ||
	    m_variants.contains(key))
		return;
	m_preparing = true;
	pipeline(*variant, variant->has_cue ? MeshBlend::FADED : MeshBlend::REPLACE);
	m_preparing = false;
}

/* The vertices and the indices of the list, its modelviews and the one of
 * none after them; room for its views and lightsets, written when first
 * used; the scene and its depth emptied, as scene_gl.cpp does: an
 * empty 3D buffer lets everything through, the clear color comes in on the
 * screen (compose). */
void
VkDrawing::begin_scene(const odin_display_list_t &list, const SceneDebug &debug)
{
	const VkDeviceSize align = std::max<VkDeviceSize>(m_card.limits().minStorageBufferOffsetAlignment, 16);
	const auto bytes = [](size_t n, size_t each) { return static_cast<VkDeviceSize>(std::max<size_t>(n, 1) * each); };

	m_list = &list;
	m_debug = debug;
	const VkDeviceSize vertex_bytes = bytes(list.verts.num, sizeof(odin_vertex_body_t));
	const VkDeviceSize index_bytes = bytes(list.indices.num, sizeof(uint32_t));
	const VkDeviceSize mv_bytes = bytes(list.num_mvs + 1, sizeof(odin_modelview_t));
	const VkDeviceSize view_bytes = bytes(list.num_vps, sizeof(ViewData));
	const VkDeviceSize ls_bytes = bytes(list.num_lss, sizeof(LightsetData));
	m_vertices = m_card.stage(vertex_bytes, 16);
	m_indices = m_card.stage(index_bytes, 16);
	const vk::Slice mvs = m_card.stage(mv_bytes, align);
	m_views = m_card.stage(view_bytes, align);
	m_lightsets = m_card.stage(ls_bytes, align);

	std::memcpy(m_vertices.bytes, list.verts.all, sizeof(odin_vertex_body_t) * list.verts.num);
	std::memcpy(m_indices.bytes, list.indices.all, sizeof(uint32_t) * list.indices.num);
	std::memcpy(mvs.bytes, list.mv_list, sizeof(odin_modelview_t) * list.num_mvs);
	m_identity = list.num_mvs;
	std::memcpy(mvs.bytes + sizeof(odin_modelview_t) * m_identity, &IDENTITY, sizeof(IDENTITY));
	m_view_written.assign(list.num_vps, false);
	m_lightset_written.assign(list.num_lss, false);

	m_list_set = m_card.frame_set(m_list_layout);
	const VkDescriptorBufferInfo buffers[3] = { { m_views.buffer, m_views.offset, view_bytes },
		                                    { mvs.buffer, mvs.offset, mv_bytes },
		                                    { m_lightsets.buffer, m_lightsets.offset, ls_bytes } };
	VkWriteDescriptorSet writes[3];
	for (uint32_t i = 0; i < 3; i++)
		writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_list_set, i, 0, 1,
		              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffers[i], nullptr };
	m_fn.vkUpdateDescriptorSets(m_card.device(), 3, writes, 0, nullptr);

	m_collecting = false;
	m_pass.depth_write = true;
	m_translucent_left = 0;
	for (unsigned vpi = 0; vpi < ODIN_VIEWPORTS; vpi++)
		if (list.num_meshes[vpi][ODIN_POLYTYPE_TRANSLUCENT])
			m_translucent_left++;
	open_scene(true);
}

/* The scene drawn into, emptied first or as it was left: every state of the
 * meshes set again (the commands may have been sent since, see count_now) */
void
VkDrawing::open_scene(bool clear, bool clear_depth)
{
	const VkCommandBuffer cb = m_card.commands();
	const VkAttachmentLoadOp load = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;

	m_card.barrier(cb, m_scene, vk::DRAWN_IN);
	m_card.barrier(cb, m_depth, vk::DEPTH_IN);
	VkRenderingAttachmentInfo colour{};
	colour.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colour.imageView = m_scene.view;
	colour.imageLayout = m_scene.layout;
	colour.loadOp = load;
	colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colour.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	VkRenderingAttachmentInfo depth = colour;
	if (clear_depth)
		depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	if (depth.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
		m_depth_used = false;
		m_depth_wanted = false;
	}
	/* Nothing reads the depth after the last translucent view, but family
	 * 0 of the 101 in a view that has both (open_family) */
	m_depth_kept = m_translucent_left > 0 || m_families_view;
	if (!m_depth_kept)
		depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth.imageView = m_depth.view;
	depth.imageLayout = m_depth.layout;
	depth.clearValue.depthStencil = { 1.0f, 0 };
	const VkRect2D all{ { 0, 0 }, { width(), height() } };
	const VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, all, 1, 0, 1, &colour, &depth,
	                                 nullptr };

	m_fn.vkCmdBeginRendering(cb, &rendering);
	set_scene_state(cb, m_collecting ? m_collect_set : VK_NULL_HANDLE);
	m_family = NO_FAMILY;
}

/* The scene drawn into with the depth and the eye depths of a family of
 * the 101 (begin_families) */
void
VkDrawing::open_family(unsigned family)
{
	const VkCommandBuffer cb = m_card.commands();
	vk::Image &depth = family ? m_flag_depth : m_depth;

	close_scene();
	m_card.barrier(cb, m_scene, vk::DRAWN_IN);
	m_card.barrier(cb, m_eye[family], vk::DRAWN_IN);
	m_card.barrier(cb, m_eye[1 - family], vk::SAMPLED);
	m_card.barrier(cb, depth, vk::DEPTH_IN);
	VkRenderingAttachmentInfo colours[2]{};
	for (unsigned i = 0; i < 2; i++) {
		const vk::Image &image = i ? m_eye[family] : m_scene;

		colours[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colours[i].imageView = image.view;
		colours[i].imageLayout = image.layout;
		colours[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		colours[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	}
	VkRenderingAttachmentInfo depths = colours[0];
	depths.imageView = depth.view;
	depths.imageLayout = depth.layout;
	const VkRect2D all{ { 0, 0 }, { width(), height() } };
	const VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, all, 1, 0, 2, colours, &depths,
	                                 nullptr };

	m_fn.vkCmdBeginRendering(cb, &rendering);
	set_scene_state(cb, m_family_set[family]);
	m_family = family;
}

/* What the meshes need set in a rendering just begun; the set of the
 * translucent fragments, or of a family, third if any */
void
VkDrawing::set_scene_state(VkCommandBuffer cb, VkDescriptorSet set3)
{
	const VkRect2D all{ { 0, 0 }, { width(), height() } };
	const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width()), static_cast<float>(height()), 0.0f, 1.0f };

	m_fn.vkCmdSetViewport(cb, 0, 1, &viewport);
	m_fn.vkCmdSetScissor(cb, 0, 1, &all);
	m_fn.vkCmdSetCullMode(cb, VK_CULL_MODE_BACK_BIT);
	m_fn.vkCmdSetFrontFace(cb, VK_FRONT_FACE_CLOCKWISE);
	/* The translucent fragments test their depth themselves */
	m_fn.vkCmdSetDepthTestEnable(cb, m_collecting ? VK_FALSE : VK_TRUE);
	m_fn.vkCmdSetDepthWriteEnable(cb, m_pass.depth_write && !m_collecting ? VK_TRUE : VK_FALSE);
	m_fn.vkCmdSetDepthCompareOp(cb, VK_COMPARE_OP_LESS_OR_EQUAL);
	m_fn.vkCmdBindVertexBuffers(cb, 0, 1, &m_vertices.buffer, &m_vertices.offset);
	m_fn.vkCmdBindIndexBuffer(cb, m_indices.buffer, m_indices.offset, VK_INDEX_TYPE_UINT32);
	const VkDescriptorSet sets[4] = { m_list_set, m_none_set.set, m_nearest_set.set, set3 };
	m_fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_mesh_layout, 0, set3 ? 4 : 3, sets, 0,
	                             nullptr);
	m_bound_pipeline = VK_NULL_HANDLE;
	m_bound_texture = 0;
	m_bound_sampler = NONE;
	m_scissor_vp_index = NONE;
	m_open = true;
}

void
VkDrawing::close_scene()
{
	if (!m_open)
		return;
	m_fn.vkCmdEndRendering(m_card.commands());
	m_open = false;
	if (!m_depth_kept)
		m_depth_used = true;
}

/* Emptied when a mesh or a translucent view first needs it, and only if
 * something was drawn into it */
void
VkDrawing::clear_depth()
{
	m_families_view = false;
	if (m_depth_used)
		m_depth_wanted = true;
}

/* Emptied in the scene, not in the depth of a family */
void
VkDrawing::use_depth()
{
	const VkClearAttachment depth{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, { .depthStencil = { 1.0f, 0 } } };
	const VkClearRect all{ { { 0, 0 }, { width(), height() } }, 0, 1 };

	if (!m_depth_wanted)
		return;
	/* MoltenVK clears the colour too with vkCmdClearAttachments */
	if (!m_open || m_family != NO_FAMILY || m_card.portability_subset()) {
		close_scene();
		open_scene(false, true);
		return;
	}
	m_fn.vkCmdClearAttachments(m_card.commands(), 1, &depth, 1, &all);
	m_depth_used = false;
	m_depth_wanted = false;
}

/* The eye depths of both families and the depth of the flagged one
 * emptied; each family reads the other's (glsl/mesh.frag, FAMILIES) */
void
VkDrawing::begin_families()
{
	const VkCommandBuffer cb = m_card.commands();

	close_scene();
	for (vk::Image &eye : m_eye)
		m_card.barrier(cb, eye, vk::DRAWN_IN);
	m_card.barrier(cb, m_flag_depth, vk::DEPTH_IN);
	VkRenderingAttachmentInfo colours[2]{};
	for (unsigned i = 0; i < 2; i++) {
		colours[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colours[i].imageView = m_eye[i].view;
		colours[i].imageLayout = m_eye[i].layout;
		colours[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colours[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	}
	VkRenderingAttachmentInfo depth = colours[0];
	depth.imageView = m_flag_depth.view;
	depth.imageLayout = m_flag_depth.layout;
	depth.clearValue.depthStencil = { 1.0f, 0 };
	const VkRect2D all{ { 0, 0 }, { width(), height() } };
	const VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, all, 1, 0, 2, colours, &depth,
	                                 nullptr };
	m_fn.vkCmdBeginRendering(cb, &rendering);
	m_fn.vkCmdEndRendering(cb);

	for (unsigned f = 0; f < 2; f++) {
		const VkDescriptorImageInfo other{ m_nearest, m_eye[1 - f].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		m_family_set[f] = m_card.frame_set(m_collect_layout);
		const VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_family_set[f], 4, 0, 1,
			                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &other, nullptr, nullptr };
		m_fn.vkUpdateDescriptorSets(m_card.device(), 1, &write, 0, nullptr);
	}
	m_families_view = true;
	m_family = NO_FAMILY;
}

void
VkDrawing::begin_pass(const PassRules &rules)
{
	m_pass.alpha_ref = rules.alpha_ref;
	m_pass.depth_bias = rules.depth_bias;
	m_pass.depth_write = rules.depth_write;
	if (m_open)
		m_fn.vkCmdSetDepthWriteEnable(m_card.commands(), rules.depth_write && !m_collecting ? VK_TRUE : VK_FALSE);
}

void
VkDrawing::end_scene()
{
	close_scene();
}

/* What a view gives the shaders: its projection (hikaru_draw.hpp), its depth
 * queue, its fog and its ambient light. The checks of scene_gl.cpp,
 * once a list. */
void
VkDrawing::write_view(unsigned index)
{
	const odin_viewport_t &vp = m_list->vp_list[index];
	ViewData view{};

	m_view_written[index] = true;
	check_view(vp);
	if (m_debug.log)
		log("vp  = %s", odin_viewport_str(&vp));

	view_projection(vp, view.projection);
	view_cue(vp, view.cue);
	view_fog(vp, view.fog, view.fog_color);
	if (!m_debug.no_ambient)
		view_ambient(vp, view.ambient);
	std::memcpy(m_views.bytes + sizeof(ViewData) * index, &view, sizeof(view));
}

/* The lights of a lightset that are on, as scene_gl.cpp gives them */
void
VkDrawing::write_lightset(unsigned index)
{
	const odin_lightset_t &ls = m_list->ls_list[index];
	LightsetData data{};

	m_lightset_written[index] = true;
	if (m_debug.log)
		log("lightset [%u] mask=%X = %s", index, odin_lightset_mask(&ls), odin_lightset_str(&ls));
	for (unsigned i = 0; i < 4; i++) {
		const odin_light_t &light = ls.lights[i];
		LightData &to = data.lights[i];

		if (odin_lightset_mask(&ls) & (1u << i))
			continue;
		std::memcpy(to.position, light.position, sizeof(to.position));
		std::memcpy(to.direction, light.direction, sizeof(to.direction));
		light_colour(light, to.color);
		if (m_debug.no_diffuse)
			to.color[0] = to.color[1] = to.color[2] = 0.0f;
		to.ambient = light_ambient(light);
		std::memcpy(to.extents, light.attenuation, sizeof(to.extents));
		std::memcpy(to.spot, light.spot, sizeof(to.spot));
		to.cone[0] = light.cos_outer;
		to.cone[1] = light.cone_scale;
		light_table(m_list->ls_tables[index][i], to.table);
	}
	std::memcpy(m_lightsets.bytes + sizeof(LightsetData) * index, &data, sizeof(data));
}

/* The clip window and the winding of the view (view_turned_over), whatever
 * the pipeline. The pictures have the bottom line first, as OpenGL's: the
 * window of glScissor holds as it is, cut to the picture (Vulkan takes no
 * corner below 0); one OpenGL refuses (a width below 0) leaves the last
 * one. Vulkan counts the turn of a triangle the other way. */
void
VkDrawing::set_view(const odin_mesh_t &mesh)
{
	const odin_viewport_t &vp = m_list->vp_list[mesh.vp_index];
	const VkCommandBuffer cb = m_card.commands();

	if (m_scissor_vp_index != mesh.vp_index) {
		const auto [x, y, w, h] = clip_window(vp, m_scale);

		m_scissor_vp_index = mesh.vp_index;
		if (w >= 0 && h >= 0) {
			const int32_t x0 = std::max(x, 0), y0 = std::max(y, 0);
			const VkRect2D scissor{ { x0, y0 },
				                { static_cast<uint32_t>(std::max(x + w - x0, 0)),
				                  static_cast<uint32_t>(std::max(y + h - y0, 0)) } };

			m_fn.vkCmdSetScissor(cb, 0, 1, &scissor);
		}
		m_fn.vkCmdSetFrontFace(cb, view_turned_over(vp) ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
	}
	if (!m_view_written[mesh.vp_index])
		write_view(mesh.vp_index);
}

/* The texture of a texhead and its sampling, bound when they change; no
 * texture reads as OpenGL's texture 0 */
void
VkDrawing::bind_texture(const MeshDraw &draw)
{
	const VkCommandBuffer cb = m_card.commands();

	if (m_bound_texture != draw.texture.id) {
		const auto found = m_textures.find(draw.texture.id);
		const VkDescriptorSet set = found != m_textures.end() ? found->second.set.set : m_none_set.set;

		m_bound_texture = draw.texture.id;
		m_fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_mesh_layout, 1, 1, &set, 0, nullptr);
	}
	if (draw.texture.id && m_bound_sampler != draw.sampler.id) {
		const auto found = m_samplers.find(draw.sampler.id);
		const VkDescriptorSet set = found != m_samplers.end() ? found->second.set.set : m_nearest_set.set;

		m_bound_sampler = draw.sampler.id;
		m_fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_mesh_layout, 2, 1, &set, 0, nullptr);
	}
}

void
VkDrawing::draw_mesh(const odin_mesh_t &mesh, const MeshDraw &draw)
{
	const MeshVariant &variant = draw.variant;

	/* Without gl_PrimitiveID, no translucent fragments (begin_collect) */
	if (variant.collect && !m_collecting)
		return;
	/* Into the depth of its family, or the scene (begin_families) */
	if (!variant.collect) {
		use_depth();
		const unsigned family = family_of(mesh, variant);

		if (!m_open || family != m_family) {
			if (family == NO_FAMILY) {
				close_scene();
				open_scene(false);
			} else {
				open_family(family);
			}
		}
		m_depth_used = true;
	}
	if (mesh.vp_index >= m_list->num_vps || (variant.has_lighting && mesh.ls_index >= m_list->num_lss)) {
		repeats::error("a mesh outside its list: vp %u, ls %u", mesh.vp_index, mesh.ls_index);
		return;
	}

	const VkCommandBuffer cb = m_card.commands();
	const VkPipeline p = pipeline(variant, draw.blend);
	if (p != m_bound_pipeline) {
		m_bound_pipeline = p;
		m_fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
	}
	set_view(mesh);
	if (variant.has_texture)
		bind_texture(draw);
	if (variant.has_lighting && !m_lightset_written[mesh.ls_index])
		write_lightset(mesh.ls_index);

	/* The 101 moves the mesh by a fraction of its own depth
	 * (scene_gl.cpp, depth_bias.hpp). */
	const int bias = m_pass.depth_bias ? static_cast<int>(mesh.depth_bias) : 0;
	m_push.view = mesh.vp_index;
	m_push.lightset = variant.has_lighting ? mesh.ls_index : 0;
	m_push.depth_scale = depth_bias::test_scale(bias);
	m_push.alpha_ref = m_pass.alpha_ref;
	m_push.weigh_by_alpha = draw.weigh_by_alpha ? 1.0f : 0.0f;
	m_push.pattern = pattern_bits(mesh.pattern);
	m_push.blend_mode = draw.blend_mode;
	m_push.sort_scale = depth_bias::sort_scale(bias);
	m_push.snap = draw.snap ? 1 : 0;
	m_push.scale = static_cast<float>(m_scale);
	/* A translucent mesh meets what kept each pixel by the families of
	 * both, in a view with both (0: one without) */
	m_push.family = depth_bias::family_sign(m_families_view, bias);

	for (unsigned i = 0; i < mesh.num_instances; i++) {
		const uint32_t mv = modelview_of(*m_list, mesh, i);

		m_push.mv = mv;
		m_push.mv2 = modelview2_of(*m_list, mesh, mv);
		m_push.order = m_pass.order;
		if (variant.collect)
			m_pass.order += mesh.num_tris;
		if (m_debug.log)
			log("mv  = [%u+%u] %s", mesh.mv_index, i,
			    odin_modelview_str(mv == m_identity ? &IDENTITY : &m_list->mv_list[mv]));
		m_fn.vkCmdPushConstants(cb, m_mesh_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
		                        sizeof(m_push), &m_push);
		m_fn.vkCmdDrawIndexed(cb, mesh.num_tris * 3, 1, mesh.first_index, 0, 0);
	}
}

} /* namespace odin */
