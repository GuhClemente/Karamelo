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

/* The pointers on Linux: each event node of /dev/input that is a light gun
 * or a mouse, read without the desktop's cursor; nodes followed as they
 * come and go (inotify). From xemu's ui/xemu-pointer-evdev.c. */

#include "odin/host/input/pointers.hpp"

#include "odin/host/input/evdev_bits.hpp"
#include "odin/host/input/pointer_names.hpp"
#include "odin/host/input/seats.hpp"

#ifdef __linux__

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>
#include <filesystem>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace odin {

namespace {

/* The bit of a button in Pointers::Aim, -1 for none: a touch is the left
 * button of a device that has none */
int
button_bit(uint16_t code, bool touch_is_left)
{
	switch (code) {
	case BTN_LEFT:
		return 0;
	case BTN_MIDDLE:
		return 1;
	case BTN_RIGHT:
		return 2;
	case BTN_SIDE:
		return 3;
	case BTN_EXTRA:
		return 4;
	case BTN_TOUCH:
		return touch_is_left ? 0 : -1;
	default:
		return code >= BTN_1 && code <= BTN_8 ? 5 + (code - BTN_1) : -1;
	}
}

/* A text the node gives (EVIOCGNAME...), without the spaces at its end */
std::string
trimmed_text(char *text)
{
	std::string s = text;

	while (!s.empty() && (s.back() == '\n' || s.back() == ' '))
		s.pop_back();
	return s;
}

/* A key or a button of the node held down */
bool
any_down(int fd)
{
	std::array<unsigned long, longs_for(KEY_MAX)> key{};

	return ioctl(fd, EVIOCGKEY(sizeof(key)), key.data()) >= 0 &&
	       std::any_of(key.begin(), key.end(), [](unsigned long bits) { return bits != 0; });
}

/* A device of a node: where it points (absolute, in its units; relative,
 * in pixels of the window), its buttons */
struct Entry {
	Pointers::Device device;
	int fd = -1;
	std::string node;
	bool touch_is_left = false, held = false, seen = false, placed = false;
	input_absinfo abs_x{}, abs_y{};
	int x = 0, y = 0;
	float rel_x = 0.0f, rel_y = 0.0f;
	uint32_t buttons = 0, pressed = 0;
};

} /* namespace */

struct Pointers::Backend {
	std::vector<Entry> entries;
	std::vector<Device> devices;
	int notify = -1;

	Entry *find_identity(const std::string &identity)
	{
		for (Entry &e : entries)
			if (e.device.identity == identity)
				return &e;
		return nullptr;
	}

	void close_entry(Entry &e)
	{
		if (e.fd < 0)
			return;
		if (e.held)
			ioctl(e.fd, EVIOCGRAB, 0);
		close(e.fd);
		e.fd = -1;
		e.held = e.seen = false;
		e.device.present = false;
		e.buttons = e.pressed = 0;
	}

	void open_node(const std::string &node);
	void scan();
	void resync(Entry &e);
	void event(Entry &e, const input_event &ev);
	void drain(Entry &e);
	void list();
};

/* A pointer: a left button (or a touch) and X and Y, absolute or
 * relative; not a gamepad nor a joystick, unless it has a left button (a
 * light gun merged with its joystick buttons, the GUN4IR of Batocera) */
void
Pointers::Backend::open_node(const std::string &node)
{
	for (const Entry &e : entries)
		if (e.fd >= 0 && e.node == node)
			return;

	const int fd = open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0)
		return;

	std::array<unsigned long, longs_for(EV_MAX)> ev{};
	std::array<unsigned long, longs_for(KEY_MAX)> key{};
	std::array<unsigned long, longs_for(ABS_MAX)> abs{};
	std::array<unsigned long, longs_for(REL_MAX)> rel{};

	ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev.data());
	ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key.data());
	ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs.data());
	ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel.data());

	const bool left = has_bit(key, BTN_LEFT), pointer = left || has_bit(key, BTN_TOUCH);
	const bool pad = !left && (has_bit(key, BTN_SOUTH) || has_bit(key, BTN_JOYSTICK) || has_bit(key, BTN_TRIGGER));
	bool absolute = has_bit(ev, EV_ABS) && has_bit(abs, ABS_X) && has_bit(abs, ABS_Y);
	const bool relative = has_bit(ev, EV_REL) && has_bit(rel, REL_X) && has_bit(rel, REL_Y);
	input_absinfo ax{}, ay{};

	if (absolute && (ioctl(fd, EVIOCGABS(ABS_X), &ax) < 0 || ioctl(fd, EVIOCGABS(ABS_Y), &ay) < 0 ||
	                 ax.maximum <= ax.minimum || ay.maximum <= ay.minimum))
		absolute = false;
	if (!pointer || pad || !(absolute || relative)) {
		close(fd);
		return;
	}

	char name_text[80] = "", phys_text[96] = "", uniq_text[64] = "";
	input_id id{};

	ioctl(fd, EVIOCGNAME(sizeof(name_text) - 1), name_text);
	ioctl(fd, EVIOCGPHYS(sizeof(phys_text) - 1), phys_text);
	ioctl(fd, EVIOCGUNIQ(sizeof(uniq_text) - 1), uniq_text);
	ioctl(fd, EVIOCGID, &id);

	const std::string name = trimmed_text(name_text), phys = trimmed_text(phys_text);
	const std::string uniq = trimmed_text(uniq_text);

	/* A serial under 8 characters is a placeholder of the firmware, the
	 * same in every unit; two of a kind take their port, then their node */
	std::string identity = uniq.size() >= 8               ? "uniq:" + identity_text(uniq) :
	                       id.vendor && id.product         ? vidpid_identity(id.vendor, id.product) :
	                       !phys.empty()                   ? "path:" + identity_text(phys) :
	                       !name.empty()                   ? "name:" + identity_text(name) : "node:" + node;
	Entry *e = find_identity(identity);

	if (e && e->fd >= 0 && !phys.empty())
		e = find_identity(identity = "path:" + identity_text(phys));
	if (e && e->fd >= 0)
		e = find_identity(identity = "node:" + node);
	if (!e) {
		if (entries.size() >= MOST) {
			close(fd);
			return;
		}
		entries.push_back({});
		e = &entries.back();
		e->device.identity = identity;
	}
	close_entry(*e);
	e->fd = fd;
	e->node = node;
	e->device.name = pointer_name(name, id.vendor, id.product);
	e->device.present = true;
	e->device.absolute = absolute;
	e->touch_is_left = !left;
	e->abs_x = ax;
	e->abs_y = ay;
	e->x = ax.value;
	e->y = ay.value;
	e->placed = false;
	std::printf("pointer: %s = %s (%s, %s)\n", e->device.name.c_str(), node.c_str(),
	            absolute ? "aims" : "moves", e->device.identity.c_str());
}

void
Pointers::Backend::scan()
{
	std::error_code error;

	for (Entry &e : entries) {
		if (e.fd >= 0 && access(e.node.c_str(), F_OK) != 0) {
			std::printf("pointer: %s unplugged\n", e.device.name.c_str());
			close_entry(e);
		}
	}
	for (const auto &file : std::filesystem::directory_iterator("/dev/input", error))
		if (file.path().filename().string().starts_with("event"))
			open_node(file.path().string());
	list();
}

/* The kernel dropped events: the state read again rather than the stream
 * trusted */
void
Pointers::Backend::resync(Entry &e)
{
	static constexpr uint16_t CODES[] = { BTN_LEFT, BTN_MIDDLE, BTN_RIGHT, BTN_SIDE, BTN_EXTRA, BTN_TOUCH, BTN_1,
		                              BTN_2,    BTN_3,      BTN_4,     BTN_5,    BTN_6,     BTN_7,     BTN_8 };
	std::array<unsigned long, longs_for(KEY_MAX)> key{};

	if (e.device.absolute && ioctl(e.fd, EVIOCGABS(ABS_X), &e.abs_x) >= 0 &&
	    ioctl(e.fd, EVIOCGABS(ABS_Y), &e.abs_y) >= 0) {
		e.x = e.abs_x.value;
		e.y = e.abs_y.value;
		e.seen = true;
	}
	if (ioctl(e.fd, EVIOCGKEY(sizeof(key)), key.data()) < 0)
		return;
	e.buttons = 0;
	for (const uint16_t code : CODES)
		if (const int bit = button_bit(code, e.touch_is_left); bit >= 0 && has_bit(key, code))
			e.buttons |= 1u << bit;
	e.pressed |= e.buttons;
}

void
Pointers::Backend::event(Entry &e, const input_event &ev)
{
	switch (ev.type) {
	case EV_ABS:
		if (ev.code == ABS_X || ev.code == ABS_Y) {
			(ev.code == ABS_X ? e.x : e.y) = ev.value;
			e.seen = true;
		}
		break;
	case EV_REL:
		if (ev.code == REL_X || ev.code == REL_Y) {
			(ev.code == REL_X ? e.rel_x : e.rel_y) += static_cast<float>(ev.value);
			e.seen = true;
		}
		break;
	case EV_KEY:
		if (const int bit = button_bit(ev.code, e.touch_is_left); bit >= 0 && ev.value <= 1) {
			if (ev.value) {
				e.buttons |= 1u << bit;
				e.pressed |= 1u << bit;
			} else {
				e.buttons &= ~(1u << bit);
			}
		}
		break;
	case EV_SYN:
		if (ev.code == SYN_DROPPED)
			resync(e);
		break;
	default:
		break;
	}
}

void
Pointers::Backend::drain(Entry &e)
{
	std::array<input_event, 64> events;

	for (;;) {
		const ssize_t got = read(e.fd, events.data(), sizeof(events));

		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			std::printf("pointer: %s unplugged\n", e.device.name.c_str());
			close_entry(e);
			list();
			return;
		}
		if (got <= 0)
			return;
		for (size_t i = 0; i < static_cast<size_t>(got) / sizeof(input_event); i++)
			event(e, events[i]);
	}
}

void
Pointers::Backend::list()
{
	devices.clear();
	for (const Entry &e : entries)
		devices.push_back(e.device);
}

Pointers::Pointers(SDL_Window *window) : m_window(window), m_backend(std::make_unique<Backend>())
{
	m_backend->scan();
	/* IN_ATTRIB: udev gives the node its rights after it is made */
	m_backend->notify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (m_backend->notify >= 0)
		inotify_add_watch(m_backend->notify, "/dev/input", IN_CREATE | IN_DELETE | IN_ATTRIB);
}

Pointers::~Pointers()
{
	for (Entry &e : m_backend->entries)
		m_backend->close_entry(e);
	if (m_backend->notify >= 0)
		close(m_backend->notify);
}

void
Pointers::poll()
{
	Backend &b = *m_backend;

	if (b.notify >= 0) {
		alignas(inotify_event) char buffer[4096];
		bool changed = false;
		ssize_t got;

		while ((got = read(b.notify, buffer, sizeof(buffer))) > 0) {
			for (char *at = buffer; at < buffer + got;) {
				const auto *const ie = reinterpret_cast<const inotify_event *>(at);

				changed = changed || (ie->len > 0 && std::strncmp(ie->name, "event", 5) == 0);
				at += sizeof(inotify_event) + ie->len;
			}
		}
		if (changed)
			b.scan();
	}
	for (Entry &e : b.entries)
		if (e.fd >= 0)
			b.drain(e);
}

const std::vector<Pointers::Device> &
Pointers::devices() const
{
	return m_backend->devices;
}

/* A gun spans the whole window: in full screen, the screen it is set for */
std::optional<Pointers::Aim>
Pointers::take(const std::string &identity)
{
	Entry *const e = m_backend->find_identity(identity);
	int w = 0, h = 0;

	if (!e || e->fd < 0 || !e->seen)
		return std::nullopt;
	SDL_GetWindowSizeInPixels(m_window, &w, &h);

	const float fw = static_cast<float>(std::max(w, 1)), fh = static_cast<float>(std::max(h, 1));
	Aim aim;

	/* Of a range: how far into it */
	const auto into = [](int value, const input_absinfo &range) {
		return static_cast<float>(value - range.minimum) / static_cast<float>(range.maximum - range.minimum);
	};

	if (e->device.absolute) {
		aim.x = into(e->x, e->abs_x) * fw;
		aim.y = into(e->y, e->abs_y) * fh;
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

void
Pointers::hold(std::span<const std::string> identities)
{
	for (Entry &e : m_backend->entries) {
		const bool want =
			e.fd >= 0 && std::find(identities.begin(), identities.end(), e.device.identity) != identities.end();

		if (e.fd < 0 || want == e.held)
			continue;
		if (!want) {
			ioctl(e.fd, EVIOCGRAB, 0);
			e.held = false;
			continue;
		}
		/* Taken with all its buttons up, looked at again once taken: one
		 * down as it is taken stays down for the desktop, which never sees
		 * it let go */
		if (any_down(e.fd) || ioctl(e.fd, EVIOCGRAB, 1) < 0)
			continue;
		if (any_down(e.fd))
			ioctl(e.fd, EVIOCGRAB, 0);
		else
			e.held = true;
	}
}

} /* namespace odin */

#endif
