#ifndef HW_RENDER_VULKAN_H_INCLUDED
#define HW_RENDER_VULKAN_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

struct retro_hw_render_callback;

// Vulkan sibling of hw_render.cpp's OpenGL path. A core requests this via
// RETRO_ENVIRONMENT_SET_HW_RENDER with context_type == RETRO_HW_CONTEXT_VULKAN;
// the actual interface (device/queue/set_image/...) is hand-shaken separately
// via RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE - see core_runner.cpp's
// CB_Environment for both call sites.
//
// Like the OpenGL path, this never presents anything through Vulkan itself:
// the core renders into an image it owns, we copy that image back to a CPU
// buffer every frame (VkHwReadPixels), and the existing GDI/CRT-filter/OSD
// pipeline takes it from there, unchanged.

// Interface de negociacao de contexto, entregue pelo core via
// RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE. E por ela que
// o core escolhe o dispositivo fisico, as extensoes, as features e a fila de
// que o renderizador dele precisa. Sem isso o frontend so sabe entregar um
// dispositivo generico, e cores como PPSSPP, Flycast e PCSX2 falham dentro do
// proprio setup - motivo pelo qual o Vulkan vinha sendo recusado para eles.
//
// O ponteiro e const struct retro_hw_render_context_negotiation_interface_vulkan*
// (void* aqui para o cabecalho nao arrastar vulkan.h). Vale ate o core ser
// descarregado; VkHwClearNegotiationInterface() e chamada nesse momento.
void VkHwSetNegotiationInterface(const void* iface);
void VkHwClearNegotiationInterface();

bool VkHwSetRenderCallback(struct retro_hw_render_callback* cb);
bool VkHwIsActive();
bool VkHwIsAvailable();

// Returns a const struct retro_hw_render_interface* (void* here so this
// header does not need to drag in vulkan.h - core_runner.cpp casts it).
const void* VkHwGetRenderInterface();

bool VkHwEnsureSurface(unsigned width, unsigned height);
// Returns false if the core's context_reset() crashed and hw-render Vulkan
// had to be disabled for this session (see hw_render_vulkan.cpp) - the
// caller should treat that the same as a failed load.
bool VkHwContextReset();
void VkHwContextDestroy();
void VkHwSetSkipContextDestroy(bool skip);
bool VkHwReadPixels(uint32_t* dest, unsigned width, unsigned height);
void VkHwShutdown();

#endif
