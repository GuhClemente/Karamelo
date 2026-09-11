// Este arquivo e o unico caminho de video do projeto que ja nasceu portavel:
// tudo que ele usa do sistema vem do SDL3, e nenhuma funcao Vulkan e linkada
// em tempo de build. As tres dependencias de Windows que restavam - windows.h,
// CRITICAL_SECTION e o SEH __try/__except - foram trocadas por equivalentes de
// biblioteca padrao ou isoladas atras de macro, para que Linux e macOS
// precisem apenas de um toolchain, nao de uma reescrita.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include "compat_win32.h"
#endif
#include <stdio.h>
#include <string.h>
#include <mutex>
#include <vector>

// O SEH e uma extensao da Microsoft. Fora dela, a tentativa vira execucao
// direta: uma falha dentro do core derruba o processo em vez de ser contida.
// Nao ha equivalente portavel - sinal de segmentacao nao e excecao de C++ e
// nao pode ser capturado por catch de forma definida - entao o que se pode
// fazer e deixar a diferenca explicita em vez de escondida.
#ifndef VK_TRY
#if defined(_WIN32) && defined(_MSC_VER)
#define VK_TRY        __try
#define VK_EXCEPT_ALL __except (EXCEPTION_EXECUTE_HANDLER)
#else
#define VK_TRY        try
#define VK_EXCEPT_ALL catch (...)
#endif
#endif

// Dynamic loading, not link-time: there is no Vulkan SDK/import-lib vendored
// here (matching the rest of this project's "no external SDK at build time"
// stance). VK_NO_PROTOTYPES makes vulkan.h emit only types/enums/PFN_
// typedefs; every actual function pointer below is resolved at runtime via
// SDL_Vulkan_GetVkGetInstanceProcAddr(), the same way hw_render.cpp resolves
// its own GL functions through SDL_GL_GetProcAddress instead of linking a
// GL loader.
//
// vulkan.h has to come before SDL_vulkan.h: SDL_vulkan.h only forward-declares
// its own minimal VkInstance/VkPhysicalDevice typedefs if it does not detect
// the real vulkan_core.h header guard already defined, to avoid clashing with it.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "libretro.h"
#include "libretro_vulkan.h"
#include "hw_render_vulkan.h"

static void VkHwLog(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	FILE* f = fopen("karamelo.log", "a");
	if (f) { fprintf(f, "[INFO] [HW-VK] %s\n", buf); fclose(f); }
}

// ---------------------------------------------------------------------------
// Dynamically-loaded Vulkan entry points, split by the level they need to be
// fetched at (global/instance/device) - mirrors how the Vulkan loader itself
// works: instance functions are only valid once an instance exists, device
// functions only once a device exists.
// ---------------------------------------------------------------------------
static PFN_vkGetInstanceProcAddr        vkGetInstanceProcAddr_ = NULL;
static PFN_vkCreateInstance             vkCreateInstance_ = NULL;
static PFN_vkEnumerateInstanceVersion   vkEnumerateInstanceVersion_ = NULL;

static PFN_vkDestroyInstance                     vkDestroyInstance_ = NULL;
static PFN_vkEnumeratePhysicalDevices            vkEnumeratePhysicalDevices_ = NULL;
static PFN_vkGetPhysicalDeviceProperties         vkGetPhysicalDeviceProperties_ = NULL;
static PFN_vkGetPhysicalDeviceFeatures           vkGetPhysicalDeviceFeatures_ = NULL;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties_ = NULL;
static PFN_vkGetPhysicalDeviceMemoryProperties   vkGetPhysicalDeviceMemoryProperties_ = NULL;
static PFN_vkCreateDevice                        vkCreateDevice_ = NULL;
static PFN_vkGetDeviceProcAddr                   vkGetDeviceProcAddr_ = NULL;

static PFN_vkDestroyDevice              vkDestroyDevice_ = NULL;
static PFN_vkGetDeviceQueue             vkGetDeviceQueue_ = NULL;
static PFN_vkDeviceWaitIdle             vkDeviceWaitIdle_ = NULL;
static PFN_vkCreateCommandPool          vkCreateCommandPool_ = NULL;
static PFN_vkDestroyCommandPool         vkDestroyCommandPool_ = NULL;
static PFN_vkAllocateCommandBuffers     vkAllocateCommandBuffers_ = NULL;
static PFN_vkFreeCommandBuffers         vkFreeCommandBuffers_ = NULL;
static PFN_vkBeginCommandBuffer         vkBeginCommandBuffer_ = NULL;
static PFN_vkEndCommandBuffer           vkEndCommandBuffer_ = NULL;
static PFN_vkResetCommandBuffer         vkResetCommandBuffer_ = NULL;
static PFN_vkCmdPipelineBarrier         vkCmdPipelineBarrier_ = NULL;
static PFN_vkCmdCopyImageToBuffer       vkCmdCopyImageToBuffer_ = NULL;
static PFN_vkQueueSubmit                vkQueueSubmit_ = NULL;
static PFN_vkCreateFence                vkCreateFence_ = NULL;
static PFN_vkDestroyFence               vkDestroyFence_ = NULL;
static PFN_vkWaitForFences              vkWaitForFences_ = NULL;
static PFN_vkResetFences                vkResetFences_ = NULL;
static PFN_vkCreateBuffer               vkCreateBuffer_ = NULL;
static PFN_vkDestroyBuffer              vkDestroyBuffer_ = NULL;
static PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements_ = NULL;
static PFN_vkAllocateMemory             vkAllocateMemory_ = NULL;
static PFN_vkFreeMemory                 vkFreeMemory_ = NULL;
static PFN_vkBindBufferMemory           vkBindBufferMemory_ = NULL;
static PFN_vkMapMemory                  vkMapMemory_ = NULL;
static PFN_vkUnmapMemory                vkUnmapMemory_ = NULL;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static VkInstance       g_instance = VK_NULL_HANDLE;
static VkPhysicalDevice g_gpu = VK_NULL_HANDLE;
static VkDevice         g_device = VK_NULL_HANDLE;
static VkQueue          g_queue = VK_NULL_HANDLE;
static uint32_t         g_queue_family = 0;
static VkPhysicalDeviceMemoryProperties g_mem_props;

static VkCommandPool    g_cmd_pool = VK_NULL_HANDLE;
static VkCommandBuffer  g_cmd_buf = VK_NULL_HANDLE;
static VkFence          g_fence = VK_NULL_HANDLE;

static VkBuffer         g_staging_buf = VK_NULL_HANDLE;
static VkDeviceMemory   g_staging_mem = VK_NULL_HANDLE;
static VkDeviceSize     g_staging_size = 0;
static unsigned         g_surface_w = 0;
static unsigned         g_surface_h = 0;

// recursive_mutex, e nao mutex: o CRITICAL_SECTION que estava aqui e
// recursivo por definicao, e o core pode chamar lock_queue enquanto o
// frontend ja segura a fila para a leitura do quadro. Trocar por um mutex
// simples transformaria isso em deadlock so em algumas maquinas.
static std::recursive_mutex g_queue_lock;
static bool                 g_queue_lock_init = false;

static struct retro_hw_render_callback g_hw_cb;
static bool g_hw_active = false;
static bool g_context_live = false;
static bool g_vk_ready = false;

// The image/format/layout/semaphores the core handed us via set_image, for
// the next readback - valid until the next call to VkHwReadPixels consumes it.
static struct retro_vulkan_image g_pending_image;
static bool     g_pending_image_valid = false;
static uint32_t g_pending_num_semaphores = 0;
static VkSemaphore g_pending_semaphores[8];
static uint32_t g_pending_src_queue_family = VK_QUEUE_FAMILY_IGNORED;
static VkSemaphore g_pending_signal_semaphore = VK_NULL_HANDLE;

static std::vector<VkCommandBuffer> g_pending_core_cmds;

static struct retro_hw_render_interface_vulkan g_iface;

// ---------------------------------------------------------------------------
// Negociacao de contexto
// ---------------------------------------------------------------------------
// O core entrega esta interface para escolher ele mesmo o dispositivo fisico,
// as extensoes, as features e a fila. Antes de existir, o frontend recusava
// Vulkan para quem pedisse negociacao (PPSSPP, Flycast, PCSX2), e os tres
// caiam para OpenGL.
static const struct retro_hw_render_context_negotiation_interface_vulkan* g_negotiation = NULL;
// Quem criou o VkDevice decide quem chama destroy_device no fim. O device em
// si e sempre destruido pelo frontend - a especificacao e explicita: "Device
// provided to frontend is owned by the frontend" -, o que o core libera em
// destroy_device sao os recursos auxiliares dele, que vivem nesse device e
// portanto precisam sair antes.
static bool g_core_created_device = false;
static bool g_core_created_instance = false;

void VkHwSetNegotiationInterface(const void* iface)
{
	g_negotiation = (const struct retro_hw_render_context_negotiation_interface_vulkan*)iface;
}

void VkHwClearNegotiationInterface()
{
	g_negotiation = NULL;
}

// Envelopes exigidos pela v2: o core nao chama vkCreateInstance/vkCreateDevice
// direto, chama estes, para que o frontend possa acrescentar o que precisa ao
// create_info antes de repassar. Nao acrescentamos nada - este backend nunca
// apresenta pela swapchain, so le o quadro de volta - mas o ponto de extensao
// fica onde a especificacao manda.
static VkInstance VkCreateInstanceWrapper(void* opaque, const VkInstanceCreateInfo* create_info)
{
	(void)opaque;
	VkInstance inst = VK_NULL_HANDLE;
	if (!create_info || !vkCreateInstance_) return VK_NULL_HANDLE;
	if (vkCreateInstance_(create_info, NULL, &inst) != VK_SUCCESS) return VK_NULL_HANDLE;
	return inst;
}

static VkDevice VkCreateDeviceWrapper(VkPhysicalDevice gpu, void* opaque, const VkDeviceCreateInfo* create_info)
{
	(void)opaque;
	VkDevice dev = VK_NULL_HANDLE;
	if (!create_info || !vkCreateDevice_) return VK_NULL_HANDLE;
	if (vkCreateDevice_(gpu, create_info, NULL, &dev) != VK_SUCCESS) return VK_NULL_HANDLE;
	return dev;
}

static unsigned NegotiationVersion()
{
	return g_negotiation ? g_negotiation->interface_version : 0;
}

// ---------------------------------------------------------------------------
// retro_hw_render_interface_vulkan callbacks - see libretro_vulkan.h for the
// full contract each of these has to honor.
// ---------------------------------------------------------------------------
static void VkCb_SetImage(void* handle, const struct retro_vulkan_image* image,
	uint32_t num_semaphores, const VkSemaphore* semaphores, uint32_t src_queue_family)
{
	(void)handle;
	if (!image) return;
	g_pending_image = *image;
	g_pending_image_valid = true;
	g_pending_num_semaphores = (num_semaphores > 8) ? 8 : num_semaphores;
	for (uint32_t i = 0; i < g_pending_num_semaphores; i++) g_pending_semaphores[i] = semaphores[i];
	g_pending_src_queue_family = src_queue_family;
}

static uint32_t VkCb_GetSyncIndex(void* handle)
{
	(void)handle;
	// No real swapchain here (we read back to CPU instead of presenting), so
	// there is only ever one "buffer" in flight - readback is fully
	// synchronous (see VkHwReadPixels's vkWaitForFences). Index 0 is the only
	// value that will ever be returned, matching get_sync_index_mask below.
	return 0;
}

static uint32_t VkCb_GetSyncIndexMask(void* handle)
{
	(void)handle;
	return 1; // only bit 0 (index 0) exists
}

static void VkCb_SetCommandBuffers(void* handle, uint32_t num_cmd, const VkCommandBuffer* cmd)
{
	(void)handle;
	g_pending_core_cmds.assign(cmd, cmd + num_cmd);
}

static void VkCb_WaitSyncIndex(void* handle)
{
	(void)handle;
	// Synchronous readback already waits on g_fence every frame - by the time
	// any core code could call this, the previous frame's GPU work (and our
	// own copy-out) is already known complete.
}

static void VkCb_LockQueue(void* handle)
{
	(void)handle;
	if (g_queue_lock_init) g_queue_lock.lock();
}

static void VkCb_UnlockQueue(void* handle)
{
	(void)handle;
	if (g_queue_lock_init) g_queue_lock.unlock();
}

static void VkCb_SetSignalSemaphore(void* handle, VkSemaphore semaphore)
{
	(void)handle;
	g_pending_signal_semaphore = semaphore;
}

// ---------------------------------------------------------------------------

static void* VkGetProc(const char* name)
{
	return (void*)vkGetInstanceProcAddr_(g_instance, name);
}

static void* VkGetDeviceProc(const char* name)
{
	return (void*)vkGetDeviceProcAddr_(g_device, name);
}

static bool LoadInstanceFunctions()
{
	vkDestroyInstance_ = (PFN_vkDestroyInstance)VkGetProc("vkDestroyInstance");
	vkEnumeratePhysicalDevices_ = (PFN_vkEnumeratePhysicalDevices)VkGetProc("vkEnumeratePhysicalDevices");
	vkGetPhysicalDeviceProperties_ = (PFN_vkGetPhysicalDeviceProperties)VkGetProc("vkGetPhysicalDeviceProperties");
	vkGetPhysicalDeviceFeatures_ = (PFN_vkGetPhysicalDeviceFeatures)VkGetProc("vkGetPhysicalDeviceFeatures");
	vkGetPhysicalDeviceQueueFamilyProperties_ = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)VkGetProc("vkGetPhysicalDeviceQueueFamilyProperties");
	vkGetPhysicalDeviceMemoryProperties_ = (PFN_vkGetPhysicalDeviceMemoryProperties)VkGetProc("vkGetPhysicalDeviceMemoryProperties");
	vkCreateDevice_ = (PFN_vkCreateDevice)VkGetProc("vkCreateDevice");
	vkGetDeviceProcAddr_ = (PFN_vkGetDeviceProcAddr)VkGetProc("vkGetDeviceProcAddr");

	return vkDestroyInstance_ && vkEnumeratePhysicalDevices_ && vkGetPhysicalDeviceProperties_ &&
	       vkGetPhysicalDeviceFeatures_ &&
	       vkGetPhysicalDeviceQueueFamilyProperties_ && vkGetPhysicalDeviceMemoryProperties_ &&
	       vkCreateDevice_ && vkGetDeviceProcAddr_;
}

static bool LoadDeviceFunctions()
{
	vkDestroyDevice_ = (PFN_vkDestroyDevice)VkGetDeviceProc("vkDestroyDevice");
	vkGetDeviceQueue_ = (PFN_vkGetDeviceQueue)VkGetDeviceProc("vkGetDeviceQueue");
	vkDeviceWaitIdle_ = (PFN_vkDeviceWaitIdle)VkGetDeviceProc("vkDeviceWaitIdle");
	vkCreateCommandPool_ = (PFN_vkCreateCommandPool)VkGetDeviceProc("vkCreateCommandPool");
	vkDestroyCommandPool_ = (PFN_vkDestroyCommandPool)VkGetDeviceProc("vkDestroyCommandPool");
	vkAllocateCommandBuffers_ = (PFN_vkAllocateCommandBuffers)VkGetDeviceProc("vkAllocateCommandBuffers");
	vkFreeCommandBuffers_ = (PFN_vkFreeCommandBuffers)VkGetDeviceProc("vkFreeCommandBuffers");
	vkBeginCommandBuffer_ = (PFN_vkBeginCommandBuffer)VkGetDeviceProc("vkBeginCommandBuffer");
	vkEndCommandBuffer_ = (PFN_vkEndCommandBuffer)VkGetDeviceProc("vkEndCommandBuffer");
	vkResetCommandBuffer_ = (PFN_vkResetCommandBuffer)VkGetDeviceProc("vkResetCommandBuffer");
	vkCmdPipelineBarrier_ = (PFN_vkCmdPipelineBarrier)VkGetDeviceProc("vkCmdPipelineBarrier");
	vkCmdCopyImageToBuffer_ = (PFN_vkCmdCopyImageToBuffer)VkGetDeviceProc("vkCmdCopyImageToBuffer");
	vkQueueSubmit_ = (PFN_vkQueueSubmit)VkGetDeviceProc("vkQueueSubmit");
	vkCreateFence_ = (PFN_vkCreateFence)VkGetDeviceProc("vkCreateFence");
	vkDestroyFence_ = (PFN_vkDestroyFence)VkGetDeviceProc("vkDestroyFence");
	vkWaitForFences_ = (PFN_vkWaitForFences)VkGetDeviceProc("vkWaitForFences");
	vkResetFences_ = (PFN_vkResetFences)VkGetDeviceProc("vkResetFences");
	vkCreateBuffer_ = (PFN_vkCreateBuffer)VkGetDeviceProc("vkCreateBuffer");
	vkDestroyBuffer_ = (PFN_vkDestroyBuffer)VkGetDeviceProc("vkDestroyBuffer");
	vkGetBufferMemoryRequirements_ = (PFN_vkGetBufferMemoryRequirements)VkGetDeviceProc("vkGetBufferMemoryRequirements");
	vkAllocateMemory_ = (PFN_vkAllocateMemory)VkGetDeviceProc("vkAllocateMemory");
	vkFreeMemory_ = (PFN_vkFreeMemory)VkGetDeviceProc("vkFreeMemory");
	vkBindBufferMemory_ = (PFN_vkBindBufferMemory)VkGetDeviceProc("vkBindBufferMemory");
	vkMapMemory_ = (PFN_vkMapMemory)VkGetDeviceProc("vkMapMemory");
	vkUnmapMemory_ = (PFN_vkUnmapMemory)VkGetDeviceProc("vkUnmapMemory");

	return vkDestroyDevice_ && vkGetDeviceQueue_ && vkDeviceWaitIdle_ && vkCreateCommandPool_ &&
	       vkDestroyCommandPool_ && vkAllocateCommandBuffers_ && vkFreeCommandBuffers_ &&
	       vkBeginCommandBuffer_ && vkEndCommandBuffer_ && vkResetCommandBuffer_ &&
	       vkCmdPipelineBarrier_ && vkCmdCopyImageToBuffer_ && vkQueueSubmit_ && vkCreateFence_ &&
	       vkDestroyFence_ && vkWaitForFences_ && vkResetFences_ && vkCreateBuffer_ &&
	       vkDestroyBuffer_ && vkGetBufferMemoryRequirements_ && vkAllocateMemory_ &&
	       vkFreeMemory_ && vkBindBufferMemory_ && vkMapMemory_ && vkUnmapMemory_;
}

// So o loader: biblioteca, vkGetInstanceProcAddr e as duas funcoes de nivel
// global. Separado da criacao do dispositivo porque a disponibilidade e
// consultada muito cedo - no arranque do app e ao montar a lista de drivers no
// menu - e criar o dispositivo definitivo ali seria criar antes de o core
// existir, que e exatamente o que impedia a negociacao de funcionar.
static bool VkLoadLoader()
{
	if (vkCreateInstance_) return true;

	if (!SDL_Vulkan_LoadLibrary(NULL))
	{
		VkHwLog("SDL_Vulkan_LoadLibrary falhou: %s", SDL_GetError());
		return false;
	}

	vkGetInstanceProcAddr_ = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
	if (!vkGetInstanceProcAddr_)
	{
		VkHwLog("SDL_Vulkan_GetVkGetInstanceProcAddr falhou: %s", SDL_GetError());
		return false;
	}

	vkCreateInstance_ = (PFN_vkCreateInstance)vkGetInstanceProcAddr_(NULL, "vkCreateInstance");
	vkEnumerateInstanceVersion_ = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr_(NULL, "vkEnumerateInstanceVersion");
	if (!vkCreateInstance_)
	{
		VkHwLog("vkCreateInstance indisponivel");
		return false;
	}
	return true;
}

// Versao de API a pedir, respeitando o que o loader instalado suporta e o que
// o core pediu em get_application_info. A especificacao permite ao frontend
// subir a versao se precisar, nunca descer abaixo do que o core exige.
static uint32_t PickApiVersion(const VkApplicationInfo* from_core)
{
	uint32_t api_version = VK_API_VERSION_1_1;
	if (from_core && from_core->apiVersion > api_version)
		api_version = from_core->apiVersion;

	if (vkEnumerateInstanceVersion_)
	{
		uint32_t supported = 0;
		if (vkEnumerateInstanceVersion_(&supported) == VK_SUCCESS && supported < api_version)
			api_version = supported;
	}
	return api_version;
}

// Cria instancia, dispositivo e fila de verdade, consultando a negociacao.
// Idempotente: a primeira chamada monta tudo, as seguintes retornam.
static bool VkHwEnsureDevice()
{
	if (g_vk_ready) return true;
	if (!VkLoadLoader()) return false;

	// O core fala primeiro: se tem negociacao e sabe dizer de que versao de
	// Vulkan precisa, e essa que vale.
	const VkApplicationInfo* core_app = NULL;
	if (g_negotiation && g_negotiation->get_application_info)
		core_app = g_negotiation->get_application_info();

	uint32_t api_version = PickApiVersion(core_app);

	VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app_info.pApplicationName = "Karamelo";
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "Karamelo";
	app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion = api_version;
	if (core_app)
	{
		// O nome e a versao do core valem mais que os nossos: alguns drivers
		// aplicam correcoes por aplicativo a partir deles.
		app_info = *core_app;
		app_info.apiVersion = api_version;
	}

	VkInstanceCreateInfo inst_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	inst_info.pApplicationInfo = &app_info;
	// No extensions requested: we never present through Vulkan (no surface/
	// swapchain), only render off-screen and copy back to CPU - the same
	// division of labour hw_render.cpp's OpenGL path already uses.

	// v2: o core pode querer criar a propria instancia, para habilitar
	// extensoes de nivel de instancia que a v1 nao tinha como pedir.
	g_core_created_instance = false;
	if (NegotiationVersion() >= 2 && g_negotiation->create_instance)
	{
		g_instance = g_negotiation->create_instance(vkGetInstanceProcAddr_, &app_info,
			VkCreateInstanceWrapper, NULL);
		if (g_instance != VK_NULL_HANDLE)
		{
			g_core_created_instance = true;
			VkHwLog("instancia criada pelo core (negociacao v2)");
		}
		else
		{
			VkHwLog("create_instance do core devolveu VK_NULL_HANDLE - criando a instancia padrao");
		}
	}

	if (g_instance == VK_NULL_HANDLE &&
	    vkCreateInstance_(&inst_info, NULL, &g_instance) != VK_SUCCESS)
	{
		VkHwLog("vkCreateInstance falhou");
		return false;
	}

	if (!LoadInstanceFunctions())
	{
		VkHwLog("falha ao carregar funcoes de instancia");
		VkHwShutdown();
		return false;
	}

	uint32_t gpu_count = 0;
	vkEnumeratePhysicalDevices_(g_instance, &gpu_count, NULL);
	if (gpu_count == 0)
	{
		VkHwLog("nenhuma GPU com suporte a Vulkan");
		VkHwShutdown();
		return false;
	}
	std::vector<VkPhysicalDevice> gpus(gpu_count);
	vkEnumeratePhysicalDevices_(g_instance, &gpu_count, gpus.data());

	// Prefer a discrete GPU; fall back to whatever is first (integrated,
	// software rasterizer via lavapipe/etc - still correct, just slower).
	g_gpu = gpus[0];
	for (VkPhysicalDevice gpu : gpus)
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties_(gpu, &props);
		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { g_gpu = gpu; break; }
	}

	// --- Negociacao: o core escolhe o dispositivo ---------------------------
	//
	// E aqui que mora a diferenca. O core recebe a instancia e a GPU que
	// preferimos e devolve dispositivo, fila e indice de familia ja criados
	// com as extensoes e features de que o renderizador dele precisa. Entregar
	// um dispositivo generico no lugar disso e o que fazia PPSSPP, Flycast e
	// PCSX2 falharem dentro do proprio setup - e o que levou o frontend a
	// recusar Vulkan para eles.
	g_core_created_device = false;
	if (g_negotiation)
	{
		struct retro_vulkan_context ctx;
		memset(&ctx, 0, sizeof(ctx));

		bool ok = false;
		if (NegotiationVersion() >= 2 && g_negotiation->create_device2)
		{
			ok = g_negotiation->create_device2(&ctx, g_instance, g_gpu, VK_NULL_HANDLE,
				vkGetInstanceProcAddr_, VkCreateDeviceWrapper, NULL);
			if (!ok)
			{
				// A especificacao pede exatamente isto: se o core recusa a GPU
				// que escolhemos, oferecer VK_NULL_HANDLE e deixar ele decidir.
				VkHwLog("create_device2 recusou a GPU escolhida - repetindo sem indicar GPU");
				memset(&ctx, 0, sizeof(ctx));
				ok = g_negotiation->create_device2(&ctx, g_instance, VK_NULL_HANDLE, VK_NULL_HANDLE,
					vkGetInstanceProcAddr_, VkCreateDeviceWrapper, NULL);
			}
		}
		else if (g_negotiation->create_device)
		{
			// v1. Nao exigimos extensao, camada nem feature nenhuma: este
			// backend nao apresenta pela swapchain, so le o quadro de volta.
			// O ponteiro de features precisa existir mesmo assim - ha core que
			// o desreferencia sem checar.
			VkPhysicalDeviceFeatures required;
			memset(&required, 0, sizeof(required));
			ok = g_negotiation->create_device(&ctx, g_instance, g_gpu, VK_NULL_HANDLE,
				vkGetInstanceProcAddr_, NULL, 0, NULL, 0, &required);
		}

		if (ok && ctx.device != VK_NULL_HANDLE && ctx.queue != VK_NULL_HANDLE)
		{
			g_gpu = ctx.gpu != VK_NULL_HANDLE ? ctx.gpu : g_gpu;
			g_device = ctx.device;
			g_queue = ctx.queue;
			g_queue_family = ctx.queue_family_index;
			g_core_created_device = true;
			VkHwLog("dispositivo negociado pelo core (v%u, familia de fila %u)",
				NegotiationVersion(), g_queue_family);
		}
		else if (ok)
		{
			VkHwLog("negociacao devolveu sucesso com contexto incompleto - usando o dispositivo padrao");
		}
		else
		{
			// Falhar aqui nao e fatal: a especificacao manda o frontend seguir
			// como se create_device nunca tivesse sido chamada.
			VkHwLog("negociacao falhou - usando o dispositivo padrao");
		}
	}

	VkPhysicalDeviceProperties gpu_props;
	vkGetPhysicalDeviceProperties_(g_gpu, &gpu_props);
	vkGetPhysicalDeviceMemoryProperties_(g_gpu, &g_mem_props);

	// Without a context negotiation interface (not implemented yet - see
	// core_runner.cpp's CB_Environment, which currently refuses
	// RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE outright),
	// a core has no way to ask for the specific features/extensions its
	// renderer needs, and has to make do with whatever "default" device we
	// hand it. Enabling every feature this GPU actually supports, rather than
	// leaving VkPhysicalDeviceFeatures zeroed, is the difference between that
	// default device being usable by a real renderer (flycast confirmed
	// needing this) versus one that silently assumes a feature is on when it
	// is not and crashes deep in its own pipeline setup instead of failing
	// a validation check.
	VkPhysicalDeviceFeatures gpu_features;
	if (!g_core_created_device)
		vkGetPhysicalDeviceFeatures_(g_gpu, &gpu_features);
	else
		memset(&gpu_features, 0, sizeof(gpu_features));

	// Tudo daqui ate o vkCreateDevice e o caminho padrao: so roda quando nao
	// houve negociacao, ou quando ela falhou. Com o dispositivo vindo do core,
	// a fila e a familia ja vieram junto no retro_vulkan_context.
	if (!g_core_created_device)
	{
	// The interface requires a queue supporting both GRAPHICS and COMPUTE.
	uint32_t qf_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties_(g_gpu, &qf_count, NULL);
	std::vector<VkQueueFamilyProperties> qfs(qf_count);
	vkGetPhysicalDeviceQueueFamilyProperties_(g_gpu, &qf_count, qfs.data());

	int chosen = -1;
	for (uint32_t i = 0; i < qf_count; i++)
	{
		if ((qfs[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
			(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
		{
			chosen = (int)i;
			break;
		}
	}
	if (chosen < 0)
	{
		VkHwLog("nenhuma fila com suporte a GRAPHICS+COMPUTE");
		VkHwShutdown();
		return false;
	}
	g_queue_family = (uint32_t)chosen;

	float priority = 1.0f;
	VkDeviceQueueCreateInfo qinfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	qinfo.queueFamilyIndex = g_queue_family;
	qinfo.queueCount = 1;
	qinfo.pQueuePriorities = &priority;

	VkDeviceCreateInfo dev_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	dev_info.queueCreateInfoCount = 1;
	dev_info.pQueueCreateInfos = &qinfo;
	dev_info.pEnabledFeatures = &gpu_features;

	if (vkCreateDevice_(g_gpu, &dev_info, NULL, &g_device) != VK_SUCCESS)
	{
		VkHwLog("vkCreateDevice falhou");
		VkHwShutdown();
		return false;
	}
	}

	if (!LoadDeviceFunctions())
	{
		VkHwLog("falha ao carregar funcoes de device");
		VkHwShutdown();
		return false;
	}

	// Com dispositivo negociado a fila ja veio pronta do core; pedir outra por
	// vkGetDeviceQueue devolveria uma fila que o core nao conhece e cujo acesso
	// nao esta coberto pelos lock_queue/unlock_queue dele.
	if (!g_core_created_device)
		vkGetDeviceQueue_(g_device, g_queue_family, 0, &g_queue);

	g_queue_lock_init = true;

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = g_queue_family;
	if (vkCreateCommandPool_(g_device, &pool_info, NULL, &g_cmd_pool) != VK_SUCCESS)
	{
		VkHwLog("vkCreateCommandPool falhou");
		VkHwShutdown();
		return false;
	}

	VkCommandBufferAllocateInfo cmd_alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cmd_alloc.commandPool = g_cmd_pool;
	cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc.commandBufferCount = 1;
	if (vkAllocateCommandBuffers_(g_device, &cmd_alloc, &g_cmd_buf) != VK_SUCCESS)
	{
		VkHwLog("vkAllocateCommandBuffers falhou");
		VkHwShutdown();
		return false;
	}

	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	if (vkCreateFence_(g_device, &fence_info, NULL, &g_fence) != VK_SUCCESS)
	{
		VkHwLog("vkCreateFence falhou");
		VkHwShutdown();
		return false;
	}

	g_iface.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
	g_iface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
	g_iface.handle = NULL;
	g_iface.instance = g_instance;
	g_iface.gpu = g_gpu;
	g_iface.device = g_device;
	g_iface.get_device_proc_addr = vkGetDeviceProcAddr_;
	g_iface.get_instance_proc_addr = vkGetInstanceProcAddr_;
	g_iface.queue = g_queue;
	g_iface.queue_index = g_queue_family;
	g_iface.set_image = VkCb_SetImage;
	g_iface.get_sync_index = VkCb_GetSyncIndex;
	g_iface.get_sync_index_mask = VkCb_GetSyncIndexMask;
	g_iface.set_command_buffers = VkCb_SetCommandBuffers;
	g_iface.wait_sync_index = VkCb_WaitSyncIndex;
	g_iface.lock_queue = VkCb_LockQueue;
	g_iface.unlock_queue = VkCb_UnlockQueue;
	g_iface.set_signal_semaphore = VkCb_SetSignalSemaphore;

	VkHwLog("dispositivo Vulkan criado: %s (fila %u)", gpu_props.deviceName, g_queue_family);

	g_vk_ready = true;
	return true;
}

void VkHwShutdown()
{
	if (g_device)
	{
		if (vkDeviceWaitIdle_) vkDeviceWaitIdle_(g_device);

		if (g_staging_buf && vkDestroyBuffer_) { vkDestroyBuffer_(g_device, g_staging_buf, NULL); g_staging_buf = VK_NULL_HANDLE; }
		if (g_staging_mem && vkFreeMemory_) { vkFreeMemory_(g_device, g_staging_mem, NULL); g_staging_mem = VK_NULL_HANDLE; }
		if (g_fence && vkDestroyFence_) { vkDestroyFence_(g_device, g_fence, NULL); g_fence = VK_NULL_HANDLE; }
		if (g_cmd_buf && vkFreeCommandBuffers_ && g_cmd_pool) { vkFreeCommandBuffers_(g_device, g_cmd_pool, 1, &g_cmd_buf); g_cmd_buf = VK_NULL_HANDLE; }
		if (g_cmd_pool && vkDestroyCommandPool_) { vkDestroyCommandPool_(g_device, g_cmd_pool, NULL); g_cmd_pool = VK_NULL_HANDLE; }

		// destroy_device antes de vkDestroyDevice, nessa ordem: o que o core
		// libera ai sao recursos auxiliares dele, e todos vivem neste
		// dispositivo. A especificacao tambem exige que isto aconteca antes de
		// a VkInstance do frontend morrer, e que seja chamado mesmo quando
		// context_reset nunca chegou a rodar - por isso o gatilho e ter havido
		// create_device com sucesso, nao o contexto estar vivo.
		//
		// Envolto no mesmo SEH do resto: um core que estoura aqui nao pode
		// levar junto o desligamento do dispositivo, senao vaza tudo.
		if (g_core_created_device && g_negotiation && g_negotiation->destroy_device)
		{
			VK_TRY { g_negotiation->destroy_device(); }
			VK_EXCEPT_ALL
			{
				VkHwLog("excecao dentro do destroy_device do core - ignorada");
			}
		}

		if (vkDestroyDevice_) vkDestroyDevice_(g_device, NULL);
		g_device = VK_NULL_HANDLE;
		g_core_created_device = false;
	}

	if (g_instance)
	{
		// Vale tanto para a instancia que criamos quanto para a que veio do
		// create_instance do core: a v2 diz que a VkInstance devolvida
		// pertence ao frontend.
		if (vkDestroyInstance_) vkDestroyInstance_(g_instance, NULL);
		g_instance = VK_NULL_HANDLE;
		g_core_created_instance = false;
	}

	g_gpu = VK_NULL_HANDLE;
	g_queue = VK_NULL_HANDLE;
	g_vk_ready = false;
	g_hw_active = false;
	g_context_live = false;
	g_surface_w = g_surface_h = 0;
	g_staging_size = 0;
	g_pending_image_valid = false;
	g_pending_core_cmds.clear();
	memset(&g_hw_cb, 0, sizeof(g_hw_cb));
}

// "Ativo" passou a significar "o core pediu Vulkan", nao "o dispositivo ja
// existe". A criacao foi adiada de proposito (ver VkHwSetRenderCallback), e
// core_runner.cpp usa este predicado para decidir se chama VkHwEnsureSurface e
// VkHwContextReset - se ele exigisse o dispositivo pronto, nenhum dos dois
// seria chamado e o dispositivo nunca seria criado.
bool VkHwIsActive() { return g_hw_active; }

static int g_vk_probe_result = -1;

// Sonda: existe Vulkan nesta maquina? Cria uma instancia descartavel so para
// perguntar, e a destroi em seguida.
//
// Antes, esta pergunta era respondida criando o dispositivo definitivo - e ela
// e feita cedo demais para isso: no arranque do app (main_win32.cpp) e ao
// montar a lista de drivers do menu, ambos antes de qualquer core existir. O
// dispositivo ficava pronto antes de o core ter chance de entregar a interface
// de negociacao, e a negociacao chegava sempre tarde demais para valer.
bool VkHwIsAvailable()
{
	if (g_vk_probe_result >= 0) return g_vk_probe_result != 0;
	g_vk_probe_result = 0;

	if (!VkLoadLoader()) return false;

	VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app_info.pApplicationName = "Karamelo";
	app_info.pEngineName = "Karamelo";
	app_info.apiVersion = PickApiVersion(NULL);

	VkInstanceCreateInfo inst_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	inst_info.pApplicationInfo = &app_info;

	VkInstance probe = VK_NULL_HANDLE;
	if (vkCreateInstance_(&inst_info, NULL, &probe) != VK_SUCCESS)
	{
		VkHwLog("sonda: vkCreateInstance falhou - Vulkan indisponivel");
		return false;
	}

	PFN_vkEnumeratePhysicalDevices enum_gpus =
		(PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr_(probe, "vkEnumeratePhysicalDevices");
	PFN_vkDestroyInstance destroy_inst =
		(PFN_vkDestroyInstance)vkGetInstanceProcAddr_(probe, "vkDestroyInstance");

	uint32_t gpu_count = 0;
	if (enum_gpus) enum_gpus(probe, &gpu_count, NULL);
	if (destroy_inst) destroy_inst(probe, NULL);

	if (gpu_count == 0)
	{
		VkHwLog("sonda: nenhuma GPU com suporte a Vulkan");
		return false;
	}

	g_vk_probe_result = 1;
	return true;
}

const void* VkHwGetRenderInterface()
{
	// VkHwIsActive(), not g_vk_ready alone: g_vk_ready stays true for the rest
	// of the process once a Vulkan device has ever been built (VkHwContextDestroy
	// only clears g_hw_active), so gating on it meant this kept handing out the
	// Vulkan interface to cores that never asked for a Vulkan context. Combined
	// with core_runner.cpp's GET_HW_RENDER_INTERFACE trying D3D11 first, a core
	// could be handed the *other* backend's struct - which starts with an
	// interface_type field saying so, and which the core would then misread.
	if (!VkHwIsActive() || !g_vk_ready) return NULL;
	return &g_iface;
}

bool VkHwSetRenderCallback(struct retro_hw_render_callback* cb)
{
	if (!cb) return false;
	if (cb->context_type != RETRO_HW_CONTEXT_VULKAN)
	{
		VkHwLog("core pediu contexto tipo %d - nao e Vulkan", (int)cb->context_type);
		return false;
	}

	if (!VkHwIsAvailable()) return false;

	// Repare que o dispositivo NAO e criado aqui, so aceito o pedido. Ha core -
	// PPSSPP e o caso medido - que chama SET_HW_RENDER antes de entregar a
	// interface de negociacao. Criando o dispositivo neste ponto, a negociacao
	// chegaria com tudo ja decidido e seria inutil, que era exatamente o estado
	// anterior. A criacao acontece em VkHwEnsureDevice(), no primeiro uso real,
	// quando o core ja falou tudo o que tinha para falar.
	g_hw_cb = *cb;
	g_hw_active = true;
	VkHwLog("hardware render Vulkan aceito (depth=%d, stencil=%d) - dispositivo sera criado ao carregar o jogo",
		cb->depth ? 1 : 0, cb->stencil ? 1 : 0);
	return true;
}

static uint32_t FindMemoryType(uint32_t type_bits, VkMemoryPropertyFlags want)
{
	for (uint32_t i = 0; i < g_mem_props.memoryTypeCount; i++)
	{
		if ((type_bits & (1u << i)) &&
			(g_mem_props.memoryTypes[i].propertyFlags & want) == want)
			return i;
	}
	return UINT32_MAX;
}

bool VkHwEnsureSurface(unsigned width, unsigned height)
{
	// Primeiro uso real do dispositivo no carregamento de um jogo: e aqui, ou
	// em VkHwContextReset logo abaixo, que ele finalmente e criado.
	if (!VkHwEnsureDevice()) return false;

	if (!g_vk_ready || width == 0 || height == 0) return false;

	VkDeviceSize needed = (VkDeviceSize)width * height * 4;
	if (g_staging_buf && needed <= g_staging_size)
	{
		g_surface_w = width;
		g_surface_h = height;
		return true;
	}

	if (g_staging_buf) { vkDestroyBuffer_(g_device, g_staging_buf, NULL); g_staging_buf = VK_NULL_HANDLE; }
	if (g_staging_mem) { vkFreeMemory_(g_device, g_staging_mem, NULL); g_staging_mem = VK_NULL_HANDLE; }

	VkBufferCreateInfo buf_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	buf_info.size = needed;
	buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer_(g_device, &buf_info, NULL, &g_staging_buf) != VK_SUCCESS)
	{
		VkHwLog("vkCreateBuffer (staging) falhou");
		return false;
	}

	VkMemoryRequirements reqs;
	vkGetBufferMemoryRequirements_(g_device, g_staging_buf, &reqs);
	uint32_t mem_type = FindMemoryType(reqs.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (mem_type == UINT32_MAX)
	{
		VkHwLog("nenhum tipo de memoria host-visible compativel");
		vkDestroyBuffer_(g_device, g_staging_buf, NULL);
		g_staging_buf = VK_NULL_HANDLE;
		return false;
	}

	VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc_info.allocationSize = reqs.size;
	alloc_info.memoryTypeIndex = mem_type;
	if (vkAllocateMemory_(g_device, &alloc_info, NULL, &g_staging_mem) != VK_SUCCESS)
	{
		VkHwLog("vkAllocateMemory (staging) falhou");
		vkDestroyBuffer_(g_device, g_staging_buf, NULL);
		g_staging_buf = VK_NULL_HANDLE;
		return false;
	}
	vkBindBufferMemory_(g_device, g_staging_buf, g_staging_mem, 0);

	g_staging_size = reqs.size;
	g_surface_w = width;
	g_surface_h = height;
	VkHwLog("staging buffer %ux%u pronto (%llu bytes)", width, height, (unsigned long long)needed);
	return true;
}

bool VkHwContextReset()
{
	if (!g_hw_active) return false;
	if (!VkHwEnsureDevice())
	{
		VkHwLog("nao foi possivel criar o dispositivo Vulkan - hw-render desativado nesta sessao");
		g_hw_active = false;
		return false;
	}
	if (g_hw_cb.context_reset)
	{
		// Some cores (e.g. Flycast) call context_reset() assuming a device
		// created via their own negotiation interface (create_device/create_device2,
		// RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE - not yet
		// implemented here) and crash with a null-pointer access deep in their own
		// renderer when handed our generic device instead. Wrap this call in SEH so
		// that failure is a contained "hw-render Vulkan falhou" instead of taking
		// down the whole process, matching VkHwContextDestroy's own guard below.
		// The caller (CoreLoadGame) must treat a false return as a failed load -
		// letting retro_run() keep pumping a core that half-crashed mid-init just
		// moves the same fault a few frames later.
		VK_TRY
		{
			g_hw_cb.context_reset();
			g_context_live = true;
			VkHwLog("context_reset entregue ao core");
			return true;
		}
		VK_EXCEPT_ALL
		{
			VkHwLog("context_reset do core lancou excecao - core provavelmente precisa de negociacao de contexto Vulkan (nao implementada); desativando hw-render Vulkan para esta sessao");
			// Full teardown, not just clearing the ready flags: leaving
			// g_instance/g_device/g_staging_buf alive here would leak them
			// (the next VkHwInit() recreates everything from scratch without
			// ever destroying these), and worse, VkHwEnsureSurface()'s reuse
			// fast path could then hand the *new* device a staging buffer
			// still bound to this now-orphaned one - invalid cross-device
			// Vulkan usage. VkHwShutdown() is safe to call here: every
			// vkDestroy*/vkFree* call inside it is already null-checked.
			g_hw_active = false;
			VkHwShutdown();
			return false;
		}
	}
	return true;
}

// context_destroy pertence ao core, e nem todo core volta dele. O PCSX2 nao
// volta: medido, gira indefinidamente esperando o estado da VM transicionar
// para Paused/Stopped, mas retro_unload_game ja foi chamado e ja encerrou
// as threads da VM.
//
// Para cores conhecidos por esse comportamento (como PCSX2), pulamos diretamente
// via VkHwSetSkipContextDestroy(true). Para qualquer outro core que venha a travar,
// chamamos em uma thread dedicada com prazo de 3s para nao pendurar o processo.
static DWORD WINAPI VkDestroyThreadProc(LPVOID param)
{
	retro_hw_context_reset_t fn = *(retro_hw_context_reset_t*)param;
	VK_TRY { fn(); }
	VK_EXCEPT_ALL
	{
		VkHwLog("excecao dentro do context_destroy do core (thread com prazo) - ignorada");
	}
	free(param);
	return 0;
}

static bool g_destroy_never_returns = false;
static bool g_skip_context_destroy = false;

void VkHwSetSkipContextDestroy(bool skip)
{
	g_skip_context_destroy = skip;
}

void VkHwContextDestroy()
{
	if (!g_hw_active) return;

	if ((g_skip_context_destroy || g_destroy_never_returns) && g_context_live && g_hw_cb.context_destroy)
	{
		VkHwLog("pulando context_destroy - este core nao retorna ou foi marcado para pular");
	}
	else if (g_context_live && g_hw_cb.context_destroy)
	{
		retro_hw_context_reset_t* fn =
			(retro_hw_context_reset_t*)malloc(sizeof(retro_hw_context_reset_t));
		HANDLE h = NULL;
		if (fn)
		{
			*fn = g_hw_cb.context_destroy;
			h = CreateThread(NULL, 0, VkDestroyThreadProc, fn, 0, NULL);
			if (!h) free(fn);
		}
		if (h)
		{
			if (WaitForSingleObject(h, 3000) == WAIT_TIMEOUT)
			{
				g_destroy_never_returns = true;
				VkHwLog("context_destroy do core nao retornou em 3s - seguindo sem ele; nao sera chamado de novo nesta sessao");
			}
			CloseHandle(h);
		}
		else
		{
			VK_TRY { g_hw_cb.context_destroy(); }
			VK_EXCEPT_ALL
			{
				VkHwLog("excecao dentro do context_destroy do core - ignorada");
			}
		}
	}

	g_context_live = false;
	g_hw_active = false;
	g_pending_image_valid = false;
	g_pending_core_cmds.clear();
	memset(&g_hw_cb, 0, sizeof(g_hw_cb));

	if (g_core_created_device) VkHwShutdown();
	g_skip_context_destroy = false;
}

bool VkHwReadPixels(uint32_t* dest, unsigned width, unsigned height)
{
	if (!g_vk_ready || !dest) return false;
	if (width == 0 || height == 0) return false;
	if (!g_pending_image_valid) return false;
	if (!VkHwEnsureSurface(width, height)) return false;

	g_queue_lock.lock();

	vkResetCommandBuffer_(g_cmd_buf, 0);
	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer_(g_cmd_buf, &begin_info);

	VkImage src_image = g_pending_image.create_info.image;
	VkImageLayout orig_layout = g_pending_image.image_layout;

	// Ownership transfer between queue families is a two-part protocol the
	// core is expected to drive (see the long comment on set_image in
	// libretro_vulkan.h); when the image already belongs to our own queue
	// family (by far the common case - a core using a single queue for
	// everything) no acquire/release is needed at all, so that is the only
	// case handled here for this first pass.
	uint32_t src_family = g_pending_src_queue_family;
	if (src_family == g_queue_family) src_family = VK_QUEUE_FAMILY_IGNORED;

	VkImageMemoryBarrier to_src = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	to_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	to_src.oldLayout = orig_layout;
	to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	to_src.srcQueueFamilyIndex = src_family;
	to_src.dstQueueFamilyIndex = src_family == VK_QUEUE_FAMILY_IGNORED ? VK_QUEUE_FAMILY_IGNORED : g_queue_family;
	to_src.image = src_image;
	to_src.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	vkCmdPipelineBarrier_(g_cmd_buf,
		VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, NULL, 0, NULL, 1, &to_src);

	VkBufferImageCopy region = { 0 };
	region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	region.imageExtent = { width, height, 1 };
	vkCmdCopyImageToBuffer_(g_cmd_buf, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		g_staging_buf, 1, &region);

	// Restore the layout the core had it in - it owns this image and expects
	// to find it the way it left it, the same courtesy the OpenGL path pays
	// by never leaving core-owned state behind it.
	VkImageMemoryBarrier restore = to_src;
	restore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	restore.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	restore.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	restore.newLayout = orig_layout;
	restore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	restore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	vkCmdPipelineBarrier_(g_cmd_buf,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
		0, 0, NULL, 0, NULL, 1, &restore);

	vkEndCommandBuffer_(g_cmd_buf);

	// set_command_buffers requires these to be submitted before anything
	// else this frame; appending ours directly onto g_pending_core_cmds (then
	// submitting that vector's own storage) folds them into the same
	// submission without a second vkQueueSubmit, and without allocating a
	// fresh copy of it every single frame the way a separate submit_bufs
	// vector did.
	g_pending_core_cmds.push_back(g_cmd_buf);

	VkSemaphore signal_sems[1];
	uint32_t signal_count = 0;
	if (g_pending_signal_semaphore != VK_NULL_HANDLE)
	{
		signal_sems[0] = g_pending_signal_semaphore;
		signal_count = 1;
		g_pending_signal_semaphore = VK_NULL_HANDLE;
	}

	// Fixed-size, matching g_pending_semaphores' own 8-slot cap: every wait
	// semaphore here waits on the same stage, so there is nothing a per-frame
	// heap allocation would buy over a small stack array.
	VkPipelineStageFlags wait_stages[8];
	for (uint32_t i = 0; i < g_pending_num_semaphores; ++i) wait_stages[i] = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;

	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submit.waitSemaphoreCount = g_pending_num_semaphores;
	submit.pWaitSemaphores = g_pending_num_semaphores ? g_pending_semaphores : NULL;
	submit.pWaitDstStageMask = g_pending_num_semaphores ? wait_stages : NULL;
	submit.commandBufferCount = (uint32_t)g_pending_core_cmds.size();
	submit.pCommandBuffers = g_pending_core_cmds.data();
	submit.signalSemaphoreCount = signal_count;
	submit.pSignalSemaphores = signal_count ? signal_sems : NULL;

	vkResetFences_(g_device, 1, &g_fence);
	VkResult submit_result = vkQueueSubmit_(g_queue, 1, &submit, g_fence);
	g_pending_core_cmds.clear();
	g_pending_num_semaphores = 0;

	if (submit_result != VK_SUCCESS)
	{
		g_queue_lock.unlock();
		VkHwLog("vkQueueSubmit falhou (%d)", (int)submit_result);
		return false;
	}

	// Synchronous on purpose for this first pass: simpler and safer than a
	// multi-frame-in-flight scheme, at the cost of a small CPU stall waiting
	// for the GPU every frame - the same trade-off HwReadPixels's single GL
	// glReadPixels call already makes.
	vkWaitForFences_(g_device, 1, &g_fence, VK_TRUE, UINT64_MAX);

	g_queue_lock.unlock();

	void* mapped = NULL;
	if (vkMapMemory_(g_device, g_staging_mem, 0, (VkDeviceSize)width * height * 4, 0, &mapped) != VK_SUCCESS || !mapped)
	{
		VkHwLog("vkMapMemory falhou");
		return false;
	}

	// Our downstream pipeline (CRT filter, OSD, GDI blit) expects
	// 0x00RRGGBB in a little-endian uint32 - i.e. byte order B,G,R,x. Most
	// Vulkan cores render to VK_FORMAT_B8G8R8A8_* (matches directly) or
	// VK_FORMAT_R8G8B8A8_* (needs a channel swap); the format the core
	// actually used is right there in the image_view's own create_info,
	// so there is no need to guess.
	VkFormat fmt = g_pending_image.create_info.format;
	bool needs_swap = (fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_SRGB);

	// A core can set bottom_left_origin the same way a GL core does (see
	// HwReadPixels's own handling of this flag in hw_render.cpp) even though
	// Vulkan images are natively top-down - the flag describes the image's
	// own row order, not anything about the API storing it. Writing each
	// source row directly to its flipped destination row folds the flip into
	// this same copy pass instead of a separate full-buffer swap afterward.
	const uint8_t* src = (const uint8_t*)mapped;
	for (unsigned y = 0; y < height; y++)
	{
		const uint8_t* row = src + (size_t)y * width * 4;
		unsigned dst_y = g_hw_cb.bottom_left_origin ? (height - 1 - y) : y;
		uint32_t* out_row = dest + (size_t)dst_y * width;
		if (needs_swap)
		{
			for (unsigned x = 0; x < width; x++)
			{
				uint8_t r = row[x * 4 + 0], g = row[x * 4 + 1], b = row[x * 4 + 2];
				out_row[x] = ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16);
			}
		}
		else
		{
			memcpy(out_row, row, (size_t)width * 4);
		}
	}

	vkUnmapMemory_(g_device, g_staging_mem);

	g_pending_image_valid = false;
	return true;
}
