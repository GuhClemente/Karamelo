#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// accept()/getsockopt() take an int* for the length out-param on Winsock,
// where POSIX's socklen_t is unsigned - declaring the locals as socklen_t on
// both platforms means the call sites need no #ifdef of their own.
typedef int socklen_t;
#else
// getaddrinfo/freeaddrinfo/inet_ntop/gethostname/ntohl/setsockopt/getsockopt/
// bind/listen/accept/connect/send/recv/select and the sockaddr* types are the
// same shape on POSIX as on Winsock - only the handful of Windows-only names
// below need a stand-in. FIONBIO is defined by <sys/ioctl.h> on Linux, same
// as ioctlsocket's own contract.
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
typedef int SOCKET;
typedef int BOOL;
typedef unsigned long u_long;
#define TRUE 1
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket(s) close(s)
#define ioctlsocket(s, cmd, argp) ioctl(s, cmd, argp)
#define WSAGetLastError() errno
#define WSAEWOULDBLOCK EWOULDBLOCK
static inline void WSACleanup() {}
// Both call sites in this file pass _TRUNCATE, i.e. "always null-terminate,
// silently drop what doesn't fit" - the one behavior this stand-in needs to
// match, not a general strncpy_s replacement.
#define _TRUNCATE ((size_t)-1)
static inline void strncpy_s(char* dst, size_t dst_size, const char* src, size_t) {
	strncpy(dst, src, dst_size - 1);
	dst[dst_size - 1] = '\0';
}
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <mutex>
#include <chrono>

#include "netplay.h"
#include "core_runner.h"
#include "netplay_protocol.h"


static NetplayRole  net_role = NETPLAY_NONE;
static NetplayState net_state = NETPLAY_DISCONNECTED;
static SOCKET       listen_sock = INVALID_SOCKET;
static SOCKET       peer_sock = INVALID_SOCKET;
static std::string  local_ip_str = "127.0.0.1";
static std::string  status_str = "Disconnected";
static int          ping_ms = 0;
static uint32_t     frame_counter = 0;

static int16_t remote_buttons[16] = { 0 };
static int16_t remote_analog[2][2] = { 0 };

// Connection management runs on the UI thread while input exchange runs on the
// core thread, so every socket touch is serialised. Recursive: NetplayUpdate()
// and NetplaySyncInputs() call NetplayDisconnect() while already holding it.
static std::recursive_mutex net_lock;

// Partial deliveries are normal on TCP; leftovers live here until the rest
// arrives.
static uint8_t  rx_buf[sizeof(NetPacket) * 32];
static size_t   rx_len = 0;

static uint32_t last_peer_tick = 0;   // newest send_tick seen, echoed back
static uint32_t pending_tick = 0;     // our send_tick awaiting an echo

// Milliseconds on a monotonic clock, truncated to 32 bits like the old
// GetTickCount() this replaces - only ever used for round-trip deltas
// (send_tick echoed back by the peer), so the wraparound and the fact that
// both sides run their own independent clock are both fine.
static uint32_t TickCountMs()
{
	using namespace std::chrono;
	return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void SetSocketNonBlocking(SOCKET s)
{
	u_long mode = 1;
	ioctlsocket(s, FIONBIO, &mode);
}

static void ConfigurePeerSocket(SOCKET s)
{
	SetSocketNonBlocking(s);

	// Without this, Nagle coalesces our 54-byte per-frame packets and adds up
	// to 40ms of latency - fatal for a netplay link.
	BOOL nodelay = TRUE;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
}

void NetplayInit()
{
#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
#endif

	// gethostbyname returned only the first address, which on a machine with a
	// virtual display or VPN adapter is usually the wrong one - and this is the
	// address the user reads out to a friend. Walk every address and prefer a
	// private LAN range.
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)) != 0) return;

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo* results = NULL;
	if (getaddrinfo(hostname, NULL, &hints, &results) != 0) return;

	std::string first_seen;
	for (struct addrinfo* it = results; it; it = it->ai_next)
	{
		if (it->ai_family != AF_INET) continue;

		struct sockaddr_in* sa = (struct sockaddr_in*)it->ai_addr;
		char buf[INET_ADDRSTRLEN] = { 0 };
		if (!inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf))) continue;

		uint32_t ip = ntohl(sa->sin_addr.s_addr);
		uint8_t a = (uint8_t)(ip >> 24), b = (uint8_t)(ip >> 16);

		if (a == 127) continue;                       // loopback
		if (a == 169 && b == 254) continue;           // link-local, no route

		if (first_seen.empty()) first_seen = buf;

		bool is_lan = (a == 192 && b == 168) || (a == 10) ||
		              (a == 172 && b >= 16 && b <= 31);
		if (is_lan) { local_ip_str = buf; freeaddrinfo(results); return; }
	}

	if (!first_seen.empty()) local_ip_str = first_seen;
	freeaddrinfo(results);
}

void NetplayShutdown()
{
	NetplayDisconnect();
	WSACleanup();
}

// local_ip_str/status_str are reassigned under net_lock from the core thread
// (NetplaySyncInputs's send-error path calls NetplayDisconnect()) as well as
// the UI thread, so handing out .c_str() of the live std::string without the
// lock is the same dangling-pointer-on-reallocation hazard already fixed for
// core_runner.cpp's loaded_game_name/loaded_rom_dir - copy into a thread_local
// buffer under the lock instead.
const char* NetplayGetLocalIp()
{
	static thread_local char buf[64];
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	strncpy_s(buf, sizeof(buf), local_ip_str.c_str(), _TRUNCATE);
	return buf;
}

NetplayRole NetplayGetRole()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	return net_role;
}

NetplayState NetplayGetState()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	return net_state;
}

const char* NetplayGetStatusString()
{
	static thread_local char buf[160];
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	strncpy_s(buf, sizeof(buf), status_str.c_str(), _TRUNCATE);
	return buf;
}

int NetplayGetPingMs()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	return ping_ms;
}

// NetplayStartHost/StartClient/Disconnect are called from the UI thread
// (the Netplay menu) while NetplaySyncInputs()/NetplayUpdate() touch these
// same globals - peer_sock included, mid send()/recv() - from the core
// thread under net_lock. Without taking the same lock here, a disconnect
// during an active session can close peer_sock (and hand its value back to
// the OS to reuse) at the exact moment the core thread is using it.
bool NetplayStartHost(int port)
{
	NetplayDisconnect();

	{
		std::lock_guard<std::recursive_mutex> lock(net_lock);

		listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listen_sock == INVALID_SOCKET) return false;

		// Without this, re-hosting within the TIME_WAIT window fails with
		// WSAEADDRINUSE - about two minutes of "port busy" after every session.
		BOOL reuse = TRUE;
		setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

		SetSocketNonBlocking(listen_sock);

		sockaddr_in addr = { 0 };
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons((u_short)port);

		if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			closesocket(listen_sock);
			listen_sock = INVALID_SOCKET;
			return false;
		}

		if (listen(listen_sock, 1) == SOCKET_ERROR)
		{
			closesocket(listen_sock);
			listen_sock = INVALID_SOCKET;
			return false;
		}

		net_role = NETPLAY_HOST;
		net_state = NETPLAY_LISTENING;
		status_str = "Hosting (Waiting for Player 2 on port " + std::to_string(port) + ")...";
	}
	CoreSetToast("NETPLAY: HOSTING SESSION", 120);
	return true;
}

bool NetplayStartClient(const char* host_ip, int port)
{
	NetplayDisconnect();

	{
		std::lock_guard<std::recursive_mutex> lock(net_lock);

		peer_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (peer_sock == INVALID_SOCKET) return false;

		sockaddr_in addr = { 0 };
		addr.sin_family = AF_INET;
		addr.sin_port = htons((u_short)port);
		addr.sin_addr.s_addr = inet_addr(host_ip);

		ConfigurePeerSocket(peer_sock);
		rx_len = 0;

		connect(peer_sock, (sockaddr*)&addr, sizeof(addr));

		net_role = NETPLAY_CLIENT;
		net_state = NETPLAY_CONNECTING;
		status_str = "Connecting to Host (" + std::string(host_ip) + ")...";
	}
	CoreSetToast("NETPLAY: CONNECTING TO HOST...", 120);
	return true;
}

void NetplayDisconnect()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);

	if (peer_sock != INVALID_SOCKET)
	{
		closesocket(peer_sock);
		peer_sock = INVALID_SOCKET;
	}
	if (listen_sock != INVALID_SOCKET)
	{
		closesocket(listen_sock);
		listen_sock = INVALID_SOCKET;
	}

	net_role = NETPLAY_NONE;
	net_state = NETPLAY_DISCONNECTED;
	status_str = "Disconnected";
	memset(remote_buttons, 0, sizeof(remote_buttons));
	memset(remote_analog, 0, sizeof(remote_analog));
	rx_len = 0;
	last_peer_tick = 0;
	pending_tick = 0;
	ping_ms = 0;
}

void NetplayUpdate()
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);
	frame_counter++;

	if (net_role == NETPLAY_HOST && net_state == NETPLAY_LISTENING)
	{
		sockaddr_in client_addr;
		socklen_t addr_len = sizeof(client_addr);
		SOCKET s = accept(listen_sock, (sockaddr*)&client_addr, &addr_len);
		if (s != INVALID_SOCKET)
		{
			peer_sock = s;
			ConfigurePeerSocket(peer_sock);
			rx_len = 0;
			closesocket(listen_sock);
			listen_sock = INVALID_SOCKET;

			net_state = NETPLAY_CONNECTED;
			status_str = "Connected with Player 2 (" + std::string(inet_ntoa(client_addr.sin_addr)) + ")";
			CoreSetToast("PLAYER 2 CONNECTED VIA NETPLAY!", 180);
		}
	}
	else if (net_role == NETPLAY_CLIENT && net_state == NETPLAY_CONNECTING)
	{
		fd_set write_fds;
		FD_ZERO(&write_fds);
		FD_SET(peer_sock, &write_fds);

		timeval tv = { 0, 0 };
		// nfds (1st arg): Winsock ignores it entirely, kept only for BSD-socket
		// compatibility, so "0" was harmless there and this went unnoticed for
		// as long as this file only ever ran on Windows. POSIX select() takes
		// it literally - "highest fd in any set, plus 1" - so 0 means "examine
		// no descriptors at all": peer_sock's writability was never actually
		// checked, res was always <= 0, and a Linux client sat in
		// NETPLAY_CONNECTING forever no matter how fast the host accepted it.
		int res = select((int)peer_sock + 1, NULL, &write_fds, NULL, &tv);
		if (res > 0)
		{
			int so_error = 0;
			socklen_t len = sizeof(so_error);
			getsockopt(peer_sock, SOL_SOCKET, SO_ERROR, (char*)&so_error, &len);
			if (so_error == 0)
			{
				net_state = NETPLAY_CONNECTED;
				status_str = "Connected to Host as Player 2!";
				CoreSetToast("CONNECTED TO HOST VIA NETPLAY!", 180);
			}
			else
			{
				NetplayDisconnect();
				status_str = "Connection refused";
				CoreSetToast("NETPLAY: CONNECTION REFUSED", 180);
			}
		}
	}

	// Drain the socket and frame whatever arrived.
	//
	// The old code did one recv() straight into a struct and accepted it only
	// when it happened to deliver exactly one packet. TCP does not work that
	// way: a split delivery was silently discarded with its bytes already
	// consumed, leaving the stream misaligned for good, and a burst was read
	// one packet per frame so latency grew without bound.
	if (net_state == NETPLAY_CONNECTED && peer_sock != INVALID_SOCKET)
	{
		for (;;)
		{
			if (rx_len >= sizeof(rx_buf)) break;   // parse what we have first

			int bytes = recv(peer_sock, (char*)rx_buf + rx_len,
			                 (int)(sizeof(rx_buf) - rx_len), 0);

			if (bytes > 0) { rx_len += (size_t)bytes; continue; }

			if (bytes == 0)
			{
				NetplayDisconnect();
				CoreSetToast("NETPLAY: PEER DISCONNECTED", 120);
				return;
			}

			// SOCKET_ERROR: only WOULDBLOCK means "nothing right now". A real
			// error used to be indistinguishable from an idle socket, so a
			// dropped connection was never noticed.
			int err = WSAGetLastError();
			if (err != WSAEWOULDBLOCK)
			{
				NetplayDisconnect();
				CoreSetToast("NETPLAY: CONNECTION LOST", 120);
				return;
			}
			break;
		}

		NetPacket packets[32];
		size_t got = 0;
		size_t consumed = NetplayParseStream(rx_buf, rx_len, packets, 32, &got);

		if (consumed > 0)
		{
			memmove(rx_buf, rx_buf + consumed, rx_len - consumed);
			rx_len -= consumed;
		}

		// Only the newest input matters; older ones are already stale.
		for (size_t i = 0; i < got; i++)
		{
			const NetPacket& pk = packets[i];
			if (pk.type != NET_TYPE_INPUT) continue;

			memcpy(remote_buttons, pk.buttons, sizeof(remote_buttons));
			memcpy(remote_analog, pk.analog, sizeof(remote_analog));

			last_peer_tick = pk.send_tick;

			// Real round trip: the peer echoes back the timestamp we sent, so
			// this measures the loop instead of the packet interval. The old
			// version subtracted consecutive arrival times and therefore always
			// reported roughly one frame, whatever the actual latency was.
			if (pk.echo_tick != 0 && pk.echo_tick == pending_tick)
			{
				ping_ms = (int)(TickCountMs() - pk.echo_tick);
				pending_tick = 0;
			}
		}
	}

}

void NetplaySyncInputs(int16_t local_p1_buttons[16], int16_t local_p1_analog[2][2],
                       int16_t out_p2_buttons[16], int16_t out_p2_analog[2][2])
{
	std::lock_guard<std::recursive_mutex> lock(net_lock);

	if (net_state != NETPLAY_CONNECTED || peer_sock == INVALID_SOCKET)
		return;

	NetPacket pkt;
	memset(&pkt, 0, sizeof(pkt));
	pkt.magic = NET_MAGIC;
	pkt.type = NET_TYPE_INPUT;
	pkt.frame_seq = frame_counter;
	pkt.send_tick = TickCountMs();
	pkt.echo_tick = last_peer_tick;   // lets the peer time its round trip
	memcpy(pkt.buttons, local_p1_buttons, sizeof(pkt.buttons));
	memcpy(pkt.analog, local_p1_analog, sizeof(pkt.analog));

	if (pending_tick == 0) pending_tick = pkt.send_tick;

	// send() on a non-blocking socket can take only part of the buffer, and a
	// partial write would desynchronise the peer's stream exactly like a
	// partial read desynchronised ours. Push the remainder before returning.
	const char* out = (const char*)&pkt;
	int remaining = (int)sizeof(NetPacket);

	while (remaining > 0)
	{
		int sent = send(peer_sock, out, remaining, MSG_NOSIGNAL);
		if (sent > 0) { out += sent; remaining -= sent; continue; }

		int err = WSAGetLastError();
		if (sent == SOCKET_ERROR && (err == WSAEWOULDBLOCK || err == EAGAIN))
		{
			// Socket buffer full: the peer is not keeping up. Dropping this
			// frame's input is better than blocking the emulator, but the
			// packet must not go out half-written.
			if (remaining != (int)sizeof(NetPacket))
			{
				NetplayDisconnect();
				CoreSetToast("NETPLAY: LINK DESYNCHRONISED", 150);
			}
			break;
		}

		NetplayDisconnect();
		CoreSetToast("NETPLAY: CONNECTION LOST", 120);
		break;
	}

	// Output remote inputs into Player 2
	memcpy(out_p2_buttons, remote_buttons, sizeof(remote_buttons));
	memcpy(out_p2_analog, remote_analog, sizeof(remote_analog));
}
