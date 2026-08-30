#ifndef HW_RENDER_H_INCLUDED
#define HW_RENDER_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

struct retro_hw_render_callback;

// OpenGL backing for cores that render on the GPU.
//
// Everything downstream of here - the CRT filters, the OSD, the blit - works on
// a CPU framebuffer, so this does not turn the app into a GL renderer. It gives
// the core a real GL context and an FBO to draw into, then reads the result
// back so the existing pipeline sees an ordinary frame.
//
// That readback costs a copy per frame. It is the price of leaving the whole
// software presentation path intact, and it is what makes N64, Dreamcast,
// GameCube and PS2 cores load at all: they refuse outright without OpenGL.
//
// Every function here must be called from the core thread, which is where the
// GL context is made current.

// Creates the hidden window, the GL context and loads the extensions.
// Safe to call repeatedly; returns false when the machine has no usable GL.
bool HwInit();
void HwShutdown();

// True once a core has asked for hardware rendering and we accepted.
bool HwIsActive();

// Probes once whether this machine can give a core an OpenGL context. Used to
// pick core options that match the render mode: handing a core a GL plugin on
// a machine without GL hangs it, and handing it a software rasteriser when GL
// is available throws the GPU away.
bool HwIsAvailable();

// The cached result only. Safe to call from any thread and from inside a core
// callback, because it never creates a window or a context.
bool HwGlProbed();

// A GL context lives on one thread at a time. The startup probe must release
// it so the core thread can claim it.
void HwReleaseCurrent();
bool HwMakeCurrent();

// Handles RETRO_ENVIRONMENT_SET_HW_RENDER. Fills in the callbacks the core
// needs and returns false for context types we cannot serve (Vulkan, D3D).
bool HwSetRenderCallback(struct retro_hw_render_callback* cb);

// Sizes the render target. Called with the core's max geometry after load.
bool HwEnsureSurface(unsigned width, unsigned height);

// Tells the core its context is ready / going away.
void HwContextReset();
void HwContextDestroy();

// Pulls the current FBO contents into a CPU buffer as 0x00RRGGBB, flipping to
// top-down. Returns false if there is nothing to read.
bool HwReadPixels(uint32_t* dest, unsigned width, unsigned height);

#endif
