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

/* The graphics card of the host with Vulkan (host_graphics.hpp; the card
 * itself: vk_common.hpp). Everything is drawn into the image of the window,
 * the top line first as Vulkan and Dear ImGui have it; show_window copies it
 * into the swap chain and shows it. The bezel, the sides blurred and the
 * blurred background of the menu as host_graphics_gl.cpp draws them: what
 * OpenGL counts from the bottom of the window is counted from the top. */

#include "odin/host/gfx/host_graphics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_vulkan.h"
#include "odin/host/renderer/hikaru/drawing.hpp"
#include "odin/host/gfx/spirv.hpp"
#include "odin/host/gfx/vk_common.hpp"

namespace odin {

namespace {

constexpr VkFormat UI_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

/* What the sides lit by the picture are pushed, as sides.frag has it */
struct SidesPush {
	float rect[4];
	float band[2];
	float light[2];
	float window_h;
};
static_assert(sizeof(SidesPush) == 36);

void
imgui_check(VkResult result)
{
	if (result < 0) {
		std::fprintf(stderr, "ERROR: vulkan: the menu: %d\n", static_cast<int>(result));
		std::abort();
	}
}

/* A level of an image from one layout to another, after what wrote it */
void
level_barrier(const vk::Functions &fn, VkCommandBuffer cb, VkImage image, unsigned level, VkImageLayout from,
              VkImageLayout to, VkPipelineStageFlags2 next_stage, VkAccessFlags2 next_access)
{
	const VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr,
	                                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
	                                     next_stage, next_access, from, to, VK_QUEUE_FAMILY_IGNORED,
	                                     VK_QUEUE_FAMILY_IGNORED, image,
	                                     { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1 } };
	const VkDependencyInfo info{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &barrier };

	fn.vkCmdPipelineBarrier2(cb, &info);
}

class VkGraphics : public HostGraphics {
public:
	VkGraphics(SDL_Window *window, std::unique_ptr<vk::Context> card);
	~VkGraphics() override;

	const std::string &name() const override { return m_card->name(); }
	bool vulkan() const override { return true; }
	const char *api() const override { return "Vulkan"; }
	/* Vulkan has no context to make current: one thread at a time uses
	 * the card, as display.hpp has it */
	void take_thread() override {}
	void leave_thread() override {}

	void clear_window(unsigned window_w, unsigned window_h) override;
	void show_window() override;
	bool set_waits(bool wait) override;
	void read(HostPicture picture, unsigned w, unsigned h, uint8_t *pixels) override;

	HostImage make_image(const SDL_Surface &rgba, bool smooth) override;
	void forget_image(HostImage image) override;

	void draw_bezel(HostImage bezel, unsigned window_w, unsigned window_h) override;
	void draw_blurred_sides(HostPicture picture, PictureRect rect, unsigned window_w, unsigned window_h) override;
	void draw_white_border(PictureRect rect, unsigned size, unsigned window_w, unsigned window_h) override;

	void blur_background(HostPicture picture) override;
	void blur_window(unsigned window_w, unsigned window_h) override;
	void draw_background(unsigned window_w, unsigned window_h, unsigned w, unsigned h, float light) override;
	void start_ui() override;
	void draw_ui(ImDrawData *data, unsigned window_w, unsigned window_h, const char *what) override;
	void stop_ui() override;
	uint64_t ui_image(HostImage image) const override;
	void ui_blocks(ImDrawList *list, bool on) const override;
	/* Dear ImGui's smooth sampler reads the levels already */
	void ui_levels(ImDrawList *, bool) const override {}

	std::unique_ptr<Drawing> make_drawing() override { return Drawing::make_vk(*m_card); }

private:
	void make_swapchain();
	void destroy_swapchain();
	bool can_present(VkPresentModeKHR mode) const;
	void make_passes();
	void blur(vk::Image &from, unsigned w, unsigned h, bool turned);

	SDL_Window *m_window;
	std::unique_ptr<vk::Context> m_card;
	const vk::Functions &m_fn;

	/* The swap chain, made again at the next frame when the window or the
	 * wait for the display changes; a semaphore for each of its images,
	 * which the card signals once that image is drawn, and one for each
	 * frame, signalled once its image of the chain is free */
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	VkExtent2D m_extent{};
	std::vector<VkImage> m_chain;
	std::vector<VkSemaphore> m_drawn;
	VkSemaphore m_free[vk::Context::FRAMES] = {};
	std::vector<VkPresentModeKHR> m_modes;
	bool m_waits = false;
	bool m_remake = true;

	/* The images of the menu and the bezel, by number, and whether each
	 * is shown smoothly when larger; the descriptor of each that Dear
	 * ImGui draws with, made when first asked for */
	std::unordered_map<uint32_t, vk::Image> m_images;
	std::unordered_map<uint32_t, bool> m_smooth;
	uint32_t m_next_image = 1;
	mutable std::unordered_map<uint32_t, VkDescriptorSet> m_ui_sets;
	bool m_ui = false;
	/* The sampler of the images in blocks for Dear ImGui (ui_blocks), in
	 * a set of its own as the backend binds its own samplers */
	VkSampler m_ui_sampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_ui_sampler_layout = VK_NULL_HANDLE;
	VkDescriptorPool m_ui_sampler_pool = VK_NULL_HANDLE;
	VkDescriptorSet m_ui_sampler_set = VK_NULL_HANDLE;
	static void bind_ui_blocks(const ImDrawList *list, const ImDrawCmd *cmd);

	/* The bezel, the sides lit by the picture, the background of the
	 * menu: their passes (made when first needed), the samplers (linear
	 * and clamped; the levels of an image, larger smooth or not), the mean
	 * of the picture at two sizes and the sides in a small image of the
	 * window, the background at half the size blurred back and forth (the
	 * result in the first) and whether it has its bottom line first */
	vk::Pass m_image_over, m_blur, m_show, m_average, m_sides_lit;
	VkSampler m_linear = VK_NULL_HANDLE, m_levels[2] = {};
	vk::Image m_sides[3], m_blurred[2];
	bool m_blurred_turned = false;
};

VkGraphics::VkGraphics(SDL_Window *window, std::unique_ptr<vk::Context> card)
	: m_window(window), m_card(std::move(card)), m_fn(m_card->fn())
{
	uint32_t count = 0;

	m_fn.vkGetPhysicalDeviceSurfacePresentModesKHR(m_card->gpu(), m_card->surface(), &count, nullptr);
	m_modes.resize(count);
	m_fn.vkGetPhysicalDeviceSurfacePresentModesKHR(m_card->gpu(), m_card->surface(), &count, m_modes.data());

	const VkSemaphoreCreateInfo semaphore{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
	for (VkSemaphore &free : m_free)
		m_card->check(m_fn.vkCreateSemaphore(m_card->device(), &semaphore, nullptr, &free), "a semaphore");
}

VkGraphics::~VkGraphics()
{
	m_fn.vkDeviceWaitIdle(m_card->device());
	stop_ui();
	for (auto &[id, image] : m_images)
		m_card->destroy_image(image);
	for (vk::Image *image : { &m_sides[0], &m_sides[1], &m_sides[2], &m_blurred[0], &m_blurred[1] })
		if (image->image)
			m_card->destroy_image(*image);
	if (m_image_over.pipeline) {
		for (vk::Pass *pass : { &m_image_over, &m_blur, &m_show, &m_average, &m_sides_lit })
			m_card->destroy_pass(*pass);
		for (const VkSampler sampler : { m_linear, m_levels[0], m_levels[1] })
			m_fn.vkDestroySampler(m_card->device(), sampler, nullptr);
	}
	destroy_swapchain();
	for (const VkSemaphore free : m_free)
		m_fn.vkDestroySemaphore(m_card->device(), free, nullptr);
}

bool
VkGraphics::can_present(VkPresentModeKHR mode) const
{
	return std::find(m_modes.begin(), m_modes.end(), mode) != m_modes.end();
}

void
VkGraphics::destroy_swapchain()
{
	for (const VkSemaphore drawn : m_drawn)
		m_fn.vkDestroySemaphore(m_card->device(), drawn, nullptr);
	m_drawn.clear();
	m_chain.clear();
	if (m_swapchain)
		m_fn.vkDestroySwapchainKHR(m_card->device(), m_swapchain, nullptr);
	m_swapchain = VK_NULL_HANDLE;
}

/* At the size of the window in pixels; none while it has no size (made
 * small). Its images take a copy of the window image, as they are: no
 * colour space conversion, as OpenGL's window. */
void
VkGraphics::make_swapchain()
{
	VkSurfaceCapabilitiesKHR caps;
	uint32_t count = 0;
	int w = 0, h = 0;

	m_card->send_and_wait();
	m_fn.vkQueueWaitIdle(m_card->queue());
	destroy_swapchain();
	m_remake = false;

	m_fn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_card->gpu(), m_card->surface(), &caps);
	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	m_extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent :
	           VkExtent2D{ std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width),
	                       std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height) };
	if (m_extent.width == 0 || m_extent.height == 0)
		return;
	if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		m_card->fatal("the swap chain takes no copy", VK_ERROR_FEATURE_NOT_PRESENT);

	m_fn.vkGetPhysicalDeviceSurfaceFormatsKHR(m_card->gpu(), m_card->surface(), &count, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(count);
	m_fn.vkGetPhysicalDeviceSurfaceFormatsKHR(m_card->gpu(), m_card->surface(), &count, formats.data());
	VkSurfaceFormatKHR format = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
	for (const VkSurfaceFormatKHR &f : formats)
		if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
		    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			format = f;

	/* Waiting for the display, or not: the frame shown at once, else the
	 * last one drawn */
	VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
	if (!m_waits)
		mode = can_present(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR :
		       can_present(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;

	uint32_t images = caps.minImageCount + 1;
	if (caps.maxImageCount)
		images = std::min(images, caps.maxImageCount);
	const VkCompositeAlphaFlagBitsKHR alpha =
		(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR :
		static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
	const VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr, 0, m_card->surface(),
	                                     images, format.format, format.colorSpace, m_extent, 1,
	                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr,
	                                     caps.currentTransform, alpha, mode, VK_TRUE, VK_NULL_HANDLE };
	m_card->check(m_fn.vkCreateSwapchainKHR(m_card->device(), &info, nullptr, &m_swapchain), "the swap chain");

	m_fn.vkGetSwapchainImagesKHR(m_card->device(), m_swapchain, &count, nullptr);
	m_chain.resize(count);
	m_fn.vkGetSwapchainImagesKHR(m_card->device(), m_swapchain, &count, m_chain.data());
	const VkSemaphoreCreateInfo semaphore{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
	m_drawn.resize(count);
	for (VkSemaphore &drawn : m_drawn)
		m_card->check(m_fn.vkCreateSemaphore(m_card->device(), &semaphore, nullptr, &drawn), "a semaphore");
}

void
VkGraphics::clear_window(unsigned window_w, unsigned window_h)
{
	m_card->clear(m_card->commands(), m_card->window_image(window_w, window_h), { { 0.0f, 0.0f, 0.0f, 1.0f } });
}

/* The window image into the image of the chain that is free, each pixel
 * the nearest where the window has just changed its size */
void
VkGraphics::show_window()
{
	int w = 0, h = 0;

	SDL_GetWindowSizeInPixels(m_window, &w, &h);
	if (m_swapchain && (m_extent.width != static_cast<uint32_t>(w) || m_extent.height != static_cast<uint32_t>(h)))
		m_remake = true;
	if (m_remake)
		make_swapchain();

	const VkCommandBuffer cb = m_card->commands();
	const VkSemaphore free = m_free[m_card->frame()];
	uint32_t index = 0;

	if (!m_swapchain ||
	    m_fn.vkAcquireNextImageKHR(m_card->device(), m_swapchain, UINT64_MAX, free, VK_NULL_HANDLE, &index) < 0) {
		m_remake = true;
		m_card->end_frame({});
		return;
	}

	vk::Image &window = m_card->window_image();
	if (!window.image)
		clear_window(m_extent.width, m_extent.height);
	m_card->barrier(cb, window, vk::COPIED_FROM);
	level_barrier(m_fn, cb, m_chain[index], 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	              VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
	const VkImageBlit blit{ { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
	                        { { 0, 0, 0 }, { static_cast<int32_t>(window.w), static_cast<int32_t>(window.h), 1 } },
	                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
	                        { { 0, 0, 0 },
	                          { static_cast<int32_t>(m_extent.width), static_cast<int32_t>(m_extent.height), 1 } } };
	m_fn.vkCmdBlitImage(cb, window.image, window.layout, m_chain[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
	                    VK_FILTER_NEAREST);
	level_barrier(m_fn, cb, m_chain[index], 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	              VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
	m_card->end_frame({ free, m_drawn[index] });

	const VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 1, &m_drawn[index], 1, &m_swapchain,
	                                &index, nullptr };
	const VkResult result = m_fn.vkQueuePresentKHR(m_card->queue(), &present);
	if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
		m_remake = true;
	else if (result != VK_SUCCESS)
		m_card->fatal("showing the window", result);
}

/* False when the display cannot be left unwaited: no present mode for it */
bool
VkGraphics::set_waits(bool wait)
{
	if (!wait && !can_present(VK_PRESENT_MODE_IMMEDIATE_KHR) && !can_present(VK_PRESENT_MODE_MAILBOX_KHR))
		return false;
	if (wait != m_waits) {
		m_waits = wait;
		m_remake = true;
	}
	return true;
}

/* A picture of the board drawing keeps the lines in the order of OpenGL,
 * the bottom one first; the window has the top one first. */
void
VkGraphics::read(HostPicture picture, unsigned w, unsigned h, uint8_t *pixels)
{
	vk::Image *const image = picture.id ? m_card->picture(picture.id) : &m_card->window_image();

	if (!image || !image->image || w > image->w || h > image->h) {
		std::memset(pixels, 0, size_t{w} * h * 3);
		return;
	}
	const VkCommandBuffer cb = m_card->commands();
	vk::Buffer &out = m_card->staging(VkDeviceSize{w} * h * 4);
	const VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { w, h, 1 } };

	m_card->barrier(cb, *image, vk::COPIED_FROM);
	m_fn.vkCmdCopyImageToBuffer(cb, image->image, image->layout, out.buffer, 1, &region);
	m_card->send_and_wait();

	for (unsigned y = 0; y < h; y++) {
		const uint8_t *from = out.bytes + size_t{picture.id ? y : h - 1 - y} * w * 4;

		for (unsigned x = 0; x < w; x++, from += 4, pixels += 3)
			std::memcpy(pixels, from, 3);
	}
}

/* Its levels made each from the one before, smoothly, as glGenerateMipmap
 * does. The menu reads it as Dear ImGui does; the bezel as OpenGL would,
 * `smooth` when larger. */
HostImage
VkGraphics::make_image(const SDL_Surface &rgba, bool smooth)
{
	const unsigned w = static_cast<unsigned>(rgba.w), h = static_cast<unsigned>(rgba.h);
	unsigned levels = 1;

	while ((std::max(w, h) >> levels) > 0)
		levels++;
	vk::Image image = m_card->make_image(VK_FORMAT_R8G8B8A8_UNORM, w, h, levels,
	                                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
	                                             VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	const VkCommandBuffer cb = m_card->commands();
	vk::Buffer &in = m_card->staging(VkDeviceSize{w} * h * 4);

	for (unsigned y = 0; y < h; y++)
		std::memcpy(in.bytes + size_t{y} * w * 4, static_cast<const uint8_t *>(rgba.pixels) + size_t(y) * size_t(rgba.pitch),
		            size_t{w} * 4);
	m_card->barrier(cb, image, vk::COPIED_TO);
	const VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { w, h, 1 } };
	m_fn.vkCmdCopyBufferToImage(cb, in.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	for (unsigned level = 1; level < levels; level++) {
		const auto size = [&](unsigned l) {
			return VkOffset3D{ static_cast<int32_t>(std::max(w >> l, 1u)), static_cast<int32_t>(std::max(h >> l, 1u)), 1 };
		};
		const VkImageBlit blit{ { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1 }, { { 0, 0, 0 }, size(level - 1) },
		                        { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 }, { { 0, 0, 0 }, size(level) } };

		level_barrier(m_fn, cb, image.image, level - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
		              VK_ACCESS_2_TRANSFER_READ_BIT);
		m_fn.vkCmdBlitImage(cb, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.image,
		                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
	}
	for (unsigned level = 0; level < levels; level++)
		level_barrier(m_fn, cb, image.image, level,
		              level + 1 < levels ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	const uint32_t id = m_next_image++;
	m_images[id] = image;
	m_smooth[id] = smooth;
	return { id, static_cast<float>(w), static_cast<float>(h) };
}

void
VkGraphics::forget_image(HostImage image)
{
	const auto found = m_images.find(image.id);

	if (found == m_images.end())
		return;
	vk::Image gone = found->second;
	m_images.erase(found);
	m_smooth.erase(image.id);
	m_card->later([card = m_card.get(), gone]() mutable { card->destroy_image(gone); });
	if (const auto set = m_ui_sets.find(image.id); set != m_ui_sets.end()) {
		const VkDescriptorSet descriptors = set->second;

		m_ui_sets.erase(set);
		m_card->later([descriptors] { ImGui_ImplVulkan_RemoveTexture(descriptors); });
	}
}

/* The passes and samplers of the bezel and the blurs, as OpenGL reads
 * them: its textures repeat (the bezel), its framebuffers do not */
void
VkGraphics::make_passes()
{
	if (m_image_over.pipeline)
		return;
	m_image_over = m_card->make_pass(ODIN_SPIRV_OF(image_vert), ODIN_SPIRV_OF(image_frag), 1, sizeof(float),
	                                 VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, UI_FORMAT, true);
	m_blur = m_card->make_pass(ODIN_SPIRV_OF(whole_window_vert), ODIN_SPIRV_OF(blur_frag), 1, 2 * sizeof(float),
	                           VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, UI_FORMAT);
	m_show = m_card->make_pass(ODIN_SPIRV_OF(whole_window_vert), ODIN_SPIRV_OF(show_frag), 1, 2 * sizeof(float),
	                           VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, UI_FORMAT);
	m_average = m_card->make_pass(ODIN_SPIRV_OF(whole_window_vert), ODIN_SPIRV_OF(average_frag), 1,
	                              2 * sizeof(int32_t), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, UI_FORMAT);
	m_sides_lit = m_card->make_pass(ODIN_SPIRV_OF(whole_window_vert), ODIN_SPIRV_OF(sides_frag), 2, sizeof(SidesPush),
	                                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, UI_FORMAT);

	VkSamplerCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.magFilter = info.minFilter = VK_FILTER_LINEAR;
	info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	info.maxLod = 0.25f;
	m_card->check(m_fn.vkCreateSampler(m_card->device(), &info, nullptr, &m_linear), "a sampler");
	info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	info.maxLod = VK_LOD_CLAMP_NONE;
	for (unsigned smooth = 0; smooth < 2; smooth++) {
		info.magFilter = smooth ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		m_card->check(m_fn.vkCreateSampler(m_card->device(), &info, nullptr, &m_levels[smooth]), "a sampler");
	}
}

/* Over the window, the largest of its size that fits, in the middle */
void
VkGraphics::draw_bezel(HostImage bezel, unsigned window_w, unsigned window_h)
{
	const auto found = m_images.find(bezel.id);

	if (found == m_images.end())
		return;
	make_passes();

	const BezelArea area = bezel_area(bezel, window_w, window_h);
	const VkCommandBuffer cb = m_card->commands();
	vk::Image &window = m_card->window_image(window_w, window_h);
	const VkViewport viewport{ static_cast<float>(area.x),
	                           static_cast<float>(static_cast<int>(window_h) - area.h - area.below),
	                           static_cast<float>(area.w), static_cast<float>(area.h), 0.0f, 1.0f };
	const float light = 1.0f;

	m_card->begin_drawing(cb, window, { { 0, 0 }, { window_w, window_h } });
	m_fn.vkCmdSetViewport(cb, 0, 1, &viewport);
	m_card->bind_images(cb, m_image_over, { &found->second }, m_levels[m_smooth[bezel.id]]);
	m_fn.vkCmdPushConstants(cb, m_image_over.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(light), &light);
	m_fn.vkCmdDraw(cb, 3, 1, 0, 0);
	m_card->end_drawing(cb);
}

/* Each band cleared white in the image of the window */
void
VkGraphics::draw_white_border(PictureRect rect, unsigned size, unsigned window_w, unsigned window_h)
{
	VkClearRect bands[4];
	uint32_t count = 0;

	for (const PictureRect &band : border_bands(rect, size))
		if (band.w && band.h)
			bands[count++] = { { { static_cast<int32_t>(band.x), static_cast<int32_t>(band.y) }, { band.w, band.h } },
				           0, 1 };
	if (!count)
		return;

	const VkCommandBuffer cb = m_card->commands();
	const VkClearAttachment white{ VK_IMAGE_ASPECT_COLOR_BIT, 0, { { { 1.0f, 1.0f, 1.0f, 1.0f } } } };

	m_card->begin_drawing(cb, m_card->window_image(window_w, window_h), { { 0, 0 }, { window_w, window_h } });
	m_fn.vkCmdClearAttachments(cb, 1, &white, count, bands);
	m_card->end_drawing(cb);
}

/* The picture made smaller twice, then over the whole window, the side of
 * the picture that fills it, drawn only around the picture (upside down,
 * as OpenGL draws it: the picture has its bottom line first and image.vert
 * turns it as an image of SDL) */
void
VkGraphics::draw_blurred_sides(HostPicture picture, PictureRect rect, unsigned window_w, unsigned window_h)
{
	vk::Image *const from = m_card->picture(picture.id);

	if (!from)
		return;
	make_passes();
	const VkCommandBuffer cb = m_card->commands();
	for (unsigned i = 0; i < 3; i++)
		if (!m_sides[i].image)
			m_sides[i] = m_card->make_image(UI_FORMAT, static_cast<unsigned>(SIDES_W[i]),
			                                static_cast<unsigned>(SIDES_H[i]), 1,
			                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

	/* The mean of the picture, then of that */
	const vk::Image *const steps[2] = { from, &m_sides[0] };
	const int32_t blocks[2][2] = { { static_cast<int32_t>(picture.w) / SIDES_W[0],
		                         static_cast<int32_t>(picture.h) / SIDES_H[0] },
		                       { SIDES_W[0] / SIDES_W[1], SIDES_H[0] / SIDES_H[1] } };
	m_card->barrier(cb, *from, vk::SAMPLED);
	for (unsigned i = 0; i < 2; i++) {
		m_card->begin_drawing(cb, m_sides[i],
		                      { { 0, 0 }, { static_cast<uint32_t>(SIDES_W[i]), static_cast<uint32_t>(SIDES_H[i]) } });
		m_card->bind_images(cb, m_average, { steps[i] }, m_linear);
		m_fn.vkCmdPushConstants(cb, m_average.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(blocks[i]), blocks[i]);
		m_fn.vkCmdDraw(cb, 3, 1, 0, 0);
		m_card->end_drawing(cb);
		m_card->barrier(cb, m_sides[i], vk::SAMPLED);
	}

	/* The sides in the small image of the window, which counts y from
	 * the bottom as OpenGL does */
	const int ww = static_cast<int>(window_w), wh = static_cast<int>(window_h);
	const int px = static_cast<int>(rect.x), top = static_cast<int>(rect.y);
	const int pr = px + static_cast<int>(rect.w), bottom = top + static_cast<int>(rect.h);
	const float sx = static_cast<float>(SIDES_W[2]) / static_cast<float>(ww);
	const float sy = static_cast<float>(SIDES_H[2]) / static_cast<float>(wh);
	const float below = static_cast<float>(wh - bottom);
	const SidesPush push{ { static_cast<float>(px) * sx, below * sy, static_cast<float>(rect.w) * sx,
		                static_cast<float>(rect.h) * sy },
		              { static_cast<float>(px) * sx, below * sy },
		              { SIDES_NEAR, SIDES_FAR },
		              static_cast<float>(SIDES_H[2]) };

	m_card->begin_drawing(cb, m_sides[2],
	                      { { 0, 0 }, { static_cast<uint32_t>(SIDES_W[2]), static_cast<uint32_t>(SIDES_H[2]) } });
	m_card->bind_images(cb, m_sides_lit, { &m_sides[0], &m_sides[1] }, m_linear);
	m_fn.vkCmdPushConstants(cb, m_sides_lit.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
	m_fn.vkCmdDraw(cb, 3, 1, 0, 0);
	m_card->end_drawing(cb);
	m_card->barrier(cb, m_sides[2], vk::SAMPLED);

	/* That image over the window (its top line first, as the window's),
	 * only around the picture */
	const int sides[4][4] = { { 0, 0, px, wh }, { pr, 0, ww - pr, wh }, { px, 0, pr - px, top },
		                  { px, bottom, pr - px, wh - bottom } };
	const struct {
		float light;
		int32_t turn;
	} stretch{ 1.0f, 0 };
	vk::Image &window = m_card->window_image(window_w, window_h);

	m_card->begin_drawing(cb, window, { { 0, 0 }, { window_w, window_h } });
	m_card->bind_images(cb, m_show, { &m_sides[2] }, m_linear);
	m_fn.vkCmdPushConstants(cb, m_show.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(stretch), &stretch);
	for (const auto &side : sides) {
		if (side[2] <= 0 || side[3] <= 0)
			continue;
		const VkRect2D scissor{ { side[0], side[1] },
			                { static_cast<uint32_t>(side[2]), static_cast<uint32_t>(side[3]) } };

		m_fn.vkCmdSetScissor(cb, 0, 1, &scissor);
		m_fn.vkCmdDraw(cb, 3, 1, 0, 0);
	}
	m_card->end_drawing(cb);
}

/* Half the size, each texel the mean of four pixels; then blurred across
 * into the second image and down back into the first, BLUR_ROUNDS times */
void
VkGraphics::blur(vk::Image &from, unsigned w, unsigned h, bool turned)
{
	const unsigned hw = w / 2, hh = h / 2;

	if (!from.image || hw == 0 || hh == 0)
		return;
	make_passes();
	if (m_blurred[0].w != hw || m_blurred[0].h != hh) {
		for (vk::Image &image : m_blurred) {
			if (image.image) {
				vk::Image old = image;

				m_card->later([card = m_card.get(), old]() mutable { card->destroy_image(old); });
			}
			image = m_card->make_image(UI_FORMAT, hw, hh, 1,
			                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		}
	}
	const VkCommandBuffer cb = m_card->commands();
	const VkImageBlit blit{ { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
	                        { { 0, 0, 0 }, { static_cast<int32_t>(w), static_cast<int32_t>(h), 1 } },
	                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
	                        { { 0, 0, 0 }, { static_cast<int32_t>(hw), static_cast<int32_t>(hh), 1 } } };

	m_card->barrier(cb, from, vk::COPIED_FROM);
	m_card->barrier(cb, m_blurred[0], vk::COPIED_TO);
	m_fn.vkCmdBlitImage(cb, from.image, from.layout, m_blurred[0].image, m_blurred[0].layout, 1, &blit,
	                    VK_FILTER_LINEAR);

	const auto pass = [&](unsigned i, float x, float y) {
		const float step[2] = { x, y };

		m_card->barrier(cb, m_blurred[i], vk::SAMPLED);
		m_card->begin_drawing(cb, m_blurred[1 - i], { { 0, 0 }, { hw, hh } });
		m_card->bind_images(cb, m_blur, { &m_blurred[i] }, m_linear);
		m_fn.vkCmdPushConstants(cb, m_blur.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(step), step);
		m_fn.vkCmdDraw(cb, 3, 1, 0, 0);
		m_card->end_drawing(cb);
	};
	for (unsigned round = 0; round < BLUR_ROUNDS; round++) {
		pass(0, BLUR_STEP / static_cast<float>(hw), 0.0f);
		pass(1, 0.0f, BLUR_STEP / static_cast<float>(hh));
	}
	m_blurred_turned = turned;
}

/* The picture of the game has its bottom line first, the window its top one */
void
VkGraphics::blur_background(HostPicture picture)
{
	if (vk::Image *const from = m_card->picture(picture.id))
		blur(*from, picture.w, picture.h, true);
}

void
VkGraphics::blur_window(unsigned window_w, unsigned window_h)
{
	blur(m_card->window_image(window_w, window_h), window_w, window_h, false);
}

/* The background, w by h in the middle of the window, at that light */
void
VkGraphics::draw_background(unsigned window_w, unsigned window_h, unsigned w, unsigned h, float light)
{
	if (!m_blurred[0].image)
		return;
	const int gw = static_cast<int>(w), gh = static_cast<int>(h);
	const int below = (static_cast<int>(window_h) - gh) / 2;
	const VkCommandBuffer cb = m_card->commands();
	vk::Image &window = m_card->window_image(window_w, window_h);
	const VkViewport viewport{ static_cast<float>((static_cast<int>(window_w) - gw) / 2),
	                           static_cast<float>(static_cast<int>(window_h) - gh - below), static_cast<float>(gw),
	                           static_cast<float>(gh), 0.0f, 1.0f };
	const struct {
		float light;
		int32_t turn;
	} push{ light, m_blurred_turned ? 1 : 0 };

	m_card->barrier(cb, m_blurred[0], vk::SAMPLED);
	m_card->begin_drawing(cb, window, { { 0, 0 }, { window_w, window_h } });
	m_fn.vkCmdSetViewport(cb, 0, 1, &viewport);
	m_card->bind_images(cb, m_show, { &m_blurred[0] }, m_linear);
	m_fn.vkCmdPushConstants(cb, m_show.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
	m_fn.vkCmdDraw(cb, 3, 1, 0, 0);
	m_card->end_drawing(cb);
}

/* Dear ImGui finds the functions of Vulkan through the instance. */
void
VkGraphics::start_ui()
{
	const auto load = [](const char *name, void *self) {
		const vk::Context &card = *static_cast<VkGraphics *>(self)->m_card;

		return card.fn().vkGetInstanceProcAddr(card.instance(), name);
	};
	static const VkFormat format = UI_FORMAT;
	ImGui_ImplVulkan_InitInfo info{};

	ImGui_ImplVulkan_LoadFunctions(m_card->api_version(), load, this);
	info.ApiVersion = m_card->api_version();
	info.Instance = m_card->instance();
	info.PhysicalDevice = m_card->gpu();
	info.Device = m_card->device();
	info.QueueFamily = m_card->queue_family();
	info.Queue = m_card->queue();
	info.DescriptorPoolSize = 256;
	info.MinImageCount = 2;
	info.ImageCount = std::max<uint32_t>(2, static_cast<uint32_t>(m_chain.size()));
	info.UseDynamicRendering = true;
	info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
	info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
	info.CheckVkResultFn = imgui_check;
	ImGui_ImplVulkan_Init(&info);
	m_ui = true;

	/* As GL_NEAREST and GL_LINEAR_MIPMAP_LINEAR, repeated as OpenGL does
	 * by default */
	VkSamplerCreateInfo sampler{};
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_NEAREST;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.maxLod = VK_LOD_CLAMP_NONE;
	m_card->check(m_fn.vkCreateSampler(m_card->device(), &sampler, nullptr, &m_ui_sampler), "a sampler");

	const VkDescriptorSetLayoutBinding binding{ 0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	const VkDescriptorSetLayoutCreateInfo layout{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1,
	                                              &binding };
	m_card->check(m_fn.vkCreateDescriptorSetLayout(m_card->device(), &layout, nullptr, &m_ui_sampler_layout),
	              "a descriptor set layout");

	const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_SAMPLER, 1 };
	const VkDescriptorPoolCreateInfo pool{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &size };
	m_card->check(m_fn.vkCreateDescriptorPool(m_card->device(), &pool, nullptr, &m_ui_sampler_pool), "a descriptor pool");

	const VkDescriptorSetAllocateInfo set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_ui_sampler_pool, 1,
	                                       &m_ui_sampler_layout };
	m_card->check(m_fn.vkAllocateDescriptorSets(m_card->device(), &set, &m_ui_sampler_set), "a descriptor set");

	const VkDescriptorImageInfo image{ m_ui_sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
	const VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_ui_sampler_set, 0, 0, 1,
	                                  VK_DESCRIPTOR_TYPE_SAMPLER, &image, nullptr, nullptr };
	m_fn.vkUpdateDescriptorSets(m_card->device(), 1, &write, 0, nullptr);
}

void
VkGraphics::draw_ui(ImDrawData *data, unsigned window_w, unsigned window_h, const char *)
{
	const VkCommandBuffer cb = m_card->commands();
	vk::Image &window = m_card->window_image(window_w, window_h);
	const VkRenderingAttachmentInfo colour{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, window.view,
	                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE,
	                                        VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_LOAD,
	                                        VK_ATTACHMENT_STORE_OP_STORE, {} };
	const VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, { { 0, 0 }, { window_w, window_h } },
	                                 1, 0, 1, &colour, nullptr, nullptr };

	m_card->barrier(cb, window, vk::DRAWN_IN);
	m_fn.vkCmdBeginRendering(cb, &rendering);
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplVulkan_RenderDrawData(data, cb);
	m_fn.vkCmdEndRendering(cb);
}

void
VkGraphics::stop_ui()
{
	if (!m_ui)
		return;
	m_card->send_and_wait();
	for (const auto &[id, descriptors] : m_ui_sets)
		ImGui_ImplVulkan_RemoveTexture(descriptors);
	m_ui_sets.clear();
	ImGui_ImplVulkan_Shutdown();
	m_ui = false;
	m_fn.vkDestroyDescriptorPool(m_card->device(), m_ui_sampler_pool, nullptr);
	m_fn.vkDestroyDescriptorSetLayout(m_card->device(), m_ui_sampler_layout, nullptr);
	m_fn.vkDestroySampler(m_card->device(), m_ui_sampler, nullptr);
	m_ui_sampler_pool = VK_NULL_HANDLE;
	m_ui_sampler_layout = VK_NULL_HANDLE;
	m_ui_sampler = VK_NULL_HANDLE;
	m_ui_sampler_set = VK_NULL_HANDLE;
}

uint64_t
VkGraphics::ui_image(HostImage image) const
{
	if (const auto set = m_ui_sets.find(image.id); set != m_ui_sets.end())
		return reinterpret_cast<uint64_t>(set->second);
	const auto found = m_images.find(image.id);
	if (!m_ui || found == m_images.end())
		return 0;
	const VkDescriptorSet descriptors =
		ImGui_ImplVulkan_AddTexture(found->second.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	m_ui_sets[image.id] = descriptors;
	return reinterpret_cast<uint64_t>(descriptors);
}

void
VkGraphics::ui_blocks(ImDrawList *list, bool on) const
{
	if (!on)
		list->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
	else if (m_ui_sampler_set)
		list->AddCallback(bind_ui_blocks, const_cast<VkGraphics *>(this));
}

/* Called by Dear ImGui as it draws (ui_blocks): its samplers are the set 1 */
void
VkGraphics::bind_ui_blocks(const ImDrawList *, const ImDrawCmd *cmd)
{
	const VkGraphics &self = *static_cast<const VkGraphics *>(cmd->UserCallbackData);
	const ImGui_ImplVulkan_RenderState &state = *ImGui_ImplVulkan_GetRenderState();

	self.m_fn.vkCmdBindDescriptorSets(state.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state.PipelineLayout, 1, 1,
	                                  &self.m_ui_sampler_set, 0, nullptr);
}

} /* namespace */

uint64_t
HostGraphics::window_flags_vk()
{
	return SDL_WINDOW_VULKAN;
}

const std::vector<std::string> &
HostGraphics::gpus()
{
	static const std::vector<std::string> all = vk::Context::gpus();

	return all;
}

/* Vulkan for that window, or nothing with the reason on stderr */
std::unique_ptr<HostGraphics>
HostGraphics::make_vk(SDL_Window *window, unsigned, unsigned, const std::string &gpu)
{
	std::string why;
	std::unique_ptr<vk::Context> card = vk::Context::make(window, gpu, why);

	if (!card) {
		std::fprintf(stderr, "ERROR: vulkan: %s\n", why.c_str());
		return nullptr;
	}
	std::printf("renderer: Vulkan %u.%u on %s\n", VK_API_VERSION_MAJOR(card->api_version()),
	            VK_API_VERSION_MINOR(card->api_version()), card->name().c_str());
	return std::make_unique<VkGraphics>(window, std::move(card));
}

} /* namespace odin */
