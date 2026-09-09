#ifndef APP_INFO_H_INCLUDED
#define APP_INFO_H_INCLUDED

// Single source of truth for the application's identity, and the same figures
// the website publishes.
//
// These used to be written out separately in the window title, the OSD title,
// the About page and the site, and they had already drifted: three different
// names, two different version numbers and three different core counts, with
// nothing agreeing with anything and none of it checkable.

// Short form. This is what the OSD draws: across the header and rotated
// down the left sidebar, 8 px per character, so it has to stay short
// enough to fit the panel. APP_NAME_FULL is for prose, the window title
// bar, the About page and anything the packaging writes out.
#define APP_NAME        "Karamelo"
#define APP_NAME_FULL   "Karamelo Emulador"
#define APP_VERSION     "0.9.3"
#define APP_ARCH        "x64"
#define APP_EXE_BASE    "Karamelo"
#define APP_EXE_NAME    "Karamelo_v0.9.3.exe"
#define APP_GITHUB_REPO "GuhClemente/Karamelo"
#define APP_SITE        "karamelo-emu.com"

// One entry per system in the main menu, and every one of them now has a
// core behind it in cores/. Rows whose DLL is absent are hidden at build
// time by SystemHasCore(), so this and the menu cannot drift apart.
#define APP_SYSTEM_COUNT 35

// Two different figures, because they are different things:
//   FILES   - .dll files in cores/, what the About page counts at runtime
//   ENGINES - distinct emulators, which is what "41 cores" ought to mean
// They differ by four, because four files are byte-for-byte copies of another
// core under a second name: n64_parallel.dll = n64.dll, pcsx2.dll and
// pcsx2_libretro.dll = ps2.dll, play_libretro.dll = ps2_play.dll. The
// published number is the engine count, since shipping the same emulator
// twice does not give the player another system.
//
// Measured, not estimated: hash every .dll in cores/ and count distinct
// digests. Re-measure after adding or removing a core - these two numbers had
// already drifted five files and two engines behind reality once.
#define APP_CORE_FILES   45
#define APP_CORE_ENGINES 41

#endif
