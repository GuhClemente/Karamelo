#pragma once
#include <string>
#include <string_view>

enum CoreOptionCategory {
  CORE_CAT_ROOT = -1,
  CORE_CAT_VIDEO = 0,
  CORE_CAT_AUDIO,
  CORE_CAT_SYSTEM,
  CORE_CAT_PERF,
  CORE_CAT_INPUT,
  CORE_CAT_MISC,
  CORE_CAT_COUNT
};

inline constexpr int kActionCoreCategoryBase = 750; // 750..755
inline constexpr int kCoreOptionsMaxRows = 98;

inline std::string CoreOptionToLowerAscii(std::string_view s) {
  std::string r;
  r.reserve(s.size());
  for (char c : s) {
    r.push_back((c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c);
  }
  return r;
}

inline bool CoreOptionContainsWord(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

inline CoreOptionCategory ClassifyCoreOption(std::string_view key_raw, std::string_view label_raw) {
  std::string k = CoreOptionToLowerAscii(key_raw);
  std::string l = CoreOptionToLowerAscii(label_raw);
  std::string combined = k + " " + l;

  // 1. INPUT & CONTROLS
  static const char* const kInputWords[] = {
    "pad", "controller", "controle", "stick", "analog", "mouse", "gun", "lightgun",
    "trigger", "rumble", "vibrat", "touch", "stylus", "multitap", "deadzone",
    "sensitiv", "turbo", "autofire", "wheel", "pointer", "cursor", "gamepad", "joystick"
  };
  for (const char* w : kInputWords) {
    if (CoreOptionContainsWord(combined, w)) return CORE_CAT_INPUT;
  }

  // 2. AUDIO & SOUND
  static const char* const kAudioWords[] = {
    "audio", "sound", "volume", "dsp", "sample", "reverb", "cdda", "spu", "apu",
    "ym2413", "ym2612", "sn76489", "psg", "resampler", "equalizer", "sfx",
    "stereo", "mono", "surround", "pan", "lowpass", "highpass"
  };
  for (const char* w : kAudioWords) {
    if (CoreOptionContainsWord(combined, w)) return CORE_CAT_AUDIO;
  }
  if (CoreOptionContainsWord(k, "_fm") || CoreOptionContainsWord(combined, " fm ") || CoreOptionContainsWord(combined, " fm")) {
    return CORE_CAT_AUDIO;
  }

  // 3. SYSTEM, BIOS & REGION
  static const char* const kSystemWords[] = {
    "region", "regiao", "bios", "hle", "cable", "cabo", "broadcast", "vmu",
    "memcard", "memory_card", "rtc", "language", "idioma", "country", "pais",
    "unibios", "dipswitch", "lock_on", "system_hw", "force_dtack", "addr_error",
    "cartridge", "cartucho", "expansion", "addon", "model", "modelo"
  };
  for (const char* w : kSystemWords) {
    if (CoreOptionContainsWord(combined, w)) return CORE_CAT_SYSTEM;
  }
  if (CoreOptionContainsWord(k, "system_type") || CoreOptionContainsWord(k, "_system") || CoreOptionContainsWord(combined, "sistema")) {
    return CORE_CAT_SYSTEM;
  }

  // 4. VIDEO & GRAPHICS
  static const char* const kVideoWords[] = {
    "video", "render", "gfx", "graphic", "grafic", "res", "resolution", "resolucao",
    "aspect", "widescreen", "filter", "filtro", "scale", "upscale", "alpha",
    "fog", "shadow", "sombra", "mip", "texture", "textura", "dither", "vsync",
    "scanline", "shader", "screen", "tela", "display", "pgxp", "color", "cor",
    "depth", "bilinear", "multisampling", "msaa", "anti-aliasing", "antialiasing",
    "fxaa", "interlace", "deinterlace", "overscan", "crop", "gamma", "bright",
    "contrast", "wireframe", "vram", "blend", "vdp", "ppu", "rdp", "blargg",
    "refresh", "pixel"
  };
  for (const char* w : kVideoWords) {
    if (CoreOptionContainsWord(combined, w)) return CORE_CAT_VIDEO;
  }

  // 5. ADVANCED & PERFORMANCE
  static const char* const kPerfWords[] = {
    "dynarec", "jit", "thread", "async", "overclock", "underclock", "timing",
    "fastload", "precache", "cpu", "cycle", "ciclo", "interpreter", "recompiler",
    "frameskip", "frame_skip", "skip", "countperop", "rsp", "affinity", "idle",
    "speed", "velocidade", "perf", "dma", "irq", "cache", "cd_latency", "fast"
  };
  for (const char* w : kPerfWords) {
    if (CoreOptionContainsWord(combined, w)) return CORE_CAT_PERF;
  }

  return CORE_CAT_MISC;
}

inline bool IsCoreOptionPrimary(std::string_view key_raw, std::string_view label_raw) {
  std::string k = CoreOptionToLowerAscii(key_raw);
  std::string l = CoreOptionToLowerAscii(label_raw);
  std::string combined = k + " " + l;

  if (CoreOptionContainsWord(combined, "internal_res") || CoreOptionContainsWord(combined, "internal resolution") ||
      CoreOptionContainsWord(combined, "resolution") || CoreOptionContainsWord(combined, "resolucao") ||
      CoreOptionContainsWord(k, "screensize")) {
    return true;
  }
  if (CoreOptionContainsWord(combined, "widescreen")) {
    return true;
  }
  if (CoreOptionContainsWord(combined, "region") || CoreOptionContainsWord(combined, "regiao")) {
    return true;
  }
  if (CoreOptionContainsWord(combined, "overclock") || CoreOptionContainsWord(k, "cpu_mode")) {
    return true;
  }
  return false;
}
