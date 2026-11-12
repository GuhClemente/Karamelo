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

#ifndef ODIN_SOCKETS_HPP
#define ODIN_SOCKETS_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

/* The TCP sockets of the computer, the same way on Linux and on Windows. A
 * socket is an intptr_t, NONE when there is none. */
namespace odin::sockets {

constexpr intptr_t NONE = -1;

/* Windows wants its sockets started once; true when they can be used */
bool network_up();
void close_socket(intptr_t s);
bool nonblocking(intptr_t s);
/* The last call failed only because it would have waited */
bool would_block();
/* What one call sends or receives: its count, 0 when the other end has
 * closed (receive), below 0 on an error */
long send_some(intptr_t s, const char *data, size_t size);
long receive_some(intptr_t s, char *data, size_t size);

/* A socket that waits for clients on `port`, of this computer only or of
 * every address; NONE when the port cannot be opened */
intptr_t listen_on(uint16_t port, bool this_computer_only);
/* The next client waiting, its bytes sent at once (no delay); NONE when
 * none waits on a listener that does not wait */
intptr_t accept_client(intptr_t listener);
/* `host` (a name or an address), port `port`, given up after `wait_ms`;
 * NONE when it does not answer */
intptr_t connect_to(const std::string &host, uint16_t port, int wait_ms);
/* Something to read, a client to accept, within `wait_ms` */
bool readable(intptr_t s, int wait_ms);
/* The same on two sockets: bit 0 for `a`, bit 1 for `b` */
unsigned readable(intptr_t a, intptr_t b, int wait_ms);

/* All of it, on a socket that waits; false when the socket fails */
bool send_all(intptr_t s, const void *data, size_t size);
/* All of it, on a socket that waits; false when it fails, closes, or
 * `stop` is set (looked at every 100 ms) */
bool receive_all(intptr_t s, void *data, size_t size, const std::atomic<bool> &stop);

} /* namespace odin::sockets */

#endif /* ODIN_SOCKETS_HPP */
