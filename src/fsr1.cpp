#include "fsr1.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <thread>

// Tabela estática para Lanczos-2 rápida: w(x) = sinc(x) * sinc(x/2)
static const int LANCZOS_TABLE_STEPS = 512;
static float s_lanczos2_lut[LANCZOS_TABLE_STEPS + 1];
static bool s_lanczos_init = false;

static void InitLanczosTable() {
    if (s_lanczos_init) return;
    const double PI = 3.14159265358979323846;
    for (int i = 0; i <= LANCZOS_TABLE_STEPS; i++) {
        double x = (i / (double)LANCZOS_TABLE_STEPS) * 2.0; // range [0, 2]
        if (x < 1e-6) {
            s_lanczos2_lut[i] = 1.0f;
        } else {
            double pix = PI * x;
            double sinc1 = std::sin(pix) / pix;
            double sinc2 = std::sin(pix * 0.5) / (pix * 0.5);
            s_lanczos2_lut[i] = (float)(sinc1 * sinc2);
        }
    }
    s_lanczos_init = true;
}

static inline float Lanczos2Fast(float d) {
    if (d < 0.0f) d = -d;
    if (d >= 2.0f) return 0.0f;
    int idx = (int)(d * (LANCZOS_TABLE_STEPS * 0.5f) + 0.5f);
    if (idx > LANCZOS_TABLE_STEPS) idx = LANCZOS_TABLE_STEPS;
    return s_lanczos2_lut[idx];
}

static inline float GetLuma(uint32_t c) {
    float r = (float)((c >> 16) & 0xFF);
    float g = (float)((c >> 8) & 0xFF);
    float b = (float)(c & 0xFF);
    return 0.25f * r + 0.50f * g + 0.25f * b;
}

void Fsr1EasuWithPitch(const uint32_t* src, int src_w, int src_h, int src_pitch,
                       uint32_t* dst, int dst_w, int dst_h, int dst_pitch) {
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;
    if (!s_lanczos_init) InitLanczosTable();

    double scale_x = (double)src_w / (double)dst_w;
    double scale_y = (double)src_h / (double)dst_h;

    auto clamp_x = [src_w](int x) -> int { return std::clamp(x, 0, src_w - 1); };
    auto clamp_y = [src_h](int y) -> int { return std::clamp(y, 0, src_h - 1); };

    auto sample = [&](int x, int y) -> uint32_t {
        return src[(size_t)clamp_y(y) * src_pitch + clamp_x(x)];
    };

    auto process_band = [&](int y_from, int y_to) {
        for (int dy = y_from; dy < y_to; dy++) {
            double syf = ((double)dy + 0.5) * scale_y - 0.5;
            int by = (int)std::floor(syf);
            float fy = (float)(syf - by);

            uint32_t* dst_row = dst + (size_t)dy * dst_pitch;

            for (int dx = 0; dx < dst_w; dx++) {
                double sxf = ((double)dx + 0.5) * scale_x - 0.5;
                int bx = (int)std::floor(sxf);
                float fx = (float)(sxf - bx);

                uint32_t c00 = sample(bx,     by);
                uint32_t c10 = sample(bx + 1, by);
                uint32_t c01 = sample(bx,     by + 1);
                uint32_t c11 = sample(bx + 1, by + 1);

                uint32_t r_l0 = sample(bx - 1, by);
                uint32_t r_l1 = sample(bx - 1, by + 1);
                uint32_t r_r0 = sample(bx + 2, by);
                uint32_t r_r1 = sample(bx + 2, by + 1);
                uint32_t r_t0 = sample(bx,     by - 1);
                uint32_t r_t1 = sample(bx + 1, by - 1);
                uint32_t r_b0 = sample(bx,     by + 2);
                uint32_t r_b1 = sample(bx + 1, by + 2);

                float l_l0 = GetLuma(r_l0); float l_l1 = GetLuma(r_l1);
                float l_r0 = GetLuma(r_r0); float l_r1 = GetLuma(r_r1);
                float l_t0 = GetLuma(r_t0); float l_t1 = GetLuma(r_t1);
                float l_b0 = GetLuma(r_b0); float l_b1 = GetLuma(r_b1);

                float gx = (l_r0 + l_r1) - (l_l0 + l_l1);
                float gy = (l_b0 + l_b1) - (l_t0 + l_t1);

                float g_len_sq = gx * gx + gy * gy;
                float dir_x = 1.0f, dir_y = 0.0f;
                float edge_factor = 0.0f;

                if (g_len_sq > 1e-4f) {
                    float inv_len = 1.0f / std::sqrt(g_len_sq);
                    dir_x = gx * inv_len;
                    dir_y = gy * inv_len;
                    edge_factor = std::min(std::sqrt(g_len_sq) / 255.0f, 1.0f);
                }

                float tan_x = -dir_y;
                float tan_y =  dir_x;

                float total_w = 0.0f;
                float acc_r = 0.0f, acc_g = 0.0f, acc_b = 0.0f;

                const struct Tap { int ox, oy; uint32_t c; } taps[12] = {
                    {0, 0, c00}, {1, 0, c10}, {0, 1, c01}, {1, 1, c11},
                    {-1, 0, r_l0}, {-1, 1, r_l1}, {2, 0, r_r0}, {2, 1, r_r1},
                    {0, -1, r_t0}, {1, -1, r_t1}, {0, 2, r_b0}, {1, 2, r_b1}
                };

                for (int t = 0; t < 12; t++) {
                    float ox = (float)taps[t].ox - fx;
                    float oy = (float)taps[t].oy - fy;

                    float d_euclid = std::sqrt(ox * ox + oy * oy);
                    float d_across = std::abs(ox * dir_x + oy * dir_y);
                    float d_along  = std::abs(ox * tan_x + oy * tan_y);

                    float w_iso = Lanczos2Fast(d_euclid);
                    float w_aniso = Lanczos2Fast(d_along * 0.75f) * Lanczos2Fast(d_across * 1.35f);

                    float w = w_iso * (1.0f - edge_factor) + w_aniso * edge_factor;

                    if (w > 0.0f) {
                        uint32_t color = taps[t].c;
                        acc_r += ((color >> 16) & 0xFF) * w;
                        acc_g += ((color >> 8) & 0xFF) * w;
                        acc_b += (color & 0xFF) * w;
                        total_w += w;
                    }
                }

                if (total_w > 1e-5f) {
                    float inv_w = 1.0f / total_w;
                    uint32_t ur = (uint32_t)std::clamp(acc_r * inv_w + 0.5f, 0.0f, 255.0f);
                    uint32_t ug = (uint32_t)std::clamp(acc_g * inv_w + 0.5f, 0.0f, 255.0f);
                    uint32_t ub = (uint32_t)std::clamp(acc_b * inv_w + 0.5f, 0.0f, 255.0f);
                    dst_row[dx] = (ur << 16) | (ug << 8) | ub;
                } else {
                    dst_row[dx] = c00;
                }
            }
        }
    };

    unsigned hw = std::thread::hardware_concurrency();
    int bands = (hw > 1) ? (int)((hw > 8) ? 8 : hw) : 1;
    if (dst_h < 64) bands = 1;

    if (bands <= 1) {
        process_band(0, dst_h);
    } else {
        int step = (dst_h + bands - 1) / bands;
        std::vector<std::thread> pool;
        pool.reserve((size_t)bands - 1);
        for (int b = 1; b < bands; b++) {
            int a = b * step;
            int z = std::min(a + step, dst_h);
            if (a < z) pool.emplace_back(process_band, a, z);
        }
        process_band(0, std::min(step, dst_h));
        for (auto& t : pool) t.join();
    }
}

void Fsr1Easu(const uint32_t* src, int src_w, int src_h, uint32_t* dst, int dst_w, int dst_h) {
    Fsr1EasuWithPitch(src, src_w, src_h, src_w, dst, dst_w, dst_h, dst_w);
}

void Fsr1RcasWithPitch(const uint32_t* src, int src_pitch,
                       uint32_t* dst, int dst_pitch,
                       int w, int h, float sharpness) {
    if (!src || !dst || w <= 0 || h <= 0) return;
    if (sharpness <= 0.0f) {
        if (src != dst || src_pitch != dst_pitch) {
            for (int y = 0; y < h; y++) {
                std::memcpy(dst + (size_t)y * dst_pitch, src + (size_t)y * src_pitch, (size_t)w * sizeof(uint32_t));
            }
        }
        return;
    }

    sharpness = std::clamp(sharpness, 0.0f, 1.0f);

    // RCAS reads 4 neighbors (x+-1, y+-1). Running in-place (src == dst) both
    // turns the filter recursive and causes a multi-thread data race across
    // horizontal band boundaries. Snapshot src into a scratch buffer first.
    std::vector<uint32_t> scratch;
    if (src == dst) {
        scratch.resize((size_t)w * (size_t)h);
        for (int y = 0; y < h; y++) {
            std::memcpy(scratch.data() + (size_t)y * w, src + (size_t)y * src_pitch, (size_t)w * sizeof(uint32_t));
        }
        src = scratch.data();
        src_pitch = w;
    }

    auto clamp_x = [w](int x) -> int { return std::clamp(x, 0, w - 1); };
    auto clamp_y = [h](int y) -> int { return std::clamp(y, 0, h - 1); };

    auto process_band = [&](int y_from, int y_to) {
        for (int y = y_from; y < y_to; y++) {
            const uint32_t* row_c = src + (size_t)y * src_pitch;
            const uint32_t* row_u = src + (size_t)clamp_y(y - 1) * src_pitch;
            const uint32_t* row_d = src + (size_t)clamp_y(y + 1) * src_pitch;
            uint32_t* dst_row = dst + (size_t)y * dst_pitch;

            for (int x = 0; x < w; x++) {
                int xl = clamp_x(x - 1);
                int xr = clamp_x(x + 1);

                uint32_t c = row_c[x];
                uint32_t u = row_u[x];
                uint32_t d = row_d[x];
                uint32_t l = row_c[xl];
                uint32_t r = row_c[xr];

                float lc = GetLuma(c);
                float lu = GetLuma(u);
                float ld = GetLuma(d);
                float ll = GetLuma(l);
                float lr = GetLuma(r);

                float min_l = std::min({lc, lu, ld, ll, lr});
                float max_l = std::max({lc, lu, ld, ll, lr});
                float range = max_l - min_l;

                if (range < 1e-4f) {
                    dst_row[x] = c;
                    continue;
                }

                float lobe = -std::min(min_l, 255.0f - max_l) / (range + 0.001f);
                lobe = std::clamp(lobe, -0.25f, 0.0f) * sharpness;

                float rc = (float)((c >> 16) & 0xFF);
                float gc = (float)((c >> 8) & 0xFF);
                float bc = (float)(c & 0xFF);

                float ru = (float)((u >> 16) & 0xFF); float rd = (float)((d >> 16) & 0xFF);
                float rl = (float)((l >> 16) & 0xFF); float rr = (float)((r >> 16) & 0xFF);

                float gu = (float)((u >> 8) & 0xFF);  float gd = (float)((d >> 8) & 0xFF);
                float gl = (float)((l >> 8) & 0xFF);  float gr = (float)((r >> 8) & 0xFF);

                float bu = (float)(u & 0xFF);         float bd = (float)(d & 0xFF);
                float bl = (float)(l & 0xFF);         float br = (float)(r & 0xFF);

                float sum_r = ru + rd + rl + rr;
                float sum_g = gu + gd + gl + gr;
                float sum_b = bu + bd + bl + br;

                float div = 1.0f + 4.0f * lobe;
                float inv_div = (std::abs(div) > 1e-5f) ? (1.0f / div) : 1.0f;

                uint32_t out_r = (uint32_t)std::clamp((rc + lobe * sum_r) * inv_div + 0.5f, 0.0f, 255.0f);
                uint32_t out_g = (uint32_t)std::clamp((gc + lobe * sum_g) * inv_div + 0.5f, 0.0f, 255.0f);
                uint32_t out_b = (uint32_t)std::clamp((bc + lobe * sum_b) * inv_div + 0.5f, 0.0f, 255.0f);

                dst_row[x] = (out_r << 16) | (out_g << 8) | out_b;
            }
        }
    };

    unsigned hw = std::thread::hardware_concurrency();
    int bands = (hw > 1) ? (int)((hw > 8) ? 8 : hw) : 1;
    if (h < 64) bands = 1;

    if (bands <= 1) {
        process_band(0, h);
    } else {
        int step = (h + bands - 1) / bands;
        std::vector<std::thread> pool;
        pool.reserve((size_t)bands - 1);
        for (int b = 1; b < bands; b++) {
            int a = b * step;
            int z = std::min(a + step, h);
            if (a < z) pool.emplace_back(process_band, a, z);
        }
        process_band(0, std::min(step, h));
        for (auto& t : pool) t.join();
    }
}

void Fsr1Rcas(const uint32_t* src, uint32_t* dst, int w, int h, float sharpness) {
    Fsr1RcasWithPitch(src, w, dst, w, w, h, sharpness);
}

void Fsr1UpscaleWithPitch(const uint32_t* src, int src_w, int src_h, int src_pitch,
                          uint32_t* dst, int dst_w, int dst_h, int dst_pitch,
                          float sharpness) {
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;

    if (src_w == dst_w && src_h == dst_h) {
        if (sharpness > 0.0f) {
            Fsr1RcasWithPitch(src, src_pitch, dst, dst_pitch, dst_w, dst_h, sharpness);
        } else if (src != dst || src_pitch != dst_pitch) {
            for (int y = 0; y < dst_h; y++) {
                std::memcpy(dst + (size_t)y * dst_pitch, src + (size_t)y * src_pitch, (size_t)dst_w * sizeof(uint32_t));
            }
        }
        return;
    }

    std::vector<uint32_t> easu_temp((size_t)dst_w * dst_h);
    Fsr1EasuWithPitch(src, src_w, src_h, src_pitch, easu_temp.data(), dst_w, dst_h, dst_w);

    if (sharpness > 0.0f) {
        Fsr1RcasWithPitch(easu_temp.data(), dst_w, dst, dst_pitch, dst_w, dst_h, sharpness);
    } else {
        for (int y = 0; y < dst_h; y++) {
            std::memcpy(dst + (size_t)y * dst_pitch, easu_temp.data() + (size_t)y * dst_w, (size_t)dst_w * sizeof(uint32_t));
        }
    }
}

void Fsr1Upscale(const uint32_t* src, int src_w, int src_h,
                 uint32_t* dst, int dst_w, int dst_h,
                 float sharpness) {
    Fsr1UpscaleWithPitch(src, src_w, src_h, src_w, dst, dst_w, dst_h, dst_w, sharpness);
}

void FidelityFxCasWithPitch(const uint32_t* src, int src_pitch,
                            uint32_t* dst, int dst_pitch,
                            int w, int h, float sharpness) {
    Fsr1RcasWithPitch(src, src_pitch, dst, dst_pitch, w, h, sharpness);
}

void FidelityFxCas(const uint32_t* src, uint32_t* dst, int w, int h, float sharpness) {
    Fsr1RcasWithPitch(src, w, dst, w, w, h, sharpness);
}
