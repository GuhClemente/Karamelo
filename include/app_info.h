#ifndef APP_INFO_H_INCLUDED
#define APP_INFO_H_INCLUDED

// Single source of truth for the application's identity, and the same figures
// the website publishes.
//
// These used to be written out separately in the window title, the OSD title,
// the About page and the site, and they had already drifted: the window said
// "MiSTer 4 ALL", the About said "MiSTer Flavor v2.0" with "11 Consoles", and
// the site said v1.1 with 24 cores. Nothing agreed with anything, and none of
// it could be checked.
#define APP_NAME        "MiSTer 4 ALL"
#define APP_VERSION     "0.9.0"
#define APP_ARCH        "x64"
#define APP_EXE_BASE    "MiSTer_4_ALL"
#define APP_EXE_NAME    "MiSTer_4_ALL_v0.9.0.exe"
#define APP_GITHUB_REPO "gfdac/MiSTer-4-All"
#define APP_SITE        "mister4all.com"

// One entry per system in the main menu, and every one of them now has a
// core behind it in cores/. Rows whose DLL is absent are hidden at build
// time by SystemHasCore(), so this and the menu cannot drift apart.
#define APP_SYSTEM_COUNT 35

// Two different figures, because they are different things:
//   FILES   - .dll files in cores/, what the About page counts at runtime
//   ENGINES - distinct emulators, which is what "23 cores" ought to mean
// They differ by one: play_libretro.dll is a byte-for-byte copy of ps2.dll
// that nothing references. The published number is the engine count, since
// shipping the same emulator twice does not give the user another system.
#define APP_CORE_FILES   40
#define APP_CORE_ENGINES 39

#endif
