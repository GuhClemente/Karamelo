#ifndef NETPLAY_H_INCLUDED
#define NETPLAY_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <string>

enum NetplayRole
{
	NETPLAY_NONE = 0,
	NETPLAY_HOST,
	NETPLAY_CLIENT
};

enum NetplayState
{
	NETPLAY_DISCONNECTED = 0,
	NETPLAY_LISTENING,
	NETPLAY_CONNECTING,
	NETPLAY_CONNECTED
};

void NetplayInit();
void NetplayShutdown();

bool NetplayStartHost(int port = 55435);
bool NetplayStartClient(const char* host_ip, int port = 55435);
void NetplayDisconnect();

void NetplayUpdate();

NetplayRole  NetplayGetRole();
NetplayState NetplayGetState();
const char*  NetplayGetStatusString();
int          NetplayGetPingMs();
const char*  NetplayGetLocalIp();

// Input synchronization per frame
void NetplaySyncInputs(int16_t local_p1_buttons[16], int16_t local_p1_analog[2][2],
                       int16_t out_p2_buttons[16], int16_t out_p2_analog[2][2]);

#endif
