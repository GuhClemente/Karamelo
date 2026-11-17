#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float accel_x, accel_y, accel_z;   // Aceleração em Gs
    float gyro_pitch, gyro_yaw, gyro_roll; // Velocidade angular em rad/s
    bool connected;
    uint32_t sequence;
} DsuMotionState;

// Inicializa o cliente UDP Cemuhook DSU apontando para host:port (ex: "127.0.0.1", 26760)
bool DsuClientInit(const char* host, int port);

// Faz o poll não-bloqueante de pacotes UDP pendentes (deve ser chamado a cada quadro)
void DsuClientPoll(void);

// Obtém o estado mais recente de movimento
void DsuClientGetMotion(DsuMotionState* out_state);

// Retorna se o cliente DSU está ativo e conectado a um servidor
bool DsuClientIsConnected(void);

// Encerra a conexão e libera o socket UDP
void DsuClientShutdown(void);

// Utilitários de protocolo para testes unitários
uint32_t DsuCalculateCrc(const uint8_t* data, size_t len);
size_t DsuBuildRequest(uint8_t out[28], uint32_t client_id, uint32_t req_type, int slot);
bool DsuDecodePacket(const uint8_t* data, size_t len, DsuMotionState* out_state);

#ifdef __cplusplus
}
#endif
