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

#include "odin/host/input/gun_serial.hpp"

#include <cstdio>
#include <cstring>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#else
#include <cerrno>
#include <filesystem>
#include <fstream>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace odin {

namespace {

constexpr unsigned XGUNNER_VENDOR = 0x1209;

#ifdef _WIN32

/* {4D36E978-E325-11CE-BFC1-08002BE10318}: the class of the serial ports */
constexpr GUID PORTS = { 0x4d36e978, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };

/* Its COM port: of the ports plugged in, the one whose hardware id names
 * that VID and PID */
std::string
port_of(unsigned product)
{
	char wanted[32];

	std::snprintf(wanted, sizeof(wanted), "VID_%04X&PID_%04X", XGUNNER_VENDOR, product);

	const HDEVINFO set = SetupDiGetClassDevsA(&PORTS, nullptr, nullptr, DIGCF_PRESENT);
	std::string port;

	if (set == INVALID_HANDLE_VALUE)
		return port;

	SP_DEVINFO_DATA info{};

	info.cbSize = sizeof(info);
	for (DWORD i = 0; port.empty() && SetupDiEnumDeviceInfo(set, i, &info); i++) {
		char ids[512] = {};

		if (!SetupDiGetDeviceRegistryPropertyA(set, &info, SPDRP_HARDWAREID, nullptr, reinterpret_cast<BYTE *>(ids),
		                                       sizeof(ids) - 2, nullptr) ||
		    !std::strstr(ids, wanted))
			continue;

		const HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
		char name[32] = {};
		DWORD size = sizeof(name) - 1;

		if (key == INVALID_HANDLE_VALUE)
			continue;
		if (RegQueryValueExA(key, "PortName", nullptr, nullptr, reinterpret_cast<BYTE *>(name), &size) == ERROR_SUCCESS)
			port = name;
		RegCloseKey(key);
	}
	SetupDiDestroyDeviceInfoList(set);
	return port;
}

#else

/* A number of a file of sysfs, in hexadecimal; 0 without one */
unsigned
hex_of(const std::filesystem::path &file)
{
	std::ifstream in(file);
	unsigned value = 0;

	in >> std::hex >> value;
	return value;
}

/* Its port: the ttyACM whose USB device has that VID and PID */
std::string
port_of(unsigned product)
{
	namespace fs = std::filesystem;
	std::error_code error;

	for (const fs::directory_entry &tty : fs::directory_iterator("/sys/class/tty", error)) {
		const std::string name = tty.path().filename().string();

		if (!name.starts_with("ttyACM"))
			continue;

		/* The port is an interface of the USB device, which names it */
		const fs::path usb = fs::canonical(tty.path() / "device", error).parent_path();

		if (!error && hex_of(usb / "idVendor") == XGUNNER_VENDOR && hex_of(usb / "idProduct") == product)
			return "/dev/" + name;
	}
	return {};
}

#endif

} /* namespace */

std::optional<unsigned>
xgunner_player(std::string_view identity, std::string_view name)
{
	unsigned vendor = 0, product = 0;

	if (std::sscanf(std::string(identity).c_str(), "vidpid:%x:%x", &vendor, &product) == 2)
		return vendor == XGUNNER_VENDOR && product >= 1 && product <= 4 ? std::optional(product) : std::nullopt;
	for (const std::string_view mark : { "XGUNNER-P", "XGunner P" }) {
		const size_t at = name.find(mark);

		if (at != std::string_view::npos && at + mark.size() < name.size() && name[at + mark.size()] >= '1' &&
		    name[at + mark.size()] <= '4')
			return static_cast<unsigned>(name[at + mark.size()] - '0');
	}
	return std::nullopt;
}

bool
GunSerial::shakes(std::string_view identity, std::string_view name)
{
	return xgunner_player(identity, name).has_value();
}

std::unique_ptr<GunSerial>
GunSerial::open(std::string_view identity, std::string_view name)
{
	const std::optional<unsigned> product = xgunner_player(identity, name);

	if (!product)
		return nullptr;

	const std::string port = port_of(*product);

	if (port.empty()) {
		std::fprintf(stderr, "ERROR: gun: no serial port for the XGunner P%u: no vibration or recoil\n",
		             *product);
		return nullptr;
	}
#ifdef _WIN32
	const HANDLE handle = CreateFileA(("\\\\.\\" + port).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
	                                  OPEN_EXISTING, 0, nullptr);

	if (handle == INVALID_HANDLE_VALUE) {
		std::fprintf(stderr,
		             "ERROR: gun: %s cannot be opened (%lu): no vibration or recoil for the XGunner P%u\n",
		             port.c_str(), GetLastError(), *product);
		return nullptr;
	}

	DCB line{};
	COMMTIMEOUTS waits{};

	line.DCBlength = sizeof(line);
	GetCommState(handle, &line);
	line.BaudRate = CBR_9600;
	line.ByteSize = 8;
	line.Parity = NOPARITY;
	line.StopBits = ONESTOPBIT;
	SetCommState(handle, &line);
	/* A command is two bytes: never waited for long */
	waits.WriteTotalTimeoutConstant = 20;
	SetCommTimeouts(handle, &waits);

	std::unique_ptr<GunSerial> gun(new GunSerial(reinterpret_cast<intptr_t>(handle)));
#else
	const int fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

	if (fd < 0) {
		std::fprintf(stderr,
		             "ERROR: gun: %s: %s (add your user to the port's group: uucp or dialout): no vibration or "
		             "recoil for the XGunner P%u\n",
		             port.c_str(), std::strerror(errno), *product);
		return nullptr;
	}

	termios line{};

	tcgetattr(fd, &line);
	cfmakeraw(&line);
	cfsetispeed(&line, B9600);
	cfsetospeed(&line, B9600);
	line.c_cflag &= ~static_cast<tcflag_t>(PARENB | CSTOPB);
	line.c_cflag |= CLOCAL | CREAD | CS8;
	tcsetattr(fd, TCSANOW, &line);

	std::unique_ptr<GunSerial> gun(new GunSerial(fd));
#endif
	std::printf("gun: XGunner P%u on %s, vibration and recoil driven by the game\n", *product, port.c_str());
	gun->send("S6");
	return gun;
}

GunSerial::~GunSerial()
{
	send("E");
#ifdef _WIN32
	CloseHandle(reinterpret_cast<HANDLE>(m_handle));
#else
	::close(static_cast<int>(m_handle));
#endif
}

/* Written at once: a port that is full drops it, the game never waits */
void
GunSerial::send(const char *command)
{
	const size_t size = std::strlen(command);
#ifdef _WIN32
	DWORD written = 0;

	WriteFile(reinterpret_cast<HANDLE>(m_handle), command, static_cast<DWORD>(size), &written, nullptr);
#else
	if (::write(static_cast<int>(m_handle), command, size) < 0 && errno != EAGAIN)
		std::fprintf(stderr, "ERROR: gun: %s\n", std::strerror(errno));
#endif
}

} /* namespace odin */
