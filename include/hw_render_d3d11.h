#ifndef HW_RENDER_D3D11_H_INCLUDED
#define HW_RENDER_D3D11_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

struct retro_hw_render_callback;

// D3D11 sibling of hw_render.cpp (OpenGL) and hw_render_vulkan.cpp - a core
// requests this via RETRO_ENVIRONMENT_SET_HW_RENDER with
// context_type == RETRO_HW_CONTEXT_D3D11; the actual interface
// (device/context/D3DCompile) is handed over separately via
// RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE - see core_runner.cpp's
// CB_Environment for both call sites.
//
// Unlike Vulkan, libretro has no context negotiation interface for D3D -
// confirmed against both the spec (retro_hw_render_context_negotiation_interface_type
// only ever lists VULKAN) and Flycast's own libretro shell (set_dx11_hw_render()
// calls SET_HW_RENDER directly, no negotiation call at all), so there is no
// equivalent to hw_render_vulkan.cpp's g_core_wants_vk_negotiation escape hatch
// needed here.
//
// Readback contract (verified against Flycast's core/rend/dx11/dx11context_lr.cpp
// and RetroArch's own gfx/drivers/d3d11.c): a core renders into a texture it
// owns, binds that texture's shader resource view to PS slot 0
// (ID3D11DeviceContext::PSSetShaderResources(0, 1, &srv)) as an out-of-band
// "here is this frame" signal, then calls video_refresh_cb with
// RETRO_HW_FRAME_BUFFER_VALID. D3D11HwReadPixels retrieves that same SRV via
// PSGetShaderResources(0, ...) and copies it back to a CPU buffer - the exact
// mechanism RetroArch's own D3D11 driver uses, not something invented here.

bool D3D11HwSetRenderCallback(struct retro_hw_render_callback* cb);
bool D3D11HwIsActive();
bool D3D11HwIsAvailable();

// Returns a const struct retro_hw_render_interface* (void* here so this
// header does not need to drag in d3d11.h - core_runner.cpp casts it).
const void* D3D11HwGetRenderInterface();

bool D3D11HwEnsureSurface(unsigned width, unsigned height);
bool D3D11HwContextReset();
void D3D11HwContextDestroy();
void D3D11HwSetSkipContextDestroy(bool skip);
bool D3D11HwReadPixels(uint32_t* dest, unsigned width, unsigned height);
void D3D11HwShutdown();

#endif
