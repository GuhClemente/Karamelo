#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libretro.h"
#include "libretro_d3d11.h"
#include "hw_render_d3d11.h"

// D3D11 is a plain system API here, unlike Vulkan - d3d11.lib/d3dcompiler.lib
// are always present in the Windows SDK this project already builds against
// (compile_port.bat links them), so this links directly instead of the
// dynamic-load dance hw_render_vulkan.cpp needs for a not-vendored SDK.

static void D3D11HwLog(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	FILE* f = fopen("karamelo.log", "a");
	if (f) { fprintf(f, "[INFO] [HW-D3D11] %s\n", buf); fclose(f); }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static ID3D11Device*        g_device = NULL;
static ID3D11DeviceContext* g_context = NULL;
static D3D_FEATURE_LEVEL    g_feature_level = D3D_FEATURE_LEVEL_11_0;

static ID3D11Texture2D* g_staging_tex = NULL;
static unsigned g_staging_w = 0;
static unsigned g_staging_h = 0;
static DXGI_FORMAT g_staging_fmt = DXGI_FORMAT_UNKNOWN;

static struct retro_hw_render_callback g_hw_cb;
static bool g_hw_active = false;
static bool g_context_live = false;
static bool g_d3d11_ready = false;

static struct retro_hw_render_interface_d3d11 g_iface;

static bool D3D11HwInit()
{
	if (g_d3d11_ready) return true;

	D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
	};

	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	HRESULT hr = D3D11CreateDevice(
		NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
		levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
		&g_device, &g_feature_level, &g_context);

	if (FAILED(hr))
	{
		D3D11HwLog("D3D11CreateDevice falhou (hr=0x%08lX)", (long)hr);
		return false;
	}

	g_iface.interface_type = RETRO_HW_RENDER_INTERFACE_D3D11;
	g_iface.interface_version = RETRO_HW_RENDER_INTERFACE_D3D11_VERSION;
	g_iface.handle = NULL;
	g_iface.device = g_device;
	g_iface.context = g_context;
	g_iface.featureLevel = g_feature_level;
	g_iface.D3DCompile = D3DCompile;

	D3D11HwLog("dispositivo D3D11 criado (feature level 0x%04X)", (unsigned)g_feature_level);
	g_d3d11_ready = true;
	return true;
}

void D3D11HwShutdown()
{
	if (g_staging_tex) { g_staging_tex->Release(); g_staging_tex = NULL; }
	if (g_context) { g_context->ClearState(); g_context->Flush(); g_context->Release(); g_context = NULL; }
	if (g_device) { g_device->Release(); g_device = NULL; }

	g_staging_w = g_staging_h = 0;
	g_staging_fmt = DXGI_FORMAT_UNKNOWN;
	g_d3d11_ready = false;
	g_hw_active = false;
	g_context_live = false;
	memset(&g_hw_cb, 0, sizeof(g_hw_cb));
}

bool D3D11HwIsActive() { return g_hw_active && g_d3d11_ready; }

static int g_d3d11_probe_result = -1;

bool D3D11HwIsAvailable()
{
	if (g_d3d11_probe_result < 0)
	{
		g_d3d11_probe_result = D3D11HwInit() ? 1 : 0;
	}
	return g_d3d11_probe_result != 0;
}

const void* D3D11HwGetRenderInterface()
{
	// D3D11HwIsActive(), not g_d3d11_ready alone - see the same fix in
	// hw_render_vulkan.cpp's VkHwGetRenderInterface. g_d3d11_ready survives
	// D3D11HwContextDestroy, so gating on it handed this backend's interface
	// to whichever core asked next, Vulkan cores included (and D3D11 is tried
	// first at the call site, so it was the one that won).
	if (!D3D11HwIsActive()) return NULL;
	return &g_iface;
}

bool D3D11HwSetRenderCallback(struct retro_hw_render_callback* cb)
{
	if (!cb) return false;
	if (cb->context_type != RETRO_HW_CONTEXT_D3D11)
	{
		D3D11HwLog("core pediu contexto tipo %d - nao e D3D11", (int)cb->context_type);
		return false;
	}

	if (!D3D11HwInit()) return false;

	g_hw_cb = *cb;
	g_hw_active = true;
	D3D11HwLog("hardware render D3D11 aceito (depth=%d, stencil=%d)", cb->depth ? 1 : 0, cb->stencil ? 1 : 0);
	return true;
}

bool D3D11HwEnsureSurface(unsigned width, unsigned height)
{
	if (!g_d3d11_ready || width == 0 || height == 0) return false;

	// The staging texture's format has to match whatever the core's own
	// render-target texture turns out to be (D3D11 CopyResource requires
	// identical formats) - defaulting to BGRA here since that is what every
	// core seen so far (Flycast included) actually uses, and D3D11HwReadPixels
	// recreates this if the real format ever disagrees.
	if (g_staging_tex && width == g_staging_w && height == g_staging_h &&
		g_staging_fmt == DXGI_FORMAT_B8G8R8A8_UNORM)
		return true;

	if (g_staging_tex) { g_staging_tex->Release(); g_staging_tex = NULL; }

	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	HRESULT hr = g_device->CreateTexture2D(&desc, NULL, &g_staging_tex);
	if (FAILED(hr))
	{
		D3D11HwLog("CreateTexture2D (staging) falhou (hr=0x%08lX)", (long)hr);
		return false;
	}

	g_staging_w = width;
	g_staging_h = height;
	g_staging_fmt = DXGI_FORMAT_B8G8R8A8_UNORM;
	D3D11HwLog("staging texture %ux%u pronta", width, height);
	return true;
}

// The other half of a failed reset: a full D3D11HwShutdown(), not the
// lighter flag-clear this function used to be. Clearing g_d3d11_ready
// without releasing g_device/g_context/g_staging_tex meant the next
// D3D11HwInit() call (gated on that same flag) recreated the device from
// scratch and overwrote those COM pointers without ever Release()-ing
// them - a leak - and D3D11HwEnsureSurface()'s reuse check could then hand
// the new device's context a staging texture still bound to the old,
// now-orphaned one, which CopyResource() requires to share a device with.
// Mirrors the equivalent fix in VkHwContextReset's own SEH recovery path.
static void D3D11HwDisableAfterFault()
{
	D3D11HwShutdown();
}

bool D3D11HwContextReset()
{
	if (!g_hw_active || !g_d3d11_ready) return false;
	if (g_hw_cb.context_reset)
	{
		// No context negotiation interface exists for D3D in the libretro spec
		// (confirmed against both the spec's own enum and Flycast's libretro
		// shell - it calls SET_HW_RENDER directly, nothing else), so there is
		// no equivalent to hw_render_vulkan.cpp's known crash case. This SEH
		// guard is still here purely as the same defensive belt-and-suspenders
		// every other context_reset call site in this codebase gets, not
		// because a specific failure is expected.
		__try
		{
			g_hw_cb.context_reset();
			g_context_live = true;
			D3D11HwLog("context_reset entregue ao core");
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			D3D11HwLog("context_reset do core lancou excecao - desativando hw-render D3D11 para esta sessao");
			D3D11HwDisableAfterFault();
			return false;
		}
	}
	return true;
}

// context_destroy pertence ao core, e nem todo core volta dele. O PCSX2 nao
// volta: medido, ele gira a 96% de um nucleo indefinidamente, com a memoria
// parada, e o app so saia dali por TerminateThread - que mata a thread sem
// soltar os locks dela e deixa a interface presa em "LOADING..." para sempre.
//
// Chamar numa thread propria e esperar com prazo troca uma trava garantida por
// um vazamento de contexto D3D11 quando o prazo estoura. O vazamento e feio e
// dura ate o app fechar; a trava e definitiva. Entre os dois nao ha duvida.
// O parametro vai no heap, nao na pilha: quando o prazo estoura esta funcao
// retorna e a thread continua viva: um ponteiro para variavel local viraria
// lixo debaixo dela. Quem libera e a propria thread, se um dia terminar.
static DWORD WINAPI D3D11DestroyThreadProc(LPVOID param)
{
	retro_hw_context_reset_t fn = *(retro_hw_context_reset_t*)param;
	__try { fn(); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		D3D11HwLog("excecao dentro do context_destroy do core (thread com prazo) - ignorada");
	}
	free(param);
	return 0;
}

// Um core cujo context_destroy estourou o prazo uma vez nao vai voltar na
// proxima: medido, o PCSX2 estourou em 20 de 20 descarregamentos. Chamar de
// novo so cria mais uma thread presa girando a 96% de um nucleo, e elas se
// acumulam dentro do processo - quem abrisse cinco jogos de PS2 numa sessao
// ficaria com cinco. Depois do primeiro estouro, o teardown e simplesmente
// pulado: o contexto vaza igual, sem thread nova e sem os 5s de espera.
static bool g_destroy_never_returns = false;

void D3D11HwContextDestroy()
{
	if (!g_hw_active) return;

	if (g_destroy_never_returns && g_context_live && g_hw_cb.context_destroy)
	{
		D3D11HwLog("pulando context_destroy - este core ja provou que nao retorna");
	}
	else if (g_context_live && g_hw_cb.context_destroy)
	{
		retro_hw_context_reset_t* fn =
			(retro_hw_context_reset_t*)malloc(sizeof(retro_hw_context_reset_t));
		HANDLE h = NULL;
		if (fn)
		{
			*fn = g_hw_cb.context_destroy;
			h = CreateThread(NULL, 0, D3D11DestroyThreadProc, fn, 0, NULL);
			if (!h) free(fn);
		}
		if (h)
		{
			// 5s e folgado para um teardown honesto e curto o bastante para o
			// jogador nao achar que o app morreu.
			if (WaitForSingleObject(h, 5000) == WAIT_TIMEOUT)
			{
				g_destroy_never_returns = true;
				D3D11HwLog("context_destroy do core nao retornou em 5s - seguindo sem ele (contexto vazado ate fechar o app); nao sera chamado de novo nesta sessao");
				// A thread fica onde esta. Nao se mata: ela esta dentro do
				// core, provavelmente segurando algum lock, e TerminateThread
				// aqui reproduziria exatamente o problema que isto evita.
			}
			CloseHandle(h);
		}
		else
		{
			__try { g_hw_cb.context_destroy(); }
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				D3D11HwLog("excecao dentro do context_destroy do core - ignorada");
			}
		}
	}

	g_context_live = false;
	g_hw_active = false;
	memset(&g_hw_cb, 0, sizeof(g_hw_cb));
}

bool D3D11HwReadPixels(uint32_t* dest, unsigned width, unsigned height)
{
	if (!g_d3d11_ready || !dest) return false;
	if (width == 0 || height == 0) return false;

	// Contract (verified against Flycast's core/rend/dx11/dx11context_lr.cpp
	// and RetroArch's own gfx/drivers/d3d11.c): a core renders its frame into
	// its own texture, then binds that texture's SRV to pixel-shader slot 0
	// as an out-of-band "here is this frame" signal before calling
	// video_refresh_cb(RETRO_HW_FRAME_BUFFER_VALID, ...). There is no other
	// defined way for the frontend to learn which texture holds the frame.
	ID3D11ShaderResourceView* srv = NULL;
	g_context->PSGetShaderResources(0, 1, &srv);
	if (!srv)
	{
		D3D11HwLog("nenhuma SRV vinculada no slot 0 - core nao sinalizou o frame");
		return false;
	}

	ID3D11Resource* resource = NULL;
	srv->GetResource(&resource);
	srv->Release();
	if (!resource)
	{
		D3D11HwLog("SRV sem recurso associado");
		return false;
	}

	ID3D11Texture2D* src_tex = NULL;
	HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&src_tex);
	resource->Release();
	if (FAILED(hr) || !src_tex)
	{
		D3D11HwLog("recurso da SRV nao e uma ID3D11Texture2D");
		return false;
	}

	D3D11_TEXTURE2D_DESC src_desc;
	src_tex->GetDesc(&src_desc);

	// CopyResource requires identical formats - recreate the staging texture
	// to match whenever the core's own format disagrees with our BGRA default
	// (seen so far: only Flycast, which is BGRA, so this path is untested but
	// cheap insurance against a future core that differs).
	if (!g_staging_tex || src_desc.Width != g_staging_w || src_desc.Height != g_staging_h ||
		src_desc.Format != g_staging_fmt)
	{
		if (g_staging_tex) { g_staging_tex->Release(); g_staging_tex = NULL; }

		D3D11_TEXTURE2D_DESC desc = { 0 };
		desc.Width = src_desc.Width;
		desc.Height = src_desc.Height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = src_desc.Format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		if (FAILED(g_device->CreateTexture2D(&desc, NULL, &g_staging_tex)))
		{
			D3D11HwLog("CreateTexture2D (staging, formato %d) falhou", (int)src_desc.Format);
			src_tex->Release();
			return false;
		}
		g_staging_w = src_desc.Width;
		g_staging_h = src_desc.Height;
		g_staging_fmt = src_desc.Format;
	}

	g_context->CopyResource(g_staging_tex, src_tex);
	src_tex->Release();

	D3D11_MAPPED_SUBRESOURCE mapped;
	hr = g_context->Map(g_staging_tex, 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(hr))
	{
		D3D11HwLog("Map (staging) falhou (hr=0x%08lX)", (long)hr);
		return false;
	}

	// Our downstream pipeline (CRT filter, OSD, GDI blit) expects
	// 0x00RRGGBB in a little-endian uint32 - i.e. byte order B,G,R,x, which
	// matches DXGI_FORMAT_B8G8R8A8_UNORM (every core seen so far) directly;
	// an RGBA-ordered format needs the same channel swap the Vulkan/GL paths
	// already do for their own R8G8B8A8 case.
	bool needs_swap = (g_staging_fmt == DXGI_FORMAT_R8G8B8A8_UNORM);

	unsigned copy_w = width < g_staging_w ? width : g_staging_w;
	unsigned copy_h = height < g_staging_h ? height : g_staging_h;
	// See VkHwReadPixels's own comment on this same flag: bottom_left_origin
	// describes the core's own row order, independent of the graphics API
	// storing the image, so it needs the same handling here as the GL and
	// Vulkan paths - folded into this copy pass via the destination row index.
	const uint8_t* src = (const uint8_t*)mapped.pData;
	for (unsigned y = 0; y < copy_h; y++)
	{
		const uint8_t* row = src + (size_t)y * mapped.RowPitch;
		unsigned dst_y = g_hw_cb.bottom_left_origin ? (copy_h - 1 - y) : y;
		uint32_t* out_row = dest + (size_t)dst_y * width;
		if (needs_swap)
		{
			for (unsigned x = 0; x < copy_w; x++)
			{
				uint8_t r = row[x * 4 + 0], g = row[x * 4 + 1], b = row[x * 4 + 2];
				out_row[x] = ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16);
			}
		}
		else
		{
			memcpy(out_row, row, (size_t)copy_w * 4);
		}
	}

	g_context->Unmap(g_staging_tex, 0);
	return true;
}
