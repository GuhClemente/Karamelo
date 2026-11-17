#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum UpscalerMode {
    UPSCALER_OFF = 0,     // Resolução nativa / filtro bilinear padrão
    UPSCALER_FSR1 = 1,    // AMD FSR 1.0 (EASU + RCAS completo)
    UPSCALER_CAS = 2      // FidelityFX CAS (Nitidez Contrast-Adaptive pura)
};

// Executa o algoritmo completo AMD FSR 1.0:
// 1. EASU: upscale espacial com detecção de arestas 12-tap
// 2. RCAS: restauração de nitidez sem halos/ringing
void Fsr1Upscale(const uint32_t* src, int src_w, int src_h,
                 uint32_t* dst, int dst_w, int dst_h,
                 float sharpness);

// Versão com controle de pitch (número de uint32_t por linha)
void Fsr1UpscaleWithPitch(const uint32_t* src, int src_w, int src_h, int src_pitch,
                          uint32_t* dst, int dst_w, int dst_h, int dst_pitch,
                          float sharpness);

// EASU direto
void Fsr1Easu(const uint32_t* src, int src_w, int src_h,
              uint32_t* dst, int dst_w, int dst_h);

void Fsr1EasuWithPitch(const uint32_t* src, int src_w, int src_h, int src_pitch,
                       uint32_t* dst, int dst_w, int dst_h, int dst_pitch);

// RCAS direto
void Fsr1Rcas(const uint32_t* src, uint32_t* dst, int w, int h, float sharpness);

void Fsr1RcasWithPitch(const uint32_t* src, int src_pitch,
                       uint32_t* dst, int dst_pitch,
                       int w, int h, float sharpness);

// FidelityFX CAS direto
void FidelityFxCas(const uint32_t* src, uint32_t* dst, int w, int h, float sharpness);

void FidelityFxCasWithPitch(const uint32_t* src, int src_pitch,
                            uint32_t* dst, int dst_pitch,
                            int w, int h, float sharpness);

#ifdef __cplusplus
}
#endif
