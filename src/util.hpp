// util.hpp - helpers for the C++ port of Vortex Minecraft Launcher 1.1.20
// (original: PureBasic, https://github.com/Kron4ek/minecraft-vortex-launcher, GPL-3.0)
//
// Internal string representation for ALL launcher logic and paths is UTF-8
// (same as the PureBasic original, which is UTF-8-based). Win32 W-API is used
// with explicit conversions.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------- encoding
inline std::wstring u2w(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), 0, 0);
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
inline std::string w2u(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), 0, 0, 0, 0);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, 0, 0);
    return s;
}

// ---------------------------------------------------------------- strings
inline void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}
// PureBasic StringField(): 1-based field index, multi-char separator.
inline std::string field(const std::string& s, size_t idx, const std::string& sep) {
    if (sep.empty() || idx == 0) return std::string();
    size_t pos = 0;
    for (size_t k = 1; k < idx; ++k) {
        size_t p = s.find(sep, pos);
        if (p == std::string::npos) return std::string();
        pos = p + sep.size();
    }
    size_t e = s.find(sep, pos);
    return s.substr(pos, (e == std::string::npos ? s.size() : e) - pos);
}
// PureBasic Val(): decimal integer from string start.
inline long val(const std::string& s) {
    return (long)strtol(s.c_str(), nullptr, 10);
}
inline std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t e = s.find('\n', start);
        if (e == std::string::npos) { out.push_back(s.substr(start)); break; }
        std::string line = s.substr(start, e - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
        start = e + 1;
    }
    if (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

// ---------------------------------------------------------------- files (UTF-8 paths)
inline long long fileSize(const std::string& path) {          // -1 when missing
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(u2w(path).c_str(), GetFileExInfoStandard, &d)) return -1;
    ULARGE_INTEGER sz; sz.HighPart = d.nFileSizeHigh; sz.LowPart = d.nFileSizeLow;
    return (long long)sz.QuadPart;
}
inline bool readFileAll(const std::string& path, std::string& out) {
    FILE* f = _wfopen(u2w(path).c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    if (n > 0) fread(&out[0], 1, (size_t)n, f);
    fclose(f);
    return true;
}
inline bool deleteFile(const std::string& path) { return DeleteFileW(u2w(path).c_str()) != 0; }
inline bool copyFileP(const std::string& src, const std::string& dst) {
    return CopyFileW(u2w(src).c_str(), u2w(dst).c_str(), FALSE) != 0;
}
inline bool movePath(const std::string& src, const std::string& dst) {
    return MoveFileW(u2w(src).c_str(), u2w(dst).c_str()) != 0;
}
inline void mkdirOne(const std::string& path) {               // one level, ignore errors
    CreateDirectoryW(u2w(path).c_str(), nullptr);
}
// PureBasic CreateDirectoryRecursive(): walk the path element by element.
inline void createDirectoryRecursive(const std::string& path) {
    std::string fullPath;
    for (size_t i = 1;; ++i) {
        std::string el = field(path, i, "\\");
        fullPath += el + "\\";
        mkdirOne(fullPath);
        if (el.empty()) break;
    }
}
// PureBasic GetPathPart(): path without the last element (no trailing backslash).
inline std::string getPathPart(const std::string& path) {
    size_t p = path.find_last_of("\\/");
    if (p == std::string::npos) return std::string();
    return path.substr(0, p);
}
// PureBasic fileRead(): whole file as UTF-8 string, "" when missing.
inline std::string fileRead(const std::string& path) {
    std::string data;
    readFileAll(path, data);
    return data;
}

// ---------------------------------------------------------------- prefs (INI)
// The PureBasic original uses OpenPreferences("vortex_launcher.conf") which is a
// plain INI file with the default section "PureBasic-Preferences". We stay
// 100% compatible so existing config files can be reused as-is.
static const wchar_t* INI_SECTION = L"PureBasic-Preferences";
inline std::wstring g_confPath;
inline std::wstring prefRaw(const wchar_t* key, const std::wstring& def) {
    wchar_t buf[8192];
    DWORD n = GetPrivateProfileStringW(INI_SECTION, key, def.c_str(), buf, 8191, g_confPath.c_str());
    return std::wstring(buf, n);
}
inline std::string prefStr(const wchar_t* key, const std::string& defUtf8) {
    return w2u(prefRaw(key, u2w(defUtf8)));
}
inline long prefInt(const wchar_t* key, long def) {
    return (long)GetPrivateProfileIntW(INI_SECTION, key, (UINT)def, g_confPath.c_str());
}
inline void writePref(const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(INI_SECTION, key, value.c_str(), g_confPath.c_str());
}
inline void writePrefI(const wchar_t* key, long value) {
    writePref(key, std::to_wstring(value));
}

// ---------------------------------------------------------------- MD5 (as PureBasic StringFingerprint)
inline std::string md5Hex(const std::string& data) {
    static const char* hexd = "0123456789abcdef";
    HCRYPTPROV prov = 0; HCRYPTHASH hash = 0;
    std::string out;
    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
        return out;
    if (!CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) { CryptReleaseContext(prov, 0); return out; }
    if (!CryptHashData(hash, (const BYTE*)data.data(), (DWORD)data.size(), 0)) {
        CryptDestroyHash(hash); CryptReleaseContext(prov, 0); return out;
    }
    BYTE dg[16]; DWORD dl = 16;
    if (CryptGetHashParam(hash, HP_HASHVAL, dg, &dl, 0) && dl == 16) {
        for (DWORD i = 0; i < 16; ++i) { out += hexd[dg[i] >> 4]; out += hexd[dg[i] & 15]; }
    }
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
    return out;
}

// ---------------------------------------------------------------- run a process (PureBasic RunProgram)
inline bool runProcess(const std::string& program, const std::string& parameters, const std::string& cwd) {
    std::wstring cmd = L"\"" + u2w(program) + L"\"";
    if (!parameters.empty()) { cmd += L" "; cmd += u2w(parameters); }
    std::wstring wcwd = u2w(cwd);
    STARTUPINFOW si; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr,
                             wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    return ok != FALSE;
}
