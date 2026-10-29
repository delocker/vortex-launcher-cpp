// http.hpp - WinHTTP replacements for PureBasic's ReceiveHTTPMemory / ReceiveHTTPFile.
// Based on Vortex Minecraft Launcher by Kron4ek (PureBasic, GPL-3.0):
// https://github.com/Kron4ek/minecraft-vortex-launcher - see LICENSE at the repo root.
// Follows redirects (like PB's libcurl-based downloader), streams to disk for files.
#pragma once
#include <string>

bool httpInit();
// ReceiveHTTPMemory(): fetch whole body into `out`; false on any network/HTTP error.
bool httpGetMemory(const std::string& url, std::string& out);
// ReceiveHTTPFile(): download straight into the target path (overwrite).
// On failure the partial file is deleted, like PB's ReceiveHTTPFile does.
bool httpDownloadFile(const std::string& url, const std::string& destPath);
