#ifndef ARCHIVE_HELPER_H_INCLUDED
#define ARCHIVE_HELPER_H_INCLUDED

#include <string>
#include <vector>

bool ArchiveIsCompressed(const std::string& filepath);
std::string ArchiveResolveCoreForPath(const std::string& file_path, const std::string& dir_hint);
bool ArchiveExtractRom(const std::string& archive_path, std::string& out_extracted_rom_path, std::string& out_core_dll);

#endif
