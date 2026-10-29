// zip.hpp - minimal ZIP reader + inflate, replacing PureBasic's OpenPack/ExtractPackFile.
// Based on Vortex Minecraft Launcher by Kron4ek (PureBasic, GPL-3.0):
// https://github.com/Kron4ek/minecraft-vortex-launcher - see LICENSE at the repo root.
// Supports stored (0) and deflated (8) entries; extracts entries one by one.
#pragma once
#include <vector>
#include <string>

// Extract one file from the zip archive to destPath (parents must exist).
bool zipExtractFile(const std::string& zipPath, const std::string& entryName, const std::string& destPath);
// List all entry names (used only for completeness/tests).
std::vector<std::string> zipList(const std::string& zipPath);

// PB OpenPack equivalent: load the whole archive + locate the central directory.
bool zipOpenArchive(const std::string& zipPath, std::vector<unsigned char>& buf,
                    size_t& cdOff, size_t& cdCount);
// PB ExaminePack + ExtractPackFile loop: extract every entry whose extracted
// file is missing/empty under destDir (subdirectories created on demand).
bool zipExtractMissing(const std::vector<unsigned char>& buf, size_t cdOff, size_t cdCount,
                       const std::string& destDir);
