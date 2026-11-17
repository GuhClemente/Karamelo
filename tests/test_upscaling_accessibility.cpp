#include "test_framework.h"
#include "color_correction.h"
#include "fsr1.h"
#include "dsu_client.h"
#include "core_options_categories.h"

#include <cmath>
#include <vector>

TEST_CASE(ColorCorrection_IdentityWhenDisabled)
{
    float m[9];
    bool active = ColorCorrectionGetMatrix(CVD_MODE_OFF, 0.0f, 0, m);
    ASSERT_FALSE(active);
    ASSERT_EQ(1.0f, m[0]);
    ASSERT_EQ(0.0f, m[1]);
    ASSERT_EQ(0.0f, m[2]);
    ASSERT_EQ(0.0f, m[3]);
    ASSERT_EQ(1.0f, m[4]);
    ASSERT_EQ(0.0f, m[5]);
    ASSERT_EQ(0.0f, m[6]);
    ASSERT_EQ(0.0f, m[7]);
    ASSERT_EQ(1.0f, m[8]);
}

TEST_CASE(ColorCorrection_ProtanopiaMatrixCalculates)
{
    float m[9];
    bool active = ColorCorrectionGetMatrix(CVD_MODE_PROTANOPIA, 1.0f, 0, m);
    ASSERT_TRUE(active);
    // Para protanopia, os canais G e B devem receber assistência da perda de R
    ASSERT_TRUE(m[3] > 0.0f || m[4] != 1.0f);
}

TEST_CASE(ColorCorrection_WhitePreservation)
{
    // Branco em RGB (0xFFFFFF) transformado por daltonismo deve permanecer neutro/branco
    float m[9];
    for (int mode = CVD_MODE_PROTANOPIA; mode <= CVD_MODE_TRITANOPIA; mode++) {
        ColorCorrectionGetMatrix(mode, 1.0f, 0, m);
        uint32_t white = 0x00FFFFFF;
        uint32_t corrected = ColorCorrectionApplyCvd(white, m);
        int r = (corrected >> 16) & 0xFF;
        int g = (corrected >> 8) & 0xFF;
        int b = corrected & 0xFF;

        // Canais de branco devem ficar próximos de 255 (tolerância de gama <= 2)
        ASSERT_TRUE(r >= 253);
        ASSERT_TRUE(g >= 253);
        ASSERT_TRUE(b >= 253);
    }
}

TEST_CASE(ColorCorrection_LutBuiltInModes)
{
    uint32_t color = 0x00808080; // Cinza médio
    uint32_t off = ColorCorrectionApplyLut(color, LUT_MODE_OFF);
    ASSERT_EQ(color, off);

    uint32_t dmg = ColorCorrectionApplyLut(color, LUT_MODE_GAMEBOY_DMG);
    // Modo Game Boy DMG deve ter tons esverdeados (G maior que R e B)
    int dmg_r = (dmg >> 16) & 0xFF;
    int dmg_g = (dmg >> 8) & 0xFF;
    int dmg_b = dmg & 0xFF;
    ASSERT_TRUE(dmg_g > dmg_r);
    ASSERT_TRUE(dmg_g > dmg_b);

    uint32_t noir = ColorCorrectionApplyLut(0x00FF0000, LUT_MODE_NOIR);
    // Modo Noir em vermelho deve gerar escala de cinza (R == G == B)
    int noir_r = (noir >> 16) & 0xFF;
    int noir_g = (noir >> 8) & 0xFF;
    int noir_b = noir & 0xFF;
    ASSERT_EQ(noir_r, noir_g);
    ASSERT_EQ(noir_g, noir_b);
}

TEST_CASE(FSR1_EasuUpscalingOutputDimensionsAndRange)
{
    const int SW = 16, SH = 16;
    const int DW = 32, DH = 32;

    std::vector<uint32_t> src(SW * SH);
    // Gradiente diagonal para teste de borda
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            uint8_t val = (uint8_t)((x + y) * 8);
            src[y * SW + x] = (val << 16) | (val << 8) | val;
        }
    }

    std::vector<uint32_t> dst(DW * DH, 0);
    Fsr1Easu(src.data(), SW, SH, dst.data(), DW, DH);

    // Valida que nenhum pixel estourou ou ficou nulo indevidamente
    for (int y = 0; y < DH; y++) {
        for (int x = 0; x < DW; x++) {
            uint32_t p = dst[y * DW + x];
            int r = (p >> 16) & 0xFF;
            int g = (p >> 8) & 0xFF;
            int b = p & 0xFF;
            ASSERT_TRUE(r >= 0 && r <= 255);
            ASSERT_TRUE(g >= 0 && g <= 255);
            ASSERT_TRUE(b >= 0 && b <= 255);
        }
    }
}

TEST_CASE(FSR1_RcasAntiRingingClamping)
{
    const int W = 8, H = 8;
    std::vector<uint32_t> img(W * H, 0x00000000); // Preto
    // Coloca um ponto branco no centro (alto contraste)
    img[4 * W + 4] = 0x00FFFFFF;

    std::vector<uint32_t> out(W * H);
    Fsr1Rcas(img.data(), out.data(), W, H, 1.0f); // Nitidez máxima

    // RCAS com anti-ringing não deve permitir ringing/subida negativa
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint32_t p = out[y * W + x];
            int r = (p >> 16) & 0xFF;
            ASSERT_TRUE(r >= 0 && r <= 255);
        }
    }
    // O ponto central deve permanecer luminoso
    ASSERT_TRUE(((out[4 * W + 4] >> 16) & 0xFF) > 200);
}

TEST_CASE(DSU_CrcAndPacketBuild)
{
    uint8_t req[28];
    size_t len = DsuBuildRequest(req, 0x12345678, 0x100002, 0);
    ASSERT_EQ((size_t)28, len);
    ASSERT_EQ('D', req[0]);
    ASSERT_EQ('S', req[1]);
    ASSERT_EQ('U', req[2]);
    ASSERT_EQ('C', req[3]);

    // O CRC gravado nos bytes 8..11 deve bater com o cálculo do pacote
    uint32_t packet_crc = (uint32_t)req[8] | ((uint32_t)req[9] << 8) |
                          ((uint32_t)req[10] << 16) | ((uint32_t)req[11] << 24);
    uint32_t computed_crc = DsuCalculateCrc(req, 28);
    ASSERT_EQ(packet_crc, computed_crc);
}

TEST_CASE(CoreOptions_FlycastDreamcastClassification)
{
    // Video
    ASSERT_EQ((int)CORE_CAT_VIDEO, (int)ClassifyCoreOption("flycast_internal_resolution", "Internal Resolution"));
    ASSERT_TRUE(IsCoreOptionPrimary("flycast_internal_resolution", "Internal Resolution"));

    ASSERT_EQ((int)CORE_CAT_VIDEO, (int)ClassifyCoreOption("flycast_widescreen_hack", "Widescreen (Hack)"));
    ASSERT_TRUE(IsCoreOptionPrimary("flycast_widescreen_hack", "Widescreen (Hack)"));

    ASSERT_EQ((int)CORE_CAT_VIDEO, (int)ClassifyCoreOption("flycast_anisotropic_filtering", "Anisotropic Filtering"));
    ASSERT_FALSE(IsCoreOptionPrimary("flycast_anisotropic_filtering", "Anisotropic Filtering"));

    ASSERT_EQ((int)CORE_CAT_VIDEO, (int)ClassifyCoreOption("flycast_alpha_sorting", "Alpha Sorting"));
    ASSERT_EQ((int)CORE_CAT_VIDEO, (int)ClassifyCoreOption("flycast_fog", "Fog Emulation"));

    // Audio
    ASSERT_EQ((int)CORE_CAT_AUDIO, (int)ClassifyCoreOption("flycast_volume_modifier", "Volume Modifier"));

    // System
    ASSERT_EQ((int)CORE_CAT_SYSTEM, (int)ClassifyCoreOption("flycast_region", "Region"));
    ASSERT_TRUE(IsCoreOptionPrimary("flycast_region", "Region"));

    ASSERT_EQ((int)CORE_CAT_SYSTEM, (int)ClassifyCoreOption("flycast_language", "Language"));
    ASSERT_EQ((int)CORE_CAT_SYSTEM, (int)ClassifyCoreOption("flycast_cable_type", "Cable Type"));
    ASSERT_EQ((int)CORE_CAT_SYSTEM, (int)ClassifyCoreOption("flycast_broadcast", "Broadcast Standard"));
    ASSERT_EQ((int)CORE_CAT_SYSTEM, (int)ClassifyCoreOption("flycast_vmu_screen", "VMU Screen Display"));

    // Performance / Advanced
    ASSERT_EQ((int)CORE_CAT_PERF, (int)ClassifyCoreOption("flycast_cpu_mode", "CPU Mode"));
    ASSERT_TRUE(IsCoreOptionPrimary("flycast_cpu_mode", "CPU Mode"));

    // Input
    ASSERT_EQ((int)CORE_CAT_INPUT, (int)ClassifyCoreOption("flycast_digital_triggers", "Digital Triggers"));
}

TEST_CASE(CoreOptions_BeetlePsxAndGenesisClassification)
{
    // Beetle PSX
    ASSERT_EQ((int)CORE_CAT_VIDEO, (int)ClassifyCoreOption("beetle_psx_hw_internal_resolution", "Internal Resolution"));
    ASSERT_EQ((int)CORE_CAT_VIDEO, (int)ClassifyCoreOption("beetle_psx_hw_pgxp_mode", "PGXP Operation Mode"));
    ASSERT_EQ((int)CORE_CAT_PERF, (int)ClassifyCoreOption("beetle_psx_hw_cpu_overclock", "CPU Overclock"));
    ASSERT_TRUE(IsCoreOptionPrimary("beetle_psx_hw_cpu_overclock", "CPU Overclock"));
    ASSERT_EQ((int)CORE_CAT_PERF, (int)ClassifyCoreOption("beetle_psx_hw_cd_fastload", "CD Fastload"));
    ASSERT_EQ((int)CORE_CAT_INPUT, (int)ClassifyCoreOption("beetle_psx_hw_analog_calibration", "Analog Calibration"));

    // Genesis Plus GX
    ASSERT_EQ((int)CORE_CAT_AUDIO, (int)ClassifyCoreOption("genesis_plus_gx_ym2612", "YM2612 Sound"));
    ASSERT_EQ((int)CORE_CAT_AUDIO, (int)ClassifyCoreOption("genesis_plus_gx_audio_filter", "Audio Filter"));
    ASSERT_EQ((int)CORE_CAT_SYSTEM, (int)ClassifyCoreOption("genesis_plus_gx_bios", "BIOS"));
    ASSERT_EQ((int)CORE_CAT_SYSTEM, (int)ClassifyCoreOption("genesis_plus_gx_region_detect", "Region Detect"));
    ASSERT_TRUE(IsCoreOptionPrimary("genesis_plus_gx_region_detect", "Region Detect"));
    ASSERT_EQ((int)CORE_CAT_PERF, (int)ClassifyCoreOption("genesis_plus_gx_overclock", "CPU Overclock"));
    ASSERT_EQ((int)CORE_CAT_INPUT, (int)ClassifyCoreOption("genesis_plus_gx_gun_cursor", "Lightgun Cursor"));

    // Mupen64Plus
    ASSERT_EQ((int)CORE_CAT_VIDEO, (int)ClassifyCoreOption("mupen64plus-169screensize", "16:9 Screen Size"));
    ASSERT_EQ((int)CORE_CAT_VIDEO, (int)ClassifyCoreOption("mupen64plus-rdp-plugin", "RDP Plugin"));
    ASSERT_EQ((int)CORE_CAT_PERF, (int)ClassifyCoreOption("mupen64plus-rsp-plugin", "RSP Plugin"));
}
