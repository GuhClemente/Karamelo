/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

#include "odin/host/gfx/spirv.hpp"
#include "odin/host/renderer/hikaru/vk/drawing_vk.hpp"

/* Draws Neon 250 scenes to a target, then to the frame's picture */

namespace odin {

namespace {

/* The rank of the draw's first triangle and its blend codes are for the
 * fragments that go to the lists of their pixels (pmx.frag, LIST) */
struct DrawPush {
	float most_w, lod_bias;
	int32_t flags;
	float scale;
	float fog_density;
	uint32_t fog_color, fog_vertex_color;
	uint32_t order, mode;
};

/* The chip's depth compare codes (PG, the chip's programming guide, p. 69), 4 "greater". Without a float
 * depth, polygons the chip's 1/W tells apart tie, and flat ones fail
 * against the clear: "greater or equal" there (HEURISTIC) */
constexpr std::array<VkCompareOp, 8> COMPARE = { VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL,
	                                         VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER,
	                                         VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL,
	                                         VK_COMPARE_OP_ALWAYS };

/* The last fragment of each pixel: emptied, then written and read by the
 * fragment shaders (as translucent_vk.cpp) */
constexpr vk::Use HEADS_EMPTIED = { VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
	                            VK_ACCESS_2_TRANSFER_WRITE_BIT };
constexpr vk::Use HEADS_USED = { VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	                         VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT };
/* A fragment of the lists takes 16 bytes, its rank in TA order 26 bits of
 * them (pmx.frag) */
constexpr VkDeviceSize PMX_NODE_SIZE = 16;
constexpr size_t RANKS = size_t{ 1 } << 26;
constexpr std::array<VkBlendFactor, 8> SRC = { VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_DST_COLOR,
	                                       VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_SRC_ALPHA,
	                                       VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA,
	                                       VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA };
constexpr std::array<VkBlendFactor, 8> DST = { VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR,
	                                       VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_SRC_ALPHA,
	                                       VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA,
	                                       VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA };

} /* namespace */

struct VkDrawing::Pmx {
	vk::Context &card;
	const vk::Functions &fn;
	/* The pipeline cache of the drawing, kept with its mesh pipelines
	 * (keep_programs_in); its pipelines taken from it, and compiled; the
	 * list of those first met while drawing */
	VkPipelineCache &cache;
	unsigned &read_back, &compiled;
	ProgramList &learned;
	bool preparing = false;
	VkDescriptorSetLayout images_layout = VK_NULL_HANDLE, lists_layout = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
	/* Native 2D (pmx.geom), where supported */
	VkShaderModule geometry = VK_NULL_HANDLE, native_fragment = VK_NULL_HANDLE;
	/* The fragments of the translucent draws sorted pixel by pixel go to
	 * lists (pmx.frag, LIST), which pmx_sort.frag blends: where the card
	 * has geometry shaders (gl_PrimitiveID) */
	bool sorts = false;
	VkShaderModule list_fragment = VK_NULL_HANDLE, native_list_fragment = VK_NULL_HANDLE;
	vk::Pass sorter;
	vk::Buffer nodes, count, readback;
	VkDeviceSize room = 0;
	/* A float depth as the chip's 1/W, where the card has one */
	VkFormat depth_format = VK_FORMAT_UNDEFINED;
	vk::Image depth_picture;
	std::unordered_map<uint32_t, VkPipeline> pipelines;
	std::unordered_map<uint32_t, VkSampler> samplers;
	std::unordered_map<uint32_t, vk::Image> images;
	/* The fog table, and what it holds */
	vk::Image fog;
	VkSampler fog_sampler = VK_NULL_HANDLE;
	std::array<uint8_t, 256> fog_table{};
	bool fog_loaded = false;
	/* Descriptor sets by image and sampler, from dedicated pools. They
	 * last as long as their image. */
	std::unordered_map<VkSampler, uint32_t> sampler_number;
	std::unordered_map<uint64_t, VkDescriptorSet> sets;
	struct Pool {
		VkDescriptorPool pool;
		uint32_t left;
	};
	std::vector<Pool> pools;
	static constexpr uint32_t SETS_A_POOL = 1024;
	/* Sets of freed images and the scene that last used them, returned to
	 * their pool once the GPU has finished that scene */
	struct Retired {
		VkDescriptorSet set;
		size_t pool;
		uint64_t scene;
	};
	std::deque<Retired> retired;
	std::unordered_map<VkDescriptorSet, size_t> pool_of;
	uint64_t scenes = 0;

	VkDescriptorSet make_set(const vk::Image &image, VkSampler sampler)
	{
		size_t from = 0;

		while (from < pools.size() && pools[from].left == 0)
			from++;
		if (from == pools.size()) {
			const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * SETS_A_POOL };
			const VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr,
				                               VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, SETS_A_POOL, 1, &size };
			VkDescriptorPool pool;

			card.check(fn.vkCreateDescriptorPool(card.device(), &info, nullptr, &pool), "the sets of the Neon 250");
			pools.push_back({ pool, SETS_A_POOL });
		}

		const VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, pools[from].pool, 1, &images_layout };
		VkDescriptorSet set;

		card.check(fn.vkAllocateDescriptorSets(card.device(), &info, &set), "a set of the Neon 250");
		pools[from].left--;
		pool_of[set] = from;

		const VkDescriptorImageInfo described{ sampler, image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		const VkDescriptorImageInfo fogged{ fog_sampler, fog.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		constexpr VkStructureType WRITE = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		constexpr VkDescriptorType SAMPLED = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		const VkWriteDescriptorSet writes[2] = {
			{ WRITE, nullptr, set, 0, 0, 1, SAMPLED, &described, nullptr, nullptr },
			{ WRITE, nullptr, set, 1, 0, 1, SAMPLED, &fogged, nullptr, nullptr },
		};

		fn.vkUpdateDescriptorSets(card.device(), 2, writes, 0, nullptr);
		return set;
	}

	void forget_sets(uint32_t image)
	{
		for (auto it = sets.begin(); it != sets.end();) {
			if (it->first >> 32 != image) {
				++it;
				continue;
			}
			const auto owner = pool_of.find(it->second);

			retired.push_back({ it->second, owner->second, scenes });
			pool_of.erase(owner);
			it = sets.erase(it);
		}
	}

	/* At most one scene per frame, so the GPU has finished any scene that
	 * many behind */
	void take_back_sets()
	{
		while (!retired.empty() && scenes - retired.front().scene > vk::Context::FRAMES) {
			const Retired &gone = retired.front();

			fn.vkFreeDescriptorSets(card.device(), pools[gone.pool].pool, 1, &gone.set);
			pools[gone.pool].left++;
			retired.pop_front();
		}
	}

	Pmx(vk::Context &c, const vk::Functions &f, VkPipelineCache &kept, unsigned &taken, unsigned &made,
	    ProgramList &met)
		: card(c), fn(f), cache(kept), read_back(taken), compiled(made), learned(met)
	{
		/* The texture, and the fog table */
		const VkDescriptorSetLayoutBinding bindings[2] = {
			{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		};
		const VkDescriptorSetLayoutCreateInfo set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2,
			                                   bindings };

		card.check(fn.vkCreateDescriptorSetLayout(card.device(), &set, nullptr, &images_layout), "the images of the Neon 250");
		fog = card.make_image(VK_FORMAT_R8G8_UNORM, 128, 1, 1,
		                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		fog_sampler = sampler(PmxDraw{}, 1);

		const VkDescriptorSetLayoutBinding lists[3] = {
			{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		};
		const VkDescriptorSetLayoutCreateInfo lists_set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			                                         nullptr, 0, 3, lists };

		card.check(fn.vkCreateDescriptorSetLayout(card.device(), &lists_set, nullptr, &lists_layout),
		           "the lists of the Neon 250");

		const VkDescriptorSetLayout layouts[2] = { images_layout, lists_layout };
		const VkPushConstantRange range{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawPush) };
		const VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 2,
			                               layouts, 1, &range };

		card.check(fn.vkCreatePipelineLayout(card.device(), &info, nullptr, &layout), "the layout of the Neon 250");

		const vk::Spirv vert = ODIN_SPIRV_OF(pmx_vert), frag = ODIN_SPIRV_OF(pmx_frag);
		const VkShaderModuleCreateInfo v{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, vert.bytes, vert.code };
		const VkShaderModuleCreateInfo g{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, frag.bytes, frag.code };

		card.check(fn.vkCreateShaderModule(card.device(), &v, nullptr, &vertex), "a shader");
		card.check(fn.vkCreateShaderModule(card.device(), &g, nullptr, &fragment), "a shader");
		if (card.geometry_shaders()) {
			const vk::Spirv between = ODIN_SPIRV_OF(pmx_geom), native = ODIN_SPIRV_OF(pmx_native_frag);
			const VkShaderModuleCreateInfo b{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, between.bytes, between.code };
			const VkShaderModuleCreateInfo n{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, native.bytes, native.code };

			card.check(fn.vkCreateShaderModule(card.device(), &b, nullptr, &geometry), "a shader");
			card.check(fn.vkCreateShaderModule(card.device(), &n, nullptr, &native_fragment), "a shader");

			const vk::Spirv list = ODIN_SPIRV_OF(pmx_list_frag);
			const vk::Spirv native_list = ODIN_SPIRV_OF(pmx_native_list_frag);
			constexpr VkStructureType MODULE = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			const VkShaderModuleCreateInfo l{ MODULE, nullptr, 0, list.bytes, list.code };
			const VkShaderModuleCreateInfo nl{ MODULE, nullptr, 0, native_list.bytes, native_list.code };
			constexpr VkShaderStageFlags FRAGMENT = VK_SHADER_STAGE_FRAGMENT_BIT;
			const std::vector<VkDescriptorSetLayoutBinding> sorting = {
				{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, FRAGMENT, nullptr },
				{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, FRAGMENT, nullptr },
				{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, FRAGMENT, nullptr },
			};
			const VkBufferUsageFlags counted = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
			                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT;

			card.check(fn.vkCreateShaderModule(card.device(), &l, nullptr, &list_fragment), "a shader");
			card.check(fn.vkCreateShaderModule(card.device(), &nl, nullptr, &native_list_fragment),
			           "a shader");
			sorter = card.make_pass(ODIN_SPIRV_OF(whole_picture_vert), ODIN_SPIRV_OF(pmx_sort_frag),
			                        sorting, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, PICTURE_FORMAT);
			count = card.make_card_buffer(4, counted);
			readback = card.make_buffer(4 * vk::Context::FRAMES, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
			std::memset(readback.bytes, 0, 4 * vk::Context::FRAMES);
			sorts = true;
		}

		VkFormatProperties props;

		fn.vkGetPhysicalDeviceFormatProperties(card.gpu(), VK_FORMAT_D32_SFLOAT, &props);
		if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			depth_format = VK_FORMAT_D32_SFLOAT;
	}

	~Pmx()
	{
		const VkDevice device = card.device();

		/* Nothing is in use now, and the sets go with their pools */
		card.send_and_wait();
		for (const Pool &pool : pools)
			fn.vkDestroyDescriptorPool(device, pool.pool, nullptr);
		for (auto &[id, image] : images)
			card.destroy_image(image);
		card.destroy_image(fog);
		for (const auto &[key, pipeline] : pipelines)
			fn.vkDestroyPipeline(device, pipeline, nullptr);
		for (const auto &[key, sampler] : samplers)
			fn.vkDestroySampler(device, sampler, nullptr);
		fn.vkDestroyShaderModule(device, vertex, nullptr);
		fn.vkDestroyShaderModule(device, fragment, nullptr);
		fn.vkDestroyShaderModule(device, geometry, nullptr);
		fn.vkDestroyShaderModule(device, native_fragment, nullptr);
		fn.vkDestroyShaderModule(device, list_fragment, nullptr);
		fn.vkDestroyShaderModule(device, native_list_fragment, nullptr);
		if (sorts)
			card.destroy_pass(sorter);
		for (vk::Buffer *buffer : { &nodes, &count, &readback })
			if (buffer->buffer)
				card.destroy_buffer(*buffer);
		if (depth_picture.image)
			card.destroy_image(depth_picture);
		fn.vkDestroyPipelineLayout(device, layout, nullptr);
		fn.vkDestroyDescriptorSetLayout(device, images_layout, nullptr);
		fn.vkDestroyDescriptorSetLayout(device, lists_layout, nullptr);
	}

	/* One per blend mode, and for the lists; the rest is dynamic state */
	VkPipeline pipeline(const PmxDraw &draw, VkFormat colour, VkFormat depth_in, bool listed)
	{
		const bool blended = draw.blend && !listed;
		const uint32_t codes = uint32_t{ draw.src & 7u } << 1 | uint32_t{ draw.dst & 7u } << 4;
		const uint32_t blend = blended ? 1u | codes : 0u;
		const uint32_t key = blend | uint32_t{ draw.native } << 7 | uint32_t{ listed } << 8;
		const auto found = pipelines.find(key);

		if (found != pipelines.end())
			return found->second;

		const VkPipelineShaderStageCreateInfo stages[3] = {
			{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr },
			{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
			  listed ? (draw.native ? native_list_fragment : list_fragment)
			         : draw.native ? native_fragment : fragment,
			  "main", nullptr },
			{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_GEOMETRY_BIT, geometry, "main", nullptr },
		};
		const VkVertexInputBindingDescription binding{ 0, sizeof(PmxVertex), VK_VERTEX_INPUT_RATE_VERTEX };
		const VkVertexInputAttributeDescription attributes[4] = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PmxVertex, x) },
			{ 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PmxVertex, u) },
			{ 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(PmxVertex, color) },
			{ 3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(PmxVertex, offset) },
		};
		const VkPipelineVertexInputStateCreateInfo input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0,
			                                          1, &binding, 4, attributes };
		const VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0,
			                                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
		const VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
		VkPipelineRasterizationStateCreateInfo raster{};
		raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		raster.polygonMode = VK_POLYGON_MODE_FILL;
		raster.cullMode = VK_CULL_MODE_NONE;
		raster.lineWidth = 1.0f;
		VkPipelineMultisampleStateCreateInfo samples{};
		samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkPipelineDepthStencilStateCreateInfo depth{};
		depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth.depthTestEnable = VK_TRUE;
		VkPipelineColorBlendAttachmentState write{};
		/* Alpha reads back as all ones after the clear; the lists write no colour */
		constexpr VkColorComponentFlags RGB = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                                      VK_COLOR_COMPONENT_B_BIT;

		write.colorWriteMask = listed ? 0 : RGB;
		if (blended) {
			write.blendEnable = VK_TRUE;
			write.srcColorBlendFactor = SRC[draw.src & 7];
			write.dstColorBlendFactor = DST[draw.dst & 7];
			write.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			write.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		}
		VkPipelineColorBlendStateCreateInfo blending{};
		blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blending.attachmentCount = 1;
		blending.pAttachments = &write;
		const VkDynamicState states[4] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
			                           VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP };
		const VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 4, states };
		/* Whether the pipeline came from the cache */
		VkPipelineCreationFeedback made{}, stage_made[3] = {};
		const VkPipelineCreationFeedbackCreateInfo feedback{
			VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO, nullptr, &made, draw.native ? 3u : 2u,
			stage_made
		};
		const VkPipelineRenderingCreateInfo rendering{
			VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, card.creation_feedback() ? &feedback : nullptr, 0, 1,
			&colour, depth_in, VK_FORMAT_UNDEFINED
		};
		VkGraphicsPipelineCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.pNext = &rendering;
		info.stageCount = draw.native ? 3 : 2;
		info.pStages = stages;
		info.pVertexInputState = &input;
		info.pInputAssemblyState = &assembly;
		info.pViewportState = &viewport;
		info.pRasterizationState = &raster;
		info.pMultisampleState = &samples;
		info.pDepthStencilState = &depth;
		info.pColorBlendState = &blending;
		info.pDynamicState = &dynamic;
		info.layout = layout;

		VkPipeline created;

		card.check(fn.vkCreateGraphicsPipelines(card.device(), cache, 1, &info, nullptr, &created),
		           "a pipeline of the Neon 250");
		if ((made.flags & VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT) &&
		    (made.flags & VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT))
			read_back++;
		else
			compiled++;
		pipelines.emplace(key, created);
		if (!preparing)
			learned.add(key);
		return created;
	}

	VkSampler sampler(const PmxDraw &draw, unsigned anisotropy)
	{
		const uint32_t key = static_cast<uint32_t>(draw.wrap_u) | static_cast<uint32_t>(draw.wrap_v) << 2 |
		                     uint32_t{ draw.smooth } << 4 | uint32_t{ draw.mipmap } << 5 | anisotropy << 6;
		const auto found = samplers.find(key);

		if (found != samplers.end())
			return found->second;

		const auto wrap = [](PmxWrap how) {
			return how == PmxWrap::CLAMP ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE :
			       how == PmxWrap::MIRROR ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : VK_SAMPLER_ADDRESS_MODE_REPEAT;
		};
		const float most = std::min(static_cast<float>(anisotropy), card.max_anisotropy());
		VkSamplerCreateInfo info{};

		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = info.minFilter = draw.smooth ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = wrap(draw.wrap_u);
		info.addressModeV = wrap(draw.wrap_v);
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.anisotropyEnable = most > 1.0f && draw.mipmap ? VK_TRUE : VK_FALSE;
		info.maxAnisotropy = std::max(most, 1.0f);
		info.maxLod = draw.mipmap ? VK_LOD_CLAMP_NONE : 0.0f;

		VkSampler made;

		card.check(fn.vkCreateSampler(card.device(), &info, nullptr, &made), "a sampler of the Neon 250");
		samplers.emplace(key, made);
		sampler_number[made] = static_cast<uint32_t>(samplers.size());
		return made;
	}
};

/* Pmx, then the pipelines known before any scene: the plain one, that of
 * the lists where the card sorts per pixel, their native 2D forms where it
 * draws them, then those of the keys the card runs; the others when first
 * met, then listed (list_pmx_in) */
void
VkDrawing::prepare_pmx(const std::vector<uint64_t> &keys, const std::function<void(unsigned, unsigned)> &progress)
{
	if (m_pmx)
		return;

	const bool native = draws_native(), lists = sorts_per_pixel();
	std::vector<uint64_t> wanted = { 0x000 };

	if (lists)
		wanted.push_back(0x100);
	if (native)
		wanted.push_back(0x080);
	if (native && lists)
		wanted.push_back(0x180);
	for (const uint64_t key : keys)
		if ((native || !(key & 0x080)) && (lists || !(key & 0x100)) &&
		    std::find(wanted.begin(), wanted.end(), key) == wanted.end())
			wanted.push_back(key);

	const unsigned total = 1 + static_cast<unsigned>(wanted.size());
	unsigned done = 0;

	progress(0, total);
	m_pmx = std::make_shared<Pmx>(m_card, m_fn, m_cache, m_read_back, m_compiled, m_pmx_list);
	progress(++done, total);

	const VkFormat depth_in =
		m_pmx->depth_format != VK_FORMAT_UNDEFINED ? m_pmx->depth_format : m_card.depth_format();

	m_pmx->preparing = true;
	for (const uint64_t key : wanted) {
		PmxDraw draw{};

		draw.blend = key & 1;
		draw.src = static_cast<uint8_t>(key >> 1 & 7);
		draw.dst = static_cast<uint8_t>(key >> 4 & 7);
		draw.native = key & 0x080;
		draw.per_pixel = key & 0x100;
		m_pmx->pipeline(draw, PICTURE_FORMAT, depth_in, draw.per_pixel);
		progress(++done, total);
	}
	m_pmx->preparing = false;
}

void
VkDrawing::list_pmx_in(const std::string &path)
{
	m_pmx_list.list_in(path, PMX_LIST_HEADER);
}

void
VkDrawing::draw_pmx(const PmxScene &scene, unsigned anisotropy)
{
	if (!m_pmx)
		m_pmx = std::make_shared<Pmx>(m_card, m_fn, m_cache, m_read_back, m_compiled, m_pmx_list);

	Pmx &pmx = *m_pmx;
	const VkCommandBuffer cb = m_card.commands();

	pmx.scenes++;
	pmx.take_back_sets();

	for (const uint32_t id : scene.gone_images)
		if (const auto found = pmx.images.find(id); found != pmx.images.end()) {
			pmx.forget_sets(id);
			m_card.later([card = &m_card, image = found->second]() mutable { card->destroy_image(image); });
			pmx.images.erase(found);
		}
	for (const std::shared_ptr<const PmxImage> &image : scene.new_images) {
		const unsigned levels = static_cast<unsigned>(image->levels.size());
		vk::Image made = m_card.make_image(VK_FORMAT_R8G8B8A8_UNORM, image->width, image->height, levels,
		                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

		m_card.barrier(cb, made, vk::COPIED_TO);
		for (unsigned level = 0; level < levels; level++) {
			const unsigned w = std::max(image->width >> level, 1u), h = std::max(image->height >> level, 1u);
			const VkDeviceSize bytes = VkDeviceSize{ w } * h * 4;
			const vk::Slice in = m_card.stage(bytes, 4);

			std::memcpy(in.bytes, image->levels[level].data(), bytes);

			const VkBufferImageCopy region{ in.offset, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 }, { 0, 0, 0 }, { w, h, 1 } };

			m_fn.vkCmdCopyBufferToImage(cb, in.buffer, made.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		}
		m_card.barrier(cb, made, vk::SAMPLED);
		pmx.images[image->id] = made;
	}

	if (!pmx.fog_loaded || pmx.fog_table != scene.fog_table) {
		const vk::Slice in = m_card.stage(scene.fog_table.size(), 4);
		const VkBufferImageCopy region{ in.offset, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { 128, 1, 1 } };

		std::memcpy(in.bytes, scene.fog_table.data(), scene.fog_table.size());
		m_card.barrier(cb, pmx.fog, vk::COPIED_TO);
		m_fn.vkCmdCopyBufferToImage(cb, in.buffer, pmx.fog.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		m_card.barrier(cb, pmx.fog, vk::SAMPLED);
		pmx.fog_table = scene.fog_table;
		pmx.fog_loaded = true;
	}

	const VkDeviceSize vertex_bytes = std::max<size_t>(scene.vertices.size(), 1) * sizeof(PmxVertex);
	const vk::Slice vertices = m_card.stage(vertex_bytes, 16);

	if (!scene.vertices.empty())
		std::memcpy(vertices.bytes, scene.vertices.data(), scene.vertices.size() * sizeof(PmxVertex));

	/* The depth as a float where the card has one, else the board's */
	const bool float_depth = pmx.depth_format != VK_FORMAT_UNDEFINED;

	if (float_depth && (pmx.depth_picture.w != width() || pmx.depth_picture.h != height())) {
		if (pmx.depth_picture.image) {
			vk::Image old = pmx.depth_picture;

			m_card.later([card = &m_card, old]() mutable { card->destroy_image(old); });
		}
		pmx.depth_picture = m_card.make_image(pmx.depth_format, width(), height(), 1,
		                                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	}
	vk::Image &depth_image = float_depth ? pmx.depth_picture : m_depth;
	const VkFormat depth_in = float_depth ? pmx.depth_format : m_card.depth_format();
	std::array<VkCompareOp, 8> compare = COMPARE;

	if (!float_depth)
		compare[4] = VK_COMPARE_OP_GREATER_OR_EQUAL;

	/* The lists emptied, and the buffer sized by the count FRAMES frames
	 * before (as with OpenGL: room for 8 a pixel of the board at first) */
	const bool sorting = pmx.sorts && scene.vertices.size() / 3 < RANKS &&
	                     std::any_of(scene.draws.begin(), scene.draws.end(),
	                                 [](const PmxDraw &draw) { return draw.per_pixel; });
	const uint64_t slot = m_card.sending() % vk::Context::FRAMES;
	VkDescriptorSet lists_set = VK_NULL_HANDLE;

	if (sorting) {
		uint32_t counted = 0;

		std::memcpy(&counted, pmx.readback.bytes + 4 * slot, 4);

		/* At first 8 fragments a pixel of the board's picture (the smoke of
		 * Smashing Drive needs 5.2), or one a pixel drawn; more when a frame
		 * needs them */
		const VkDeviceSize board = VkDeviceSize{ 8 } * SCREEN_WIDTH * SCREEN_HEIGHT;
		VkDeviceSize wanted = pmx.room ? pmx.room : std::max(board, VkDeviceSize{ width() } * height());

		if (pmx.room && counted + counted / 4 > pmx.room)
			wanted = VkDeviceSize{ counted } + counted / 2;
		wanted = std::min<VkDeviceSize>(wanted, m_card.limits().maxStorageBufferRange / PMX_NODE_SIZE);
		if (wanted > pmx.room) {
			if (pmx.nodes.buffer) {
				vk::Buffer old = pmx.nodes;

				m_card.later([card = &m_card, old]() mutable { card->destroy_buffer(old); });
			}
			pmx.nodes = m_card.make_card_buffer(wanted * PMX_NODE_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
			pmx.room = wanted;
		}

		const VkClearColorValue zero{};
		const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		m_card.barrier(cb, m_heads, HEADS_EMPTIED);
		m_fn.vkCmdClearColorImage(cb, m_heads.image, m_heads.layout, &zero, 1, &all);
		m_fn.vkCmdFillBuffer(cb, pmx.count.buffer, 0, 4, 0);
		m_card.memory_barrier(cb, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		                      VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
		m_card.barrier(cb, m_heads, HEADS_USED);

		lists_set = m_card.frame_set(pmx.lists_layout);

		const VkDescriptorBufferInfo nodes{ pmx.nodes.buffer, 0, VK_WHOLE_SIZE };
		const VkDescriptorImageInfo heads{ VK_NULL_HANDLE, m_heads.view, m_heads.layout };
		const VkDescriptorBufferInfo count{ pmx.count.buffer, 0, 4 };
		constexpr VkStructureType WRITE = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		constexpr VkDescriptorType BUFFER = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		constexpr VkDescriptorType IMAGE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		const VkWriteDescriptorSet writes[3] = {
			{ WRITE, nullptr, lists_set, 0, 0, 1, BUFFER, nullptr, &nodes, nullptr },
			{ WRITE, nullptr, lists_set, 1, 0, 1, IMAGE, &heads, nullptr, nullptr },
			{ WRITE, nullptr, lists_set, 2, 0, 1, BUFFER, nullptr, &count, nullptr },
		};

		m_fn.vkUpdateDescriptorSets(m_card.device(), 3, writes, 0, nullptr);
	}

	m_card.barrier(cb, m_scene, vk::DRAWN_IN);
	m_card.barrier(cb, depth_image, vk::DEPTH_IN);

	VkRenderingAttachmentInfo colour{};
	colour.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colour.imageView = m_scene.view;
	colour.imageLayout = m_scene.layout;
	colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colour.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	VkRenderingAttachmentInfo depth{};
	depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depth.imageView = depth_image.view;
	depth.imageLayout = depth_image.layout;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	/* Kept for the draws after the lists are blended */
	depth.storeOp = sorting ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth.clearValue.depthStencil = { 0.0f, 0 };
	const VkRect2D area{ { 0, 0 }, { width(), height() } };
	VkRenderingInfo rendering{};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea = area;
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &colour;
	rendering.pDepthAttachment = &depth;
	m_fn.vkCmdBeginRendering(cb, &rendering);

	const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width()), static_cast<float>(height()), 0.0f, 1.0f };

	m_fn.vkCmdSetViewport(cb, 0, 1, &viewport);
	m_fn.vkCmdSetScissor(cb, 0, 1, &area);
	m_fn.vkCmdBindVertexBuffers(cb, 0, 1, &vertices.buffer, &vertices.offset);

	VkPipeline bound = VK_NULL_HANDLE;
	VkDescriptorSet bound_set = VK_NULL_HANDLE;
	bool collecting = false;

	for (size_t k = 0; k < scene.draws.size(); k++) {
		const PmxDraw &draw = scene.draws[k];
		const bool listed = sorting && draw.per_pixel;
		const VkPipeline pipeline = pmx.pipeline(draw, PICTURE_FORMAT, depth_in, listed);

		if (pipeline != bound) {
			m_fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			bound = pipeline;
		}
		if (listed && !collecting) {
			m_fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pmx.layout, 1, 1,
			                             &lists_set, 0, nullptr);
			collecting = true;
		}
		m_fn.vkCmdSetDepthCompareOp(cb, compare[draw.compare & 7]);
		m_fn.vkCmdSetDepthWriteEnable(cb, draw.write_z ? VK_TRUE : VK_FALSE);

		/* One set per image and sampler, cached with them */
		const auto found = draw.image ? pmx.images.find(draw.image) : pmx.images.end();
		const vk::Image &image = found != pmx.images.end() ? found->second : m_none;
		const VkSampler sampler = pmx.sampler(draw, anisotropy);
		const uint64_t key = uint64_t{ found != pmx.images.end() ? draw.image : 0u } << 32 |
		                     pmx.sampler_number[sampler];
		auto kept = pmx.sets.find(key);

		if (kept == pmx.sets.end()) {
			const VkDescriptorSet set = pmx.make_set(image, sampler);

			kept = pmx.sets.emplace(key, set).first;
		}
		if (kept->second != bound_set) {
			bound_set = kept->second;
			m_fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pmx.layout, 0, 1, &bound_set, 0, nullptr);
		}

		const DrawPush push{ scene.most_w,
			             draw.lod_bias,
			             (draw.image ? 1 : 0) | (draw.use_alpha ? 2 : 0) | (draw.ignore_texture_alpha ? 4 : 0) |
			                     (draw.offset ? 8 : 0) | draw.shading << 4 | draw.fog << 6,
			             static_cast<float>(m_scale),
			             scene.fog_density,
			             scene.fog_color,
			             scene.fog_vertex_color,
			             draw.first / 3,
			             (draw.src & 7u) | (draw.dst & 7u) << 3 };

		m_fn.vkCmdPushConstants(cb, pmx.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
		m_fn.vkCmdDraw(cb, draw.count, 1, draw.first, 0);
		if (!collecting || (k + 1 < scene.draws.size() && scene.draws[k + 1].per_pixel))
			continue;

		/* The lists blended over the colour below, then the drawing goes on */
		m_card.end_drawing(cb);
		m_card.memory_barrier(cb, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
		copy_whole(cb, m_scene, m_below);
		m_card.barrier(cb, m_below, vk::SAMPLED);
		m_card.barrier(cb, m_heads, HEADS_USED);

		const VkDescriptorSet sort_set = m_card.frame_set(pmx.sorter.images);
		const VkDescriptorBufferInfo nodes{ pmx.nodes.buffer, 0, VK_WHOLE_SIZE };
		const VkDescriptorImageInfo heads{ VK_NULL_HANDLE, m_heads.view, m_heads.layout };
		const VkDescriptorImageInfo below{ m_nearest, m_below.view, m_below.layout };
		constexpr VkStructureType WRITE = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		constexpr VkDescriptorType BUFFER = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		constexpr VkDescriptorType IMAGE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		constexpr VkDescriptorType SAMPLED = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		const VkWriteDescriptorSet writes[3] = {
			{ WRITE, nullptr, sort_set, 0, 0, 1, BUFFER, nullptr, &nodes, nullptr },
			{ WRITE, nullptr, sort_set, 1, 0, 1, IMAGE, &heads, nullptr, nullptr },
			{ WRITE, nullptr, sort_set, 2, 0, 1, SAMPLED, &below, nullptr, nullptr },
		};

		m_fn.vkUpdateDescriptorSets(m_card.device(), 3, writes, 0, nullptr);
		m_card.begin_drawing(cb, m_scene, area);
		m_fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pmx.sorter.pipeline);
		m_fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pmx.sorter.layout, 0, 1, &sort_set, 0,
		                             nullptr);
		m_fn.vkCmdDraw(cb, 3, 1, 0, 0);
		m_card.end_drawing(cb);

		/* The next group's lists start empty, as OpenGL's do (begin_lists) */
		const VkClearColorValue zero{};
		const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		m_card.barrier(cb, m_heads, HEADS_EMPTIED);
		m_fn.vkCmdClearColorImage(cb, m_heads.image, m_heads.layout, &zero, 1, &all);
		m_card.barrier(cb, m_heads, HEADS_USED);
		m_card.barrier(cb, m_scene, vk::DRAWN_IN);
		m_card.barrier(cb, depth_image, vk::DEPTH_IN);
		colour.imageLayout = m_scene.layout;
		colour.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depth.imageLayout = depth_image.layout;
		depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		m_fn.vkCmdBeginRendering(cb, &rendering);
		m_fn.vkCmdSetViewport(cb, 0, 1, &viewport);
		m_fn.vkCmdSetScissor(cb, 0, 1, &area);
		m_fn.vkCmdBindVertexBuffers(cb, 0, 1, &vertices.buffer, &vertices.offset);
		bound = VK_NULL_HANDLE;
		bound_set = VK_NULL_HANDLE;
		collecting = false;
	}
	m_card.end_drawing(cb);
	if (sorting) {
		const VkBufferCopy copy{ 0, 4 * slot, 4 };

		m_card.memory_barrier(cb, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
		                      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
		m_fn.vkCmdCopyBuffer(cb, pmx.count.buffer, pmx.readback.buffer, 1, &copy);
		/* As translucent_vk.cpp: the count read by the host once the card is done */
		m_card.memory_barrier(cb, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		                      VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
	}

	const VkImageCopy copy{ { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		                { 0, 0, 0 }, { width(), height(), 1 } };

	m_card.barrier(cb, m_scene, vk::COPIED_FROM);
	m_card.barrier(cb, m_frame, vk::COPIED_TO);
	m_fn.vkCmdCopyImage(cb, m_scene.image, m_scene.layout, m_frame.image, m_frame.layout, 1, &copy);
}

} /* namespace odin */
