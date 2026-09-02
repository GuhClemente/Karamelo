#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>

#include "archive_helper.h"
#include "menu.h"

namespace fs = std::filesystem;

// Extraction must run to completion. The old 15s cap returned while tar.exe was
// still writing, so callers scanned a half-extracted folder and then launched a
// second extractor on top of the first, corrupting the result.
#define EXTRACT_TIMEOUT_MS (10 * 60 * 1000) // 10 min, only to survive a wedged tool

static bool RunHiddenCommand(const std::string& cmd)
{
	STARTUPINFOA si = { 0 };
	PROCESS_INFORMATION pi = { 0 };
	si.cb = sizeof(STARTUPINFOA);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;

	char cmd_buf[2048];
	strncpy_s(cmd_buf, cmd.c_str(), sizeof(cmd_buf) - 1);

	if (!CreateProcessA(NULL, cmd_buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
		return false;

	DWORD wait = WaitForSingleObject(pi.hProcess, EXTRACT_TIMEOUT_MS);

	if (wait == WAIT_TIMEOUT)
	{
		// Never leave it running in the background writing into the folder we
		// are about to scan.
		TerminateProcess(pi.hProcess, 1);
		WaitForSingleObject(pi.hProcess, 5000);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return false;
	}

	DWORD exit_code = 1;
	GetExitCodeProcess(pi.hProcess, &exit_code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return (exit_code == 0);
}

// Repacks in the wild routinely ship a cue whose FILE entries do not match what
// is actually inside the archive: a data track saved under a different name,
// audio tracks that were never included. A core given such a cue refuses the
// whole disc and boots to its BIOS instead. Rather than give up, write a
// corrected cue beside it in the cache and hand the core that.
//
// Two repairs, in order of confidence:
//   1. The data track (first FILE) is missing but exactly one unreferenced
//      image file sits in the same folder -> it is that file, renamed.
//   2. An audio track is missing -> truncate there. Tracks on a real disc are
//      contiguous, so keeping later ones would renumber everything after the
//      gap and hand the game the wrong music.
static std::string CueQuotedName(const std::string& line)
{
	size_t open_quote = line.find('"');
	if (open_quote == std::string::npos) return "";
	size_t close_quote = line.find('"', open_quote + 1);
	if (close_quote == std::string::npos) return "";
	return line.substr(open_quote + 1, close_quote - open_quote - 1);
}

static bool LineIsFileEntry(const std::string& line)
{
	size_t i = line.find_first_not_of(" \t");
	if (i == std::string::npos) return false;
	return line.compare(i, 4, "FILE") == 0;
}

static bool SanitizeCue(const fs::path& cue_path, std::string& out_cue_path)
{
	out_cue_path = cue_path.string();

	std::ifstream in(cue_path);
	if (!in) return false;

	std::vector<std::string> lines;
	std::string line;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(line);
	}
	in.close();
	if (lines.empty()) return false;

	const fs::path dir = cue_path.parent_path();

	// Split into a header plus one block per FILE entry.
	std::vector<std::string> header;
	std::vector<std::vector<std::string>> blocks;

	for (const auto& l : lines)
	{
		if (LineIsFileEntry(l)) blocks.push_back({ l });
		else if (blocks.empty()) header.push_back(l);
		else blocks.back().push_back(l);
	}

	if (blocks.empty()) return false;

	// Names the cue already refers to, so a substitution never steals one.
	std::vector<std::string> referenced;
	for (const auto& b : blocks)
	{
		std::string n = CueQuotedName(b[0]);
		std::transform(n.begin(), n.end(), n.begin(), ::tolower);
		if (!n.empty()) referenced.push_back(n);
	}

	bool modified = false;
	size_t keep = blocks.size();

	for (size_t i = 0; i < blocks.size(); i++)
	{
		std::string name = CueQuotedName(blocks[i][0]);
		if (!name.empty() && fs::exists(dir / name)) continue;

		if (i == 0)
		{
			// Repair 1: find the one image file nothing else claims.
			std::string candidate;
			int candidate_count = 0;

			try
			{
				for (const auto& entry : fs::directory_iterator(dir))
				{
					if (entry.is_directory()) continue;

					std::string ext = entry.path().extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
					if (ext != ".iso" && ext != ".bin" && ext != ".img") continue;

					std::string fname = entry.path().filename().string();
					std::string lower = fname;
					std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

					if (std::find(referenced.begin(), referenced.end(), lower) != referenced.end())
						continue;

					candidate = fname;
					candidate_count++;
				}
			}
			catch (...) {}

			if (candidate_count != 1) return false; // ambiguous: leave it alone

			// A FILE line with no closing quote (or no quotes at all) leaves
			// close_quote at npos; substr(npos) throws std::out_of_range,
			// which nothing here catches - it used to propagate out of this
			// function entirely. Same treatment as an ambiguous match above:
			// leave the line alone rather than fail the whole repair.
			size_t open_quote = blocks[i][0].find('"');
			size_t close_quote = (open_quote == std::string::npos)
				? std::string::npos : blocks[i][0].find('"', open_quote + 1);
			if (open_quote == std::string::npos || close_quote == std::string::npos)
				return false;
			blocks[i][0] = blocks[i][0].substr(0, open_quote + 1) + candidate +
						   blocks[i][0].substr(close_quote);
			modified = true;
			continue;
		}

		// Repair 2: first missing audio track ends the disc.
		keep = i;
		modified = true;
		break;
	}

	if (!modified) return true; // cue was fine as shipped
	if (keep == 0) return false;

	fs::path fixed = dir / "mister_fixed.cue";
	std::ofstream out(fixed, std::ios::binary);
	if (!out) return false;

	for (const auto& l : header) out << l << "\r\n";
	for (size_t i = 0; i < keep; i++)
		for (const auto& l : blocks[i]) out << l << "\r\n";
	out.close();

	out_cue_path = fixed.string();
	return true;
}

bool ArchiveIsCompressed(const std::string& filepath)
{
	fs::path p(filepath);
	std::string ext = p.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	return (ext == ".zip" || ext == ".7z" || ext == ".rar" || ext == ".tar" || ext == ".gz");
}

std::string ArchiveResolveCoreForPath(const std::string& file_path, const std::string& dir_hint)
{
	std::string ext = fs::path(file_path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	const std::string& hint = dir_hint.empty() ? file_path : dir_hint;
	auto in = [&hint](const char* needle) {
		return hint.find(needle) != std::string::npos;
	};

	if (ext == ".z64" || ext == ".n64" || ext == ".v64") return "cores/n64.dll";
	if (ext == ".nes" || ext == ".fds") return "cores/nes.dll";
	if (ext == ".sfc" || ext == ".smc") return "cores/snes.dll";
	if (ext == ".md" || ext == ".gen" || ext == ".smd") return "cores/genesis.dll";
	if (ext == ".sms" || ext == ".gg" || ext == ".sg") return "cores/sms.dll";
	if (ext == ".pce" || ext == ".sgx") return "cores/pce.dll";
	if (ext == ".neo") return "cores/neogeo.dll";
	if (ext == ".a26") return "cores/atari2600.dll";
	if (ext == ".a52") return "cores/atari5200.dll";
	if (ext == ".a78") return "cores/atari7800.dll";
	if (ext == ".nds") return "cores/nds.dll";
	if (ext == ".3ds" || ext == ".cia") return "cores/3ds.dll";
	if (ext == ".gdi" || ext == ".cdi") return "cores/dreamcast.dll";
	if (ext == ".gcm" || ext == ".rvz" || ext == ".wbfs") return "cores/gamecube.dll";
	if (ext == ".gba") return "cores/gba.dll";
	if (ext == ".gb" || ext == ".gbc") return "cores/gb.dll";
	if (ext == ".ngp" || ext == ".ngc") return "cores/ngp.dll";
	if (ext == ".ws" || ext == ".wsc") return "cores/wswan.dll";
	if (ext == ".lnx") return "cores/lynx.dll";
	if (ext == ".32x") return "cores/32x.dll";
	if (ext == ".j64" || ext == ".jag") return "cores/jaguar.dll";
	if (ext == ".col") return "cores/coleco.dll";
	if (ext == ".adf" || ext == ".hdf" || ext == ".lha") return "cores/amiga.dll";
	if (ext == ".d64" || ext == ".t64" || ext == ".prg" || ext == ".crt") return "cores/c64.dll";
	if (ext == ".tzx" || ext == ".tap" || ext == ".z80" || ext == ".sna") return "cores/spectrum.dll";
	if (ext == ".pcfx") return "cores/pcfx.dll";
	if (ext == ".cso") return "cores/psp.dll";

	if (ext == ".chd" || ext == ".cue" || ext == ".iso" || ext == ".m3u" ||
		ext == ".pbp" || ext == ".toc")
	{
		if (in("Arcade"))
		{
			std::string f_lower = file_path;
			std::transform(f_lower.begin(), f_lower.end(), f_lower.begin(), ::tolower);
			if (f_lower.find("gdl-") != std::string::npos || f_lower.find("cvs2") != std::string::npos ||
				f_lower.find("mvsc2") != std::string::npos || f_lower.find("naomi") != std::string::npos)
			{
				return "cores/dreamcast.dll";
			}
			return "cores/arcade_fbneo.dll";
		}
		if (in("NeoGeo")) return "cores/neocd_alt.dll";
		if (in("Saturn")) return "cores/saturn.dll";
		if (in("MegaCD")) return "cores/genesis.dll";
		if (in("TurboGrafx") || in("PCE")) return "cores/pce.dll";
		if (in("Dreamcast")) return "cores/dreamcast.dll";
		if (in("GameCube")) return "cores/gamecube.dll";
		if (in("PlayStation2") || in("PS2")) return "cores/ps2.dll";
		if (in("PSP")) return "cores/psp.dll";
		if (in("3DO")) return "cores/3do.dll";
		if (in("Amiga") || in("CD32")) return "cores/amiga.dll";
		if (in("PCFX") || in("PC-FX")) return "cores/pcfx.dll";
		return "cores/psx.dll";
	}

	if (ext == ".mx1" || ext == ".mx2") return "cores/msx.dll";

	if (ext == ".bin" || ext == ".rom" || ext == ".dsk" || ext == ".cas")
	{
		if (in("NeoGeo")) return "cores/neogeo.dll";
		if (in("Atari5200")) return "cores/atari5200.dll";
		if (in("Atari7800")) return "cores/atari7800.dll";
		if (in("Atari")) return "cores/atari2600.dll";
		if (in("Genesis")) return "cores/genesis.dll";
		if (in("32X")) return "cores/32x.dll";
		if (in("NES")) return "cores/nes.dll";
		if (in("Saturn")) return "cores/saturn.dll";
		if (in("PlayStation2") || in("PS2")) return "cores/ps2.dll";
		if (in("PlayStation")) return "cores/psx.dll";
		if (in("Dreamcast")) return "cores/dreamcast.dll";
		if (in("MSX")) return "cores/msx.dll";
		if (in("Amiga")) return "cores/amiga.dll";
		if (in("3DO")) return "cores/3do.dll";
		if (in("Coleco")) return "cores/coleco.dll";
		return "cores/genesis.dll";
	}

	if (ext == ".zip" || ext == ".7z" || ext == ".rar")
	{
		if (in("Arcade"))
		{
			std::string stem = fs::path(file_path).stem().string();
			std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);

			// 1. Sega NAOMI / Sammy Atomiswave 3D arcade games -> Flycast
			if (stem == "mvsc2" || stem == "cvs2" || stem == "cvs2gd" || stem == "cvs2gd-chd" ||
				stem == "mslug6" || stem == "slasho" || stem == "hokuto" || stem == "fotns" ||
				stem == "ikaruga" || stem == "dolphinblue" || stem == "kofnw" || stem == "kofxi" ||
				stem == "ngbc" || stem == "ggx" || stem == "ggxx" || stem == "ggxxac" ||
				stem == "monkeyba" || stem == "jambo" || stem == "dybb99" || stem == "samba" ||
				stem == "hotd2" || stem == "csmash" || stem == "senko" || stem == "radirgy" ||
				stem == "karous" || stem == "underdef" || stem == "trizeal" || stem == "mamoru" ||
				stem == "illvelo" || stem == "spkrnch" || stem == "virtuafg" || stem == "vf4" ||
				stem == "vf4evo" || stem == "vf4tuned" || stem == "vf4final" || stem == "ctrhunt" ||
				stem == "gwing2" || stem == "zerogun2" || stem == "spawn" || stem == "claychal" ||
				stem == "heavybox" || stem == "deathcml" || stem == "smarine" || stem == "alienfnt" ||
				stem == "pstone" || stem == "pstone2" || stem == "gundmvg" || stem == "gundmfl" ||
				stem == "meltyb" || stem == "meltyba" || stem == "capcsv" || stem == "toukon" ||
				stem == "demolish" || stem == "dirtdvls" || stem == "slashout" || stem == "crzytaxi" ||
				stem == "zombrvn" || stem == "18wheel" || stem == "airline" || stem == "alpilot" ||
				stem == "clubk" || stem == "wldkicks" || stem == "ringout" || stem == "giantgr") {
				return "cores/dreamcast.dll";
			}

			// 2. Midway Y/T-Unit & Williams games not supported in FBNeo -> MAME 2003
			if (stem == "mk" || stem == "mk2" || stem == "mk2r14" || stem == "mk2r20" ||
				stem == "mk2r21" || stem == "mk2r30" || stem == "mk2r31" || stem == "mk2r32" ||
				stem == "mk2r42" || stem == "mk2r91" || stem == "mk3" || stem == "mk3r10" ||
				stem == "mk3r20" || stem == "mk3r21" || stem == "mk3r22" || stem == "umk3" ||
				stem == "umk3r10" || stem == "umk3r11" || stem == "umk3r12" || stem == "nbajam" ||
				stem == "nbajamte" || stem == "nbajamr1" || stem == "nbajamr2" || stem == "nbahangt" ||
				stem == "kinst" || stem == "kinst2" || stem == "openice" || stem == "wwfmania" ||
				stem == "rampage" || stem == "ramprt" || stem == "tmnt" || stem == "tmnt2" ||
				stem == "trog" || stem == "smash_tv" || stem == "smashtv" || stem == "archrivl" ||
				stem == "crusnusa" || stem == "crusnwld" || stem == "crusnu40" || stem == "carnevil" ||
				stem == "mace" || stem == "wargods" || stem == "nbaonfl" || stem == "nflblitz" ||
				stem == "nflblitz99" || stem == "hydro" || stem == "offroad" || stem == "paperboy" ||
				stem == "gauntlet" || stem == "gaunt2" || stem == "marble" || stem == "joust" ||
				stem == "defender" || stem == "sinistar" || stem == "robotron" || stem == "tapper" ||
				stem == "timber" || stem == "rootbeer" || stem == "spyhunt" || stem == "twotigers" ||
				stem == "xenophobe" || stem == "pigskin" || stem == "highimp" || stem == "strkfc" ||
				stem == "blasted" || stem == "bmaster" || stem == "clowns") {
				return "cores/mame2003.dll";
			}

			// 3. Namco System 11/12 3D games -> MAME 2010
			if (stem == "tekken" || stem == "tekken2" || stem == "tekken3" || stem == "tekkenub" ||
				stem == "soulclbr" || stem == "souledge" || stem == "ridge4" || stem == "pointblk" ||
				stem == "pointbl2" || stem == "gunbarl" || stem == "timecris" || stem == "timecrs2" ||
				stem == "cryptkpr" || stem == "outfxies" || stem == "machbrkr" || stem == "sws97" ||
				stem == "aquarush" || stem == "liblrn" || stem == "tenkomor" || stem == "derbyqd" ||
				stem == "pacrev" || stem == "ehrgeiz" || stem == "dunkmnia") {
				return "cores/mame2010.dll";
			}

			return "cores/arcade_fbneo.dll";
		}
		if (in("NeoGeo")) return "cores/neogeo.dll";
		if (in("Atari5200")) return "cores/atari5200.dll";
		if (in("Atari7800")) return "cores/atari7800.dll";
		if (in("Atari")) return "cores/atari2600.dll";
		if (in("Jaguar")) return "cores/jaguar.dll";
		if (in("Lynx")) return "cores/lynx.dll";
		if (in("Coleco")) return "cores/coleco.dll";
		if (in("MasterSystem")) return "cores/sms.dll";
		if (in("MegaCD")) return "cores/genesis.dll";
		if (in("32X")) return "cores/32x.dll";
		if (in("Genesis")) return "cores/genesis.dll";
		if (in("SNES")) return "cores/snes.dll";
		if (in("NES")) return "cores/nes.dll";
		if (in("Nintendo64") || in("N64")) return "cores/n64.dll";
		if (in("GBA")) return "cores/gba.dll";
		if (in("GameBoy") || in("GB")) return "cores/gb.dll";
		if (in("NDS")) return "cores/nds.dll";
		if (in("3DS")) return "cores/3ds.dll";
		if (in("PSP")) return "cores/psp.dll";
		if (in("DOS") || in("MSDOS")) return "cores/dosbox_pure.dll";
		if (in("MSX")) return "cores/msx.dll";
		if (in("Amiga")) return "cores/amiga.dll";
		if (in("C64") || in("Commodore")) return "cores/c64.dll";
		if (in("Spectrum") || in("ZXSpectrum")) return "cores/spectrum.dll";
		if (in("3DO")) return "cores/3do.dll";
		if (in("NGP") || in("NeoGeoPocket")) return "cores/ngp.dll";
		if (in("WonderSwan") || in("WSwan")) return "cores/wswan.dll";
		if (in("PCFX") || in("PC-FX")) return "cores/pcfx.dll";
		if (in("Saturn")) return "cores/saturn.dll";
		if (in("TurboGrafx")) return "cores/pce.dll";
		if (in("Dreamcast")) return "cores/dreamcast.dll";
		if (in("GameCube")) return "cores/gamecube.dll";
		if (in("PlayStation2") || in("PS2")) return "cores/ps2.dll";
		if (in("PlayStation")) return "cores/psx.dll";
		return "cores/arcade_fbneo.dll";
	}

	return "";
}

// Lists the archive's entry names via "tar.exe -tf" (no extraction) and
// rejects it if any entry could escape dest_dir: a ".." path component, a
// leading path separator, or a drive letter. Archives handled by this
// function are not always trustworthy input - a Ports & Recomp download
// comes from whatever repo a PortDefinition points at, and this same
// function extracts user-supplied ROM zips too. tar/Expand-Archive extract
// wherever an entry's path resolves to with no containment of their own, so
// this has to happen before the real extraction, not after.
static bool ArchiveHasUnsafeEntry(const std::string& archive_path)
{
	std::string list_cmd = "tar.exe -tf \"" + archive_path + "\"";

	SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
	HANDLE read_pipe = NULL, write_pipe = NULL;
	if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) return true; // fail closed
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOA si = { 0 };
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdOutput = write_pipe;
	si.hStdError = write_pipe;
	PROCESS_INFORMATION pi = { 0 };

	std::vector<char> cmd_buf(list_cmd.begin(), list_cmd.end());
	cmd_buf.push_back('\0');

	bool started = CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, TRUE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	CloseHandle(write_pipe);

	if (!started) { CloseHandle(read_pipe); return true; } // fail closed: can't list, don't trust it

	std::string output;
	char buf[4096];
	DWORD n = 0;
	while (ReadFile(read_pipe, buf, sizeof(buf), &n, NULL) && n > 0)
		output.append(buf, n);
	CloseHandle(read_pipe);

	WaitForSingleObject(pi.hProcess, 30000);
	DWORD exit_code = 1;
	GetExitCodeProcess(pi.hProcess, &exit_code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	// tar couldn't even list this archive's contents (a format it can't
	// parse, or it's genuinely corrupt) - the PowerShell fallback in
	// ArchiveExtractAll would otherwise extract it completely unvalidated.
	// Refusing it here is a real behavior change for whatever edge case that
	// fallback existed for, but extracting something we could not check the
	// paths of is worse.
	if (exit_code != 0) return true;

	std::stringstream ss(output);
	std::string entry;
	while (std::getline(ss, entry))
	{
		if (!entry.empty() && entry.back() == '\r') entry.pop_back();
		if (entry.empty()) continue;

		if (entry.find("..") != std::string::npos) return true;
		if (entry[0] == '/' || entry[0] == '\\') return true;
		if (entry.size() >= 2 && entry[1] == ':') return true; // drive-letter absolute path
	}
	return false;
}

bool ArchiveExtractAll(const std::string& archive_path, const std::string& dest_dir)
{
	if (!fs::exists(archive_path)) return false;
	if (ArchiveHasUnsafeEntry(archive_path)) return false;
	fs::create_directories(dest_dir);

	// 1. Try Windows tar.exe (fast native extractor)
	std::string tar_cmd = "tar.exe -xf \"" + archive_path + "\" -C \"" + dest_dir + "\"";
	bool ok = RunHiddenCommand(tar_cmd);

	// 2. Only if tar genuinely failed (RAR, or a format it cannot read) fall
	//    back to PowerShell. Running both against the same folder corrupts the
	//    result. RunHiddenCommand now guarantees tar is no longer alive here.
	if (!ok)
	{
		// A single quote inside the path closes the PowerShell string early, so a
		// ROM named after "Marvel's ..." or "Tony Hawk's ..." never extracted.
		// Doubling it is how PowerShell escapes a quote inside a literal string.
		auto ps_quote = [](const std::string& s) {
			std::string out;
			for (char c : s) { out += c; if (c == '\'') out += c; }
			return out;
		};
		std::string ps_cmd = "powershell.exe -NoProfile -NonInteractive -Command \"try { Expand-Archive -LiteralPath '" + ps_quote(archive_path) + "' -DestinationPath '" + ps_quote(dest_dir) + "' -Force } catch {}\"";
		RunHiddenCommand(ps_cmd);
	}

	return fs::exists(dest_dir) && !fs::is_empty(dest_dir);
}

bool ArchiveExtractRom(const std::string& archive_path, std::string& out_extracted_rom_path, std::string& out_core_dll)
{
	fs::path arch_path(archive_path);
	if (!fs::exists(arch_path)) return false;

	std::string stem = arch_path.stem().string();
	std::string ext = arch_path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	// Special case: NeoGeo Arcade ROM sets (.zip) loaded directly by FBNeo/Geolith
	if (archive_path.find("NeoGeo") != std::string::npos && ext == ".zip")
	{
		out_extracted_rom_path = archive_path;
		out_core_dll = "cores/neogeo.dll";
		return true;
	}

	std::string cache_dir = (fs::current_path() / "cache" / stem).string();
	if (!ArchiveExtractAll(archive_path, cache_dir)) return false;

	// 3. Scan the extracted directory. Ordered by priority, not by whatever the
	//    filesystem happens to return first: a multi-track disc set contains a
	//    playlist, a cue AND its raw tracks, and picking the wrong one boots a
	//    core with no game.
	static const std::vector<std::string> KNOWN_EXTS = {
		".m3u",                          // multi-disc playlist wins outright
		".cue", ".chd", ".toc", ".iso", ".gdi", ".cdi", ".gcm", ".rvz", ".wbfs", ".cso", ".pbp",  // disc images
		".z64", ".n64", ".v64",
		".sfc", ".smc",
		".nes", ".fds",
		".md", ".gen", ".smd",
		".sms", ".gg", ".sg",
		".pce", ".sgx", ".pcfx",
		".neo",
		".gb", ".gbc", ".gba",
		".nds", ".3ds", ".cia",
		".32x",
		".a26", ".a52", ".a78", ".j64", ".jag", ".lnx", ".col",
		".ws", ".wsc", ".ngp", ".ngc",
		".adf", ".hdf", ".lha", ".d64", ".t64", ".prg", ".crt",
		".tzx", ".tap", ".z80", ".sna", ".dsk", ".cas",
		".mx1", ".mx2",
		".bin", ".rom"
	};

	// Collect once, then pick by priority.
	std::vector<fs::path> found;
	try
	{
		for (const auto& entry : fs::recursive_directory_iterator(cache_dir))
		{
			if (!entry.is_directory()) found.push_back(entry.path());
		}
	}
	catch (...)
	{
		return false;
	}

	try
	{
		for (const auto& target_ext : KNOWN_EXTS)
		{
			for (const auto& entry_path : found)
			{
				std::string f_ext = entry_path.extension().string();
				std::transform(f_ext.begin(), f_ext.end(), f_ext.begin(), ::tolower);

				if (f_ext == target_ext)
				{
					// A cue whose tracks do not all resolve makes the core boot
					// to its BIOS with no disc. Repair what can be repaired and
					// hand over the corrected copy.
					std::string usable_path = entry_path.string();
					if (f_ext == ".cue" && !SanitizeCue(entry_path, usable_path))
						continue;

					out_extracted_rom_path = usable_path;
					out_core_dll = ArchiveResolveCoreForPath(usable_path, archive_path);
					if (!out_core_dll.empty())
						return true;
				}
			}
		}
	}
	catch (...)
	{
		return false;
	}

	return false;
}
