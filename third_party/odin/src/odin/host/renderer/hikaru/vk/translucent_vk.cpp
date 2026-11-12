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

/* The translucent fragments of a view with Vulkan (drawing.hpp; the
 * rules they follow: renderer_translucent.cpp), as translucent_gl.cpp
 * does it: a fragment goes to the list of its pixel, in a storage buffer,
 * then every pixel blends its list (glsl/sort.frag). The copies and the
 * barriers between those steps stay out of the rendering of the scene,
 * which is closed and opened again around them (scene_vk.cpp). The
 * meshes read gl_PrimitiveID: a card with neither geometry nor tessellation
 * shaders draws no translucent fragments. */

#include "odin/host/renderer/hikaru/vk/drawing_vk.hpp"
#include "odin/host/gfx/spirv.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace odin {

namespace {

/* Where the processor reads the counts: those of the three last lists,
 * eight views each, then the one read now */
constexpr uint32_t COUNT_NOW = Drawing::PIPE * ODIN_VIEWPORTS;

/* The last fragment of each pixel: emptied, then written and read by the
 * fragment shaders */
constexpr vk::Use HEADS_EMPTIED = { VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
	                            VK_ACCESS_2_TRANSFER_WRITE_BIT };
constexpr vk::Use HEADS_USED = { VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	                         VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT };

} /* namespace */

/* The pass that blends the fragments, the counts of the views and where
 * the processor reads them */
void
VkDrawing::build_lists()
{
	const std::vector<VkDescriptorSetLayoutBinding> sort = {
		{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};

	m_sorter = m_card.make_pass(ODIN_SPIRV_OF(whole_picture_vert), ODIN_SPIRV_OF(sort_frag), sort, 0,
	                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, PICTURE_FORMAT);
	m_count_stride = std::max<VkDeviceSize>(m_card.limits().minStorageBufferOffsetAlignment, 4);
	m_counts = m_card.make_card_buffer(ODIN_VIEWPORTS * m_count_stride, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	                                                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
	                                                               VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	m_readback = m_card.make_buffer((COUNT_NOW + 1) * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	std::memset(m_readback.bytes, 0, (COUNT_NOW + 1) * sizeof(uint32_t));
}

/* The card is done with them: the frames were waited for. */
void
VkDrawing::destroy_lists()
{
	for (vk::Buffer *buffer : { &m_nodes, &m_counts, &m_readback })
		if (buffer->buffer)
			m_card.destroy_buffer(*buffer);
	m_card.destroy_pass(m_sorter);
}

/* The pictures of the lists, at the size of the scene: the depth the other
 * types left, the last fragment of each pixel, the colour under them */
void
VkDrawing::build_list_pictures()
{
	m_solid_depth = m_card.make_image(m_card.depth_format(), width(), height(), 1,
	                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	m_heads = m_card.make_image(VK_FORMAT_R32_UINT, width(), height(), 1,
	                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	m_below = m_card.make_image(PICTURE_FORMAT, width(), height(), 1,
	                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	for (vk::Image &eye : m_eye)
		eye = m_card.make_image(EYE_FORMAT, width(), height(), 1,
		                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	m_flag_depth = m_card.make_image(m_card.depth_format(), width(), height(), 1,
	                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

/* What the buffer held is lost; the old one goes once the card is done. */
void
VkDrawing::size_fragments(uint32_t max)
{
	const VkDeviceSize bytes = VkDeviceSize{std::max(max, 1u)} * NODE_SIZE;

	if (m_nodes.buffer) {
		vk::Buffer old = m_nodes;

		m_card.later([card = &m_card, old]() mutable { card->destroy_buffer(old); });
	}
	m_nodes = m_card.make_card_buffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

uint32_t
VkDrawing::fragment_limit() const
{
	return static_cast<uint32_t>(m_card.limits().maxStorageBufferRange / NODE_SIZE);
}

/* The depth the other types left */
void
VkDrawing::begin_translucent()
{
	const VkCommandBuffer cb = m_card.commands();

	use_depth();
	close_scene();
	if (m_translucent_left)
		m_translucent_left--;
	if (!m_card.primitive_ids()) {
		if (!m_said_no_lists)
			std::fprintf(stderr, "ERROR: vulkan: no translucent polygons on this card (no geometry nor tessellation shaders)\n");
		m_said_no_lists = true;
		return;
	}
	copy_whole(cb, m_depth, m_solid_depth, VK_IMAGE_ASPECT_DEPTH_BIT);
	m_card.barrier(cb, m_solid_depth, vk::SAMPLED);
	/* What each family left, in a view with both (begin_families) */
	for (vk::Image &eye : m_eye)
		m_card.barrier(cb, eye, vk::SAMPLED);
}

/* The lists of the pixels and the count of the view emptied; what the
 * last blending read is read before (glMemoryBarrier of all in OpenGL). Each
 * viewport has its count, as with OpenGL. */
void
VkDrawing::begin_collect(unsigned view, unsigned)
{
	if (!m_card.primitive_ids())
		return;
	const VkCommandBuffer cb = m_card.commands();
	const VkClearColorValue zero{};
	const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	m_card.memory_barrier(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
	                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
	                      VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
	m_card.barrier(cb, m_heads, HEADS_EMPTIED);
	m_fn.vkCmdClearColorImage(cb, m_heads.image, m_heads.layout, &zero, 1, &all);
	m_fn.vkCmdFillBuffer(cb, m_counts.buffer, view * m_count_stride, sizeof(uint32_t), 0);
	m_card.memory_barrier(cb, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
	                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	                      VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	m_card.barrier(cb, m_heads, HEADS_USED);

	m_collect_set = m_card.frame_set(m_collect_layout);
	const VkDescriptorBufferInfo nodes{ m_nodes.buffer, 0, VK_WHOLE_SIZE };
	const VkDescriptorImageInfo heads{ VK_NULL_HANDLE, m_heads.view, m_heads.layout };
	const VkDescriptorBufferInfo count{ m_counts.buffer, view * m_count_stride, sizeof(uint32_t) };
	const VkDescriptorImageInfo depth{ m_nearest, m_solid_depth.view, m_solid_depth.layout };
	const VkDescriptorImageInfo eyes[2] = { { m_nearest, m_eye[0].view, m_eye[0].layout },
		                                { m_nearest, m_eye[1].view, m_eye[1].layout } };
	const VkWriteDescriptorSet writes[6] = {
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_collect_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		  nullptr, &nodes, nullptr },
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_collect_set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		  &heads, nullptr, nullptr },
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_collect_set, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		  nullptr, &count, nullptr },
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_collect_set, 3, 0, 1,
		  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depth, nullptr, nullptr },
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_collect_set, 4, 0, 1,
		  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &eyes[0], nullptr, nullptr },
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_collect_set, 5, 0, 1,
		  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &eyes[1], nullptr, nullptr },
	};
	m_fn.vkUpdateDescriptorSets(m_card.device(), 6, writes, 0, nullptr);

	m_collecting = true;
	m_pass.order = 0;
	open_scene(false);
}

/* What the meshes wrote is seen by the copies and the blending after */
void
VkDrawing::end_collect()
{
	if (!m_collecting)
		return;
	close_scene();
	m_collecting = false;
	m_card.memory_barrier(m_card.commands(), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	                      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	                      VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

/* Nobody waits for the card here: the count goes where the processor reads
 * it once the commands it is sent with are done (counted_fragments). */
void
VkDrawing::keep_count(unsigned view, unsigned slot)
{
	if (!m_card.primitive_ids())
		return;
	const VkCommandBuffer cb = m_card.commands();
	const VkBufferCopy copy{ view * m_count_stride, count_index(slot, view) * sizeof(uint32_t), sizeof(uint32_t) };

	m_fn.vkCmdCopyBuffer(cb, m_counts.buffer, m_readback.buffer, 1, &copy);
	m_card.memory_barrier(cb, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
	                      VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
	m_counted[slot] = m_card.sending();
}

/* The commands sent and waited for: the next ones begin another command
 * buffer, which the scene sets up again when it opens. */
uint32_t
VkDrawing::count_now(unsigned view, unsigned)
{
	if (!m_card.primitive_ids())
		return 0;
	const VkCommandBuffer cb = m_card.commands();
	const VkBufferCopy copy{ view * m_count_stride, COUNT_NOW * sizeof(uint32_t), sizeof(uint32_t) };
	uint32_t used;

	m_fn.vkCmdCopyBuffer(cb, m_counts.buffer, m_readback.buffer, 1, &copy);
	m_card.memory_barrier(cb, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
	                      VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
	m_card.send_and_wait();
	std::memcpy(&used, m_readback.bytes + COUNT_NOW * sizeof(uint32_t), sizeof(used));
	return used;
}

/* Blends them over the colour below, the whole picture */
void
VkDrawing::end_translucent(bool blend)
{
	if (!m_card.primitive_ids() || !blend)
		return;
	const VkCommandBuffer cb = m_card.commands();

	copy_whole(cb, m_scene, m_below);
	m_card.barrier(cb, m_below, vk::SAMPLED);
	m_card.barrier(cb, m_heads, HEADS_USED);

	const VkDescriptorSet set = m_card.frame_set(m_sorter.images);
	const VkDescriptorBufferInfo nodes{ m_nodes.buffer, 0, VK_WHOLE_SIZE };
	const VkDescriptorImageInfo heads{ VK_NULL_HANDLE, m_heads.view, m_heads.layout };
	const VkDescriptorImageInfo below{ m_nearest, m_below.view, m_below.layout };
	const VkWriteDescriptorSet writes[3] = {
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr,
		  &nodes, nullptr },
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &heads,
		  nullptr, nullptr },
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		  &below, nullptr, nullptr },
	};
	m_fn.vkUpdateDescriptorSets(m_card.device(), 3, writes, 0, nullptr);

	m_card.begin_drawing(cb, m_scene, { { 0, 0 }, { width(), height() } });
	m_fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sorter.pipeline);
	m_fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_sorter.layout, 0, 1, &set, 0, nullptr);
	m_fn.vkCmdDraw(cb, 3, 1, 0, 0);
	m_card.end_drawing(cb);
}

/* The most fragments a view of the list of that picture left, the commands
 * that copied its counts done */
uint32_t
VkDrawing::counted_fragments(unsigned slot, uint32_t views)
{
	uint32_t used = 0;

	if (m_counted[slot]) {
		m_card.wait_sent(m_counted[slot]);
		m_counted[slot] = 0;
	}
	for (unsigned vpi = 0; vpi < ODIN_VIEWPORTS; vpi++) {
		uint32_t count;

		if (!(views & (1u << vpi)))
			continue;
		std::memcpy(&count, m_readback.bytes + count_index(slot, vpi) * sizeof(uint32_t), sizeof(count));
		used = std::max(used, count);
	}
	return used;
}

} /* namespace odin */
