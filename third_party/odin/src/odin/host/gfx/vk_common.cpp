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

#include "odin/host/gfx/vk_common.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "odin/common/env.hpp"

namespace odin::vk {

namespace {

bool
has(const std::vector<VkExtensionProperties> &list, const char *name)
{
	return std::any_of(list.begin(), list.end(), [&](const VkExtensionProperties &e) {
		return std::strcmp(e.extensionName, name) == 0;
	});
}

/* What Odin asks of a card that has not Vulkan 1.3, as extensions: those of
 * the functions it uses, and what rendering without render passes needs
 * below Vulkan 1.2 */
std::vector<const char *>
extensions_below_13(uint32_t api)
{
	std::vector<const char *> names = { VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
	                                    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
	                                    VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME };

	if (api < VK_API_VERSION_1_2) {
		names.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
		names.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
	}
	return names;
}

/* The features of the card Odin needs, read or asked for in one chain; the
 * clip space of OpenGL, where the card has its extension */
struct Features {
	VkPhysicalDeviceVulkan13Features v13{};
	VkPhysicalDeviceDynamicRenderingFeaturesKHR rendering{};
	VkPhysicalDeviceSynchronization2FeaturesKHR sync2{};
	VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic{};
	VkPhysicalDeviceDepthClipControlFeaturesEXT clip{};
	VkPhysicalDeviceHostQueryResetFeatures reset{};
	VkPhysicalDeviceFeatures2 all{};

	explicit Features(bool core13)
	{
		v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		rendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
		sync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
		dynamic.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
		clip.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT;
		reset.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
		all.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		if (core13) {
			all.pNext = &v13;
		} else {
			all.pNext = &rendering;
			rendering.pNext = &sync2;
			sync2.pNext = &dynamic;
		}
	}
	void with_clip()
	{
		clip.pNext = all.pNext;
		all.pNext = &clip;
	}
	void with_reset()
	{
		reset.pNext = all.pNext;
		all.pNext = &reset;
	}
	bool enough(bool core13) const
	{
		return all.features.fragmentStoresAndAtomics &&
		       (core13 ? v13.dynamicRendering && v13.synchronization2 :
		                 rendering.dynamicRendering && sync2.synchronization2 && dynamic.extendedDynamicState);
	}
};

const char *
result_name(VkResult result)
{
	switch (result) {
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "out of host memory";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "out of device memory";
	case VK_ERROR_INITIALIZATION_FAILED: return "initialization failed";
	case VK_ERROR_DEVICE_LOST: return "device lost";
	case VK_ERROR_SURFACE_LOST_KHR: return "surface lost";
	case VK_ERROR_OUT_OF_DATE_KHR: return "out of date";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "feature not present";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "extension not present";
	case VK_ERROR_LAYER_NOT_PRESENT: return "layer not present";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "incompatible driver";
	default: return "error";
	}
}

} /* namespace */

std::unique_ptr<Context>
Context::make(SDL_Window *window, const std::string &gpu, std::string &why)
{
	std::unique_ptr<Context> card(new Context());

	card->m_validation = env_flag("ODIN_VK_VALIDATION");
	if (!card->load(why) || !card->make_instance(why))
		return nullptr;
	if (!SDL_Vulkan_CreateSurface(window, card->m_instance, nullptr, &card->m_surface)) {
		why = std::string("no surface for the window: ") + SDL_GetError();
		return nullptr;
	}
	if (!card->choose_gpu(gpu, why) || !card->make_device(why))
		return nullptr;
	return card;
}

/* An instance of its own: OpenGL may be drawing, and SDL has not loaded
 * Vulkan for it */
std::vector<std::string>
Context::gpus()
{
	std::vector<std::string> out;
	const bool loaded = SDL_Vulkan_LoadLibrary(nullptr);

	{
		Context card;
		std::string why;

		if (card.load(why) && card.make_instance(why)) {
			uint32_t count = 0;

			card.m_fn.vkEnumeratePhysicalDevices(card.m_instance, &count, nullptr);
			std::vector<VkPhysicalDevice> gpus(count);
			card.m_fn.vkEnumeratePhysicalDevices(card.m_instance, &count, gpus.data());

			out = card.names_of(gpus);
		}
	}
	if (loaded)
		SDL_Vulkan_UnloadLibrary();
	return out;
}

std::vector<std::string>
Context::names_of(const std::vector<VkPhysicalDevice> &gpus) const
{
	std::vector<std::string> plain, out;

	for (const VkPhysicalDevice gpu : gpus) {
		VkPhysicalDeviceProperties props;

		m_fn.vkGetPhysicalDeviceProperties(gpu, &props);
		const auto before = std::count(plain.begin(), plain.end(), props.deviceName);

		plain.emplace_back(props.deviceName);
		out.push_back(before ? plain.back() + " (" + std::to_string(before + 1) + ")" : plain.back());
	}
	return out;
}

Context::~Context()
{
	if (m_device) {
		m_fn.vkDeviceWaitIdle(m_device);
		for (Frame &frame : m_frames) {
			for (const auto &destroy : frame.later)
				destroy();
			for (Buffer &buffer : frame.staging)
				destroy_buffer(buffer);
			m_fn.vkDestroyFence(m_device, frame.done, nullptr);
			m_fn.vkDestroyCommandPool(m_device, frame.pool, nullptr);
			m_fn.vkDestroyDescriptorPool(m_device, frame.descriptors, nullptr);
		}
		destroy_image(m_window);
		for (const Block &block : m_blocks)
			m_fn.vkFreeMemory(m_device, block.memory, nullptr);
		m_fn.vkDestroyDevice(m_device, nullptr);
	}
	if (m_surface)
		m_fn.vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	if (m_messenger)
		m_fn.vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
	if (m_instance)
		m_fn.vkDestroyInstance(m_instance, nullptr);
}

/* SDL has loaded Vulkan with the window (SDL_WINDOW_VULKAN): its entry
 * gives the rest. */
bool
Context::load(std::string &why)
{
	m_fn.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
	if (!m_fn.vkGetInstanceProcAddr) {
		why = std::string("no Vulkan loader: ") + SDL_GetError();
		return false;
	}
#define ODIN_VK_LOAD(name) m_fn.name = reinterpret_cast<PFN_##name>(m_fn.vkGetInstanceProcAddr(VK_NULL_HANDLE, #name));
	ODIN_VK_GLOBAL(ODIN_VK_LOAD)
#undef ODIN_VK_LOAD
	if (!m_fn.vkCreateInstance) {
		why = "the Vulkan loader has no vkCreateInstance";
		return false;
	}
	return true;
}

bool
Context::make_instance(std::string &why)
{
	uint32_t loader = VK_API_VERSION_1_0, count = 0;

	if (m_fn.vkEnumerateInstanceVersion)
		m_fn.vkEnumerateInstanceVersion(&loader);
	if (loader < VK_API_VERSION_1_1) {
		why = "the Vulkan loader is older than 1.1";
		return false;
	}
	m_api_version = std::min<uint32_t>(loader, VK_API_VERSION_1_3);

	m_fn.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> available(count);
	m_fn.vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

	Uint32 sdl_count = 0;
	const char *const *sdl = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
	std::vector<const char *> extensions(sdl, sdl + sdl_count);
	VkInstanceCreateFlags flags = 0;

	/* MoltenVK is a "portability" driver: only listed when asked for */
	if (has(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	}

	const std::vector<const char *> layers = validation_layers(available, extensions);

	const VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Odin", 1, "Odin", 1, m_api_version };
	const VkInstanceCreateInfo info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, flags, &app,
	                                 static_cast<uint32_t>(layers.size()), layers.data(),
	                                 static_cast<uint32_t>(extensions.size()), extensions.data() };
	if (const VkResult result = m_fn.vkCreateInstance(&info, nullptr, &m_instance); result != VK_SUCCESS) {
		why = std::string("no Vulkan instance: ") + result_name(result);
		return false;
	}
#define ODIN_VK_LOAD(name) m_fn.name = reinterpret_cast<PFN_##name>(m_fn.vkGetInstanceProcAddr(m_instance, #name));
	ODIN_VK_INSTANCE(ODIN_VK_LOAD)
#undef ODIN_VK_LOAD

	listen_to_validation();
	return true;
}

/* The validation layer, when asked for (it says what Odin does wrong) and
 * there: its name, and the extension of its messages added to those of
 * the instance; none, said, when it is not there */
std::vector<const char *>
Context::validation_layers(const std::vector<VkExtensionProperties> &available, std::vector<const char *> &extensions)
{
	std::vector<const char *> layers;
	uint32_t count = 0;

	if (!m_validation)
		return layers;

	std::vector<VkLayerProperties> known;

	m_fn.vkEnumerateInstanceLayerProperties(&count, nullptr);
	known.resize(count);
	m_fn.vkEnumerateInstanceLayerProperties(&count, known.data());
	const bool found = std::any_of(known.begin(), known.end(), [](const VkLayerProperties &l) {
		return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
	});
	if (found && has(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
		layers.push_back("VK_LAYER_KHRONOS_validation");
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	} else {
		std::fprintf(stderr, "ERROR: vulkan: no validation layer\n");
		m_validation = false;
	}
	return layers;
}

/* The warnings and errors of the validation layer, to stderr (message) */
void
Context::listen_to_validation()
{
	if (!m_validation || !m_fn.vkCreateDebugUtilsMessengerEXT)
		return;

	const VkDebugUtilsMessengerCreateInfoEXT messages{
		VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT, nullptr, 0,
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		&Context::message, this };

	m_fn.vkCreateDebugUtilsMessengerEXT(m_instance, &messages, nullptr, &m_messenger);
}

VKAPI_ATTR VkBool32 VKAPI_CALL
Context::message(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                 const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
{
	const bool error = severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

	std::fprintf(stderr, "vulkan: %s: %s\n", error ? "ERROR" : "warning", data->pMessage);
	return VK_FALSE;
}

/* The card that has all Odin needs and a queue that draws and shows in
 * the window: the one `wanted`, else a card of its own before one of the
 * processor */
bool
Context::choose_gpu(const std::string &wanted, std::string &why)
{
	const uint32_t instance_api = m_api_version;
	uint32_t count = 0;
	int best = 0;

	m_fn.vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
	std::vector<VkPhysicalDevice> gpus(count);
	m_fn.vkEnumeratePhysicalDevices(m_instance, &count, gpus.data());

	const std::vector<std::string> names = names_of(gpus);
	bool found = wanted.empty();

	why = "no graphics card has Vulkan 1.1 with what Odin needs (rendering without render passes, synchronization2, "
	      "the extended dynamic state, fragment shaders that write to buffers)";

	for (size_t i = 0; i < gpus.size(); i++) {
		const VkPhysicalDevice gpu = gpus[i];
		VkPhysicalDeviceProperties props;
		uint32_t n = 0;

		m_fn.vkGetPhysicalDeviceProperties(gpu, &props);
		const uint32_t api = std::min(props.apiVersion, instance_api);
		if (api < VK_API_VERSION_1_1)
			continue;
		m_fn.vkEnumerateDeviceExtensionProperties(gpu, nullptr, &n, nullptr);
		std::vector<VkExtensionProperties> extensions(n);
		m_fn.vkEnumerateDeviceExtensionProperties(gpu, nullptr, &n, extensions.data());
		if (!has(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			continue;

		const bool core13 = api >= VK_API_VERSION_1_3;
		if (!core13) {
			const std::vector<const char *> needed = extensions_below_13(api);

			if (!std::all_of(needed.begin(), needed.end(), [&](const char *e) { return has(extensions, e); }))
				continue;
		}
		Features features(core13);
		const bool clip = has(extensions, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
		if (clip)
			features.with_clip();
		/* Core in Vulkan 1.2 */
		const bool reset_ext = api < VK_API_VERSION_1_2 && has(extensions, VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME);
		const bool reset = api >= VK_API_VERSION_1_2 || reset_ext;
		if (reset)
			features.with_reset();
		m_fn.vkGetPhysicalDeviceFeatures2(gpu, &features.all);
		if (!features.enough(core13))
			continue;

		m_fn.vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, nullptr);
		std::vector<VkQueueFamilyProperties> families(n);
		m_fn.vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, families.data());
		const std::optional<uint32_t> family = shown_family(gpu, families);
		if (!family)
			continue;

		const bool chosen = names[i] == wanted;
		const int score = chosen ? 4 :
		                  props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 :
		                  props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;

		found = found || chosen;
		if (score <= best)
			continue;
		best = score;
		m_gpu = gpu;
		m_queue_family = *family;
		m_core13 = core13;
		m_name = props.deviceName;
		m_properties = props;
		m_max_anisotropy = props.limits.maxSamplerAnisotropy;
		m_gl_clip_space = clip && features.clip.depthClipControl;
		m_host_query_reset = reset && features.reset.hostQueryReset;
		m_stamp_bits = families[*family].timestampValidBits;
		m_creation_feedback = core13 || has(extensions, VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME);
		list_extensions(extensions, api, core13, reset_ext);
		m_api_version = api;
	}
	if (!found)
		std::fprintf(stderr, "ERROR: vulkan: no card '%s' can draw: the best one does\n", wanted.c_str());
	if (!m_gpu)
		return false;
	why.clear();
	m_fn.vkGetPhysicalDeviceMemoryProperties(m_gpu, &m_memory);
	choose_depth_format();
	return true;
}

/* The first queue family of the card that draws and shows in the window */
std::optional<uint32_t>
Context::shown_family(VkPhysicalDevice gpu, const std::vector<VkQueueFamilyProperties> &families) const
{
	for (uint32_t f = 0; f < families.size(); f++) {
		VkBool32 shows = VK_FALSE;

		m_fn.vkGetPhysicalDeviceSurfaceSupportKHR(gpu, f, m_surface, &shows);
		if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && shows)
			return f;
	}
	return std::nullopt;
}

/* The extensions of the device for the card chosen: those Odin needs
 * (below Vulkan 1.3, those that are core from it), and those it uses when
 * the card has them */
void
Context::list_extensions(const std::vector<VkExtensionProperties> &extensions, uint32_t api, bool core13, bool reset_ext)
{
	m_extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	/* Dear ImGui asks for this one by name, even under 1.3 */
	if (has(extensions, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
		m_extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
	if (!core13)
		for (const char *e : extensions_below_13(api))
			if (std::strcmp(e, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) != 0)
				m_extensions.push_back(e);
	m_portability_subset = has(extensions, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
	if (m_portability_subset)
		m_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
	if (m_gl_clip_space)
		m_extensions.push_back(VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
	if (m_host_query_reset && reset_ext)
		m_extensions.push_back(VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME);
	if (!core13 && m_creation_feedback)
		m_extensions.push_back(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME);
}

/* The format of the depth, which is also copied for the translucent
 * fragments, and read */
void
Context::choose_depth_format()
{
	const VkFormatFeatureFlags depth = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
	                                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
	                                   VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

	for (const VkFormat format : { VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT }) {
		VkFormatProperties props;

		m_fn.vkGetPhysicalDeviceFormatProperties(m_gpu, format, &props);
		if ((props.optimalTilingFeatures & depth) == depth) {
			m_depth_format = format;
			break;
		}
	}
}

bool
Context::make_device(std::string &why)
{
	const float priority = 1.0f;
	const VkDeviceQueueCreateInfo queue{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, m_queue_family, 1,
	                                     &priority };
	Features wanted(m_core13), had(m_core13);

	m_fn.vkGetPhysicalDeviceFeatures2(m_gpu, &had.all);
	wanted.all.features.fragmentStoresAndAtomics = VK_TRUE;
	wanted.all.features.samplerAnisotropy = had.all.features.samplerAnisotropy;
	if (!had.all.features.samplerAnisotropy)
		m_max_anisotropy = 1.0f;
	wanted.all.features.geometryShader = m_geometry_shaders = had.all.features.geometryShader;
	wanted.all.features.independentBlend = m_independent_blend = had.all.features.independentBlend;
	m_primitive_ids_tessellation = !had.all.features.geometryShader && had.all.features.tessellationShader;
	wanted.all.features.tessellationShader = m_primitive_ids_tessellation;
	m_primitive_ids = had.all.features.geometryShader || m_primitive_ids_tessellation;
	wanted.v13.dynamicRendering = wanted.v13.synchronization2 = VK_TRUE;
	wanted.rendering.dynamicRendering = wanted.sync2.synchronization2 = VK_TRUE;
	wanted.dynamic.extendedDynamicState = VK_TRUE;
	if (m_gl_clip_space) {
		wanted.with_clip();
		wanted.clip.depthClipControl = VK_TRUE;
	}
	if (m_host_query_reset) {
		wanted.with_reset();
		wanted.reset.hostQueryReset = VK_TRUE;
	}
	VkPhysicalDevicePortabilitySubsetFeaturesKHR subset{};
	if (m_portability_subset) {
		subset.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
		VkPhysicalDeviceFeatures2 query{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &subset, {} };
		m_fn.vkGetPhysicalDeviceFeatures2(m_gpu, &query);
		subset.pNext = wanted.all.pNext;
		wanted.all.pNext = &subset;
	}

	const VkDeviceCreateInfo info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &wanted.all, 0, 1, &queue, 0, nullptr,
	                               static_cast<uint32_t>(m_extensions.size()), m_extensions.data(), nullptr };
	if (const VkResult result = m_fn.vkCreateDevice(m_gpu, &info, nullptr, &m_device); result != VK_SUCCESS) {
		why = std::string("no Vulkan device: ") + result_name(result);
		return false;
	}
#define ODIN_VK_LOAD(name) m_fn.name = reinterpret_cast<PFN_##name>(m_fn.vkGetDeviceProcAddr(m_device, #name));
#define ODIN_VK_LOAD2(name, other) \
	ODIN_VK_LOAD(name) \
	if (!m_fn.name) \
		m_fn.name = reinterpret_cast<PFN_##name>(m_fn.vkGetDeviceProcAddr(m_device, #other));
	ODIN_VK_DEVICE(ODIN_VK_LOAD, ODIN_VK_LOAD2)
#undef ODIN_VK_LOAD
#undef ODIN_VK_LOAD2
	m_fn.vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);

	make_frames();
	return true;
}

/* What each frame in flight has of its own: its command buffers (one for
 * the uploads), its fence, its descriptors */
void
Context::make_frames()
{
	for (Frame &frame : m_frames) {
		const VkCommandPoolCreateInfo pool{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
		                                    VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, m_queue_family };
		const VkFenceCreateInfo fence{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0 };

		check(m_fn.vkCreateCommandPool(m_device, &pool, nullptr, &frame.pool), "a command pool");
		const VkCommandBufferAllocateInfo buffer{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, frame.pool,
		                                          VK_COMMAND_BUFFER_LEVEL_PRIMARY, 2 };
		VkCommandBuffer cbs[2];
		check(m_fn.vkAllocateCommandBuffers(m_device, &buffer, cbs), "a command buffer");
		frame.cb = cbs[0];
		frame.uploads = cbs[1];
		check(m_fn.vkCreateFence(m_device, &fence, nullptr, &frame.done), "a fence");

		/* The passes of a frame: a few dozen at most; the lists drawn,
		 * a few */
		const VkDescriptorPoolSize sizes[4] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 },
			                                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 512 },
			                                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 128 },
			                                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128 } };
		const VkDescriptorPoolCreateInfo descriptors{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 256, 4,
		                                              sizes };
		check(m_fn.vkCreateDescriptorPool(m_device, &descriptors, nullptr, &frame.descriptors), "a descriptor pool");
	}
}

void
Context::fatal(const char *what, VkResult result) const
{
	std::fprintf(stderr, "ERROR: vulkan: %s: %s (%d)\n", what, result_name(result), static_cast<int>(result));
	std::abort();
}

/* What was sent of the frame is done: what waited for it goes. */
void
Context::wait(Frame &frame)
{
	if (!frame.sent)
		return;
	check(m_fn.vkWaitForFences(m_device, 1, &frame.done, VK_TRUE, UINT64_MAX), "waiting for a frame");
	check(m_fn.vkResetFences(m_device, 1, &frame.done), "a fence");
	frame.sent = false;
	for (const auto &destroy : frame.later)
		destroy();
	frame.later.clear();
}

/* The commands of a frame may be sent several times (send_and_wait): its
 * staging buffers and descriptors are free again only when it begins
 * again, those given before stay whole for the commands that follow. */
VkCommandBuffer
Context::commands()
{
	Frame &frame = m_frames[m_frame];

	if (!frame.written) {
		const VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
		                                      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr };

		wait(frame);
		if (frame.again) {
			frame.again = false;
			frame.staging_used = 0;
			frame.sliced = SIZE_MAX;
			check(m_fn.vkResetDescriptorPool(m_device, frame.descriptors, 0), "a descriptor pool");
		}
		check(m_fn.vkResetCommandPool(m_device, frame.pool, 0), "a command pool");
		check(m_fn.vkBeginCommandBuffer(frame.cb, &begin), "a command buffer");
		frame.written = true;
	}
	return frame.cb;
}

VkCommandBuffer
Context::uploads()
{
	Frame &frame = m_frames[m_frame];

	commands();
	if (!frame.uploading) {
		const VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
		                                      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr };

		check(m_fn.vkBeginCommandBuffer(frame.uploads, &begin), "a command buffer");
		frame.uploading = true;
	}
	return frame.uploads;
}

void
Context::submit(Frame &frame, const Submit &submit)
{
	const VkCommandBufferSubmitInfo cbs[2] = {
		{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, frame.uploads, 0 },
		{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, frame.cb, 0 },
	};
	const VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, submit.wait, 0,
	                                  VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, 0 };
	const VkSemaphoreSubmitInfo signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, submit.signal, 0,
	                                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0 };
	const VkSubmitInfo2 info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, submit.wait ? 1u : 0u, &wait,
	                          frame.uploading ? 2u : 1u, frame.uploading ? &cbs[0] : &cbs[1],
	                          submit.signal ? 1u : 0u, &signal };

	if (frame.uploading)
		check(m_fn.vkEndCommandBuffer(frame.uploads), "a command buffer");
	check(m_fn.vkEndCommandBuffer(frame.cb), "a command buffer");
	check(m_fn.vkQueueSubmit2(m_queue, 1, &info, frame.done), "sending commands");
	frame.written = frame.uploading = false;
	frame.sent = true;
	frame.number = ++m_sent;
}

/* The sendings run in order: once one sent at or after it is done, so is
 * it. */
void
Context::wait_sent(uint64_t number)
{
	Frame *first = nullptr;

	if (number > m_sent) {
		send_and_wait();
		return;
	}
	for (Frame &frame : m_frames)
		if (frame.sent && frame.number >= number && (!first || frame.number < first->number))
			first = &frame;
	if (first)
		wait(*first);
}

/* All the frames waited for: what one waited for may have been used by
 * the other. The card has nothing left to do then: what waits for the
 * frame being written goes too. */
void
Context::send_and_wait()
{
	Frame &frame = m_frames[m_frame];

	if (frame.written)
		submit(frame, {});
	for (Frame &f : m_frames)
		wait(f);
	for (const auto &destroy : frame.later)
		destroy();
	frame.later.clear();
}

void
Context::end_frame(const Submit &s)
{
	commands();
	submit(m_frames[m_frame], s);
	m_frame = (m_frame + 1) % FRAMES;
	m_frames[m_frame].again = true;
}

void
Context::later(std::function<void()> destroy)
{
	m_frames[m_frame].later.push_back(std::move(destroy));
}

uint32_t
Context::memory_type(uint32_t types, VkMemoryPropertyFlags flags) const
{
	for (uint32_t i = 0; i < m_memory.memoryTypeCount; i++)
		if ((types & (1u << i)) && (m_memory.memoryTypes[i].propertyFlags & flags) == flags)
			return i;
	return UINT32_MAX;
}

Image
Context::make_image(VkFormat format, unsigned w, unsigned h, unsigned levels, VkImageUsageFlags usage,
                    const VkComponentMapping &swizzle)
{
	return make_image(format, w, h, levels, usage, swizzle, false);
}

Image
Context::make_placed_image(VkFormat format, unsigned w, unsigned h, unsigned levels, VkImageUsageFlags usage,
                           const VkComponentMapping &swizzle)
{
	return make_image(format, w, h, levels, usage, swizzle, true);
}

Image
Context::make_image(VkFormat format, unsigned w, unsigned h, unsigned levels, VkImageUsageFlags usage,
                    const VkComponentMapping &swizzle, bool placed)
{
	Image image;
	const VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D, format,
	                              { w, h, 1 }, levels, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, usage,
	                              VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED };

	check(m_fn.vkCreateImage(m_device, &info, nullptr, &image.image), "an image");
	if (placed) {
		place(image);
	} else {
		VkMemoryRequirements needs;

		m_fn.vkGetImageMemoryRequirements(m_device, image.image, &needs);
		const VkMemoryAllocateInfo memory{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, needs.size,
		                                   memory_type(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
		check(m_fn.vkAllocateMemory(m_device, &memory, nullptr, &image.memory), "the memory of an image");
		check(m_fn.vkBindImageMemory(m_device, image.image, image.memory, 0), "the memory of an image");
	}

	/* A depth image is seen through its depth alone */
	if (format == VK_FORMAT_D24_UNORM_S8_UINT)
		image.aspects = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	else if (format == VK_FORMAT_X8_D24_UNORM_PACK32 || format == VK_FORMAT_D32_SFLOAT)
		image.aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
	const VkImageAspectFlags seen = image.aspects & ~VK_IMAGE_ASPECT_STENCIL_BIT;

	/* A view for the images shaders read or draw into; copies need none */
	if (usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
		const VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, image.image,
		                                  VK_IMAGE_VIEW_TYPE_2D, format, swizzle, { seen, 0, levels, 0, 1 } };
		check(m_fn.vkCreateImageView(m_device, &view, nullptr, &image.view), "the view of an image");
	}
	image.w = w;
	image.h = h;
	image.levels = levels;
	return image;
}

void
Context::destroy_image(Image &image)
{
	m_fn.vkDestroyImageView(m_device, image.view, nullptr);
	m_fn.vkDestroyImage(m_device, image.image, nullptr);
	if (image.block != Image::OWN_MEMORY)
		unplace(image);
	else
		m_fn.vkFreeMemory(m_device, image.memory, nullptr);
	image = {};
}

/* The first free part of a block that holds the image, else a new block. A
 * block holds images alone, all of optimal tiling: nothing else lies beside
 * them (bufferImageGranularity). */
void
Context::place(Image &image)
{
	constexpr VkDeviceSize BLOCK = VkDeviceSize{32} << 20;
	VkMemoryRequirements needs;

	m_fn.vkGetImageMemoryRequirements(m_device, image.image, &needs);
	const uint32_t type = memory_type(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	const auto up = [&](VkDeviceSize x) { return (x + needs.alignment - 1) / needs.alignment * needs.alignment; };

	for (uint32_t b = 0; b < m_blocks.size(); b++) {
		Block &block = m_blocks[b];

		if (block.type != type)
			continue;
		for (auto part = block.free.begin(); part != block.free.end(); ++part) {
			const VkDeviceSize start = part->first, end = part->first + part->second;
			const VkDeviceSize at = up(start);

			if (at + needs.size > end)
				continue;
			block.free.erase(part);
			if (at > start)
				block.free.emplace(start, at - start);
			if (at + needs.size < end)
				block.free.emplace(at + needs.size, end - at - needs.size);
			image.block = b;
			image.offset = at;
			image.size = needs.size;
			check(m_fn.vkBindImageMemory(m_device, image.image, block.memory, at), "the memory of an image");
			return;
		}
	}

	Block block{ VK_NULL_HANDLE, type, std::max(BLOCK, needs.size), {} };
	const VkMemoryAllocateInfo memory{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, block.size, type };
	check(m_fn.vkAllocateMemory(m_device, &memory, nullptr, &block.memory), "a block of memory for images");
	if (needs.size < block.size)
		block.free.emplace(needs.size, block.size - needs.size);
	m_blocks.push_back(std::move(block));
	image.block = static_cast<uint32_t>(m_blocks.size() - 1);
	image.offset = 0;
	image.size = needs.size;
	check(m_fn.vkBindImageMemory(m_device, image.image, m_blocks.back().memory, 0), "the memory of an image");
}

/* The part of the image free again, joined to the free parts beside it */
void
Context::unplace(const Image &image)
{
	std::map<VkDeviceSize, VkDeviceSize> &free = m_blocks[image.block].free;
	VkDeviceSize start = image.offset, size = image.size;
	auto next = free.lower_bound(start);

	if (next != free.begin()) {
		const auto before = std::prev(next);

		if (before->first + before->second == start) {
			start = before->first;
			size += before->second;
			free.erase(before);
		}
	}
	if (next != free.end() && start + size == next->first) {
		size += next->second;
		free.erase(next);
	}
	free.emplace(start, size);
}

Buffer
Context::make_card_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
	Buffer buffer;
	const VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, size, usage,
	                               VK_SHARING_MODE_EXCLUSIVE, 0, nullptr };
	VkMemoryRequirements needs;

	check(m_fn.vkCreateBuffer(m_device, &info, nullptr, &buffer.buffer), "a buffer");
	m_fn.vkGetBufferMemoryRequirements(m_device, buffer.buffer, &needs);
	const VkMemoryAllocateInfo memory{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, needs.size,
	                                   memory_type(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
	check(m_fn.vkAllocateMemory(m_device, &memory, nullptr, &buffer.memory), "the memory of a buffer");
	check(m_fn.vkBindBufferMemory(m_device, buffer.buffer, buffer.memory, 0), "the memory of a buffer");
	buffer.size = size;
	return buffer;
}

/* Memory the processor reads fast where the card has such */
Buffer
Context::make_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
	Buffer buffer;
	const VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, size, usage,
	                               VK_SHARING_MODE_EXCLUSIVE, 0, nullptr };
	VkMemoryRequirements needs;
	void *bytes = nullptr;

	check(m_fn.vkCreateBuffer(m_device, &info, nullptr, &buffer.buffer), "a buffer");
	m_fn.vkGetBufferMemoryRequirements(m_device, buffer.buffer, &needs);
	const VkMemoryPropertyFlags seen = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	uint32_t type = memory_type(needs.memoryTypeBits, seen | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	if (type == UINT32_MAX)
		type = memory_type(needs.memoryTypeBits, seen);
	const VkMemoryAllocateInfo memory{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, needs.size, type };
	check(m_fn.vkAllocateMemory(m_device, &memory, nullptr, &buffer.memory), "the memory of a buffer");
	check(m_fn.vkBindBufferMemory(m_device, buffer.buffer, buffer.memory, 0), "the memory of a buffer");
	check(m_fn.vkMapMemory(m_device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &bytes), "mapping a buffer");
	buffer.bytes = static_cast<uint8_t *>(bytes);
	buffer.size = size;
	return buffer;
}

void
Context::destroy_buffer(Buffer &buffer)
{
	m_fn.vkDestroyBuffer(m_device, buffer.buffer, nullptr);
	m_fn.vkFreeMemory(m_device, buffer.memory, nullptr);
	buffer = {};
}

Buffer &
Context::staging(VkDeviceSize size)
{
	Frame &frame = m_frames[m_frame];

	commands();
	if (frame.staging_used == frame.staging.size())
		frame.staging.emplace_back();
	Buffer &buffer = frame.staging[frame.staging_used++];
	if (buffer.size < size) {
		if (buffer.buffer)
			destroy_buffer(buffer);
		buffer = make_buffer(std::max<VkDeviceSize>(size, 1u << 20),
		                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
		                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	}
	return buffer;
}

Slice
Context::stage(VkDeviceSize size, VkDeviceSize align)
{
	constexpr VkDeviceSize SLICED = VkDeviceSize{4} << 20;
	Frame &frame = m_frames[m_frame];

	commands();
	if (frame.sliced != SIZE_MAX) {
		const Buffer &buffer = frame.staging[frame.sliced];
		const VkDeviceSize at = (frame.sliced_to + align - 1) / align * align;

		if (at + size <= buffer.size) {
			frame.sliced_to = at + size;
			return { buffer.buffer, at, buffer.bytes + at };
		}
	}
	const Buffer &buffer = staging(std::max(size, SLICED));
	frame.sliced = frame.staging_used - 1;
	frame.sliced_to = size;
	return { buffer.buffer, 0, buffer.bytes };
}

void
Context::memory_barrier(VkCommandBuffer cb, VkPipelineStageFlags2 stages, VkAccessFlags2 access,
                        VkPipelineStageFlags2 next_stages, VkAccessFlags2 next_access)
{
	const VkMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr, stages, access, next_stages,
	                                next_access };
	const VkDependencyInfo info{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 1, &barrier, 0, nullptr, 0, nullptr };

	m_fn.vkCmdPipelineBarrier2(cb, &info);
}

/* What the image was last used for is not followed closely: every write
 * of the card before is waited for. */
void
Context::barrier(VkCommandBuffer cb, Image &image, const Use &next, unsigned level)
{
	const VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, nullptr,
	                                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
	                                     next.stage, next.access, image.layout, next.layout,
	                                     VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image.image,
	                                     { image.aspects, level, image.levels - level, 0, 1 } };
	const VkDependencyInfo info{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &barrier };

	m_fn.vkCmdPipelineBarrier2(cb, &info);
	image.layout = next.layout;
}

void
Context::clear(VkCommandBuffer cb, Image &image, const VkClearColorValue &colour)
{
	const VkImageSubresourceRange all{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	barrier(cb, image, COPIED_TO);
	m_fn.vkCmdClearColorImage(cb, image.image, image.layout, &colour, 1, &all);
}

Pass
Context::make_pass(const Spirv &vertex, const Spirv &fragment, unsigned images, uint32_t push,
                   VkPrimitiveTopology topology, VkFormat format, bool over)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings;

	for (uint32_t i = 0; i < images; i++)
		bindings.push_back({ i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
	return make_pass(vertex, fragment, bindings, push, topology, format, over);
}

Pass
Context::make_pass(const Spirv &vertex, const Spirv &fragment, const std::vector<VkDescriptorSetLayoutBinding> &bindings,
                   uint32_t push, VkPrimitiveTopology topology, VkFormat format, bool over)
{
	Pass pass = pass_layout(bindings, push);
	VkShaderModule modules[2];
	const Spirv *const code[2] = { &vertex, &fragment };

	for (unsigned i = 0; i < 2; i++)
		modules[i] = shader_module(*code[i]);
	const VkPipelineShaderStageCreateInfo stages[2] = {
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, modules[0],
		  "main", nullptr },
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, modules[1],
		  "main", nullptr },
	};
	VkPipelineVertexInputStateCreateInfo input{};
	input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	const VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
	                                                       nullptr, 0, topology, VK_FALSE };
	const VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1,
	                                                  nullptr, 1, nullptr };
	VkPipelineRasterizationStateCreateInfo raster{};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo samples{};
	samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState write{};
	write.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
	                       VK_COLOR_COMPONENT_A_BIT;
	/* As glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) */
	if (over) {
		write.blendEnable = VK_TRUE;
		write.srcColorBlendFactor = write.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		write.dstColorBlendFactor = write.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	}
	VkPipelineColorBlendStateCreateInfo blend{};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &write;
	const VkDynamicState states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	const VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2,
	                                                states };
	const VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0, 1,
	                                               &format, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED };
	VkGraphicsPipelineCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.pNext = &rendering;
	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &input;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &samples;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = pass.layout;
	check(m_fn.vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &pass.pipeline), "a pipeline");
	for (const VkShaderModule module : modules)
		m_fn.vkDestroyShaderModule(m_device, module, nullptr);
	return pass;
}

/* A pass with its images (its descriptors) and its push constants laid
 * out, no pipeline yet */
Pass
Context::pass_layout(const std::vector<VkDescriptorSetLayoutBinding> &bindings, uint32_t push)
{
	Pass pass;
	const VkDescriptorSetLayoutCreateInfo set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0,
	                                           static_cast<uint32_t>(bindings.size()), bindings.data() };
	check(m_fn.vkCreateDescriptorSetLayout(m_device, &set, nullptr, &pass.images), "the images of a pass");

	const VkPushConstantRange range{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, push };
	const VkPipelineLayoutCreateInfo layout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &pass.images,
	                                         push ? 1u : 0u, &range };
	check(m_fn.vkCreatePipelineLayout(m_device, &layout, nullptr, &pass.layout), "the layout of a pass");
	return pass;
}

VkShaderModule
Context::shader_module(const Spirv &code)
{
	const VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, code.bytes,
	                                     code.code };
	VkShaderModule module;

	check(m_fn.vkCreateShaderModule(m_device, &info, nullptr, &module), "a shader");
	return module;
}

void
Context::destroy_pass(Pass &pass)
{
	m_fn.vkDestroyPipeline(m_device, pass.pipeline, nullptr);
	m_fn.vkDestroyPipelineLayout(m_device, pass.layout, nullptr);
	m_fn.vkDestroyDescriptorSetLayout(m_device, pass.images, nullptr);
	pass = {};
}

VkDescriptorSet
Context::frame_set(VkDescriptorSetLayout layout)
{
	const VkDescriptorSetAllocateInfo allocate{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
	                                            m_frames[m_frame].descriptors, 1, &layout };
	VkDescriptorSet set;

	commands();
	check(m_fn.vkAllocateDescriptorSets(m_device, &allocate, &set), "a descriptor set of the frame");
	return set;
}

void
Context::bind_images(VkCommandBuffer cb, const Pass &pass, std::initializer_list<const Image *> images,
                     VkSampler sampler)
{
	std::vector<VkDescriptorImageInfo> infos;
	std::vector<VkWriteDescriptorSet> writes;
	const VkDescriptorSet set = frame_set(pass.images);

	infos.reserve(images.size());
	for (const Image *image : images)
		infos.push_back({ sampler, image->view, image->layout });
	for (uint32_t i = 0; i < infos.size(); i++)
		writes.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, i, 0, 1,
		                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &infos[i], nullptr, nullptr });
	m_fn.vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	m_fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline);
	m_fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.layout, 0, 1, &set, 0, nullptr);
}

void
Context::begin_drawing(VkCommandBuffer cb, Image &image, const VkRect2D &area, VkAttachmentLoadOp load)
{
	const VkRenderingAttachmentInfo colour{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, image.view,
	                                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE,
	                                        VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, load,
	                                        VK_ATTACHMENT_STORE_OP_STORE, { { { 0.0f, 0.0f, 0.0f, 1.0f } } } };
	const VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, { { 0, 0 }, { image.w, image.h } },
	                                 1, 0, 1, &colour, nullptr, nullptr };
	const VkViewport viewport{ static_cast<float>(area.offset.x), static_cast<float>(area.offset.y),
	                           static_cast<float>(area.extent.width), static_cast<float>(area.extent.height), 0.0f,
	                           1.0f };

	if (load != VK_ATTACHMENT_LOAD_OP_LOAD)
		image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier(cb, image, DRAWN_IN);
	m_fn.vkCmdBeginRendering(cb, &rendering);
	m_fn.vkCmdSetViewport(cb, 0, 1, &viewport);
	m_fn.vkCmdSetScissor(cb, 0, 1, &area);
}

Image &
Context::window_image(unsigned w, unsigned h)
{
	if (m_window.image && m_window.w == w && m_window.h == h)
		return m_window;
	if (m_window.image) {
		Image old = m_window;

		later([this, old]() mutable { destroy_image(old); });
	}
	m_window = make_image(VK_FORMAT_R8G8B8A8_UNORM, std::max(w, 1u), std::max(h, 1u), 1,
	                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
	                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	return m_window;
}

Image *
Context::picture(uint32_t id) const
{
	const auto found = m_pictures.find(id);

	return found == m_pictures.end() ? nullptr : found->second;
}

} /* namespace odin::vk */
