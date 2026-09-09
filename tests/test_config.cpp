#include "test_framework.h"
#include <sstream>
#include <map>
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
