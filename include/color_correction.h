#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Modos de Daltonismo (Color Vision Deficiency - Machado et al. 2009)
enum CvdMode {
    CVD_MODE_OFF = 0,
    CVD_MODE_PROTANOPIA = 1,   // Deficiência para vermelho (L-cones)
    CVD_MODE_DEUTERANOPIA = 2, // Deficiência para verde (M-cones)
    CVD_MODE_TRITANOPIA = 3    // Deficiência para azul/amarelo (S-cones)
};

// Modos de Paletas / 3D LUT embutidas
enum LutMode {
    LUT_MODE_OFF = 0,
    LUT_MODE_SONY_PVM = 1,       // Fósforo SMPTE-C quente de monitores profissionais
    LUT_MODE_GAMEBOY_DMG = 2,    // LCD monocromático verde clássico de 1989
    LUT_MODE_GBC_CORRECT = 3,    // Correção gama e dessaturação para Game Boy Color
    LUT_MODE_NOIR = 4            // Escala de cinza de alto contraste cinematográfico
};

// Inicialização de tabelas e precomputação
void ColorCorrectionInit(void);

// Calcula a matriz 3x3 para linear RGB com base no modo e na intensidade (0.0f a 1.0f).
// Se simulate_mode > 0, aplica a simulação de como o daltônico enxerga.
// Retorna true se a matriz difere da identidade (isto é, há correção ativa).
bool ColorCorrectionGetMatrix(int mode, float strength, int simulate_mode, float out_m[9]);

// Aplica a correção de daltonismo a um pixel XRGB8888 (0x00RRGGBB).
uint32_t ColorCorrectionApplyCvd(uint32_t xrgb, const float m[9]);

// Aplica a LUT de calibração de cor a um pixel XRGB8888.
uint32_t ColorCorrectionApplyLut(uint32_t xrgb, int lut_mode);

// Transforma uma linha ou bloco de pixels de forma combinada (CVD + LUT).
// Se nenhum filtro estiver ativo, copia ou não altera nada.
void ColorCorrectionProcessRow(const uint32_t* src, uint32_t* dst, int count,
                               const float* cvd_matrix, int lut_mode);

// Carregamento de arquivo .cube externo (formato Adobe 3D LUT, tamanho N=2..64)
bool ColorCorrectionLoadCubeFile(const char* filepath);
void ColorCorrectionUnloadCubeFile(void);
bool ColorCorrectionHasCustomCube(void);

#ifdef __cplusplus
}
#endif
