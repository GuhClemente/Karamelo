#include "test_framework.h"
#include "port_catalog.h"
#include "json_mini.h"

#include <fstream>
#include <sstream>
#include <regex>
#include <string>

static std::string ReadFileText(const char* path)
{
	std::ifstream f(path, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// The copy compiled into the binary has to be data/ports.json byte for byte.
// Editing the data file and forgetting "python tools/embed_ports.py" would
// otherwise ship the old catalog without any warning.
TEST_CASE(PortCatalogEmbeddedMatchesDataFile)
{
	std::string data = ReadFileText("data/ports.json");
	ASSERT_TRUE(!data.empty());
	ASSERT_TRUE(data == PortCatalogEmbeddedJson());
}

TEST_CASE(PortCatalogEmbeddedParses)
{
	PortCatalog cat;
	std::string err;
	ASSERT_TRUE(PortCatalogParse(PortCatalogEmbeddedJson(), cat, err));
	ASSERT_TRUE(cat.catalog_version > 0);
	ASSERT_TRUE(cat.ports.size() > 0);
}

// Writing what was read and reading it again changes nothing - the serializer
// knows every field the parser does.
TEST_CASE(PortCatalogRoundTrips)
{
	PortCatalog a, b;
	std::string err;
	ASSERT_TRUE(PortCatalogParse(PortCatalogEmbeddedJson(), a, err));
	std::string once = PortCatalogToJson(a);
	ASSERT_TRUE(PortCatalogParse(once, b, err));
	ASSERT_TRUE(once == PortCatalogToJson(b));
	ASSERT_EQ(a.ports.size(), b.ports.size());
}

// CREDITS.md carries the totals the site and the docs quote ("**42 de 70 tem
// build Linux** e **31 de 70 tem build macOS**"). They are checked against the
// catalog itself, so adding a port without updating the docs fails the build
// instead of leaving the site with stale numbers.
TEST_CASE(PortCatalogCountsMatchCredits)
{
	PortCatalog cat;
	std::string err;
	ASSERT_TRUE(PortCatalogParse(PortCatalogEmbeddedJson(), cat, err));
	int visible = 0, linux_n = 0, mac_n = 0;
	for (const auto& p : cat.ports)
	{
		if (p.hidden) continue;
		visible++;
		if (p.has_linux_build) linux_n++;
		if (p.has_macos_build) mac_n++;
	}

	std::string credits = ReadFileText("CREDITS.md");
	std::smatch m;
	std::regex re("\\*\\*(\\d+) de (\\d+) t\\S+ build Linux\\*\\* e \\*\\*(\\d+) de (\\d+) t\\S+ build macOS\\*\\*");
	ASSERT_TRUE(std::regex_search(credits, m, re));
	ASSERT_EQ(std::stoi(m[1].str()), linux_n);
	ASSERT_EQ(std::stoi(m[2].str()), visible);
	ASSERT_EQ(std::stoi(m[3].str()), mac_n);
	ASSERT_EQ(std::stoi(m[4].str()), visible);

	// The Karamelo Studio page and API carry the same counts as plain text.
	std::string web = ReadFileText("src/web_admin.cpp");
	ASSERT_TRUE(web.find("\\\"ports_count\\\":" + std::to_string(visible)) != std::string::npos);
	ASSERT_TRUE(web.find(">" + std::to_string(visible) + " PC Ports") != std::string::npos);
	ASSERT_TRUE(web.find(std::to_string(linux_n) + " PC Ports nativos ELF64") != std::string::npos);
	ASSERT_TRUE(web.find(std::to_string(mac_n) + " PC Ports compilados em Mach-O") != std::string::npos);
}

TEST_CASE(PortCatalogRejectsUnsafeEntries)
{
	PortCatalog cat;
	std::string err;
	// No catalog_version.
	ASSERT_FALSE(PortCatalogParse("{\"ports\": []}", cat, err));
	// Duplicate id.
	ASSERT_FALSE(PortCatalogParse("{\"catalog_version\":1,\"ports\":[{\"id\":\"A\",\"display_name\":\"a\"},{\"id\":\"A\",\"display_name\":\"b\"}]}", cat, err));
	// An id is a folder name - nothing that climbs out of ports/.
	ASSERT_FALSE(PortCatalogParse("{\"catalog_version\":1,\"ports\":[{\"id\":\"../x\",\"display_name\":\"a\"}]}", cat, err));
	// Paths inside the port folder only.
	ASSERT_FALSE(PortCatalogParse("{\"catalog_version\":1,\"ports\":[{\"id\":\"A\",\"display_name\":\"a\",\"exe_relpath\":\"..\\\\evil.exe\"}]}", cat, err));
	ASSERT_FALSE(PortCatalogParse("{\"catalog_version\":1,\"ports\":[{\"id\":\"A\",\"display_name\":\"a\",\"exe_relpath\":\"C:\\\\evil.exe\"}]}", cat, err));
	// A custom download feed must be HTTPS.
	ASSERT_FALSE(PortCatalogParse("{\"catalog_version\":1,\"ports\":[{\"id\":\"A\",\"display_name\":\"a\",\"downloads_json_url\":\"http://x\"}]}", cat, err));
	// Wrong type.
	ASSERT_FALSE(PortCatalogParse("{\"catalog_version\":1,\"ports\":[{\"id\":\"A\",\"display_name\":\"a\",\"needs_rom\":\"yes\"}]}", cat, err));
	// Unknown fields are fine (a newer catalog read by an older app).
	ASSERT_TRUE(PortCatalogParse("{\"catalog_version\":1,\"ports\":[{\"id\":\"A\",\"display_name\":\"a\",\"future_field\":1}]}", cat, err));
}

// Project Tandem compiles itself on first use and its launcher exits at once.
TEST_CASE(PortCatalogKeepsSetupAndDetachedGameFields)
{
	PortCatalog cat;
	std::string err;
	const std::string good =
		"{\"catalog_version\":1,\"ports\":[{\"id\":\"A\",\"display_name\":\"A\","
		"\"setup_relpath\":\"Setup.cmd\",\"setup_args\":[\"-GameImage\",\"{iso}\"],"
		"\"setup_done_relpath\":\"local-data/settings.json\",\"detached_process\":\"mkdd.exe\"}]}";
	ASSERT_TRUE(PortCatalogParse(good, cat, err));
	PortCatalog again;
	ASSERT_TRUE(PortCatalogParse(PortCatalogToJson(cat), again, err));
	ASSERT_TRUE(again.ports[0].setup_relpath == "Setup.cmd");
	ASSERT_TRUE(again.ports[0].setup_args.size() == 2 && again.ports[0].setup_args[1] == "{iso}");
	ASSERT_TRUE(again.ports[0].setup_done_relpath == "local-data/settings.json");
	ASSERT_TRUE(again.ports[0].detached_process == "mkdd.exe");

	const std::string escaping_setup =
		"{\"catalog_version\":1,\"ports\":[{\"id\":\"A\",\"display_name\":\"A\",\"setup_relpath\":\"../evil.cmd\"}]}";
	ASSERT_FALSE(PortCatalogParse(escaping_setup, cat, err));
	const std::string path_as_process =
		"{\"catalog_version\":1,\"ports\":[{\"id\":\"A\",\"display_name\":\"A\",\"detached_process\":\"bin/mkdd.exe\"}]}";
	ASSERT_FALSE(PortCatalogParse(path_as_process, cat, err));
}

TEST_CASE(JsonMiniParsesEscapesAndRejectsGarbage)
{
	JsonValue v;
	std::string err;
	ASSERT_TRUE(JsonParse("{\"a\":\"x\\\"y\\u00e9\",\"b\":[1,2.5,true,null]}", v, err));
	ASSERT_STR_EQ(v.Find("a")->str.c_str(), "x\"y\xc3\xa9");
	ASSERT_EQ(v.Find("b")->items.size(), (size_t)4);
	ASSERT_FALSE(JsonParse("{\"a\":}", v, err));
	ASSERT_FALSE(JsonParse("{\"a\":1} trailing", v, err));
	ASSERT_FALSE(JsonParse("[1,2", v, err));
}
