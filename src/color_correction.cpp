#include "color_correction.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <string>

// Tabelas sRGB <-> Linear RGB para transformações espectrais de alta fidelidade
static float s_srgb_to_lin[256];
static uint8_t s_lin_to_srgb_lut[4096];
static bool s_tables_initialized = false;

// Machado et al. 2009, Severidade 1.0 (Protanopia, Deuteranopia, Tritanopia)
// Espaço de cores: Linear RGB, row-major (3x3)
static const float k_sim[3][9] = {
    {0.152286f,  1.052583f, -0.204868f,
     0.114503f,  0.786281f,  0.099216f,
    -0.003882f, -0.048116f,  1.051998f},

    {0.367322f,  0.860646f, -0.227968f,
     0.280085f,  0.672501f,  0.047413f,
    -0.011820f,  0.042940f,  0.968881f},

    {1.255528f, -0.076749f, -0.178779f,
    -0.078411f,  0.930809f,  0.147602f,
     0.004733f,  0.691367f,  0.303900f}
};

// Matrizes de deslocamento de perda espectral E
// Protan e Deutan: perda vermelho-verde vai para verde e azul
// Tritan: perda azul-amarelo vai para vermelho e verde
static const float k_shift[3][9] = {
    {0.0f, 0.0f, 0.0f,  0.7f, 1.0f, 0.0f,  0.7f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f,  0.7f, 1.0f, 0.0f,  0.7f, 0.0f, 1.0f},
    {1.0f, 0.0f, 0.7f,  0.0f, 1.0f, 0.7f,  0.0f, 0.0f, 0.0f}
};

static void MatrixIdentity(float m[9]) {
    std::memset(m, 0, 9 * sizeof(float));
    m[0] = m[4] = m[8] = 1.0f;
}

static void MatrixMultiply(const float a[9], const float b[9], float out[9]) {
    float r[9];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r[i * 3 + j] = a[i * 3]     * b[j] +
                           a[i * 3 + 1] * b[3 + j] +
                           a[i * 3 + 2] * b[6 + j];
        }
    }
    std::memcpy(out, r, sizeof(r));
}

void ColorCorrectionInit(void) {
    if (s_tables_initialized) return;

    for (int i = 0; i < 256; i++) {
        double c = i / 255.0;
        double l = (c <= 0.04045) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
        s_srgb_to_lin[i] = (float)l;
    }

    for (int i = 0; i < 4096; i++) {
        double l = i / 4095.0;
        double c = (l <= 0.0031308) ? (l * 12.92) : (1.055 * std::pow(l, 1.0 / 2.4) - 0.055);
        if (c < 0.0) c = 0.0;
        if (c > 1.0) c = 1.0;
        s_lin_to_srgb_lut[i] = (uint8_t)(c * 255.0 + 0.5);
    }

    s_tables_initialized = true;
}

static inline uint8_t LinearToSrgbFast(float lin) {
    if (lin <= 0.0f) return 0;
    if (lin >= 1.0f) return 255;
    int idx = (int)(lin * 4095.0f + 0.5f);
    if (idx > 4095) idx = 4095;
    return s_lin_to_srgb_lut[idx];
}

bool ColorCorrectionGetMatrix(int mode, float strength, int simulate_mode, float out_m[9]) {
    ColorCorrectionInit();
    MatrixIdentity(out_m);

    bool has_corr = (mode >= 1 && mode <= 3 && strength > 0.0f);
    bool has_sim  = (simulate_mode >= 1 && simulate_mode <= 3);

    if (!has_corr && !has_sim) return false;

    if (has_corr) {
        int idx = mode - 1;
        float d[9]; // I - S
        for (int i = 0; i < 9; i++) {
            d[i] = (i % 4 == 0 ? 1.0f : 0.0f) - k_sim[idx][i];
        }
        float e[9];
        MatrixMultiply(k_shift[idx], d, e);
        for (int i = 0; i < 9; i++) {
            out_m[i] += strength * e[i];
        }
    }

    if (has_sim) {
        int sim_idx = simulate_mode - 1;
        float s[9];
        MatrixIdentity(s);
        for (int i = 0; i < 9; i++) {
            s[i] = (1.0f - 1.0f) * s[i] + 1.0f * k_sim[sim_idx][i];
        }
        MatrixMultiply(s, out_m, out_m);
    }

    return true;
}

uint32_t ColorCorrectionApplyCvd(uint32_t xrgb, const float m[9]) {
    if (!s_tables_initialized) ColorCorrectionInit();

    uint8_t r_in = (xrgb >> 16) & 0xFF;
    uint8_t g_in = (xrgb >> 8) & 0xFF;
    uint8_t b_in = xrgb & 0xFF;

    float lr = s_srgb_to_lin[r_in];
    float lg = s_srgb_to_lin[g_in];
    float lb = s_srgb_to_lin[b_in];

    float out_lr = m[0] * lr + m[1] * lg + m[2] * lb;
    float out_lg = m[3] * lr + m[4] * lg + m[5] * lb;
    float out_lb = m[6] * lr + m[7] * lg + m[8] * lb;

    uint32_t r_out = LinearToSrgbFast(out_lr);
    uint32_t g_out = LinearToSrgbFast(out_lg);
    uint32_t b_out = LinearToSrgbFast(out_lb);

    return (r_out << 16) | (g_out << 8) | b_out;
}

// Estrutura para 3D LUT externa (.cube)
struct Custom3DLut {
    int size = 0;
    std::vector<float> data; // size * size * size * 3
    bool loaded = false;
};
static Custom3DLut s_custom_lut;

bool ColorCorrectionLoadCubeFile(const char* filepath) {
    if (!filepath || !filepath[0]) return false;

    FILE* f = fopen(filepath, "r");
    if (!f) return false;

    int n = 0;
    std::vector<float> rgb_data;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#' || *s == '\r' || *s == '\n') continue;

        if (strncmp(s, "LUT_3D_SIZE", 11) == 0) {
            n = atoi(s + 11);
            if (n < 2 || n > 64) {
                fclose(f);
                return false;
            }
            rgb_data.reserve((size_t)n * n * n * 3);
        } else if (strncmp(s, "DOMAIN_MIN", 10) == 0 || strncmp(s, "DOMAIN_MAX", 10) == 0 ||
                   strncmp(s, "TITLE", 5) == 0) {
            continue;
        } else {
            float r = 0, g = 0, b = 0;
            if (sscanf(s, "%f %f %f", &r, &g, &b) == 3) {
                rgb_data.push_back(r);
                rgb_data.push_back(g);
                rgb_data.push_back(b);
            }
        }
    }
    fclose(f);

    if (n >= 2 && rgb_data.size() == (size_t)n * n * n * 3) {
        s_custom_lut.size = n;
        s_custom_lut.data = std::move(rgb_data);
        s_custom_lut.loaded = true;
        return true;
    }

    return false;
}

void ColorCorrectionUnloadCubeFile(void) {
    s_custom_lut.size = 0;
    s_custom_lut.data.clear();
    s_custom_lut.loaded = false;
}

bool ColorCorrectionHasCustomCube(void) {
    return s_custom_lut.loaded;
}

// Interpolação trilinear em 3D LUT
static inline void SampleCustomLut(float r, float g, float b, float* out_r, float* out_g, float* out_b) {
    int N = s_custom_lut.size;
    float max_idx = (float)(N - 1);

    float rf = std::clamp(r * max_idx, 0.0f, max_idx);
    float gf = std::clamp(g * max_idx, 0.0f, max_idx);
    float bf = std::clamp(b * max_idx, 0.0f, max_idx);

    int r0 = (int)rf; int r1 = std::min(r0 + 1, N - 1); float fr = rf - r0;
    int g0 = (int)gf; int g1 = std::min(g0 + 1, N - 1); float fg = gf - g0;
    int b0 = (int)bf; int b1 = std::min(b0 + 1, N - 1); float fb = bf - b0;

    auto get_rgb = [&](int ir, int ig, int ib) -> const float* {
        size_t idx = ((size_t)ib * N * N + (size_t)ig * N + (size_t)ir) * 3;
        return &s_custom_lut.data[idx];
    };

    const float* c000 = get_rgb(r0, g0, b0);
    const float* c100 = get_rgb(r1, g0, b0);
    const float* c010 = get_rgb(r0, g1, b0);
    const float* c110 = get_rgb(r1, g1, b0);
    const float* c001 = get_rgb(r0, g0, b1);
    const float* c101 = get_rgb(r1, g0, b1);
    const float* c011 = get_rgb(r0, g1, b1);
    const float* c111 = get_rgb(r1, g1, b1);

    for (int ch = 0; ch < 3; ch++) {
        float v00 = c000[ch] * (1.0f - fr) + c100[ch] * fr;
        float v10 = c010[ch] * (1.0f - fr) + c110[ch] * fr;
        float v01 = c001[ch] * (1.0f - fr) + c101[ch] * fr;
        float v11 = c011[ch] * (1.0f - fr) + c111[ch] * fr;

        float v0 = v00 * (1.0f - fg) + v10 * fg;
        float v1 = v01 * (1.0f - fg) + v11 * fg;

        float result = v0 * (1.0f - fb) + v1 * fb;
        if (ch == 0) *out_r = result;
        else if (ch == 1) *out_g = result;
        else *out_b = result;
    }
}

uint32_t ColorCorrectionApplyLut(uint32_t xrgb, int lut_mode) {
    if (lut_mode == LUT_MODE_OFF) return xrgb;

    uint32_t r = (xrgb >> 16) & 0xFF;
    uint32_t g = (xrgb >> 8) & 0xFF;
    uint32_t b = xrgb & 0xFF;

    switch (lut_mode) {
    case LUT_MODE_SONY_PVM: {
        // Fósforo SMPTE-C: saturação ligeiramente mais rica em vermelhos e tons de pele quentes
        // Matriz de calibração sRGB -> SMPTE-C
        float rf = r / 255.0f;
        float gf = g / 255.0f;
        float bf = b / 255.0f;
        float r_pvm = std::clamp(1.04f * rf - 0.03f * gf - 0.01f * bf, 0.0f, 1.0f);
        float g_pvm = std::clamp(-0.02f * rf + 1.03f * gf - 0.01f * bf, 0.0f, 1.0f);
        float b_pvm = std::clamp(-0.01f * rf - 0.02f * gf + 1.03f * bf, 0.0f, 1.0f);
        // Leve aumento de densidade de preto (contraste CRT profissional)
        r_pvm = std::pow(r_pvm, 1.05f);
        g_pvm = std::pow(g_pvm, 1.05f);
        b_pvm = std::pow(b_pvm, 1.05f);
        return ((uint32_t)(r_pvm * 255.0f + 0.5f) << 16) |
               ((uint32_t)(g_pvm * 255.0f + 0.5f) << 8) |
               (uint32_t)(b_pvm * 255.0f + 0.5f);
    }

    case LUT_MODE_GAMEBOY_DMG: {
        // LCD verde de matriz passiva clássica de 1989
        // Luminância ITU-R BT.601
        float lum = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
        // Paleta de 4 tons autêntica:
        // 0: #9bbc0f (155, 188, 15)  - Fundo claro
        // 1: #8bac0f (139, 172, 15)  - Cinza claro
        // 2: #306230 (48, 98, 48)    - Cinza escuro
        // 3: #0f380f (15, 56, 15)    - Preto LCD
        static const float pal_r[4] = { 15.0f/255.0f,  48.0f/255.0f, 139.0f/255.0f, 155.0f/255.0f };
        static const float pal_g[4] = { 56.0f/255.0f,  98.0f/255.0f, 172.0f/255.0f, 188.0f/255.0f };
        static const float pal_b[4] = { 15.0f/255.0f,  48.0f/255.0f,  15.0f/255.0f,  15.0f/255.0f };

        float p = std::clamp(lum * 3.0f, 0.0f, 3.0f);
        int i0 = (int)p;
        int i1 = std::min(i0 + 1, 3);
        float frac = p - i0;

        float dr = pal_r[i0] * (1.0f - frac) + pal_r[i1] * frac;
        float dg = pal_g[i0] * (1.0f - frac) + pal_g[i1] * frac;
        float db = pal_b[i0] * (1.0f - frac) + pal_b[i1] * frac;

        return ((uint32_t)(dr * 255.0f + 0.5f) << 16) |
               ((uint32_t)(dg * 255.0f + 0.5f) << 8) |
               (uint32_t)(db * 255.0f + 0.5f);
    }

    case LUT_MODE_GBC_CORRECT: {
        // Curva de compensação de cores para telas escuras de Game Boy Color / GBA
        // Reduz a hipersaturação que foi criada para compensar telas sem iluminação
        float rf = r / 255.0f;
        float gf = g / 255.0f;
        float bf = b / 255.0f;
        float rc = std::clamp(0.82f * rf + 0.14f * gf + 0.04f * bf, 0.0f, 1.0f);
        float gc = std::clamp(0.08f * rf + 0.80f * gf + 0.12f * bf, 0.0f, 1.0f);
        float bc = std::clamp(0.05f * rf + 0.15f * gf + 0.80f * bf, 0.0f, 1.0f);
        rc = std::pow(rc, 1.15f);
        gc = std::pow(gc, 1.15f);
        bc = std::pow(bc, 1.15f);
        return ((uint32_t)(rc * 255.0f + 0.5f) << 16) |
               ((uint32_t)(gc * 255.0f + 0.5f) << 8) |
               (uint32_t)(bc * 255.0f + 0.5f);
    }

    case LUT_MODE_NOIR: {
        // Monocromático de alto contraste com curva suave de filme
        float lum = (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
        // Curva S de contraste
        float c = (lum < 0.5f) ? (2.0f * lum * lum) : (1.0f - 2.0f * (1.0f - lum) * (1.0f - lum));
        c = std::clamp(c, 0.0f, 1.0f);
        uint32_t gray = (uint32_t)(c * 255.0f + 0.5f);
        return (gray << 16) | (gray << 8) | gray;
    }

    default:
        if (s_custom_lut.loaded) {
            float out_r = 0, out_g = 0, out_b = 0;
            SampleCustomLut(r / 255.0f, g / 255.0f, b / 255.0f, &out_r, &out_g, &out_b);
            uint32_t ur = (uint32_t)(std::clamp(out_r, 0.0f, 1.0f) * 255.0f + 0.5f);
            uint32_t ug = (uint32_t)(std::clamp(out_g, 0.0f, 1.0f) * 255.0f + 0.5f);
            uint32_t ub = (uint32_t)(std::clamp(out_b, 0.0f, 1.0f) * 255.0f + 0.5f);
            return (ur << 16) | (ug << 8) | ub;
        }
        break;
    }

    return xrgb;
}

void ColorCorrectionProcessRow(const uint32_t* src, uint32_t* dst, int count,
                               const float* cvd_matrix, int lut_mode) {
    bool has_cvd = (cvd_matrix != nullptr);
    bool has_lut = (lut_mode != LUT_MODE_OFF || s_custom_lut.loaded);

    if (!has_cvd && !has_lut) {
        if (src != dst) std::memcpy(dst, src, (size_t)count * sizeof(uint32_t));
        return;
    }

    for (int i = 0; i < count; i++) {
        uint32_t px = src[i];
        if (has_cvd) px = ColorCorrectionApplyCvd(px, cvd_matrix);
        if (has_lut) px = ColorCorrectionApplyLut(px, lut_mode);
        dst[i] = px;
    }
}
