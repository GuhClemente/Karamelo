#ifndef ARCHIVE_HELPER_H_INCLUDED
#define ARCHIVE_HELPER_H_INCLUDED

#include <string>
#include <vector>

bool ArchiveIsCompressed(const std::string& filepath);
std::string ArchiveResolveCoreForPath(const std::string& file_path, const std::string& dir_hint);
bool ArchiveExtractRom(const std::string& archive_path, std::string& out_extracted_rom_path, std::string& out_core_dll);

// Extracts every file in the archive into dest_dir (creating it if needed),
// keeping the whole tree rather than picking out one ROM. Shared by
// ArchiveExtractRom and the ports installer, which needs the executable,
// its DLLs and its assets, not just a single known extension.
bool ArchiveExtractAll(const std::string& archive_path, const std::string& dest_dir);

#endif
