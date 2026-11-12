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

/* The pictures of the board drawing with Vulkan (drawing.hpp) and
 * what goes from one to the next, as pictures_gl.cpp does it with
 * OpenGL: the buffers the 3D is drawn in, the pictures kept from one frame
 * to the next, the picture of the frame and the window. */

#include "odin/host/renderer/hikaru/vk/drawing_vk.hpp"

#include <cstring>
#include <vector>

#include "odin/host/gfx/spirv.hpp"

namespace odin {

namespace {

/* What the composition is pushed, as compose.frag has it */
struct ComposePush {
	float factor, mult_f, mult_b, mult_1, mult_2;
	float keyed_1, keyed_2, keyed_3d, above_1, above_2;
	int32_t invert_f, invert_b;
	float clear[4];
};
static_assert(sizeof(ComposePush) == 64);

/* What the presentation is pushed, as present.frag has it */
struct PresentPush {
	float rect[4];
	float window_h;
};

} /* namespace */

VkDrawing::VkDrawing(vk::Context &card) : m_card(card), m_fn(card.fn())
{
	VkSamplerCreateInfo nearest{};

	nearest.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	nearest.magFilter = nearest.minFilter = VK_FILTER_NEAREST;
	nearest.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	nearest.addressModeU = nearest.addressModeV = nearest.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	m_card.check(m_fn.vkCreateSampler(m_card.device(), &nearest, nullptr, &m_nearest), "a sampler");

	m_blender = m_card.make_pass(ODIN_SPIRV_OF(whole_picture_vert), ODIN_SPIRV_OF(blend_frag), 2, sizeof(int32_t),
	                             VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, PICTURE_FORMAT);
	m_compose = m_card.make_pass(ODIN_SPIRV_OF(compose_vert), ODIN_SPIRV_OF(compose_frag), 4, sizeof(ComposePush),
	                             VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, PICTURE_FORMAT);
	m_presenter = m_card.make_pass(ODIN_SPIRV_OF(whole_picture_vert), ODIN_SPIRV_OF(present_frag), 1,
	                               sizeof(PresentPush), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, PICTURE_FORMAT);

	const VkCommandBuffer cb = m_card.commands();
	m_none = m_card.make_image(PICTURE_FORMAT, 1, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	clear(cb, m_none, 1.0f);
	m_card.barrier(cb, m_none, vk::SAMPLED);
	m_state = m_card.make_image(PICTURE_FORMAT, SCREEN_WIDTH, SCREEN_HEIGHT, 1,
	                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	build_pictures();
	build_meshes();
	build_lists();
	m_card.name_picture(FRAME_PICTURE, &m_frame);
}

VkDrawing::~VkDrawing()
{
	m_card.send_and_wait();
	m_card.forget_picture(FRAME_PICTURE);
	destroy_pictures(true);
	m_card.send_and_wait();
	m_filter.reset();
	destroy_meshes();
	destroy_lists();
	m_fn.vkDestroyQueryPool(m_card.device(), m_stamps, nullptr);
	for (vk::Image *image : { &m_state, &m_none, &m_layers[0], &m_layers[1] })
		m_card.destroy_image(*image);
	for (vk::Pass *pass : { &m_blender, &m_compose, &m_presenter })
		m_card.destroy_pass(*pass);
	m_fn.vkDestroySampler(m_card.device(), m_nearest, nullptr);
}

vk::Image
VkDrawing::make_picture(unsigned w, unsigned h)
{
	return m_card.make_image(PICTURE_FORMAT, w, h, 1,
	                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
	                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
}

void
VkDrawing::clear(VkCommandBuffer cb, vk::Image &image, float alpha)
{
	m_card.clear(cb, image, { { 0.0f, 0.0f, 0.0f, alpha } });
}

/* The two 3D buffers and the pictures of the last three lists, empty: an
 * empty 3D buffer lets everything through, its alpha is 1. */
void
VkDrawing::build_pictures()
{
	const VkCommandBuffer cb = m_card.commands();

	m_scene = make_picture(width(), height());
	m_depth = m_card.make_image(m_card.depth_format(), width(), height(), 1,
	                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	for (vk::Image &picture : m_kept) {
		picture = make_picture(width(), height());
		clear(cb, picture, 1.0f);
	}
	m_frame = make_picture(width(), height());
	build_list_pictures();
}

void
VkDrawing::destroy_pictures(bool kept_too)
{
	std::vector<vk::Image> gone = { m_scene, m_depth, m_frame, m_solid_depth, m_heads, m_below,
	                                m_eye[0], m_eye[1], m_flag_depth };

	if (kept_too)
		gone.insert(gone.end(), std::begin(m_kept), std::end(m_kept));
	m_card.later([card = &m_card, gone]() mutable {
		for (vk::Image &image : gone)
			card->destroy_image(image);
	});
}

void
VkDrawing::copy_picture(VkCommandBuffer cb, vk::Image &from, vk::Image &to)
{
	const VkImageBlit blit{ { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
	                        { { 0, 0, 0 }, { static_cast<int32_t>(from.w), static_cast<int32_t>(from.h), 1 } },
	                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
	                        { { 0, 0, 0 }, { static_cast<int32_t>(to.w), static_cast<int32_t>(to.h), 1 } } };

	m_card.barrier(cb, from, vk::COPIED_FROM);
	m_card.barrier(cb, to, vk::COPIED_TO);
	m_fn.vkCmdBlitImage(cb, from.image, from.layout, to.image, to.layout, 1, &blit, VK_FILTER_NEAREST);
}

void
VkDrawing::copy_whole(VkCommandBuffer cb, vk::Image &from, vk::Image &to, VkImageAspectFlags aspect)
{
	const VkImageCopy copy{ { aspect, 0, 0, 1 }, { 0, 0, 0 }, { aspect, 0, 0, 1 }, { 0, 0, 0 }, { width(), height(), 1 } };

	m_card.barrier(cb, from, vk::COPIED_FROM);
	m_card.barrier(cb, to, vk::COPIED_TO);
	m_fn.vkCmdCopyImage(cb, from.image, from.layout, to.image, to.layout, 1, &copy);
}

void
VkDrawing::set_scale(unsigned scale)
{
	vk::Image old[KEPT];

	if (scale == m_scale)
		return;
	std::copy(std::begin(m_kept), std::end(m_kept), std::begin(old));
	destroy_pictures(false);
	m_scale = scale;
	build_pictures();

	const VkCommandBuffer cb = m_card.commands();
	for (unsigned i = 0; i < KEPT; i++)
		copy_picture(cb, old[i], kept(i));
	m_card.later([card = &m_card, old]() mutable {
		for (vk::Image &image : old)
			card->destroy_image(image);
	});
}

void
VkDrawing::scene_to_pipe(unsigned slot)
{
	copy_whole(m_card.commands(), m_scene, kept(BUFFERS + slot));
}

/* The blender output is what the 3D buffer of the list keeps: the other one
 * then holds every earlier frame, fading by the rate at each frame (the
 * motion blur of the cabinets, pictures_gl.cpp). */
void
VkDrawing::blend_scene_to_pipe(unsigned slot, unsigned buffer, uint32_t rate)
{
	const VkCommandBuffer cb = m_card.commands();
	const int32_t push = static_cast<int32_t>(rate);

	m_card.barrier(cb, m_scene, vk::SAMPLED);
	m_card.barrier(cb, kept(buffer), vk::SAMPLED);
	m_card.begin_drawing(cb, kept(BUFFERS + slot), { { 0, 0 }, { width(), height() } }, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
	m_card.bind_images(cb, m_blender, { &m_scene, &kept(buffer) }, m_nearest);
	m_fn.vkCmdPushConstants(cb, m_blender.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
	m_fn.vkCmdDraw(cb, 3, 1, 0, 0);
	m_card.end_drawing(cb);
}

void
VkDrawing::keep_picture(unsigned slot, unsigned buffer)
{
	copy_whole(m_card.commands(), kept(BUFFERS + slot), kept(buffer));
}

/* The image of a layer is made the first time, and made again when what it
 * holds changes. Its lines are given the top one first, as OpenGL is: the
 * composition reads it upside down (compose.frag). */
void
VkDrawing::set_layer(unsigned i, LayerTexels texels, const void *pixels)
{
	const bool wide = texels == LayerTexels::A2BGR10;
	const VkDeviceSize bytes = VkDeviceSize{SCREEN_WIDTH} * SCREEN_HEIGHT * (wide ? 4 : 2);
	const VkCommandBuffer cb = m_card.commands();

	if (!m_layers[i].image || m_layer_texels[i] != texels) {
		if (m_layers[i].image) {
			vk::Image old = m_layers[i];

			m_card.later([card = &m_card, old]() mutable { card->destroy_image(old); });
		}
		m_layers[i] = m_card.make_image(wide ? VK_FORMAT_A2B10G10R10_UNORM_PACK32 : VK_FORMAT_A1R5G5B5_UNORM_PACK16,
		                                SCREEN_WIDTH, SCREEN_HEIGHT, 1,
		                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		                                wide ? VkComponentMapping{} : ABGR1555_SWIZZLE);
		m_layer_texels[i] = texels;
	}
	vk::Buffer &in = m_card.staging(bytes);
	const VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 },
	                                { SCREEN_WIDTH, SCREEN_HEIGHT, 1 } };

	std::memcpy(in.bytes, pixels, bytes);
	m_card.barrier(cb, m_layers[i], vk::COPIED_TO);
	m_fn.vkCmdCopyBufferToImage(cb, in.buffer, m_layers[i].image, m_layers[i].layout, 1, &region);
}

/* As pictures_gl.cpp: the two blender inputs, then the 2D layers */
void
VkDrawing::compose(const Compose &c, unsigned shown, const bool layers[2])
{
	const VkCommandBuffer cb = m_card.commands();
	vk::Image *const layer[2] = { layers[0] && m_layers[0].image ? &m_layers[0] : &m_none,
	                              layers[1] && m_layers[1].image ? &m_layers[1] : &m_none };
	const auto source = [&](Compose::Source s) -> vk::Image * {
		return s == Compose::Source::PICTURE ? &kept(BUFFERS + shown) : s == Compose::Source::LAYER_1 ? layer[0] : layer[1];
	};
	vk::Image *const inputs[4] = { source(c.front), source(c.back), c.alone[0] ? layer[0] : &m_none,
	                               c.alone[1] ? layer[1] : &m_none };
	const ComposePush push{ c.factor, c.mult_front, c.mult_back, c.mult_alone[0], c.mult_alone[1],
		                c.keyed[0] ? 1.0f : 0.0f, c.keyed[1] ? 1.0f : 0.0f, c.keyed_3d ? 1.0f : 0.0f,
		                c.above[0] ? 1.0f : 0.0f, c.above[1] ? 1.0f : 0.0f, c.invert_front ? 1 : 0,
		                c.invert_back ? 1 : 0, { c.clear[0], c.clear[1], c.clear[2], c.clear[3] } };

	for (vk::Image *input : inputs)
		if (input->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			m_card.barrier(cb, *input, vk::SAMPLED);
	m_card.begin_drawing(cb, m_frame, { { 0, 0 }, { width(), height() } }, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
	m_card.bind_images(cb, m_compose, { inputs[0], inputs[1], inputs[2], inputs[3] }, m_nearest);
	m_fn.vkCmdPushConstants(cb, m_compose.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
	m_fn.vkCmdDraw(cb, 4, 1, 0, 0);
	m_card.end_drawing(cb);
}

void
VkDrawing::clear_window(unsigned window_w, unsigned window_h)
{
	const VkCommandBuffer cb = m_card.commands();

	clear(cb, m_card.window_image(window_w, window_h), 1.0f);
}

/* The picture of the frame in the window, at `rect`, black around: copied
 * as it is at its size (its lines turned over: the window has the top one
 * first), else through the presentation, which counts y from the bottom as
 * OpenGL does */
void
VkDrawing::present(PictureRect rect, unsigned window_w, unsigned window_h)
{
	const unsigned w = rect.w, h = rect.h;

	if (w == 0 || h == 0 || rect.x + w > window_w || rect.y + h > window_h)
		return;
	const VkCommandBuffer cb = m_card.commands();
	vk::Image &window = m_card.window_image(window_w, window_h);
	const int32_t x = static_cast<int32_t>(rect.x), y = static_cast<int32_t>(rect.y);
	const bool around = w != window_w || h != window_h;
	/* The frame through the presentation into an image, in that rectangle */
	const auto through = [&](vk::Image &to, const VkRect2D &area, const PresentPush &push, VkAttachmentLoadOp load) {
		m_card.barrier(cb, m_frame, vk::SAMPLED);
		m_card.begin_drawing(cb, to, area, load);
		m_card.bind_images(cb, m_presenter, { &m_frame }, m_nearest);
		m_fn.vkCmdPushConstants(cb, m_presenter.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
		m_fn.vkCmdDraw(cb, 3, 1, 0, 0);
		m_card.end_drawing(cb);
	};

	if (around && (m_filter || (w == width() && h == height())))
		clear(cb, window, 1.0f);

	/* A filter takes the picture at the size of the screen of the
	 * hardware, a finer one brought down to it as below */
	if (m_filter) {
		vk::Image &frame = m_filter->frame(SCREEN_WIDTH, SCREEN_HEIGHT);
		const PresentPush push{ { 0.0f, 0.0f, static_cast<float>(m_scale), static_cast<float>(m_scale) }, 0.0f };

		through(frame, { { 0, 0 }, { SCREEN_WIDTH, SCREEN_HEIGHT } }, push, VK_ATTACHMENT_LOAD_OP_LOAD);
		m_filter->draw(window, x, y, w, h, m_filter_frames++);
		return;
	}

	if (w == width() && h == height()) {
		const VkImageBlit blit{ { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		                        { { 0, 0, 0 }, { static_cast<int32_t>(w), static_cast<int32_t>(h), 1 } },
		                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		                        { { x, y + static_cast<int32_t>(h), 0 }, { x + static_cast<int32_t>(w), y, 1 } } };

		m_card.barrier(cb, m_frame, vk::COPIED_FROM);
		m_card.barrier(cb, window, vk::COPIED_TO);
		m_fn.vkCmdBlitImage(cb, m_frame.image, m_frame.layout, window.image, window.layout, 1, &blit, VK_FILTER_NEAREST);
		return;
	}

	const PresentPush push{ { static_cast<float>(rect.x), static_cast<float>(window_h - rect.y - h),
	                          static_cast<float>(width()) / static_cast<float>(w),
	                          static_cast<float>(height()) / static_cast<float>(h) },
	                        static_cast<float>(window_h) };

	through(window, { { x, y }, { w, h } }, push, around ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD);
}

/* The filter before goes once the card is done with the frames it drew */
void
VkDrawing::set_filter(const std::string &preset)
{
	if (preset == m_filter_preset)
		return;
	m_filter_preset = preset;
	if (m_filter) {
		std::shared_ptr<VkPictureFilter> old(std::move(m_filter));

		m_card.later([old]() mutable { old.reset(); });
	}
	m_filter = preset.empty() ? nullptr : VkPictureFilter::load(m_card, preset);
	m_filter_frames = 0;
}

/* The window black, then each rectangle at its shade (the window image has
 * the top line first, as the rectangles count them) */
void
VkDrawing::draw_loading(std::span<const LoadingRect> rects, unsigned window_w, unsigned window_h)
{
	clear_window(window_w, window_h);
	if (rects.empty())
		return;

	const VkCommandBuffer cb = m_card.commands();
	vk::Image &window = m_card.window_image();

	m_card.begin_drawing(cb, window, { { 0, 0 }, { window.w, window.h } });
	for (const LoadingRect &r : rects) {
		const unsigned x0 = std::min(r.x, window.w), y0 = std::min(r.y, window.h);
		const unsigned x1 = std::min(r.x + r.w, window.w), y1 = std::min(r.y + r.h, window.h);
		if (x1 <= x0 || y1 <= y0)
			continue;
		const VkClearAttachment shade{ VK_IMAGE_ASPECT_COLOR_BIT, 0, { { { r.shade, r.shade, r.shade, 1.0f } } } };
		const VkClearRect where{ { { static_cast<int32_t>(x0), static_cast<int32_t>(y0) }, { x1 - x0, y1 - y0 } }, 0, 1 };

		m_fn.vkCmdClearAttachments(cb, 1, &shade, 1, &where);
	}
	m_card.end_drawing(cb);
}

void
VkDrawing::read_kept(std::span<uint8_t> pictures)
{
	const size_t size = PICTURE_SIZE;
	const VkCommandBuffer cb = m_card.commands();
	vk::Buffer *out[KEPT];

	for (unsigned i = 0; i < KEPT; i++) {
		const VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 },
		                                { width(), height(), 1 } };

		out[i] = &m_card.staging(VkDeviceSize{width()} * height() * 4);
		m_card.barrier(cb, kept(i), vk::COPIED_FROM);
		m_fn.vkCmdCopyImageToBuffer(cb, kept(i).image, kept(i).layout, out[i]->buffer, 1, &region);
	}
	m_card.send_and_wait();
	for (unsigned i = 0; i < KEPT; i++)
		screen_of(out[i]->bytes, m_scale, &pictures[i * size]);
}

void
VkDrawing::write_kept(std::span<const uint8_t> pictures)
{
	const size_t size = PICTURE_SIZE;
	const VkCommandBuffer cb = m_card.commands();
	const VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 },
	                                { SCREEN_WIDTH, SCREEN_HEIGHT, 1 } };

	for (unsigned i = 0; i < KEPT; i++) {
		vk::Buffer &in = m_card.staging(size);

		std::memcpy(in.bytes, &pictures[i * size], size);
		m_card.barrier(cb, m_state, vk::COPIED_TO);
		m_fn.vkCmdCopyBufferToImage(cb, in.buffer, m_state.image, m_state.layout, 1, &region);
		copy_picture(cb, m_state, kept(i));
	}
}

} /* namespace odin */
