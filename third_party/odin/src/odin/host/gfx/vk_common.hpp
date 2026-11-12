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

#ifndef ODIN_VK_COMMON_HPP
#define ODIN_VK_COMMON_HPP

/* What the window (host_graphics_vk.cpp) and the board drawing
 * (drawing_vk.cpp) share with Vulkan: its functions, loaded through SDL (Odin
 * is not linked to the Vulkan loader); the instance, the card and its queue;
 * memory, images and buffers; the commands of each frame, sent and waited for;
 * the image of the window everything is drawn into before it is shown.
 *
 * Odin asks the card for what Vulkan 1.3 has: rendering without render
 * passes, the second form of the barriers, the extended dynamic state; and
 * fragment shaders that write to buffers (the translucent fragments). A card
 * of Vulkan 1.1 or 1.2 that has them as extensions does too (MoltenVK). */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;

namespace odin::vk {

/* The functions Odin calls, by the name Vulkan gives them; those of the
 * device that came from an extension are found by either name. */
#define ODIN_VK_GLOBAL(F) \
	F(vkCreateInstance) F(vkEnumerateInstanceExtensionProperties) F(vkEnumerateInstanceLayerProperties) \
	F(vkEnumerateInstanceVersion)
#define ODIN_VK_INSTANCE(F) \
	F(vkDestroyInstance) F(vkEnumeratePhysicalDevices) F(vkGetPhysicalDeviceProperties) \
	F(vkGetPhysicalDeviceFeatures) F(vkGetPhysicalDeviceFeatures2) F(vkGetPhysicalDeviceQueueFamilyProperties) \
	F(vkGetPhysicalDeviceMemoryProperties) F(vkGetPhysicalDeviceFormatProperties) \
	F(vkEnumerateDeviceExtensionProperties) F(vkCreateDevice) F(vkGetDeviceProcAddr) F(vkDestroySurfaceKHR) \
	F(vkGetPhysicalDeviceSurfaceSupportKHR) F(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	F(vkGetPhysicalDeviceSurfaceFormatsKHR) F(vkGetPhysicalDeviceSurfacePresentModesKHR) \
	F(vkCreateDebugUtilsMessengerEXT) F(vkDestroyDebugUtilsMessengerEXT)
#define ODIN_VK_DEVICE(F, G) \
	F(vkDestroyDevice) F(vkGetDeviceQueue) F(vkDeviceWaitIdle) F(vkQueueWaitIdle) \
	F(vkCreateCommandPool) F(vkDestroyCommandPool) F(vkResetCommandPool) F(vkAllocateCommandBuffers) \
	F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkCreateFence) F(vkDestroyFence) F(vkWaitForFences) \
	F(vkResetFences) F(vkCreateSemaphore) F(vkDestroySemaphore) F(vkAllocateMemory) F(vkFreeMemory) \
	F(vkMapMemory) F(vkCreateImage) F(vkDestroyImage) F(vkGetImageMemoryRequirements) F(vkBindImageMemory) \
	F(vkCreateImageView) F(vkDestroyImageView) F(vkCreateBuffer) F(vkDestroyBuffer) \
	F(vkGetBufferMemoryRequirements) F(vkBindBufferMemory) F(vkCreateSwapchainKHR) F(vkDestroySwapchainKHR) \
	F(vkGetSwapchainImagesKHR) F(vkAcquireNextImageKHR) F(vkQueuePresentKHR) F(vkCmdClearColorImage) \
	F(vkCmdClearAttachments) F(vkCmdCopyImage) F(vkCreateShaderModule) F(vkDestroyShaderModule) \
	F(vkCreatePipelineLayout) F(vkDestroyPipelineLayout) F(vkCreateGraphicsPipelines) F(vkDestroyPipeline) \
	F(vkCreateDescriptorSetLayout) F(vkDestroyDescriptorSetLayout) F(vkCreateDescriptorPool) \
	F(vkDestroyDescriptorPool) F(vkResetDescriptorPool) F(vkAllocateDescriptorSets) F(vkUpdateDescriptorSets) \
	F(vkCreateSampler) F(vkDestroySampler) F(vkCmdBindPipeline) F(vkCmdBindDescriptorSets) F(vkCmdPushConstants) \
	F(vkCmdSetViewport) F(vkCmdSetScissor) F(vkCmdDraw) \
	F(vkCmdCopyImageToBuffer) F(vkCmdCopyBufferToImage) F(vkCmdBlitImage) \
	F(vkCmdBindVertexBuffers) F(vkCmdBindIndexBuffer) F(vkCmdDrawIndexed) F(vkFreeDescriptorSets) \
	F(vkCmdFillBuffer) F(vkCmdCopyBuffer) F(vkCreateQueryPool) F(vkDestroyQueryPool) F(vkGetQueryPoolResults) \
	F(vkCreatePipelineCache) F(vkDestroyPipelineCache) F(vkGetPipelineCacheData) F(vkMergePipelineCaches) \
	G(vkQueueSubmit2, vkQueueSubmit2KHR) G(vkCmdPipelineBarrier2, vkCmdPipelineBarrier2KHR) \
	G(vkCmdBeginRendering, vkCmdBeginRenderingKHR) G(vkCmdEndRendering, vkCmdEndRenderingKHR) \
	G(vkCmdSetCullMode, vkCmdSetCullModeEXT) G(vkCmdSetFrontFace, vkCmdSetFrontFaceEXT) \
	G(vkCmdSetDepthTestEnable, vkCmdSetDepthTestEnableEXT) \
	G(vkCmdSetDepthWriteEnable, vkCmdSetDepthWriteEnableEXT) \
	G(vkCmdSetDepthCompareOp, vkCmdSetDepthCompareOpEXT) \
	G(vkResetQueryPool, vkResetQueryPoolEXT) G(vkCmdWriteTimestamp2, vkCmdWriteTimestamp2KHR)

#define ODIN_VK_MEMBER(name) PFN_##name name = nullptr;
#define ODIN_VK_MEMBER2(name, other) PFN_##name name = nullptr;
struct Functions {
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
	ODIN_VK_GLOBAL(ODIN_VK_MEMBER)
	ODIN_VK_INSTANCE(ODIN_VK_MEMBER)
	ODIN_VK_DEVICE(ODIN_VK_MEMBER, ODIN_VK_MEMBER2)
};
#undef ODIN_VK_MEMBER
#undef ODIN_VK_MEMBER2

/* An image of the card, its memory (its own, or a part of a block shared
 * with others), a view of all its levels, its aspects and the layout it
 * was last left in */
struct Image {
	static constexpr uint32_t OWN_MEMORY = UINT32_MAX;

	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	uint32_t block = OWN_MEMORY;
	VkDeviceSize offset = 0, size = 0;	/* in the block */
	VkImageView view = VK_NULL_HANDLE;
	VkImageAspectFlags aspects = VK_IMAGE_ASPECT_COLOR_BIT;
	unsigned w = 0, h = 0, levels = 1;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

/* A buffer in memory the processor sees, mapped for good */
struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize size = 0;
	uint8_t *bytes = nullptr;
};

/* A part of a buffer the processor fills, for the frame being written */
struct Slice {
	VkBuffer buffer;
	VkDeviceSize offset;
	uint8_t *bytes;
};

/* What a stage of the card does with an image: where its layout is to go
 * and which stages and accesses of it come before and after */
struct Use {
	VkImageLayout layout;
	VkPipelineStageFlags2 stage;
	VkAccessFlags2 access;
};
/* The uses the window and the board drawing make of their images */
inline constexpr Use COPIED_TO = { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
	                           VK_ACCESS_2_TRANSFER_WRITE_BIT };
inline constexpr Use COPIED_FROM = { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
	                             VK_ACCESS_2_TRANSFER_READ_BIT };
inline constexpr Use DRAWN_IN = { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	                          VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT };
inline constexpr Use SAMPLED = { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT };
inline constexpr Use DEPTH_IN = { VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	                          VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
	                                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
	                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
	                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };

/* A shader as SPIR-V, in the binary (spirv.hpp) */
struct Spirv {
	const uint32_t *code;
	size_t bytes;
};

/* A pass drawn with one pair of shaders: the images its fragment shader
 * samples (bindings 0, 1...), its push constants, and its pipeline */
struct Pass {
	VkDescriptorSetLayout images = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
};

/* The card for one window: made with the window, taken by the drawing
 * thread (display.hpp), as OpenGL's context is. */
class Context {
public:
	/* The frames the card may be working on while the next is written */
	static constexpr unsigned FRAMES = 2;

	/* The card for that window, or nothing with the reason in `why`. Debug:
	 * ODIN_VK_VALIDATION=1, Vulkan's validation layer, its messages on
	 * stderr. */
	static std::unique_ptr<Context> make(SDL_Window *window, const std::string &gpu, std::string &why);
	/* The cards of the computer, with or without a window */
	static std::vector<std::string> gpus();
	~Context();
	Context(const Context &) = delete;
	Context &operator=(const Context &) = delete;

	const Functions &fn() const { return m_fn; }
	VkInstance instance() const { return m_instance; }
	VkPhysicalDevice gpu() const { return m_gpu; }
	VkDevice device() const { return m_device; }
	VkQueue queue() const { return m_queue; }
	uint32_t queue_family() const { return m_queue_family; }
	uint32_t api_version() const { return m_api_version; }
	VkSurfaceKHR surface() const { return m_surface; }
	const std::string &name() const { return m_name; }
	const VkPhysicalDeviceProperties &properties() const { return m_properties; }
	const VkPhysicalDeviceLimits &limits() const { return m_properties.limits; }
	/* The most anisotropic filtering, 1 for none */
	float max_anisotropy() const { return m_max_anisotropy; }
	/* The depth of the board drawing: 24 bits where the card has them,
	 * else 32-bit floats, as each card's OpenGL compares */
	VkFormat depth_format() const { return m_depth_format; }
	/* Whether pipelines clip z from -w to w, as OpenGL does
	 * (VK_EXT_depth_clip_control); else from 0 to w */
	bool gl_clip_space() const { return m_gl_clip_space; }
	/* Whether a pipeline made tells if it came from the cache */
	bool creation_feedback() const { return m_creation_feedback; }
	/* Whether fragment shaders read gl_PrimitiveID (the geometry shaders
	 * of the card, or its tessellation shaders) */
	bool primitive_ids() const { return m_primitive_ids; }
	bool primitive_ids_tessellation() const { return m_primitive_ids_tessellation; }
	bool geometry_shaders() const { return m_geometry_shaders; }
	/* Each colour attachment its own blend */
	bool independent_blend() const { return m_independent_blend; }
	bool portability_subset() const { return m_portability_subset; }
	/* Whether the queue writes the time, its stamps reset by the
	 * processor; the bits of a stamp and its unit, in ns */
	bool stamps() const { return m_stamp_bits > 0 && m_host_query_reset; }
	uint32_t stamp_bits() const { return m_stamp_bits; }
	float stamp_period() const { return m_properties.limits.timestampPeriod; }

	/* The commands of the frame being written, begun when first asked
	 * for; sent and waited for: what was written so far done, a new
	 * command buffer begun; the frame given to the next one, after its
	 * commands (submit) */
	VkCommandBuffer commands();
	/* Commands sent before those of the frame each time they are sent:
	 * copies asked for while the frame draws (the textures) */
	VkCommandBuffer uploads();
	unsigned frame() const { return m_frame; }
	void send_and_wait();
	/* The number the commands being written are sent under; that sending
	 * done (sent and waited for first if it is this one) */
	uint64_t sending() const { return m_sent + 1; }
	void wait_sent(uint64_t number);
	struct Submit {
		VkSemaphore wait = VK_NULL_HANDLE;	/* before the copy to the window */
		VkSemaphore signal = VK_NULL_HANDLE;	/* once all is done */
	};
	void end_frame(const Submit &submit);
	/* Something the card may still use, destroyed once the frames it
	 * could be in are done (send_and_wait: at once) */
	void later(std::function<void()> destroy);

	/* An image of one sample, its memory and a view of its levels; the
	 * same in a block of memory shared with other images (many small
	 * ones: the textures); a buffer the processor reads and writes,
	 * mapped */
	Image make_image(VkFormat format, unsigned w, unsigned h, unsigned levels, VkImageUsageFlags usage,
	                 const VkComponentMapping &swizzle = {});
	Image make_placed_image(VkFormat format, unsigned w, unsigned h, unsigned levels, VkImageUsageFlags usage,
	                        const VkComponentMapping &swizzle = {});
	void destroy_image(Image &image);
	Buffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage);
	/* A buffer in the memory of the card, not mapped */
	Buffer make_card_buffer(VkDeviceSize size, VkBufferUsageFlags usage);
	void destroy_buffer(Buffer &buffer);
	/* A buffer for the processor to fill or read in the frame being
	 * written, at least `size` bytes, kept until the card is done */
	Buffer &staging(VkDeviceSize size);
	/* A part of such a buffer, at an offset that is a multiple of
	 * `align` */
	Slice stage(VkDeviceSize size, VkDeviceSize align);

	/* The image from its last use to the next one, all its levels; the
	 * levels from `level` on only (they are taken as one use) */
	void barrier(VkCommandBuffer cb, Image &image, const Use &next, unsigned level = 0);
	/* Level 0 of a colour image all of `colour`, the image moved to be
	 * copied to */
	void clear(VkCommandBuffer cb, Image &image, const VkClearColorValue &colour);
	/* Those accesses of those stages done and seen by the next ones */
	void memory_barrier(VkCommandBuffer cb, VkPipelineStageFlags2 stages, VkAccessFlags2 access,
	                    VkPipelineStageFlags2 next_stages, VkAccessFlags2 next_access);

	/* A pass that samples `images` images, with `push` bytes of push
	 * constants, drawn as a list or a strip of triangles into an image of
	 * that format, nothing culled, no depth; nothing blended, or over what
	 * is there by the alpha of what is drawn (`over`) */
	Pass make_pass(const Spirv &vertex, const Spirv &fragment, unsigned images, uint32_t push,
	               VkPrimitiveTopology topology, VkFormat format, bool over = false);
	/* The same with those bindings in its set */
	Pass make_pass(const Spirv &vertex, const Spirv &fragment,
	               const std::vector<VkDescriptorSetLayoutBinding> &bindings, uint32_t push,
	               VkPrimitiveTopology topology, VkFormat format, bool over = false);
	void destroy_pass(Pass &pass);
	/* A descriptor set of that layout, for the frame being written */
	VkDescriptorSet frame_set(VkDescriptorSetLayout layout);
	/* The images a pass samples, each through that sampler, in the frame
	 * being written: they must be SAMPLED already */
	void bind_images(VkCommandBuffer cb, const Pass &pass, std::initializer_list<const Image *> images,
	                 VkSampler sampler);
	/* Drawing into the image, in that rectangle of it (its viewport and
	 * scissor), what it holds kept, or all of it made opaque black (CLEAR),
	 * or thrown away (DONT_CARE: the drawing covers it all); then done */
	void begin_drawing(VkCommandBuffer cb, Image &image, const VkRect2D &area,
	                   VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_LOAD);
	void end_drawing(VkCommandBuffer cb) { m_fn.vkCmdEndRendering(cb); }

	/* The image of the window: everything is drawn into it, then it is
	 * copied to the swap chain (host_graphics_vk.cpp). Made again at the
	 * size of the window. */
	Image &window_image(unsigned w, unsigned h);
	Image &window_image() { return m_window; }

	/* The pictures of the board drawing that the window reads
	 * (HostPicture: their number) */
	void name_picture(uint32_t id, Image *image) { m_pictures[id] = image; }
	void forget_picture(uint32_t id) { m_pictures.erase(id); }
	Image *picture(uint32_t id) const;

	/* Aborts with what went wrong */
	[[noreturn]] void fatal(const char *what, VkResult result) const;
	void check(VkResult result, const char *what) const
	{
		if (result != VK_SUCCESS)
			fatal(what, result);
	}

private:
	Context() = default;
	bool load(std::string &why);
	bool make_instance(std::string &why);
	std::vector<const char *> validation_layers(const std::vector<VkExtensionProperties> &available,
	                                            std::vector<const char *> &extensions);
	void listen_to_validation();
	bool choose_gpu(const std::string &wanted, std::string &why);
	/* Their names as HostGraphics::gpus() has them */
	std::vector<std::string> names_of(const std::vector<VkPhysicalDevice> &gpus) const;
	std::optional<uint32_t> shown_family(VkPhysicalDevice gpu,
	                                     const std::vector<VkQueueFamilyProperties> &families) const;
	void list_extensions(const std::vector<VkExtensionProperties> &extensions, uint32_t api, bool core13,
	                     bool reset_ext);
	void choose_depth_format();
	bool make_device(std::string &why);
	void make_frames();
	Pass pass_layout(const std::vector<VkDescriptorSetLayoutBinding> &bindings, uint32_t push);
	VkShaderModule shader_module(const Spirv &code);
	uint32_t memory_type(uint32_t types, VkMemoryPropertyFlags flags) const;
	Image make_image(VkFormat format, unsigned w, unsigned h, unsigned levels, VkImageUsageFlags usage,
	                 const VkComponentMapping &swizzle, bool placed);
	void place(Image &image);
	void unplace(const Image &image);
	static VKAPI_ATTR VkBool32 VKAPI_CALL message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	                                              VkDebugUtilsMessageTypeFlagsEXT type,
	                                              const VkDebugUtilsMessengerCallbackDataEXT *data, void *self);

	Functions m_fn;
	VkInstance m_instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkPhysicalDevice m_gpu = VK_NULL_HANDLE;
	uint32_t m_queue_family = 0;
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_queue = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties m_memory{};
	VkPhysicalDeviceProperties m_properties{};
	uint32_t m_api_version = 0;
	std::string m_name;
	float m_max_anisotropy = 1.0f;
	VkFormat m_depth_format = VK_FORMAT_D32_SFLOAT;
	bool m_gl_clip_space = false;
	bool m_creation_feedback = false;
	bool m_primitive_ids = false;
	bool m_primitive_ids_tessellation = false;
	bool m_geometry_shaders = false;
	bool m_independent_blend = false;
	bool m_host_query_reset = false;
	uint32_t m_stamp_bits = 0;
	bool m_validation = false;
	bool m_portability_subset = false;
	bool m_core13 = false;
	/* The extensions the device was made with */
	std::vector<const char *> m_extensions;

	struct Frame {
		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandBuffer cb = VK_NULL_HANDLE, uploads = VK_NULL_HANDLE;
		VkFence done = VK_NULL_HANDLE;
		bool written = false;	/* its command buffer begun */
		bool uploading = false;	/* the one of the uploads too */
		bool sent = false;	/* sent, its fence not waited for yet */
		bool again = false;	/* begun again, what it gave not taken back yet */
		uint64_t number = 0;	/* of its last sending */
		std::vector<std::function<void()>> later;
		std::deque<Buffer> staging;	/* a deque: what staging gave stays put */
		size_t staging_used = 0;
		/* The buffer stage cuts its slices from, and where */
		size_t sliced = SIZE_MAX;
		VkDeviceSize sliced_to = 0;
		VkDescriptorPool descriptors = VK_NULL_HANDLE;	/* emptied with the frame */
	};
	Frame m_frames[FRAMES];
	unsigned m_frame = 0;
	uint64_t m_sent = 0;	/* sendings so far */
	void wait(Frame &frame);
	void submit(Frame &frame, const Submit &submit);

	/* The blocks of memory images share: their free parts, by offset */
	struct Block {
		VkDeviceMemory memory;
		uint32_t type;
		VkDeviceSize size;
		std::map<VkDeviceSize, VkDeviceSize> free;
	};
	std::vector<Block> m_blocks;

	Image m_window;
	std::unordered_map<uint32_t, Image *> m_pictures;
};

} /* namespace odin::vk */

#endif /* ODIN_VK_COMMON_HPP */
