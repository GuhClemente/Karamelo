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

/* The board drawing with Vulkan (drawing.hpp): made, its textures and
 * their samplers. */

#include "odin/host/renderer/hikaru/vk/drawing_vk.hpp"

#include <algorithm>
#include <cstring>

namespace odin {

namespace {

/* The texels of the board as Vulkan keeps them, by Texels: formats every
 * card has, the colours put back in their place by the view */
struct VkTexels {
	VkFormat format;
	VkComponentMapping swizzle;
};
constexpr VkTexels VK_TEXELS[] = {
	/* ABGR1555: the alpha on top, then blue, green and red */
	{ VK_FORMAT_A1R5G5B5_UNORM_PACK16, ABGR1555_SWIZZLE },
	/* ABGR4444: the same; Vulkan's alpha is in the bits of the red */
	{ VK_FORMAT_B4G4R4A4_UNORM_PACK16,
	  { VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B } },
	/* LA8: the light in the three colours */
	{ VK_FORMAT_R8G8_UNORM,
	  { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G } },
};
constexpr unsigned TEXEL_BYTES = 2;

} /* namespace */

std::unique_ptr<Drawing>
Drawing::make_vk(vk::Context &card)
{
	return std::make_unique<VkDrawing>(card);
}

void
VkDrawing::make_sets(KeptSets &sets, VkDescriptorType type, VkShaderStageFlags stages)
{
	const VkDescriptorSetLayoutBinding binding{ 0, type, 1, stages, nullptr };
	const VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1,
	                                            &binding };

	sets.type = type;
	m_card.check(m_fn.vkCreateDescriptorSetLayout(m_card.device(), &info, nullptr, &sets.layout), "a set layout");
}

VkDrawing::KeptSet
VkDrawing::keep_set(KeptSets &sets)
{
	constexpr uint32_t SETS = 1024;
	KeptSet kept;
	const auto allocate = [&](VkDescriptorPool pool) {
		const VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, pool, 1,
		                                        &sets.layout };

		kept.pool = pool;
		return m_fn.vkAllocateDescriptorSets(m_card.device(), &info, &kept.set);
	};

	if (!sets.pools.empty() && allocate(sets.pools.back()) == VK_SUCCESS)
		return kept;
	/* The last pool is full: the sets freed in the others are taken
	 * again from them first */
	for (size_t i = 0; i + 1 < sets.pools.size(); i++)
		if (allocate(sets.pools[i]) == VK_SUCCESS)
			return kept;

	const VkDescriptorPoolSize size{ sets.type, SETS };
	const VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr,
	                                       VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, SETS, 1, &size };
	VkDescriptorPool pool;

	m_card.check(m_fn.vkCreateDescriptorPool(m_card.device(), &info, nullptr, &pool), "a descriptor pool");
	sets.pools.push_back(pool);
	m_card.check(allocate(pool), "a descriptor set");
	return kept;
}

/* Once the frames that may use it are done */
void
VkDrawing::free_set(const KeptSet &set)
{
	m_card.later([card = &m_card, set]() {
		card->fn().vkFreeDescriptorSets(card->device(), set.pool, 1, &set.set);
	});
}

void
VkDrawing::destroy_sets(KeptSets &sets)
{
	for (const VkDescriptorPool pool : sets.pools)
		m_fn.vkDestroyDescriptorPool(m_card.device(), pool, nullptr);
	m_fn.vkDestroyDescriptorSetLayout(m_card.device(), sets.layout, nullptr);
	sets.pools.clear();
	sets.layout = VK_NULL_HANDLE;
}

/* A sampled image as the mesh shaders read it */
VkDrawing::KeptSet
VkDrawing::image_set(const vk::Image &image)
{
	const KeptSet set = keep_set(m_texture_sets);
	const VkDescriptorImageInfo info{ VK_NULL_HANDLE, image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	const VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set.set, 0, 0, 1,
		                          VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &info, nullptr, nullptr };

	m_fn.vkUpdateDescriptorSets(m_card.device(), 1, &write, 0, nullptr);
	return set;
}

/* The texels are copied at once, each level packed, into memory the
 * uploads copy from before the commands of the frame: the texture may be
 * made while the meshes of a list are drawn. */
HostTexture
VkDrawing::make_texture(Texels texels, std::span<const TexelRect> levels)
{
	const VkTexels &format = VK_TEXELS[static_cast<size_t>(texels)];
	const unsigned num_levels = static_cast<unsigned>(levels.size());
	std::vector<VkBufferImageCopy> regions(num_levels);
	VkDeviceSize bytes = 0;

	for (unsigned i = 0; i < num_levels; i++) {
		bytes = (bytes + 15) / 16 * 16;
		regions[i] = { bytes, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 }, { 0, 0, 0 },
		               { levels[i].w, levels[i].h, 1 } };
		bytes += VkDeviceSize{levels[i].w} * levels[i].h * TEXEL_BYTES;
	}
	const vk::Slice in = m_card.stage(bytes, 16);
	for (unsigned i = 0; i < num_levels; i++) {
		const TexelRect &l = levels[i];
		const uint8_t *from = static_cast<const uint8_t *>(l.texels);
		uint8_t *to = in.bytes + regions[i].bufferOffset;
		const size_t line = size_t{l.w} * TEXEL_BYTES;

		for (unsigned y = 0; y < l.h; y++)
			std::memcpy(to + y * line, from + ((size_t{l.y} + y) * l.row + l.x) * TEXEL_BYTES, line);
		regions[i].bufferOffset += in.offset;
	}

	Texture texture;
	texture.image = m_card.make_placed_image(format.format, levels[0].w, levels[0].h, num_levels,
	                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	                                         format.swizzle);
	vk::Image &image = texture.image;
	const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, num_levels, 0, 1 };
	const VkImageMemoryBarrier2 before{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr,
	                                    VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
	                                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
	                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED,
	                                    VK_QUEUE_FAMILY_IGNORED, image.image, all };
	const VkImageMemoryBarrier2 after{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr,
	                                   VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
	                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
	                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                                   VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image.image, all };
	const VkDependencyInfo first{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &before };
	const VkDependencyInfo then{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &after };
	const VkCommandBuffer cb = m_card.uploads();

	m_fn.vkCmdPipelineBarrier2(cb, &first);
	m_fn.vkCmdCopyBufferToImage(cb, in.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_levels,
	                            regions.data());
	m_fn.vkCmdPipelineBarrier2(cb, &then);
	image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	texture.set = image_set(image);

	const uint32_t id = m_next_texture++;
	m_textures.emplace(id, texture);
	return { id };
}

void
VkDrawing::forget_textures(std::span<const HostTexture> textures)
{
	for (const HostTexture t : textures) {
		const auto found = m_textures.find(t.id);

		if (found == m_textures.end())
			continue;
		vk::Image image = found->second.image;
		free_set(found->second.set);
		m_card.later([card = &m_card, image]() mutable { card->destroy_image(image); });
		m_textures.erase(found);
	}
}

/* As OpenGL's: filtering without mipmaps reads the first level alone, as
 * the specification of Vulkan has it (maxLod 0.25); the bias as far as the
 * card takes it, as OpenGL clamps it */
HostSampler
VkDrawing::make_sampler(const TextureSampling &sampling, unsigned anisotropy)
{
	const auto wrap = [](TextureSampling::Wrap w) {
		return w == TextureSampling::Wrap::MIRROR ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT :
		       w == TextureSampling::Wrap::REPEAT ? VK_SAMPLER_ADDRESS_MODE_REPEAT :
		                                            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	};
	const float bias = m_card.limits().maxSamplerLodBias;
	VkSamplerCreateInfo info{};

	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.addressModeU = wrap(sampling.u);
	info.addressModeV = wrap(sampling.v);
	info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	switch (sampling.filter) {
	case TextureSampling::Filter::NEAREST:
		info.magFilter = info.minFilter = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.maxLod = 0.25f;
		break;
	case TextureSampling::Filter::LINEAR:
		info.magFilter = info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.maxLod = 0.25f;
		break;
	case TextureSampling::Filter::TRILINEAR:
		info.magFilter = info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		info.maxLod = VK_LOD_CLAMP_NONE;
		break;
	}
	info.mipLodBias = std::clamp(sampling.lod_bias, -bias, bias);
	if (anisotropy > 1 && m_card.max_anisotropy() > 1.0f) {
		info.anisotropyEnable = VK_TRUE;
		info.maxAnisotropy = std::min(static_cast<float>(anisotropy), m_card.max_anisotropy());
	}

	Sampler sampler;
	m_card.check(m_fn.vkCreateSampler(m_card.device(), &info, nullptr, &sampler.sampler), "a sampler");
	sampler.set = keep_set(m_sampler_sets);
	const VkDescriptorImageInfo image{ sampler.sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
	const VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sampler.set.set, 0, 0, 1,
		                          VK_DESCRIPTOR_TYPE_SAMPLER, &image, nullptr, nullptr };
	m_fn.vkUpdateDescriptorSets(m_card.device(), 1, &write, 0, nullptr);

	const uint32_t id = m_next_sampler++;
	m_samplers.emplace(id, sampler);
	return { id };
}

void
VkDrawing::forget_sampler(HostSampler s)
{
	const auto found = m_samplers.find(s.id);

	if (found == m_samplers.end())
		return;
	const VkSampler sampler = found->second.sampler;
	free_set(found->second.set);
	m_card.later([card = &m_card, sampler]() { card->fn().vkDestroySampler(card->device(), sampler, nullptr); });
	m_samplers.erase(found);
}

/* Debug: the stamps of ODIN_DRAW_TIMES, as glQueryCounter. None when all
 * are out: the frame is left out. */
uint32_t
VkDrawing::put_stamp()
{
	constexpr uint32_t STAMPS = 4096;

	if (!m_stamps) {
		const VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0,
		                                  VK_QUERY_TYPE_TIMESTAMP, STAMPS, 0 };

		m_card.check(m_fn.vkCreateQueryPool(m_card.device(), &info, nullptr, &m_stamps), "the stamps");
		m_fn.vkResetQueryPool(m_card.device(), m_stamps, 0, STAMPS);
		for (uint32_t i = STAMPS; i > 0; i--)
			m_free_stamps.push_back(i - 1);
	}
	if (m_free_stamps.empty())
		return 0;
	const uint32_t query = m_free_stamps.back();
	m_free_stamps.pop_back();
	m_fn.vkCmdWriteTimestamp2(m_card.commands(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_stamps, query);
	return query + 1;
}

bool
VkDrawing::reached(uint32_t stamp)
{
	uint64_t at;

	return stamp && m_fn.vkGetQueryPoolResults(m_card.device(), m_stamps, stamp - 1, 1, sizeof(at), &at, sizeof(at),
	                                           VK_QUERY_RESULT_64_BIT) == VK_SUCCESS;
}

/* Once reached, in ns */
uint64_t
VkDrawing::stamp_time(uint32_t stamp)
{
	const uint32_t bits = m_card.stamp_bits();
	uint64_t at = 0;

	if (stamp)
		m_fn.vkGetQueryPoolResults(m_card.device(), m_stamps, stamp - 1, 1, sizeof(at), &at, sizeof(at),
		                           VK_QUERY_RESULT_64_BIT);
	if (bits < 64)
		at &= (uint64_t{1} << bits) - 1;
	return static_cast<uint64_t>(static_cast<double>(at) * m_card.stamp_period());
}

/* A stamp the card still owes is put again once the frame being written is
 * done: the card wrote it then. */
void
VkDrawing::forget_stamp(uint32_t stamp, bool again)
{
	if (!stamp)
		return;
	const auto free = [this, query = stamp - 1]() {
		m_fn.vkResetQueryPool(m_card.device(), m_stamps, query, 1);
		m_free_stamps.push_back(query);
	};
	if (again)
		free();
	else
		m_card.later(free);
}

} /* namespace odin */
