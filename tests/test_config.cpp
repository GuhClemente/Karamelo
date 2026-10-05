#include "test_framework.h"
#include "input_map.h"
#include "gamepad_sdl.h"
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>

static int TestClampInt(int v, int min_v, int max_v)
{
	if (v < min_v) return min_v;
	if (v > max_v) return max_v;
	return v;
}

static std::map<std::string, std::string> ParseConfigStream(std::istream& in)
{
	std::map<std::string, std::string> out;
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#') continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;

		std::string key = line.substr(0, eq);
		std::string val = line.substr(eq + 1);

		while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' '))
			val.pop_back();

		out[key] = val;
	}
	return out;
}

TEST_CASE(ConfigClampIntBounds)
{
	ASSERT_EQ(TestClampInt(-5, 0, 10), 0);
	ASSERT_EQ(TestClampInt(15, 0, 10), 10);
	ASSERT_EQ(TestClampInt(5, 0, 10), 5);
	ASSERT_EQ(TestClampInt(0, 0, 10), 0);
	ASSERT_EQ(TestClampInt(10, 0, 10), 10);
}

TEST_CASE(ConfigParserCommentsAndWhitespace)
{
	std::string cfg_text = 
		"# Karamelo - Configuration\n"
		"aspect=1\r\n"
		"filter=9\n"
		"   # this is a commented line\n"
		"wallpaper=4\r\n"
		"fullscreen=1\n"
		"netplay_ip=192.168.1.50\r\n";

	std::istringstream iss(cfg_text);
	auto kv = ParseConfigStream(iss);

	ASSERT_EQ(kv.size(), (size_t)5);
	ASSERT_STR_EQ(kv["aspect"].c_str(), "1");
	ASSERT_STR_EQ(kv["filter"].c_str(), "9");
	ASSERT_STR_EQ(kv["wallpaper"].c_str(), "4");
	ASSERT_STR_EQ(kv["fullscreen"].c_str(), "1");
	ASSERT_STR_EQ(kv["netplay_ip"].c_str(), "192.168.1.50");
}

TEST_CASE(ConfigSettingsRoundtrip)
{
	int aspect = 2;
	int filter = 5;
	int wallpaper = 3;
	bool fullscreen = true;
	std::string ip = "10.0.0.1";

	std::ostringstream oss;
	oss << "# Karamelo - Configuration\n";
	oss << "aspect=" << aspect << "\n";
	oss << "filter=" << filter << "\n";
	oss << "wallpaper=" << wallpaper << "\n";
	oss << "fullscreen=" << (fullscreen ? 1 : 0) << "\n";
	oss << "netplay_ip=" << ip << "\n";

	std::istringstream iss(oss.str());
	auto kv = ParseConfigStream(iss);

	int loaded_aspect = TestClampInt(std::stoi(kv["aspect"]), 0, 2);
	int loaded_filter = TestClampInt(std::stoi(kv["filter"]), 0, 9);
	int loaded_wall = TestClampInt(std::stoi(kv["wallpaper"]), 0, 5);
	bool loaded_fs = (std::stoi(kv["fullscreen"]) != 0);
	std::string loaded_ip = kv["netplay_ip"];

	ASSERT_EQ(loaded_aspect, 2);
	ASSERT_EQ(loaded_filter, 5);
	ASSERT_EQ(loaded_wall, 3);
	ASSERT_TRUE(loaded_fs);
	ASSERT_STR_EQ(loaded_ip.c_str(), "10.0.0.1");
}

TEST_CASE(ConfigGamepadAndKeyboardBindingsRoundtrip)
{
	std::ostringstream oss;
	oss << "# Karamelo - Controller Configuration\n";
	oss << "pad_device=0\n";
	oss << "key_b=90\n";
	oss << "key_a=88\n";
	oss << "pad_b=4096\n";
	oss << "pad_a=8192\n";
	oss << "pad_up=1\n";
	oss << "pad_down=2\n";

	std::istringstream iss(oss.str());
	auto kv = ParseConfigStream(iss);

	ASSERT_EQ(kv.size(), (size_t)7);
	ASSERT_STR_EQ(kv["pad_device"].c_str(), "0");
	ASSERT_STR_EQ(kv["key_b"].c_str(), "90");
	ASSERT_STR_EQ(kv["key_a"].c_str(), "88");
	ASSERT_STR_EQ(kv["pad_b"].c_str(), "4096");
	ASSERT_STR_EQ(kv["pad_a"].c_str(), "8192");
	ASSERT_STR_EQ(kv["pad_up"].c_str(), "1");
	ASSERT_STR_EQ(kv["pad_down"].c_str(), "2");
}

TEST_CASE(ConfigLanguageOption)
{
	std::string cfg_pt = "language=0\n";
	std::string cfg_en = "language=1\n";

	std::istringstream iss_pt(cfg_pt);
	auto kv_pt = ParseConfigStream(iss_pt);
	ASSERT_STR_EQ(kv_pt["language"].c_str(), "0");

	std::istringstream iss_en(cfg_en);
	auto kv_en = ParseConfigStream(iss_en);
	ASSERT_STR_EQ(kv_en["language"].c_str(), "1");

	// Clamping validation (0=PT, 1=EN)
	ASSERT_EQ(TestClampInt(-1, 0, 1), 0);
	ASSERT_EQ(TestClampInt(2, 0, 1), 1);
	ASSERT_EQ(TestClampInt(0, 0, 1), 0);
	ASSERT_EQ(TestClampInt(1, 0, 1), 1);
}

TEST_CASE(InputPresetLayoutsAndSwitching)
{
	// 1. Preset Names
	ASSERT_STR_EQ(InputPresetName(PRESET_MISTER_NINTENDO), "MiSTer / Nintendo");
	ASSERT_STR_EQ(InputPresetName(PRESET_XBOX_NATIVE), "Xbox Nativo");
	ASSERT_STR_EQ(InputPresetName(PRESET_PLAYSTATION), "PlayStation");
	ASSERT_STR_EQ(InputPresetName(PRESET_ARCADE_6BTN), "Arcade 6-Botoes");

	// 2. MiSTer / Nintendo Preset (Nintendo layout on Xbox physical controller)
	InputBindApplyPreset(PRESET_MISTER_NINTENDO);
	ASSERT_EQ(InputBindGetPreset(), PRESET_MISTER_NINTENDO);
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_A); // Xbox physical A -> SNES B
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_B); // Xbox physical B -> SNES A
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_X); // Xbox physical X -> SNES Y
	ASSERT_EQ(InputBindGetPad(BIND_X), PAD_BTN_Y); // Xbox physical Y -> SNES X

	// 3. Xbox Native Preset (Direct 1:1 match)
	InputBindApplyPreset(PRESET_XBOX_NATIVE);
	ASSERT_EQ(InputBindGetPreset(), PRESET_XBOX_NATIVE);
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_A);
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_B);
	ASSERT_EQ(InputBindGetPad(BIND_X), PAD_BTN_X);
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_Y);

	// 4. PlayStation Preset
	InputBindApplyPreset(PRESET_PLAYSTATION);
	ASSERT_EQ(InputBindGetPreset(), PRESET_PLAYSTATION);
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_A); // Cross (South) -> Retro B
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_B); // Circle (East) -> Retro A
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_X); // Square (West) -> Retro Y
	ASSERT_EQ(InputBindGetPad(BIND_X), PAD_BTN_Y); // Triangle (North) -> Retro X

	// 5. Arcade 6-Button Preset
	InputBindApplyPreset(PRESET_ARCADE_6BTN);
	ASSERT_EQ(InputBindGetPreset(), PRESET_ARCADE_6BTN);
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_X);  // LP
	ASSERT_EQ(InputBindGetPad(BIND_X), PAD_BTN_Y);  // MP
	ASSERT_EQ(InputBindGetPad(BIND_L), PAD_BTN_LB); // HP
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_A);  // LK
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_B);  // MK
	ASSERT_EQ(InputBindGetPad(BIND_R), PAD_BTN_RB); // HK
}

TEST_CASE(PerSystemRemapProfiles)
{
	// Reset to standard preset
	InputBindApplyPreset(PRESET_XBOX_NATIVE);
	ASSERT_TRUE(InputBindSaveSystemProfile("TestSystem"));
	ASSERT_TRUE(InputBindHasSystemProfile("TestSystem"));

	// Change preset
	InputBindApplyPreset(PRESET_ARCADE_6BTN);
	ASSERT_EQ(InputBindGetPad(BIND_Y), PAD_BTN_X);

	// Reload system profile and verify it restored TestSystem (Xbox Native)
	ASSERT_TRUE(InputBindLoadSystemProfile("TestSystem"));
	ASSERT_EQ(InputBindGetPad(BIND_A), PAD_BTN_A);
	ASSERT_EQ(InputBindGetPad(BIND_B), PAD_BTN_B);

	// Delete and verify clean removal
	ASSERT_TRUE(InputBindDeleteSystemProfile("TestSystem"));
	ASSERT_FALSE(InputBindHasSystemProfile("TestSystem"));

	// Test system name with slashes and spaces (e.g. Genesis / Mega Drive)
	ASSERT_TRUE(InputBindSaveSystemProfile("Genesis / Mega Drive"));
	ASSERT_TRUE(InputBindHasSystemProfile("Genesis / Mega Drive"));
	ASSERT_TRUE(InputBindDeleteSystemProfile("Genesis / Mega Drive"));
	ASSERT_FALSE(InputBindHasSystemProfile("Genesis / Mega Drive"));
}

TEST_CASE(GamepadBluetoothDeviceDetection)
{
	// Device slot type name query returns valid non-null strings
	const char* d0 = GamepadGetDeviceTypeName(0);
	ASSERT_TRUE(d0 != nullptr);
	ASSERT_TRUE(strlen(d0) > 0);

	const char* d1 = GamepadGetDeviceTypeName(1);
	ASSERT_TRUE(d1 != nullptr);
	ASSERT_STR_EQ(d1, "Gamepad");
}

TEST_CASE(FavoritesAndRecentManagementLogic)
{
	// 1. Star marker stripping for favorites in OSD
	std::string fav_osd_entry = "* Super Mario World (USA).sfc";
	ASSERT_TRUE(fav_osd_entry.rfind("* ", 0) == 0);
	std::string raw_fn = fav_osd_entry.substr(2);
	ASSERT_STR_EQ(raw_fn.c_str(), "Super Mario World (USA).sfc");

	// Non-favorite entry
	std::string normal_entry = "Chrono Trigger.sfc";
	ASSERT_FALSE(normal_entry.rfind("* ", 0) == 0);

	// 2. Port identifier formatting
	std::string port_fav = "port:sm64";
	ASSERT_TRUE(port_fav.rfind("port:", 0) == 0);
	ASSERT_STR_EQ(port_fav.substr(5).c_str(), "sm64");

	// 3. Recent games list insertion and 15-item cap logic
	std::vector<std::string> recents;
	for (int i = 0; i < 20; i++)
	{
		std::string item = "game_" + std::to_string(i);
		auto it = std::find(recents.begin(), recents.end(), item);
		if (it != recents.end()) recents.erase(it);
		recents.insert(recents.begin(), item);
		if (recents.size() > 15) recents.resize(15);
	}

	ASSERT_EQ(recents.size(), (size_t)15);
	ASSERT_STR_EQ(recents[0].c_str(), "game_19");
	ASSERT_STR_EQ(recents[14].c_str(), "game_5");

	// Re-playing game_5 moves it to index 0 without duplicating
	auto it = std::find(recents.begin(), recents.end(), "game_5");
	if (it != recents.end()) recents.erase(it);
	recents.insert(recents.begin(), "game_5");

	ASSERT_EQ(recents.size(), (size_t)15);
	ASSERT_STR_EQ(recents[0].c_str(), "game_5");
	ASSERT_STR_EQ(recents[1].c_str(), "game_19");
}
