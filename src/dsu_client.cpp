#include "dsu_client.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif

static SOCKET s_socket = INVALID_SOCKET;
static sockaddr_in s_server_addr;
static bool s_initialized = false;
static DsuMotionState s_latest_motion = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, 0};
static uint32_t s_client_id = 0x4B415241; // "KARA"
static int s_target_slot = 0;

static uint16_t ReadLe16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t ReadLe32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float ReadLeFloat(const uint8_t* p) {
    uint32_t bits = ReadLe32(p);
    float val;
    std::memcpy(&val, &bits, sizeof(float));
    return val;
}

static void WriteLe32(uint8_t* p, uint32_t val) {
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

uint32_t DsuCalculateCrc(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        // Ignora os 4 bytes do próprio campo CRC (offset 8 a 11)
        uint8_t b = (i >= 8 && i < 12) ? 0 : data[i];
        crc ^= b;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

size_t DsuBuildRequest(uint8_t out[28], uint32_t client_id, uint32_t req_type, int slot) {
    size_t len = (req_type == 0x100000) ? 20 : 28;
    std::memset(out, 0, 28);
    std::memcpy(out, "DSUC", 4);
    out[4] = 0xE9; out[5] = 0x03; // Protocol 1001 (0x03E9 little endian)
    out[6] = (uint8_t)(len - 16);
    out[7] = 0;

    WriteLe32(out + 12, client_id);
    WriteLe32(out + 16, req_type);

    if (req_type == 0x100001) {
        WriteLe32(out + 20, 4);
        for (int i = 0; i < 4; i++) out[24 + i] = (uint8_t)i;
    } else if (req_type == 0x100002) {
        out[20] = 1;
        out[21] = (uint8_t)slot; // Subscrição no slot especificado
    }

    uint32_t crc = DsuCalculateCrc(out, len);
    WriteLe32(out + 8, crc);
    return len;
}

bool DsuDecodePacket(const uint8_t* data, size_t len, DsuMotionState* out_state) {
    if (len < 20 || std::memcmp(data, "DSUS", 4) != 0 || ReadLe16(data + 4) != 1001) {
        return false;
    }

    size_t packet_size = 16u + ReadLe16(data + 6);
    if (packet_size < 20 || packet_size > len) return false;

    uint32_t expected_crc = ReadLe32(data + 8);
    if (expected_crc != DsuCalculateCrc(data, packet_size)) return false;

    uint32_t msg_type = ReadLe32(data + 16);
    if (msg_type == 0x100002) { // Relatório de dados do controle
        if (packet_size < 100) return false;

        uint8_t slot_state = data[21];
        uint8_t is_connected = data[31];
        bool connected = (is_connected == 1 && slot_state == 2);

        out_state->connected = connected;
        out_state->sequence = ReadLe32(data + 32);

        float ax = ReadLeFloat(data + 76);
        float ay = ReadLeFloat(data + 80);
        float az = ReadLeFloat(data + 84);

        float pitch = ReadLeFloat(data + 88);
        float yaw   = ReadLeFloat(data + 92);
        float roll  = ReadLeFloat(data + 96);

        if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) ||
            !std::isfinite(pitch) || !std::isfinite(yaw) || !std::isfinite(roll)) {
            return false;
        }

        const float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
        out_state->accel_x = ax;
        out_state->accel_y = -az;
        out_state->accel_z = ay;

        out_state->gyro_pitch = pitch * DEG_TO_RAD;
        out_state->gyro_yaw   = yaw   * DEG_TO_RAD;
        out_state->gyro_roll  = -roll * DEG_TO_RAD;

        return true;
    }

    return false;
}

bool DsuClientInit(const char* host, int port) {
    DsuClientShutdown();

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_socket == INVALID_SOCKET) {
        return false;
    }

    // Configurar socket como não-bloqueante
#ifdef _WIN32
    u_long non_blocking = 1;
    ioctlsocket(s_socket, FIONBIO, &non_blocking);
#else
    int flags = fcntl(s_socket, F_GETFL, 0);
    fcntl(s_socket, F_SETFL, flags | O_NONBLOCK);
#endif

    std::memset(&s_server_addr, 0, sizeof(s_server_addr));
    s_server_addr.sin_family = AF_INET;
    s_server_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, (host && host[0]) ? host : "127.0.0.1", &s_server_addr.sin_addr) <= 0) {
        closesocket(s_socket);
        s_socket = INVALID_SOCKET;
        return false;
    }

    s_initialized = true;

    // Enviar requisição de subscrição de controle
    uint8_t req[28];
    size_t req_len = DsuBuildRequest(req, s_client_id, 0x100002, s_target_slot);
    sendto(s_socket, (const char*)req, (int)req_len, 0, (sockaddr*)&s_server_addr, sizeof(s_server_addr));

    return true;
}

void DsuClientPoll(void) {
    if (!s_initialized || s_socket == INVALID_SOCKET) return;

    static int s_poll_counter = 0;
    // A cada ~120 quadros (~2 segundos a 60fps), renova o registro com o servidor
    if (++s_poll_counter > 120) {
        s_poll_counter = 0;
        uint8_t req[28];
        size_t req_len = DsuBuildRequest(req, s_client_id, 0x100002, s_target_slot);
        sendto(s_socket, (const char*)req, (int)req_len, 0, (sockaddr*)&s_server_addr, sizeof(s_server_addr));
    }

    uint8_t buffer[512];
    sockaddr_in from_addr;

    // Esvazia a fila de pacotes recebidos pegando a amostra mais recente
    while (true) {
        socklen_t from_len = sizeof(from_addr);
        int bytes = recvfrom(s_socket, (char*)buffer, sizeof(buffer), 0, (sockaddr*)&from_addr, &from_len);
        if (bytes <= 0) break;
        if (from_addr.sin_addr.s_addr != s_server_addr.sin_addr.s_addr ||
            from_addr.sin_port != s_server_addr.sin_port) {
            continue;
        }

        DsuMotionState state;
        if (DsuDecodePacket(buffer, (size_t)bytes, &state)) {
            s_latest_motion = state;
        }
    }
}

void DsuClientGetMotion(DsuMotionState* out_state) {
    if (out_state) *out_state = s_latest_motion;
}

bool DsuClientIsConnected(void) {
    return s_initialized && s_latest_motion.connected;
}

void DsuClientShutdown(void) {
    if (s_socket != INVALID_SOCKET) {
        closesocket(s_socket);
        s_socket = INVALID_SOCKET;
    }
    s_initialized = false;
    s_latest_motion.connected = false;
}
