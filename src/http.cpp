// http.cpp - WinHTTP implementation (see http.hpp).
// Based on Vortex Minecraft Launcher by Kron4ek (PureBasic, GPL-3.0):
// https://github.com/Kron4ek/minecraft-vortex-launcher - see LICENSE at the repo root.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include "http.hpp"
#include "util.hpp"

#pragma comment(lib, "winhttp.lib")

static HINTERNET g_session = nullptr;

bool httpInit() {
    if (g_session) return true;
    g_session = WinHttpOpen(L"VortexLauncher/1.1.20",
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!g_session) return false;
    // connect, send, receive, low-speed timeouts (ms)
    WinHttpSetTimeouts(g_session, 10000, 15000, 30000, 60000);
    return true;
}

struct Resp { HINTERNET conn = nullptr, req = nullptr; };
static void closeResp(Resp& r) {
    if (r.req)  WinHttpCloseHandle(r.req);
    if (r.conn) WinHttpCloseHandle(r.conn);
    r.req = r.conn = nullptr;
}

static Resp openGet(const std::string& url) {
    Resp r;
    std::string wurlsrc = url;
    std::wstring wurl = u2w(wurlsrc);
    URL_COMPONENTS uc; ZeroMemory(&uc, sizeof uc);
    uc.dwStructSize = sizeof uc;
    wchar_t host[512], path[4096], extra[2048];
    uc.lpszHostName = host;   uc.dwHostNameLength = 511;
    uc.lpszUrlPath = path;    uc.dwUrlPathLength = 4095;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return r;

    r.conn = WinHttpConnect(g_session, host, uc.nPort, 0);
    if (!r.conn) return r;

    DWORD secure = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    std::wstring target = std::wstring(path) + extra;
    r.req = WinHttpOpenRequest(r.conn, L"GET", target.c_str(), nullptr,
                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure);
    if (!r.req) { closeResp(r); return r; }

    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(r.req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);
    if (secure) {
        DWORD prot = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        #ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        prot |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        #endif
        WinHttpSetOption(r.req, WINHTTP_OPTION_SECURE_PROTOCOLS, &prot, sizeof prot);
    }

    if (!WinHttpSendRequest(r.req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) { closeResp(r); return r; }
    if (!WinHttpReceiveResponse(r.req, nullptr)) { closeResp(r); return r; }

    DWORD status = 0, got = sizeof status;
    WinHttpQueryHeaders(r.req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &got, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) { closeResp(r); return r; }
    return r;
}

bool httpGetMemory(const std::string& url, std::string& out) {
    out.clear();
    if (!g_session) return false;
    Resp r = openGet(url);
    if (!r.req) return false;
    bool ok = true;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(r.req, &avail)) { ok = false; break; }
        if (avail == 0) break; // end of body
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(r.req, buf.data(), avail, &read)) { ok = false; break; }
        out.append(buf.data(), read);
    }
    closeResp(r);
    if (!ok) out.clear();
    return ok && !out.empty();
}

bool httpDownloadFile(const std::string& url, const std::string& destPath) {
    if (!g_session) return false;
    Resp r = openGet(url);
    if (!r.req) return false;

    std::wstring wdest = u2w(destPath);
    HANDLE h = CreateFileW(wdest.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { closeResp(r); return false; }

    bool ok = true;
    char buf[16384];
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(r.req, &avail)) { ok = false; break; }
        if (avail == 0) break;
        DWORD toRead = avail < sizeof buf ? avail : (DWORD)sizeof buf;
        DWORD read = 0;
        if (!WinHttpReadData(r.req, buf, toRead, &read)) { ok = false; break; }
        if (read) {
            DWORD written = 0;
            if (!WriteFile(h, buf, read, &written, nullptr) || written != read) { ok = false; break; }
        }
    }
    CloseHandle(h);
    closeResp(r);
    if (!ok) DeleteFileW(wdest.c_str()); // like PB's ReceiveHTTPFile: remove partial file
    return ok;
}
