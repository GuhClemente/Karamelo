#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <gl/GL.h>
#include <stdio.h>
#include <string.h>

#include "libretro.h"
#include "hw_render.h"

#pragma comment(lib, "opengl32.lib")

// ---------------------------------------------------------------------------
// Framebuffer-object entry points.
//
// opengl32.lib on Windows only exports GL 1.1, so everything FBO-related has to
// come through wglGetProcAddress at runtime.
// ---------------------------------------------------------------------------
#define GL_FRAMEBUFFER            0x8D40
#define GL_RENDERBUFFER           0x8D41
#define GL_COLOR_ATTACHMENT0      0x8CE0
#define GL_DEPTH_ATTACHMENT       0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8       0x88F0
#define GL_DEPTH_COMPONENT24      0x81A6
#define GL_FRAMEBUFFER_COMPLETE   0x8CD5
#define GL_BGRA_EXT               0x80E1
#define GL_CLAMP_TO_EDGE          0x812F

typedef void (APIENTRY *PFN_glGenFramebuffers)(GLsizei, GLuint*);
typedef void (APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (APIENTRY *PFN_glGenRenderbuffers)(GLsizei, GLuint*);
typedef void (APIENTRY *PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glDeleteRenderbuffers)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (APIENTRY *PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum (APIENTRY *PFN_glCheckFramebufferStatus)(GLenum);

static PFN_glGenFramebuffers         p_glGenFramebuffers = NULL;
static PFN_glBindFramebuffer         p_glBindFramebuffer = NULL;
static PFN_glDeleteFramebuffers      p_glDeleteFramebuffers = NULL;
static PFN_glFramebufferTexture2D    p_glFramebufferTexture2D = NULL;
static PFN_glGenRenderbuffers        p_glGenRenderbuffers = NULL;
static PFN_glBindRenderbuffer        p_glBindRenderbuffer = NULL;
static PFN_glDeleteRenderbuffers     p_glDeleteRenderbuffers = NULL;
static PFN_glRenderbufferStorage     p_glRenderbufferStorage = NULL;
static PFN_glFramebufferRenderbuffer p_glFramebufferRenderbuffer = NULL;
static PFN_glCheckFramebufferStatus  p_glCheckFramebufferStatus = NULL;

static HWND  g_gl_window = NULL;
static HDC   g_gl_dc = NULL;
static HGLRC g_gl_ctx = NULL;
static bool  g_gl_ready = false;

static GLuint g_fbo = 0;
static GLuint g_color_tex = 0;
static GLuint g_depth_rb = 0;
static unsigned g_surface_w = 0;
static unsigned g_surface_h = 0;

static struct retro_hw_render_callback g_hw_cb;
static bool g_hw_active = false;
static bool g_context_live = false;

static void HwLog(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	FILE* f = fopen("mister_flavor.log", "a");
	if (f) { fprintf(f, "[INFO] [HW] %s\n", buf); fclose(f); }
}

static void* GlProc(const char* name)
{
	void* p = (void*)wglGetProcAddress(name);
	if (p == NULL || p == (void*)0x1 || p == (void*)0x2 ||
		p == (void*)0x3 || p == (void*)-1)
	{
		// Core GL 1.1 entry points live in the DLL, not the ICD.
		HMODULE gl = GetModuleHandleA("opengl32.dll");
		p = gl ? (void*)GetProcAddress(gl, name) : NULL;
	}
	return p;
}

bool HwInit()
{
	if (g_gl_ready) return true;

	// A hidden 1x1 window is enough: we never present through GL, we read the
	// FBO back and let the existing GDI path put it on screen.
	WNDCLASSA wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = "MiSTerFlavorGL";
	RegisterClassA(&wc);

	g_gl_window = CreateWindowExA(0, "MiSTerFlavorGL", "", WS_POPUP,
		0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
	if (!g_gl_window) { HwLog("CreateWindow falhou (%lu)", GetLastError()); return false; }

	g_gl_dc = GetDC(g_gl_window);
	if (!g_gl_dc) { HwLog("GetDC falhou"); HwShutdown(); return false; }

	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;

	int pf = ChoosePixelFormat(g_gl_dc, &pfd);
	if (!pf || !SetPixelFormat(g_gl_dc, pf, &pfd))
	{
		HwLog("pixel format falhou (%lu)", GetLastError());
		HwShutdown();
		return false;
	}

	// A legacy context first - it is the only way to reach the ARB entry point
	// that creates a modern one.
	HGLRC legacy = wglCreateContext(g_gl_dc);
	if (!legacy || !wglMakeCurrent(g_gl_dc, legacy))
	{
		HwLog("wglCreateContext falhou (%lu)", GetLastError());
		HwShutdown();
		return false;
	}

	// wglCreateContext alone yields OpenGL 1.1 compatibility. Cores that
	// compile modern shaders - flycast for Dreamcast, the GL plugins for N64 -
	// need a real 3.3+ core profile, and handing them a 1.1 context made them
	// fault inside their own shader setup rather than fail cleanly.
	typedef HGLRC (WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
	PFN_wglCreateContextAttribsARB createAttribs =
		(PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");

	#define WGL_CONTEXT_MAJOR_VERSION_ARB  0x2091
	#define WGL_CONTEXT_MINOR_VERSION_ARB  0x2092
	#define WGL_CONTEXT_PROFILE_MASK_ARB   0x9126
	#define WGL_CONTEXT_CORE_PROFILE_BIT   0x00000001
	#define WGL_CONTEXT_COMPATIBILITY_BIT  0x00000002

	g_gl_ctx = NULL;

	if (createAttribs)
	{
		// Compatibility profile, highest version that will start. Cores mix
		// modern shaders with fixed-function calls often enough that a strict
		// core profile breaks them.
		static const int versions[][2] = { {4,6},{4,5},{4,3},{4,1},{3,3} };

		for (int i = 0; i < 5 && !g_gl_ctx; i++)
		{
			const int attribs[] = {
				WGL_CONTEXT_MAJOR_VERSION_ARB, versions[i][0],
				WGL_CONTEXT_MINOR_VERSION_ARB, versions[i][1],
				WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_COMPATIBILITY_BIT,
				0
			};
			g_gl_ctx = createAttribs(g_gl_dc, NULL, attribs);
		}
	}

	if (g_gl_ctx)
	{
		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(legacy);
		if (!wglMakeCurrent(g_gl_dc, g_gl_ctx))
		{
			HwLog("nao consegui ativar o contexto moderno");
			HwShutdown();
			return false;
		}
	}
	else
	{
		// No ARB path: keep the legacy context. Simple cores still work; the
		// demanding ones will refuse, which is better than crashing.
		HwLog("sem wglCreateContextAttribsARB - ficando no contexto legado");
		g_gl_ctx = legacy;
	}

	p_glGenFramebuffers         = (PFN_glGenFramebuffers)GlProc("glGenFramebuffers");
	p_glBindFramebuffer         = (PFN_glBindFramebuffer)GlProc("glBindFramebuffer");
	p_glDeleteFramebuffers      = (PFN_glDeleteFramebuffers)GlProc("glDeleteFramebuffers");
	p_glFramebufferTexture2D    = (PFN_glFramebufferTexture2D)GlProc("glFramebufferTexture2D");
	p_glGenRenderbuffers        = (PFN_glGenRenderbuffers)GlProc("glGenRenderbuffers");
	p_glBindRenderbuffer        = (PFN_glBindRenderbuffer)GlProc("glBindRenderbuffer");
	p_glDeleteRenderbuffers     = (PFN_glDeleteRenderbuffers)GlProc("glDeleteRenderbuffers");
	p_glRenderbufferStorage     = (PFN_glRenderbufferStorage)GlProc("glRenderbufferStorage");
	p_glFramebufferRenderbuffer = (PFN_glFramebufferRenderbuffer)GlProc("glFramebufferRenderbuffer");
	p_glCheckFramebufferStatus  = (PFN_glCheckFramebufferStatus)GlProc("glCheckFramebufferStatus");

	if (!p_glGenFramebuffers || !p_glBindFramebuffer || !p_glFramebufferTexture2D ||
		!p_glCheckFramebufferStatus)
	{
		HwLog("driver sem suporte a FBO");
		HwShutdown();
		return false;
	}

	const char* ver = (const char*)glGetString(GL_VERSION);
	const char* ren = (const char*)glGetString(GL_RENDERER);
	HwLog("contexto OpenGL criado: %s | %s", ver ? ver : "?", ren ? ren : "?");

	g_gl_ready = true;
	return true;
}

void HwShutdown()
{
	if (g_gl_ctx)
	{
		wglMakeCurrent(g_gl_dc, g_gl_ctx);

		if (g_fbo && p_glDeleteFramebuffers) p_glDeleteFramebuffers(1, &g_fbo);
		if (g_depth_rb && p_glDeleteRenderbuffers) p_glDeleteRenderbuffers(1, &g_depth_rb);
		if (g_color_tex) glDeleteTextures(1, &g_color_tex);

		g_fbo = g_depth_rb = g_color_tex = 0;

		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(g_gl_ctx);
		g_gl_ctx = NULL;
	}

	if (g_gl_dc) { ReleaseDC(g_gl_window, g_gl_dc); g_gl_dc = NULL; }
	if (g_gl_window) { DestroyWindow(g_gl_window); g_gl_window = NULL; }

	g_gl_ready = false;
	g_hw_active = false;
	g_context_live = false;
	memset(&g_hw_cb, 0, sizeof(g_hw_cb));
	g_surface_w = g_surface_h = 0;
}

bool HwIsActive() { return g_hw_active && g_gl_ready; }

// An OpenGL context can only be current on one thread at a time. The startup
// probe runs on the main thread and must hand the context back, or every GL
// call from the core thread silently does nothing - which is what made
// glCheckFramebufferStatus return 0 and the Dreamcast core draw into an
// invalid FBO and crash.
void HwReleaseCurrent()
{
	if (g_gl_ready) wglMakeCurrent(NULL, NULL);
}

bool HwMakeCurrent()
{
	if (!g_gl_ready) return false;
	if (wglGetCurrentContext() == g_gl_ctx) return true;

	if (!wglMakeCurrent(g_gl_dc, g_gl_ctx))
	{
		HwLog("wglMakeCurrent falhou nesta thread (%lu)", GetLastError());
		return false;
	}
	return true;
}

static int g_gl_probe_result = -1;

bool HwIsAvailable()
{
	if (g_gl_probe_result < 0) g_gl_probe_result = HwInit() ? 1 : 0;
	return g_gl_probe_result != 0;
}

bool HwGlProbed()
{
	// Never creates anything. If the probe has not run yet, assume no GL: a
	// software plugin is slow but always works, while a GL plugin without a
	// context hangs the core.
	return g_gl_probe_result > 0;
}

static uintptr_t HwGetCurrentFramebuffer(void)
{
	return (uintptr_t)g_fbo;
}

static retro_proc_address_t HwGetProcAddress(const char* sym)
{
	return (retro_proc_address_t)GlProc(sym);
}

bool HwSetRenderCallback(struct retro_hw_render_callback* cb)
{
	if (!cb) return false;

	// Only the OpenGL families are servable here. Refusing the rest is not a
	// limitation we can paper over: a Vulkan core needs a Vulkan device.
	if (cb->context_type != RETRO_HW_CONTEXT_OPENGL &&
		cb->context_type != RETRO_HW_CONTEXT_OPENGL_CORE &&
		cb->context_type != RETRO_HW_CONTEXT_OPENGLES2 &&
		cb->context_type != RETRO_HW_CONTEXT_OPENGLES3)
	{
		HwLog("core pediu contexto tipo %d - nao suportado", (int)cb->context_type);
		return false;
	}

	if (!HwInit()) return false;
	if (!HwMakeCurrent()) return false;

	g_hw_cb = *cb;

	cb->get_current_framebuffer = HwGetCurrentFramebuffer;
	cb->get_proc_address = HwGetProcAddress;

	g_hw_active = true;
	HwLog("hardware render aceito (tipo=%d, depth=%d, stencil=%d, origem=%s)",
		(int)cb->context_type, cb->depth ? 1 : 0, cb->stencil ? 1 : 0,
		cb->bottom_left_origin ? "bottom-left" : "top-left");
	return true;
}

bool HwEnsureSurface(unsigned width, unsigned height)
{
	if (!g_gl_ready || width == 0 || height == 0) return false;
	if (g_fbo && width == g_surface_w && height == g_surface_h) return true;

	if (!HwMakeCurrent()) return false;

	if (g_fbo) { p_glDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }
	if (g_depth_rb) { p_glDeleteRenderbuffers(1, &g_depth_rb); g_depth_rb = 0; }
	if (g_color_tex) { glDeleteTextures(1, &g_color_tex); g_color_tex = 0; }

	glGenTextures(1, &g_color_tex);
	glBindTexture(GL_TEXTURE_2D, g_color_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height,
		0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	p_glGenFramebuffers(1, &g_fbo);
	p_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
	p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, g_color_tex, 0);

	if (g_hw_cb.depth && p_glGenRenderbuffers)
	{
		p_glGenRenderbuffers(1, &g_depth_rb);
		p_glBindRenderbuffer(GL_RENDERBUFFER, g_depth_rb);
		p_glRenderbufferStorage(GL_RENDERBUFFER,
			g_hw_cb.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24,
			(GLsizei)width, (GLsizei)height);
		p_glFramebufferRenderbuffer(GL_FRAMEBUFFER,
			g_hw_cb.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
			GL_RENDERBUFFER, g_depth_rb);
	}

	GLenum status = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		// Status 0 is not "incomplete": the spec returns it when the call
		// itself errored, which almost always means no context is current on
		// this thread.
		HwLog("FBO %s (0x%04X) em %ux%u, glGetError=0x%04X",
			status == 0 ? "sem contexto valido" : "incompleto",
			status, width, height, glGetError());
		return false;
	}

	glViewport(0, 0, (GLsizei)width, (GLsizei)height);

	g_surface_w = width;
	g_surface_h = height;
	HwLog("superficie %ux%u pronta", width, height);
	return true;
}

void HwContextReset()
{
	if (!g_hw_active || !g_gl_ready) return;
	if (!HwMakeCurrent()) return;

	if (g_hw_cb.context_reset)
	{
		g_hw_cb.context_reset();
		g_context_live = true;
		HwLog("context_reset entregue ao core");
	}
}

void HwContextDestroy()
{
	if (g_context_live && HwMakeCurrent())
	{
		if (g_hw_cb.context_destroy) g_hw_cb.context_destroy();
	}
	g_context_live = false;
	g_hw_active = false;
	memset(&g_hw_cb, 0, sizeof(g_hw_cb));
}

bool HwReadPixels(uint32_t* dest, unsigned width, unsigned height)
{
	if (!g_gl_ready || !g_fbo || !dest) return false;
	if (width == 0 || height == 0) return false;
	if (width > g_surface_w || height > g_surface_h) return false;
	if (!HwMakeCurrent()) return false;

	p_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);

	// BGRA lands as 0x00RRGGBB in a little-endian uint32, which is exactly the
	// layout the rest of the pipeline and the DIB section expect.
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadPixels(0, 0, (GLsizei)width, (GLsizei)height,
		GL_BGRA_EXT, GL_UNSIGNED_BYTE, dest);

	// glReadPixels always returns rows starting at the bottom of the target.
	// When the core rendered with a bottom-left origin (the GL default, and
	// what mupen64plus reports) that data is upside down relative to our
	// top-down framebuffer, so it has to be flipped. The condition was
	// inverted here, which would have put the whole picture on its head.
	if (g_hw_cb.bottom_left_origin)
	{
		for (unsigned y = 0; y < height / 2; y++)
		{
			uint32_t* a = dest + (size_t)y * width;
			uint32_t* b = dest + (size_t)(height - 1 - y) * width;
			std::swap_ranges(a, a + width, b);
		}
	}

	return true;
}
