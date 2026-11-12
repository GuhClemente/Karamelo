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

/* The pointers on Windows: Windows merges every mouse into its cursor, but
 * WM_INPUT still names the device behind each packet. The window takes the
 * Raw Input of the mice, a hook of SDL reads the packets; devices are told
 * apart by the path of their interface (one per port). Nothing can take a
 * device away from the cursor of the desktop. From xemu's
 * ui/xemu-pointer-rawinput.c, after the RawInputSource of PCSX2X6. */

#include "odin/host/input/pointers.hpp"
#include "odin/host/input/pointer_names.hpp"

#ifdef _WIN32

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

#include <windows.h>
#include <hidsdi.h>

namespace odin {

namespace {

/* A wide text in UTF-8, empty when it cannot be */
std::string
narrow(const wchar_t *text)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? static_cast<size_t>(n) : 0, '\0');

	if (n <= 0 || WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr) <= 0)
		return {};
	out.pop_back();
	while (!out.empty() && (out.back() == '\n' || out.back() == ' '))
		out.pop_back();
	return out;
}

/* A device of the Raw Input list: where it points (absolute, 0 to 65535 of
 * the screen or of the whole desktop; relative, in pixels of the window),
 * its buttons; its handle changes across sleep and replug, its path not */
struct Entry {
	Pointers::Device device;
	HANDLE handle = nullptr;
	std::string path;
	bool seen = false, logged = false, desktop = false, placed = false;
	long x = 0, y = 0;
	float rel_x = 0.0f, rel_y = 0.0f;
	uint32_t buttons = 0, pressed = 0;
};

/* The interface path of a device in full (for CreateFile), and without its
 * \\?\ and its class GUID for the list: "HID#VID_16C0&PID_0F01&MI_01#8&2f5b0c8&0&0000" */
bool
device_path(HANDLE handle, std::array<wchar_t, 256> &wide, std::string &path)
{
	UINT size = static_cast<UINT>(wide.size());
	const UINT got = GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, wide.data(), &size);

	if (got == static_cast<UINT>(-1) || got == 0)
		return false;
	wide[std::min<size_t>(got, wide.size() - 1)] = L'\0';
	path = narrow(wide.data());
	if (path.starts_with("\\\\?\\") || path.starts_with("\\\\.\\"))
		path.erase(0, 4);
	if (const size_t guid = path.find("#{"); guid != std::string::npos)
		path.erase(guid);
	return !path.empty();
}

/* VID and PID in the path; none (0) without them (I2C touchpads, PS/2,
 * Bluetooth) */
std::pair<unsigned, unsigned>
vidpid_of(const std::string &path)
{
	const size_t v = path.find("VID_"), p = path.find("PID_");
	unsigned vid = 0, pid = 0;

	if (v == std::string::npos || p == std::string::npos || std::sscanf(path.c_str() + v + 4, "%4x", &vid) != 1 ||
	    std::sscanf(path.c_str() + p + 4, "%4x", &pid) != 1)
		return { 0, 0 };
	return { vid, pid };
}

/* The end of a long path: the instance there tells two of a kind apart */
std::string
path_identity(const std::string &path)
{
	return "path:" + (path.size() > 56 ? path.substr(path.size() - 56) : path);
}

/* The name of the product, from its HID driver (opened without rights,
 * which mice allow) */
std::string
product_of(const wchar_t *wide)
{
	const HANDLE dev = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	std::array<wchar_t, 128> text{};
	std::string name;

	if (dev == INVALID_HANDLE_VALUE)
		return name;
	if (HidD_GetProductString(dev, text.data(), static_cast<ULONG>(sizeof(text))))
		name = narrow(text.data());
	CloseHandle(dev);
	return name;
}

} /* namespace */

struct Pointers::Backend {
	HWND hwnd = nullptr;
	std::vector<Entry> entries;
	std::vector<Device> devices;
	bool rescan = false;	/* raised by the hook, served by poll() */

	Entry *find_identity(const std::string &identity)
	{
		for (Entry &e : entries)
			if (e.device.identity == identity)
				return &e;
		return nullptr;
	}

	static void close_entry(Entry &e)
	{
		e.handle = nullptr;
		e.device.present = false;
		e.seen = e.logged = false;
		e.buttons = e.pressed = 0;
	}

	void open_device(HANDLE handle, const wchar_t *wide, const std::string &path);
	void scan();
	void packet(Entry &e, const RAWMOUSE &m);
	void list();
	static bool SDLCALL hook(void *self, MSG *msg);
};

void
Pointers::Backend::open_device(HANDLE handle, const wchar_t *wide, const std::string &path)
{
	for (Entry &e : entries) {
		if (e.handle && e.path == path) {
			e.handle = handle;
			return;
		}
	}

	const auto [vid, pid] = vidpid_of(path);
	const std::string name = product_of(wide);

	/* VID:PID, which stays when the port changes, as in PCSX2X6; two of a
	 * kind take their port */
	std::string identity = vid && pid ? vidpid_identity(vid, pid) : path_identity(path);
	Entry *e = find_identity(identity);

	if (e && e->handle)
		e = find_identity(identity = path_identity(path));
	if (e && e->handle)
		return;
	if (!e) {
		if (entries.size() >= MOST)
			return;
		entries.push_back({});
		e = &entries.back();
		e->device.identity = identity;
	}
	close_entry(*e);
	e->handle = handle;
	e->path = path;
	e->device.name = pointer_name(name, vid, pid);
	e->device.present = true;
	/* Aiming or moving: the first packet says */
	e->placed = false;
	std::printf("pointer: %s = %s (%s)\n", e->device.name.c_str(), path.c_str(), e->device.identity.c_str());
}

void
Pointers::Backend::scan()
{
	UINT n = 0;

	if (GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
		return;

	std::vector<RAWINPUTDEVICELIST> all(n + 1);
	const UINT got = GetRawInputDeviceList(all.data(), &n, sizeof(RAWINPUTDEVICELIST));

	/* The list changed meanwhile: the next change of a device tries again */
	if (got == static_cast<UINT>(-1))
		return;

	struct Mouse {
		HANDLE handle;
		std::array<wchar_t, 256> wide;
		std::string path;
	};
	std::vector<Mouse> mice;

	for (UINT i = 0; i < got; i++) {
		Mouse m{ all[i].hDevice, {}, {} };

		if (all[i].dwType == RIM_TYPEMOUSE && device_path(m.handle, m.wide, m.path))
			mice.push_back(std::move(m));
	}
	for (Entry &e : entries) {
		const auto listed = [&e](const Mouse &m) { return m.path == e.path; };

		if (e.handle && std::none_of(mice.begin(), mice.end(), listed)) {
			std::printf("pointer: %s unplugged\n", e.device.name.c_str());
			close_entry(e);
		}
	}
	for (const Mouse &m : mice)
		open_device(m.handle, m.wide.data(), m.path);
	list();
}

/* A Sinden says 0, 0 while its camera cannot see the border: off the
 * picture, which is how it reloads */
void
Pointers::Backend::packet(Entry &e, const RAWMOUSE &m)
{
	static constexpr std::pair<USHORT, USHORT> BUTTONS[] = {
		{ RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP },
		{ RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP },
		{ RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP },
		{ RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP },
		{ RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP },
	};

	/* The first packet in the log, for reports: bit 0 of its flags is
	 * absolute */
	if (!std::exchange(e.logged, true))
		std::printf("pointer: %s first packet: flags 0x%04x x=%ld y=%ld buttons 0x%04x\n",
		            e.device.name.c_str(), m.usFlags, m.lLastX, m.lLastY, m.usButtonFlags);
	if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {
		if (!e.device.absolute) {
			e.device.absolute = true;
			list();
		}
		e.x = m.lLastX;
		e.y = m.lLastY;
		e.desktop = (m.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
		e.seen = true;
	} else if (m.lLastX || m.lLastY) {
		e.rel_x += static_cast<float>(m.lLastX);
		e.rel_y += static_cast<float>(m.lLastY);
		e.seen = true;
	}
	for (size_t bit = 0; bit < std::size(BUTTONS); bit++) {
		if (m.usButtonFlags & BUTTONS[bit].first) {
			e.buttons |= 1u << bit;
			e.pressed |= 1u << bit;
		} else if (m.usButtonFlags & BUTTONS[bit].second) {
			e.buttons &= ~(1u << bit);
		}
	}
}

void
Pointers::Backend::list()
{
	devices.clear();
	for (const Entry &e : entries)
		devices.push_back(e.device);
}

/* Inside SDL_PollEvent, on the thread of the window, which every reader of
 * the list is on: a packet changes its device; a device that comes or goes
 * waits for poll() */
bool SDLCALL
Pointers::Backend::hook(void *self, MSG *msg)
{
	Backend &b = *static_cast<Backend *>(self);

	if (msg->message == WM_INPUT_DEVICE_CHANGE) {
		b.rescan = true;
	} else if (msg->message == WM_INPUT) {
		RAWINPUT raw;
		UINT size = sizeof(raw);

		if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, &raw, &size,
		                    sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
		    raw.header.dwType == RIM_TYPEMOUSE && raw.header.hDevice) {
			for (Entry &e : b.entries)
				if (e.handle == raw.header.hDevice)
					b.packet(e, raw.data.mouse);
		}
	}
	/* SDL hands it on; DefWindowProc then frees the packet */
	return true;
}

Pointers::Pointers(SDL_Window *window) : m_window(window), m_backend(std::make_unique<Backend>())
{
	Backend &b = *m_backend;

	b.hwnd = static_cast<HWND>(
		SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
	if (!b.hwnd) {
		std::fprintf(stderr, "ERROR: pointer: no window for Raw Input\n");
		return;
	}

	/* The mice (usage page 1, usage 2), the window in the background too,
	 * with their comings and goings */
	const RAWINPUTDEVICE device = { 0x01, 0x02, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, b.hwnd };

	if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
		std::fprintf(stderr, "ERROR: pointer: Raw Input not given (%lu)\n", GetLastError());
		return;
	}
	SDL_SetWindowsMessageHook(Backend::hook, &b);
	b.scan();
}

Pointers::~Pointers()
{
	const RAWINPUTDEVICE device = { 0x01, 0x02, RIDEV_REMOVE, nullptr };

	if (m_backend->hwnd) {
		RegisterRawInputDevices(&device, 1, sizeof(device));
		SDL_SetWindowsMessageHook(nullptr, nullptr);
	}
}

void
Pointers::poll()
{
	if (std::exchange(m_backend->rescan, false))
		m_backend->scan();
}

const std::vector<Pointers::Device> &
Pointers::devices() const
{
	return m_backend->devices;
}

/* A gun says where it points on the screen (or the whole desktop): in full
 * screen that is the window, as on Linux; in a window only the part of the
 * screen under it aims, as in PCSX2X6 */
std::optional<Pointers::Aim>
Pointers::take(const std::string &identity)
{
	Entry *const e = m_backend->find_identity(identity);
	int w = 0, h = 0;

	if (!e || !e->handle || !e->seen)
		return std::nullopt;
	SDL_GetWindowSizeInPixels(m_window, &w, &h);

	const float fw = static_cast<float>(std::max(w, 1)), fh = static_cast<float>(std::max(h, 1));
	Aim aim;

	if (e->device.absolute) {
		const bool all = e->desktop;
		const auto metric = [](int which) { return static_cast<float>(GetSystemMetrics(which)); };
		const float sw = metric(all ? SM_CXVIRTUALSCREEN : SM_CXSCREEN), sh = metric(all ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
		const float left = all ? metric(SM_XVIRTUALSCREEN) : 0.0f, top = all ? metric(SM_YVIRTUALSCREEN) : 0.0f;
		POINT origin = { 0, 0 };

		ClientToScreen(m_backend->hwnd, &origin);
		aim.x = static_cast<float>(e->x) / 65535.0f * sw + left - static_cast<float>(origin.x);
		aim.y = static_cast<float>(e->y) / 65535.0f * sh + top - static_cast<float>(origin.y);
	} else {
		if (!e->placed) {
			e->rel_x = fw / 2.0f;
			e->rel_y = fh / 2.0f;
			e->placed = true;
		}
		e->rel_x = std::clamp(e->rel_x, 0.0f, fw - 1.0f);
		e->rel_y = std::clamp(e->rel_y, 0.0f, fh - 1.0f);
		aim.x = e->rel_x;
		aim.y = e->rel_y;
	}
	aim.buttons = e->buttons;
	aim.pressed = std::exchange(e->pressed, 0u);
	return aim;
}

/* One cursor for every device: holding it in the window would lock the
 * user out of its title bar (PCSX2X6 only does in its relative mode, never
 * for guns) */
void
Pointers::hold(std::span<const std::string>)
{
}

} /* namespace odin */

#endif
