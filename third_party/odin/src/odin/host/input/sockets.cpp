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

#include "odin/host/input/sockets.hpp"

#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace odin::sockets {

#ifdef _WIN32
using Socket = SOCKET;

bool
network_up()
{
	static const bool up = [] {
		WSADATA data;

		return WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}();
	return up;
}

void close_socket(intptr_t s) { closesocket(static_cast<Socket>(s)); }

bool
nonblocking(intptr_t s)
{
	u_long on = 1;

	return ioctlsocket(static_cast<Socket>(s), FIONBIO, &on) == 0;
}

static bool
blocking(intptr_t s)
{
	u_long off = 0;

	return ioctlsocket(static_cast<Socket>(s), FIONBIO, &off) == 0;
}

bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }

static bool connect_pending() { return WSAGetLastError() == WSAEWOULDBLOCK; }

long
send_some(intptr_t s, const char *data, size_t size)
{
	return send(static_cast<Socket>(s), data, static_cast<int>(size), 0);
}

long
receive_some(intptr_t s, char *data, size_t size)
{
	return recv(static_cast<Socket>(s), data, static_cast<int>(size), 0);
}
#else
using Socket = int;

bool network_up() { return true; }

void close_socket(intptr_t s) { close(static_cast<Socket>(s)); }

bool
nonblocking(intptr_t s)
{
	const int flags = fcntl(static_cast<Socket>(s), F_GETFL, 0);

	return flags >= 0 && fcntl(static_cast<Socket>(s), F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool
blocking(intptr_t s)
{
	const int flags = fcntl(static_cast<Socket>(s), F_GETFL, 0);

	return flags >= 0 && fcntl(static_cast<Socket>(s), F_SETFL, flags & ~O_NONBLOCK) == 0;
}

bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK; }

static bool connect_pending() { return errno == EINPROGRESS; }

long
send_some(intptr_t s, const char *data, size_t size)
{
#ifdef MSG_NOSIGNAL
	return send(static_cast<Socket>(s), data, size, MSG_NOSIGNAL);
#else
	return send(static_cast<Socket>(s), data, size, 0);
#endif
}

long
receive_some(intptr_t s, char *data, size_t size)
{
	return recv(static_cast<Socket>(s), data, size, 0);
}
#endif

/* Its bytes leave at once, and a peer gone does not stop Odin */
static void
no_delay(intptr_t s)
{
	const int on = 1;

	setsockopt(static_cast<Socket>(s), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&on), sizeof on);
#ifdef SO_NOSIGPIPE
	/* Where send has no MSG_NOSIGNAL */
	setsockopt(static_cast<Socket>(s), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
}

intptr_t
listen_on(uint16_t port, bool this_computer_only)
{
	const intptr_t listener = static_cast<intptr_t>(socket(AF_INET, SOCK_STREAM, 0));
	sockaddr_in address{};

	if (listener == NONE)
		return NONE;
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(this_computer_only ? INADDR_LOOPBACK : INADDR_ANY);
#ifndef _WIN32
	/* Odin run again at once takes the port back */
	const int on = 1;

	setsockopt(static_cast<Socket>(listener), SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#endif
	if (bind(static_cast<Socket>(listener), reinterpret_cast<const sockaddr *>(&address), sizeof address) != 0 ||
	    listen(static_cast<Socket>(listener), 8) != 0) {
		close_socket(listener);
		return NONE;
	}
	return listener;
}

intptr_t
accept_client(intptr_t listener)
{
	const intptr_t client = static_cast<intptr_t>(accept(static_cast<Socket>(listener), nullptr, nullptr));

	if (client != NONE)
		no_delay(client);
	return client;
}

bool
readable(intptr_t s, int wait_ms)
{
	return readable(s, s, wait_ms) != 0;
}

/* The wait of select() */
static timeval
waiting(int wait_ms)
{
	return timeval{ wait_ms / 1000, (wait_ms % 1000) * 1000 };
}

unsigned
readable(intptr_t a, intptr_t b, int wait_ms)
{
	fd_set set;
	timeval wait = waiting(wait_ms);

	FD_ZERO(&set);
	FD_SET(static_cast<Socket>(a), &set);
	FD_SET(static_cast<Socket>(b), &set);
	if (select(static_cast<int>(std::max(a, b) + 1), &set, nullptr, nullptr, &wait) <= 0)
		return 0;
	return (FD_ISSET(static_cast<Socket>(a), &set) ? 1u : 0u) | (FD_ISSET(static_cast<Socket>(b), &set) ? 2u : 0u);
}

intptr_t
connect_to(const std::string &host, uint16_t port, int wait_ms)
{
	addrinfo hints{};
	addrinfo *found = nullptr;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found)
		return NONE;

	intptr_t s = static_cast<intptr_t>(socket(found->ai_family, found->ai_socktype, found->ai_protocol));

	if (s != NONE && nonblocking(s)) {
		bool connected = connect(static_cast<Socket>(s), found->ai_addr, static_cast<int>(found->ai_addrlen)) == 0;

		if (!connected && connect_pending()) {
			fd_set set;
			timeval wait = waiting(wait_ms);
			int error = 0;
			socklen_t length = sizeof error;

			FD_ZERO(&set);
			FD_SET(static_cast<Socket>(s), &set);
			connected = select(static_cast<int>(s + 1), nullptr, &set, nullptr, &wait) > 0 &&
			            getsockopt(static_cast<Socket>(s), SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error),
			                       &length) == 0 && error == 0;
		}
		if (connected && blocking(s)) {
			no_delay(s);
		} else {
			close_socket(s);
			s = NONE;
		}
	} else if (s != NONE) {
		close_socket(s);
		s = NONE;
	}
	freeaddrinfo(found);
	return s;
}

bool
send_all(intptr_t s, const void *data, size_t size)
{
	const char *bytes = static_cast<const char *>(data);

	while (size > 0) {
		const long sent = send_some(s, bytes, size);

		if (sent <= 0)
			return false;
		bytes += sent;
		size -= static_cast<size_t>(sent);
	}
	return true;
}

bool
receive_all(intptr_t s, void *data, size_t size, const std::atomic<bool> &stop)
{
	char *bytes = static_cast<char *>(data);

	while (size > 0) {
		if (stop)
			return false;
		if (!readable(s, 100))
			continue;

		const long got = receive_some(s, bytes, size);

		if (got <= 0)
			return false;
		bytes += got;
		size -= static_cast<size_t>(got);
	}
	return true;
}

} /* namespace odin::sockets */
