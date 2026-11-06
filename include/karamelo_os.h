#pragma once
// Karamelo OS (the appliance distro) ships helper scripts in /usr/local/bin and
// starts the app from karamelo-session. The audio output switcher, Bluetooth
// pairing, the setup wizard and Desktop Mode only make sense there: on an
// ordinary Linux desktop or a Steam Deck they would run scripts that do not
// exist, or quit the app for a desktop that is already there.
// KARAMELO_OS=1 forces it on (testing), KARAMELO_OS=0 forces it off.
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <unistd.h>
#endif

static inline bool KarameloIsAppliance()
{
#if defined(__linux__)
	static int cached = -1;
	if (cached < 0)
	{
		const char* env = getenv("KARAMELO_OS");
		if (env && env[0])
			cached = (strcmp(env, "0") != 0) ? 1 : 0;
		else
			cached = (access("/usr/local/bin/karamelo-session", X_OK) == 0) ? 1 : 0;
	}
	return cached == 1;
#else
	return false;
#endif
}

// A sink name goes into a shell command line. pactl names are like
// "alsa_output.pci-0000_00_1f.3.analog-stereo" or "bluez_output.AA_BB_CC_DD_EE_FF.1";
// anything outside this set is refused rather than quoted.
static inline bool KarameloIsSafeSinkName(const char* s)
{
	if (!s || !s[0] || strlen(s) > 200) return false;
	for (const char* p = s; *p; ++p)
	{
		const char c = *p;
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		                c == '.' || c == '_' || c == '-' || c == ':' || c == '@';
		if (!ok) return false;
	}
	return true;
}
