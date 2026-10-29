// =============================================================================
//  app.cpp - C++ port of Vortex Minecraft Launcher 1.1.20 (Windows edition)
//  Original source: vlauncher_windows.pb (PureBasic), (c) Kron4ek,
//  https://github.com/Kron4ek/minecraft-vortex-launcher - GPL-3.0.
//  See the LICENSE file at the repository root for the full license text.
//
//  Ported procedure-by-procedure from the PureBasic source (not the binary),
//  keeping the same layout, strings, defaults and quirks of the original.
//  Win32 API replaces PB gadgets; WinHTTP replaces PB's HTTP.
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include "util.hpp"
#include "json.hpp"
#include "http.hpp"
#include "zip.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// ---------------------------------------------------------------------------
// Control IDs (menu ids passed to CreateWindowExW)
// ---------------------------------------------------------------------------
enum {
    IDC_NAME = 101, IDC_RAM, IDC_VERSIONS, IDC_JAVA,
    IDC_PLAY, IDC_DOWNLOAD, IDC_SETTINGS,
    IDC_AUTHOR, IDC_VERSIONTXT,

    IDC_DW_COMBO = 201, IDC_DW_SHOWALL, IDC_DW_REDOWNLOAD, IDC_DW_BUTTON,

    IDC_SET_ARGS = 301, IDC_SET_JAVAPATH, IDC_SET_THREADS,
    IDC_SET_ASYNC, IDC_SET_DLMISSING, IDC_SET_SAVESTRING, IDC_SET_USECUSTOMJAVA,
    IDC_SET_USECUSTOMPARAMS, IDC_SET_KEEPOPEN, IDC_SET_SAVE,

    IDC_PR_TEXT = 401, IDC_PR_FILESLEFT, IDC_PR_BAR, IDC_PR_OK,
};
#define WM_APP_REFRESHLIST (WM_APP + 1)

// ---------------------------------------------------------------------------
// Globals mirroring the PureBasic "Define/Global" block (lines 1-49)
// ---------------------------------------------------------------------------
static std::string workingDirectory;                      // PB Global.s workingDirectory
static std::string tempDirectory;                         // PB Global.s tempDirectory
static std::string programFilesDir[2];                    // PB Global Dim programFilesDir.s(1)
static int downloadThreadsAmount = 0;                     // PB Global.i downloadThreadsAmount
static int asyncDownload = 0;                             // PB Global.i asyncDownload
static int forceDownloadMissingLibraries = 0;             // PB Global.i forceDownloadMissingLibraries
static std::string versionsManifestString;                // PB Global.s versionsManifestString

// defaults exactly as PB lines 34-49
static const char* playerNameDefault = "PlayerName";
static const char* ramAmountDefault = "2500";
static const char* customLaunchArgumentsDefault = "-Xss1M -XX:+UnlockExperimentalVMOptions -XX:+UseG1GC -XX:G1NewSizePercent=20 -XX:G1ReservePercent=20 -XX:MaxGCPauseMillis=50 -XX:G1HeapRegionSize=32M";
static const char* customOldLaunchArgumentsDefault = "-XX:+UseConcMarkSweepGC -XX:+CMSIncrementalMode -XX:-UseAdaptiveSizePolicy -Xmn128M";
static const long downloadThreadsAmountDefault = 20;
static const long asyncDownloadDefault = 1;
static const long downloadMissingLibrariesDefault = 1;
static const long downloadAllFilesDefault = 0;
static const long versionsTypeDefault = 0;
static const long saveLaunchStringDefault = 0;
static const long useCustomParamsDefault = 0;
static const long keepLauncherOpenDefault = 0;
static const long useCustomJavaDefault = 0;
static const char* javaBinaryPathDefault = "C:\\jre8\\bin\\javaw.exe";
static const char* launcherVersion = "1.1.20";
static const char* launcherDeveloper = "Kron4ek";

// Persistent per-process state (PB module-level Define: values survive event-loop iterations)
static long releaseTime = 0;

// Window/gadget handles (NULL when the window is closed - replaces PB IsGadget())
static HWND hMain = nullptr, g_nameEdit, g_ramEdit, g_versionsCombo, g_javaCombo;
static HWND g_playButton, g_downloadButton, g_settingsButton;
static HWND hDownloader = nullptr, g_dlCombo, g_dlShowAll, g_dlRedownload, g_dlButton;
static HWND hSettings = nullptr, g_setArgs, g_setJavaPath, g_setThreads;
static HWND g_setAsync, g_setDlMissing, g_setSaveString, g_setUseCustomJava,
            g_setUseCustomParams, g_setKeepOpen;
static HWND hProgress = nullptr, g_prText, g_prFilesLeft, g_prBar, g_prOk;
static HFONT fontBold = nullptr, fontSmall = nullptr;

// Download thread state (replaces PB downloadThread / IsThread(downloadThread))
static std::atomic<bool> downloadThreadActive{ false };
static std::atomic<long> g_lines{ 0 };          // PB "lines" shared for UI text
static std::atomic<long> g_linesTotal{ 0 };
static std::atomic<long> g_failedDownloads{ 0 };
static std::atomic<bool> g_quitMain{ false };   // PB Break out of Repeat/Until

// ---------------------------------------------------------------------------
// Tiny UI helpers (GetGadgetText / SetGadgetText / gadget item helpers)
// ---------------------------------------------------------------------------
static std::string gadgetText(HWND g) {
    int n = (int)SendMessageW(g, WM_GETTEXTLENGTH, 0, 0);
    std::wstring w((size_t)n + 1, 0);
    SendMessageW(g, WM_GETTEXT, n + 1, (LPARAM)&w[0]);
    w.resize((size_t)n);
    return w2u(w);
}
static void setGadgetText(HWND g, const std::string& s) { SendMessageW(g, WM_SETTEXT, 0, (LPARAM)u2w(s).c_str()); }
static void setGadgetStateL(HWND g, LONG st) { SendMessageW(g, CB_SETCURSEL, st, 0); }
static LONG gadgetStateL(HWND g) { return (LONG)SendMessageW(g, CB_GETCURSEL, 0, 0); }
static LONG checkState(HWND g) { return (LONG)SendMessageW(g, BM_GETCHECK, 0, 0); }
static void setCheck(HWND g, LONG v) { SendMessageW(g, BM_SETCHECK, v, 0); }
static LONG comboCount(HWND g) { return (LONG)SendMessageW(g, CB_GETCOUNT, 0, 0); }
static void comboClear(HWND g) { SendMessageW(g, CB_RESETCONTENT, 0, 0); }
static void comboAdd(HWND g, const std::string& s) { SendMessageW(g, CB_ADDSTRING, 0, (LPARAM)u2w(s).c_str()); }
static std::string comboSelText(HWND g) {
    LONG sel = gadgetStateL(g);
    if (sel < 0) return std::string();
    int n = (int)SendMessageW(g, CB_GETLBTEXTLEN, sel, 0);
    std::wstring w((size_t)n + 1, 0);
    SendMessageW(g, CB_GETLBTEXT, sel, (LPARAM)&w[0]);
    w.resize((size_t)n);
    return w2u(w);
}
static void comboSelectText(HWND g, const std::string& s) {   // PB SetGadgetText on ComboBox
    LRESULT r = SendMessageW(g, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)u2w(s).c_str());
    if (r != CB_ERR) setGadgetStateL(g, (LONG)r);
}
static void disableGadget(HWND g, bool d) { EnableWindow(g, !d); }
static void hideGadget(HWND g, bool h) { ShowWindow(g, h ? SW_HIDE : SW_SHOW); }
static void messageRequester(const char* title, const char* text) {
    MessageBoxA(hMain, text, title, MB_OK);
}

static HWND makeStatic(HWND parent, int id, int x, int y, int w, int h, const char* text) {
    return CreateWindowExW(0, L"STATIC", u2w(text).c_str(), WS_CHILD | WS_VISIBLE,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}
static HWND makeButton(HWND parent, int id, int x, int y, int w, int h, const char* text) {
    return CreateWindowExW(0, L"BUTTON", u2w(text).c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}
static HWND makeCheck(HWND parent, int id, int x, int y, int w, int h, const char* text) {
    return CreateWindowExW(0, L"BUTTON", u2w(text).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}
static HWND makeEdit(HWND parent, int id, int x, int y, int w, int h, const char* text, bool numeric) {
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", u2w(text).c_str(),
                             WS_CHILD | WS_VISIBLE | (numeric ? ES_NUMBER : 0),
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    return e;
}
static HWND makeCombo(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", nullptr,
                           WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                           x, y, w, h * 12, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}
static HWND g_tip = nullptr;
static void addTip(HWND tool, const char* text) {
    if (!g_tip) {
        g_tip = CreateWindowExW(0, TOOLTIPS_CLASS, nullptr, WS_POPUP | TTS_ALWAYSTIP,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                tool, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    TOOLINFOW ti; ZeroMemory(&ti, sizeof ti);
    ti.cbSize = sizeof ti; ti.uFlags = TTF_SUBCLASS; ti.hwnd = tool;
    ti.lpszText = (LPWSTR)u2w(text).c_str();
    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

static HWND createTopWindow(LPCWSTR cls, LPCWSTR title, int w, int h) {
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT r = { 0, 0, w, h };
    AdjustWindowRect(&r, style, FALSE);
    int sx = (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2;
    return CreateWindowExW(0, cls, title, style, sx, sy, r.right - r.left, r.bottom - r.top,
                           nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// ---------------------------------------------------------------------------
// PB Procedure CreateDirectoryRecursive (line 979)
// ---------------------------------------------------------------------------
// implemented in util.hpp as createDirectoryRecursive()

// ---------------------------------------------------------------------------
// PB Procedure removeSpacesFromVersionName (line 1116)
// ---------------------------------------------------------------------------
static std::string removeSpacesFromVersionName(const std::string& clientVersion) {
    std::string newVersionName = clientVersion;
    replaceAll(newVersionName, " ", "-");
    movePath("versions\\" + clientVersion + "\\" + clientVersion + ".jar",
             "versions\\" + clientVersion + "\\" + newVersionName + ".jar");
    movePath("versions\\" + clientVersion + "\\" + clientVersion + ".json",
             "versions\\" + clientVersion + "\\" + newVersionName + ".json");
    movePath("versions\\" + clientVersion, "versions\\" + newVersionName);
    return newVersionName;
}

// ---------------------------------------------------------------------------
// PB Procedure generateProfileJson (line 993)
// ---------------------------------------------------------------------------
static void generateProfileJson() {
    std::string fileName = "launcher_profiles.json";
    long lastProfilesJsonSize = prefInt(L"LastProfilesJsonSize", 89);
    long long fsize = fileSize(fileName);

    if (fsize <= 0) {
        deleteFile(fileName);
        FILE* f = _wfopen(u2w(fileName).c_str(), L"wb");
        if (f) {
            fprintf(f, "{ \"profiles\": { \"justProfile\": { \"name\": \"justProfile\", ");
            fprintf(f, "\"lastVersionId\": \"1.12.2\" } } }\r\n");
            fclose(f);
        }
    }

    fsize = fileSize(fileName);
    if (fsize != lastProfilesJsonSize) forceDownloadMissingLibraries = 1;
    writePrefI(L"LastProfilesJsonSize", (long)fsize);
}

// ---------------------------------------------------------------------------
// PB Procedure findInstalledVersions (line 608)
// ---------------------------------------------------------------------------
static void findInstalledVersions() {
    std::string chosenVer = prefStr(L"ChosenVer", "");
    int chosenFound = 0;

    disableGadget(g_playButton, false);
    disableGadget(g_versionsCombo, false);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(u2w("versions\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::string dirName = w2u(fd.cFileName);
                if (dirName != ".." && dirName != ".") {
                    if (fileSize("versions\\" + dirName + "\\" + dirName + ".json") > -1) {
                        if (!chosenFound && dirName == chosenVer) chosenFound = 1;
                        comboAdd(g_versionsCombo, dirName);
                    }
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);

        if (chosenFound) comboSelectText(g_versionsCombo, chosenVer);
        else setGadgetStateL(g_versionsCombo, 0);
    }

    if (comboCount(g_versionsCombo) == 0) {
        disableGadget(g_playButton, true);
        disableGadget(g_versionsCombo, true);
        comboAdd(g_versionsCombo, "Versions not found");
        setGadgetStateL(g_versionsCombo, 0);
    } else {
        generateProfileJson();
    }
}

// ---------------------------------------------------------------------------
// PB Procedure findJava (line 1037)
// ---------------------------------------------------------------------------
static void findJava() {
    comboClear(g_javaCombo);
    disableGadget(g_javaCombo, false);

    if (gadgetText(g_versionsCombo) != "Versions not found" && !downloadThreadActive)
        disableGadget(g_playButton, false);

    if (prefInt(L"UseCustomJava", useCustomJavaDefault)) {
        comboAdd(g_javaCombo, "Custom Java enabled in Settings");
        setGadgetStateL(g_javaCombo, 0);
        disableGadget(g_javaCombo, true);
    } else {
        for (int i = 0; i <= 1; i++) {
            if (programFilesDir[i] != "\\") {
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW(u2w(programFilesDir[i] + "Java\\*").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    // PB quirk kept: the scan stops at the FIRST non-directory entry.
                    bool more = true;
                    while (more && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        std::string dirName = w2u(fd.cFileName);
                        if (dirName != ".." && dirName != "." &&
                            fileSize(programFilesDir[i] + "Java\\" + dirName + "\\bin\\javaw.exe") > 0) {
                            if (i) dirName += " (x32)";
                            comboAdd(g_javaCombo, dirName);
                            setGadgetStateL(g_javaCombo, 0);
                        }
                        more = FindNextFileW(h, &fd) != FALSE;
                    }
                    FindClose(h);
                }
            }
        }

        if (comboCount(g_javaCombo) == 0) {
            comboAdd(g_javaCombo, "Java not found");
            setGadgetStateL(g_javaCombo, 0);
            disableGadget(g_javaCombo, true);
            disableGadget(g_playButton, true);
        }
    }
}

// ---------------------------------------------------------------------------
// PB Procedure assetsToResources (line 1086)
// ---------------------------------------------------------------------------
static void assetsToResources(const std::string& assetsIndex) {
    J jsonFile;
    if (!jsonp::parse(fileRead("assets\\indexes\\" + assetsIndex + ".json"), jsonFile)) return;
    const J* objs = jsonFile.m("objects");
    if (!objs) return;
    for (const auto& kv : objs->o) {          // PB ExamineJSONMembers iteration order
        std::string fileHash = kv.second.ms("hash");
        long long fsz = kv.second.mi("size");
        std::string fileName = kv.first;
        replaceAll(fileName, "/", "\\");
        if (fileSize("resources\\" + fileName) != fsz) {
            createDirectoryRecursive("resources\\" + getPathPart(fileName));
            copyFileP("assets\\objects\\" + fileHash.substr(0, 2) + "\\" + fileHash,
                      "resources\\" + fileName);
        }
    }
}

// ---------------------------------------------------------------------------
// PB Procedure parseVersionsManifest (line 651)
// ---------------------------------------------------------------------------
static std::string parseVersionsManifest(int versionType = 0, int getClientJarUrl = 0,
                                         const std::string& clientVersion = "") {
    std::string url;
    J jsonFile;
    if (jsonp::parse(versionsManifestString, jsonFile)) {
        const J* versions = jsonFile.m("versions");
        if (versions) {
            for (const auto& el : versions->a) {
                if (!getClientJarUrl && !versionType && el.ms("type") != "release") continue;
                if (!getClientJarUrl) {
                    comboAdd(g_dlCombo, el.ms("id"));
                } else {
                    if (el.ms("id") == clientVersion) {
                        return el.ms("url");
                    }
                }
            }
        }
    } else {
        comboAdd(g_dlCombo, "Error");
        disableGadget(g_dlButton, true);
    }
    setGadgetStateL(g_dlCombo, 0);
    return url;
}

// ---------------------------------------------------------------------------
// PB Procedure parseLibraries (line 690)
// ---------------------------------------------------------------------------
static std::string parseLibraries(const std::string& clientVersion, int prepareForDownload = 0,
                                  const std::string& librariesString = "") {
    std::string libsString;
    FILE* downloadListFile = nullptr;

    if (prepareForDownload == 1) {
        downloadListFile = _wfopen(u2w(tempDirectory + "vlauncher_download_list.txt").c_str(), L"ab");
    }

    J jsonFile;
    if (jsonp::parse(fileRead("versions\\" + clientVersion + "\\" + clientVersion + ".json"), jsonFile)) {
        const J* libraries = jsonFile.m("libraries");
        if (libraries) {
            for (const auto& element : libraries->a) {
                int allowLib = 1, skipLib = 0;
                std::string jsonRulesOsName = "empty";

                const J* rules = element.m("rules");
                if (rules) {
                    for (const auto& rule : rules->a) {
                        const J* osMember = rule.m("os");
                        if (osMember) jsonRulesOsName = osMember->ms("name");
                        if (rule.ms("action") == "allow") {
                            if (jsonRulesOsName != "empty" && jsonRulesOsName != "windows") allowLib = 0;
                        } else {
                            if (jsonRulesOsName == "windows") allowLib = 0;
                        }
                    }
                }

                if (allowLib) {
                    std::string libName = element.ms("name");
                    std::string libSplit[5];
                    for (int k = 1; k <= 4; k++) libSplit[k] = field(libName, (size_t)k, ":");

                    std::string libNameBase = libSplit[1];
                    replaceAll(libNameBase, ".", "\\");
                    libNameBase += "\\" + libSplit[2];
                    libName = libNameBase + "\\" + libSplit[3] + "\\" + libSplit[2] + "-" + libSplit[3];

                    // PB quirk kept: dedupe only against the parent's accumulated string.
                    if (!librariesString.empty() &&
                        librariesString.find(libNameBase + "\\") != std::string::npos)
                        skipLib = 1;

                    if (!libSplit[4].empty()) libName += "-" + libSplit[4];

                    if (prepareForDownload == 1) {
                        long long fileSizeVal = 0;
                        std::string url;
                        const J* downloads = element.m("downloads");
                        if (downloads) {
                            const J* artifacts = downloads->m("artifact");
                            const J* classifiers = downloads->m("classifiers");
                            if (classifiers) {
                                const J* nativesWin = classifiers->m("natives-windows");
                                if (nativesWin) {
                                    url = nativesWin->ms("url");
                                    fileSizeVal = nativesWin->mi("size");
                                    libName += "-natives-windows";
                                }
                            } else if (artifacts) {
                                url = artifacts->ms("url");
                                fileSizeVal = artifacts->mi("size");
                            }
                        } else {
                            const J* urlMember = element.m("url");
                            std::string rel = libName;
                            replaceAll(rel, "\\", "/");
                            if (urlMember) url = urlMember->str() + rel + ".jar";
                            else url = "https://libraries.minecraft.net/" + rel + ".jar";
                        }
                        if (downloadListFile) {
                            fprintf(downloadListFile, "%s::%s::%lld\r\n", url.c_str(),
                                  ("libraries\\" + libName + ".jar").c_str(), fileSizeVal);
                        }
                    }

                    if (!element.m("natives")) {
                        if (!skipLib)
                            libsString += workingDirectory + "\\libraries\\" + libName + ".jar;";
                    } else {
                        // PB quirk kept: "-natives-windows" appended to the jar name when the
                        // stored library name does not already end with "natives-windows".
                        std::string pack = (libName.size() < 15 ||
                                            libName.compare(libName.size() - 15, 15, "natives-windows") != 0)
                            ? ("libraries\\" + libName + "-natives-windows.jar")
                            : ("libraries\\" + libName + ".jar");
                        std::vector<unsigned char> buf;
                        size_t cdOff = 0, cdCount = 0;
                        if (zipOpenArchive(pack, buf, cdOff, cdCount)) {
                            createDirectoryRecursive("versions\\" + clientVersion + "\\natives");
                            zipExtractMissing(buf, cdOff, cdCount,
                                              "versions\\" + clientVersion + "\\natives");
                        }
                    }
                }
            }
        }
    }

    if (prepareForDownload == 1 && downloadListFile) fclose(downloadListFile);
    return libsString;
}

// ---------------------------------------------------------------------------
// PB Procedure downloadFiles (line 836)
//
// PB model: main loop reads lines; up to downloadThreadsAmount+1 parallel
// async HTTP slots with per-slot retries (max 5), abort at failed>5; sync mode
// downloads one by one with a single shared retry counter (max 5).
// Here: std::thread worker pool instead of async HTTP slots.
// ---------------------------------------------------------------------------
struct DlTask { std::string url, path; long long size; int retries; };

static std::mutex g_queueMtx;
static std::deque<DlTask> g_queue;
static std::atomic<long> g_currentDownloads{ 0 };

static void refreshProgressUI() {
    if (g_prBar) {
        SendMessageW(g_prBar, PBM_SETPOS, (LPARAM)(g_linesTotal - g_lines), 0);
        std::string t = "Files remaining: " + std::to_string((long)g_lines);
        SendMessageW(g_prFilesLeft, WM_SETTEXT, 0, (LPARAM)u2w(t).c_str());
    }
}

static void finalizeDownload(bool failed, long linesLeft) {
    if (g_prFilesLeft) {
        std::string t = failed ? ("Download failed! " + std::to_string(linesLeft) + " files left.")
                               : std::string("Download complete!");
        SendMessageW(g_prFilesLeft, WM_SETTEXT, 0, (LPARAM)u2w(t).c_str());
    }
    if (g_prBar) hideGadget(g_prBar, true);
    if (g_prOk) hideGadget(g_prOk, false);

    // PB: ClearGadgetItems(versionsGadget) : findInstalledVersions() - marshalled to UI thread.
    if (hMain) PostMessageW(hMain, WM_APP_REFRESHLIST, 0, 0);

    deleteFile(tempDirectory + "vlauncher_download_list.txt");
    downloadThreadActive = false;
}

static void asyncWorker() {
    for (;;) {
        DlTask task;
        {
            std::lock_guard<std::mutex> lk(g_queueMtx);
            if (g_queue.empty() || g_failedDownloads > 5) return;
            task = g_queue.front();
            g_queue.pop_front();
        }
        // PB: a slot keeps retrying its own file (up to allowedRetries=5), then counts as failed.
        for (;;) {
            // PB: once failedDownloads>5 the main While is gone - in-flight files are abandoned
            // without any counter updates.
            if (g_failedDownloads > 5) break;
            createDirectoryRecursive(getPathPart(task.path));
            if (httpDownloadFile(task.url, task.path)) {
                g_lines -= 1;   // PB: lines-1 on HTTPProgress Success
                break;
            }
            if (task.retries < 5) { task.retries++; continue; }
            g_failedDownloads += 1;   // PB: allowedRetries exceeded
            break;
        }
        g_currentDownloads -= 1;
    }
}

static void downloadFilesCore(long downloadAllFiles, bool fromThread) {
    long failedDownloads = 0;
    std::string data;
    if (!readFileAll(tempDirectory + "vlauncher_download_list.txt", data)) {
        // PB deletes the temp list unconditionally at procedure end (outside "If file").
        deleteFile(tempDirectory + "vlauncher_download_list.txt");
        downloadThreadActive = false;
        return;
    }
    std::vector<std::string> lines = splitLines(data);
    long linesTotal = (long)lines.size();

    g_linesTotal = linesTotal;
    g_lines = linesTotal;
    g_failedDownloads = 0;

    if (g_dlButton) disableGadget(g_dlButton, true);
    if (g_prBar) SendMessageW(g_prBar, PBM_SETRANGE32, 0, (LPARAM)linesTotal);

    if (asyncDownload) {
        // PB: skip lines whose file already has the required size (lines - 1 there).
        for (auto& ln : lines) {
            if (ln.empty()) continue;
            std::string path = field(ln, 2, "::");
            long long fsz = fileSize(path);
            long long required = val(field(ln, 3, "::"));
            bool need = (downloadAllFiles == 0 && (fsz == -1 || (required != 0 && fsz != required)))
                        || downloadAllFiles == 1;
            if (!need) { g_lines -= 1; continue; }
            DlTask t{ field(ln, 1, "::"), path, required, 0 };
            std::lock_guard<std::mutex> lk(g_queueMtx);
            g_queue.push_back(t);
            g_currentDownloads += 1;   // PB currentDownloads +1 when the slot starts
        }
        // PB array is downloadThreadsAmount+1 slots.
        long nthreads = downloadThreadsAmount + 1;
        if (nthreads < 1) nthreads = 1;
        std::vector<std::thread> pool;
        for (long i = 0; i < nthreads; i++) pool.emplace_back(asyncWorker);

        // PB main loop: While (Eof=0 Or currentDownloads>0) And failedDownloads<=5 ... Delay(500)
        while ((g_currentDownloads > 0 ||
                (!g_queue.empty() && g_failedDownloads <= 5)) && g_failedDownloads <= 5) {
            refreshProgressUI();
            Sleep(500);
        }
        for (auto& t : pool) if (t.joinable()) t.join();
        // Tasks still queued were never dispatched - PB never read those lines, so they are
        // still counted in "lines" (shown as "N files left") - g_lines must stay untouched.
        {
            std::lock_guard<std::mutex> lk(g_queueMtx);
            g_queue.clear();
        }
        if (!fromThread) refreshProgressUI();
        failedDownloads = (long)g_failedDownloads;
    } else {
        // PB sync mode: single shared retry counter, Break on the 6th failure.
        long retries = 0;
        for (long n = 0; n < (long)lines.size(); n++) {   // long: n-- must be able to go to -1
            const std::string& ln = lines[n];
            if (ln.empty()) continue;
            g_lines -= 1;
            std::string path = field(ln, 2, "::");
            long long fsz = fileSize(path);
            long long required = val(field(ln, 3, "::"));
            if ((downloadAllFiles == 0 && (fsz == -1 || (required != 0 && fsz != required)))
                || downloadAllFiles == 1) {
                createDirectoryRecursive(getPathPart(path));
                if (httpDownloadFile(field(ln, 1, "::"), path)) {
                    retries = 0;
                } else {
                    if (retries < 5) { retries++; n--; }   // PB FileSeek(file,-1,Relative)
                    else { failedDownloads = 1; break; }
                }
            }
            refreshProgressUI();
        }
    }

    finalizeDownload(failedDownloads != 0, (long)g_lines);
}

// ---------------------------------------------------------------------------
// PB: progressWindow(clientVersion) (line 966)
// ---------------------------------------------------------------------------
static LRESULT CALLBACK ProgressProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK DownloaderProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK SettingsProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK MainProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

static void openProgressWindow(const std::string& clientVersion) {
    hProgress = createTopWindow(L"VLProgress", L"Download progress", 230, 85);
    if (!hProgress) return;
    if (g_prText) setGadgetText(g_prText, "Version: " + clientVersion);   // PB line 970
    ShowWindow(hProgress, SW_SHOW);
    UpdateWindow(hProgress);
}

// ---------------------------------------------------------------------------
// Play button handler (PB Case playButton, lines 119-385)
// ---------------------------------------------------------------------------
static void onPlay() {
    std::string ramAmount = gadgetText(g_ramEdit);
    std::string clientVersion = gadgetText(g_versionsCombo);
    std::string playerName = gadgetText(g_nameEdit);
    std::string javaBinaryPath = comboSelText(g_javaCombo);
    long downloadMissingLibraries = prefInt(L"DownloadMissingLibs", downloadMissingLibrariesDefault);
    std::string librariesString, clientArguments, jvmArguments, logConfArgument;

    if (clientVersion.find(' ') != std::string::npos)
        clientVersion = removeSpacesFromVersionName(clientVersion);

    if (forceDownloadMissingLibraries) downloadMissingLibraries = 1;

    if (playerName.find(' ') != std::string::npos) replaceAll(playerName, " ", "");

    if (!ramAmount.empty() && playerName.size() >= 3) {
        if (prefInt(L"UseCustomJava", useCustomJavaDefault))
            javaBinaryPath = prefStr(L"JavaPath", javaBinaryPathDefault);
        else if (javaBinaryPath.size() >= 5 && javaBinaryPath.compare(javaBinaryPath.size() - 5, 5, "(x32)") == 0)
            javaBinaryPath = programFilesDir[1] + "Java\\" + javaBinaryPath.substr(0, javaBinaryPath.size() - 6) + "\\bin\\javaw.exe";
            // PB: RemoveString(javaBinaryPath, " (x32)")
        else
            javaBinaryPath = programFilesDir[0] + "Java\\" + javaBinaryPath + "\\bin\\javaw.exe";

        if (val(ramAmount) < 350) {
            ramAmount = "350";
            messageRequester("Warning",
                "You allocated too low amount of memory!\r\n\r\nAllocated memory set to 350 MB to prevent crashes.");
        }

        writePref(L"Name", u2w(playerName));
        writePref(L"Ram", u2w(ramAmount));
        writePref(L"ChosenVer", u2w(clientVersion));

        if (runProcess(javaBinaryPath, "-version", workingDirectory)) {
            J jsonFile;
            if (jsonp::parse(fileRead("versions\\" + clientVersion + "\\" + clientVersion + ".json"), jsonFile)) {
                std::string clientJarFile = "versions\\" + clientVersion + "\\" + clientVersion + ".jar";
                std::string nativesPath = "versions\\" + clientVersion + "\\natives";
                std::string inheritsClientJar, assetsIndex;

                const J* jsonJarMember = jsonFile.m("jar");
                const J* jsonInheritsFromMember = jsonFile.m("inheritsFrom");

                if (jsonInheritsFromMember || jsonJarMember) {
                    inheritsClientJar = jsonInheritsFromMember ? jsonInheritsFromMember->str()
                                                              : jsonJarMember->str();
                    if (fileSize(clientJarFile) < 1 &&
                        fileSize("versions\\" + inheritsClientJar + "\\" + inheritsClientJar + ".jar") > 0)
                        copyFileP("versions\\" + inheritsClientJar + "\\" + inheritsClientJar + ".jar",
                                  clientJarFile);
                    nativesPath = "versions\\" + inheritsClientJar + "\\natives";
                }

                const J* releaseTimeMember = jsonFile.m("releaseTime");
                if (releaseTimeMember) {
                    std::string rt = releaseTimeMember->str();
                    releaseTime = val(field(rt, 1, "-")) * 365 + val(field(rt, 2, "-")) * 30;
                }

                const J* jsonArgumentsMember = jsonFile.m("minecraftArguments");
                const J* jsonArgumentsModernMember = jsonFile.m("arguments");

                if (jsonArgumentsMember) {
                    clientArguments = jsonArgumentsMember->str();
                } else if (jsonArgumentsModernMember) {
                    const J* gameArr = jsonArgumentsModernMember->m("game");
                    const J* jvmArr = jsonArgumentsModernMember->m("jvm");
                    if (gameArr)
                        for (const auto& el : gameArr->a)
                            if (el.t == J::STR) clientArguments += " " + el.s + " ";
                    if (jvmArr)
                        for (const auto& el : jvmArr->a)
                            if (el.t == J::STR) jvmArguments += " \"" + el.s + "\" ";
                }

                if (fileSize(clientJarFile) > 0) {
                    librariesString += parseLibraries(clientVersion, (int)downloadMissingLibraries);

                    J inheritsJson;
                    bool hasInheritsJson = false;
                    std::string logConfArgumentOwner;

                    if (jsonInheritsFromMember) {
                        inheritsClientJar = jsonInheritsFromMember->str();
                        hasInheritsJson = jsonp::parse(
                            fileRead("versions\\" + inheritsClientJar + "\\" + inheritsClientJar + ".json"),
                            inheritsJson);
                        if (hasInheritsJson) {
                            const J* inhArgs = inheritsJson.m("arguments");
                            if (inhArgs) {
                                const J* gameArr = inhArgs->m("game");
                                const J* jvmArr = inhArgs->m("jvm");
                                if (gameArr)
                                    for (const auto& el : gameArr->a)
                                        if (el.t == J::STR) clientArguments += " " + el.s + " ";
                                if (jvmArr)
                                    for (const auto& el : jvmArr->a)
                                        if (el.t == J::STR) jvmArguments += " \"" + el.s + "\" ";
                            }
                            librariesString += parseLibraries(inheritsClientJar, (int)downloadMissingLibraries,
                                                             librariesString);
                            const J* a = inheritsJson.m("assets");
                            assetsIndex = a ? a->str() : std::string();

                            releaseTimeMember = inheritsJson.m("releaseTime");
                            if (releaseTimeMember) {
                                std::string rt = releaseTimeMember->str();
                                releaseTime = val(field(rt, 1, "-")) * 365 + val(field(rt, 2, "-")) * 30;
                            }
                        } else {
                            messageRequester("Error", (inheritsClientJar + ".json file is missing!").c_str());
                            // PB line 265 "Break" exits the Repeat/Until event loop (confirmed in the
                            // decompiled event handler: jump exits the loop) -> launcher closes after launch.
                            g_quitMain = true;
                            return;
                        }
                    } else {
                        if (jsonFile.m("assets")) assetsIndex = jsonFile.ms("assets");
                        else if (releaseTime > 0 && releaseTime < 734925) assetsIndex = "pre-1.6";
                        else assetsIndex = "legacy";
                    }

                    const J* loggingMember = (jsonInheritsFromMember && hasInheritsJson)
                        ? inheritsJson.m("logging") : jsonFile.m("logging");
                    if (loggingMember) {
                        const J* loggingClient = loggingMember->m("client");
                        if (loggingClient) {
                            const J* fileMember = loggingClient->m("file");
                            if (fileMember)
                                logConfArgument = "-Dlog4j.configurationFile=assets\\log_configs\\" +
                                                  fileMember->ms("id");
                        }
                    }

                    std::string clientMainClass = jsonFile.ms("mainClass");

                    // PB lines 301-304: StringFingerprint returns UPPERCASE hex MD5, then
                    // Left(12) + LCase(Hex(Val("$"+Mid(uuid,13,2)) & $0f | $30)) + Mid(uuid,15,2)
                    //          + LCase(Hex(Val("$"+Mid(uuid,17,2)) & $3f | $80)) + Right(uuid,14)
                    std::string uuid = md5Hex("OfflinePlayer:" + playerName);
                    for (auto& c : uuid) c = (char)toupper((unsigned char)c);  // PB fingerprint style
                    if (uuid.size() == 32) {
                        long b13 = strtol(uuid.substr(12, 2).c_str(), nullptr, 16);  // Val("$"+Mid(...,13,2))
                        long b17 = strtol(uuid.substr(16, 2).c_str(), nullptr, 16);  // Val("$"+Mid(...,17,2))
                        char p1[9], p2[9];
                        sprintf(p1, "%lx", (b13 & 0x0f) | 0x30);   // Hex() + LCase(), always 2 digits here
                        sprintf(p2, "%lx", (b17 & 0x3f) | 0x80);
                        uuid = uuid.substr(0, 12) + p1 + uuid.substr(14, 2) + p2 + uuid.substr(18);
                    }

                    if (assetsIndex == "pre-1.6" || assetsIndex == "legacy")
                        assetsToResources(assetsIndex);

                    if (downloadMissingLibraries) downloadFilesCore(0, false);

                    if (jvmArguments.empty())
                        jvmArguments = "\"-Djava.library.path=" + nativesPath + "\" -cp \"" +
                                       librariesString + clientJarFile + "\"";

                    std::string customLaunchArguments;
                    if (releaseTime > 0 && releaseTime < 736780) customLaunchArguments = customOldLaunchArgumentsDefault;
                    else customLaunchArguments = customLaunchArgumentsDefault;

                    if (prefInt(L"UseCustomParameters", useCustomParamsDefault))
                        customLaunchArguments = prefStr(L"LaunchArguments", customLaunchArgumentsDefault);

                    std::string fullLaunchString = "-Xmx" + ramAmount + "M " + customLaunchArguments +
                        " -Dlog4j2.formatMsgNoLookups=true " + logConfArgument + " " + jvmArguments +
                        " " + clientMainClass + " " + clientArguments;

                    struct Rep { const char* from, *to; };
                    Rep reps[] = {
                        { "${auth_player_name}", playerName.c_str() },
                        { "${version_name}", clientVersion.c_str() },
                        { "${game_directory}", ("\"" + workingDirectory + "\"").c_str() },
                        { "${assets_root}", "assets" },
                        { "${auth_uuid}", uuid.c_str() },
                        { "${auth_access_token}", "00000000000000000000000000000000" },
                        { "${clientid}", "0000" },
                        { "${auth_xuid}", "0000" },
                        { "${user_properties}", "{}" },
                        { "${user_type}", "mojang" },
                        { "${version_type}", "release" },
                        { "${assets_index_name}", assetsIndex.c_str() },
                        { "${auth_session}", "00000000000000000000000000000000" },
                        { "${game_assets}", "resources" },
                        { "${classpath}", (librariesString + clientJarFile).c_str() },
                        { "${library_directory}", "libraries" },
                        { "${classpath_separator}", ";" },
                        { "${natives_directory}", nativesPath.c_str() },
                        { "\"-Dminecraft.launcher.brand=${launcher_name}\"", "" },
                        { "\"-Dminecraft.launcher.version=${launcher_version}\"", "" },
                    };
                    for (auto& r : reps) replaceAll(fullLaunchString, r.from, r.to);

                    runProcess(javaBinaryPath, fullLaunchString, workingDirectory);

                    if (prefInt(L"SaveLaunchString", saveLaunchStringDefault)) {
                        deleteFile("launch_string.txt");
                        FILE* f = _wfopen(u2w("launch_string.txt").c_str(), L"wb");
                        if (f) {
                            replaceAll(fullLaunchString, "  ", " ");
                            fprintf(f, "\"%s\" %s", javaBinaryPath.c_str(), fullLaunchString.c_str());
                            fclose(f);
                        }
                    }

                    if (!prefInt(L"KeepLauncherOpen", keepLauncherOpenDefault)) {
                        g_quitMain = true;      // PB "Break" leaves the Repeat/Until loop
                    }
                } else {
                    messageRequester("Error", "Client jar file is missing!");
                }
            } else {
                messageRequester("Error", "Client json file is missing!");
            }
        } else {
            messageRequester("Error",
                "Java not found! Check if Java installed.\r\n\r\nOr check if path to Java binary is correct.");
        }
    } else {
        if (playerName.empty()) messageRequester("Error", "Enter your desired name.");
        else if (ramAmount.empty()) messageRequester("Error", "Enter RAM amount.");
        else if (playerName.size() < 3) messageRequester("Error", "Name is too short! Minimum length is 3.");
    }
}

// ---------------------------------------------------------------------------
// Downloader window (PB Case downloadButton + related, lines 386-414, 491)
// ---------------------------------------------------------------------------
static void downloaderRefreshVersions() {
    comboClear(g_dlCombo);
    parseVersionsManifest((int)checkState(g_dlShowAll));
}

static void onDownloadPressed() {
    std::string versionToDownload = comboSelText(g_dlCombo);
    if (versionToDownload.empty() || versionToDownload == "Error") return;

    createDirectoryRecursive("versions\\" + versionToDownload);

    std::string url = parseVersionsManifest((int)checkState(g_dlShowAll), 1, versionToDownload);
    if (httpDownloadFile(url, "versions\\" + versionToDownload + "\\" + versionToDownload + ".json")) {
        deleteFile(tempDirectory + "vlauncher_download_list.txt");
        FILE* listOfFiles = _wfopen(u2w(tempDirectory + "vlauncher_download_list.txt").c_str(), L"wb");

        std::string assetsIndex;
        J jsonFile;
        if (jsonp::parse(fileRead("versions\\" + versionToDownload + "\\" + versionToDownload + ".json"), jsonFile)) {
            assetsIndex = jsonFile.ms("assets");

            createDirectoryRecursive("assets\\indexes");
            const J* assetIndexMember = jsonFile.m("assetIndex");
            httpDownloadFile(assetIndexMember ? assetIndexMember->ms("url") : "",
                             "assets\\indexes\\" + assetsIndex + ".json");

            const J* loggingMember = jsonFile.m("logging");
            if (loggingMember) {
                const J* loggingClient = loggingMember->m("client");
                if (loggingClient) {
                    const J* fileMember = loggingClient->m("file");
                    if (fileMember) {
                        std::string logConfId = fileMember->ms("id");
                        std::string logConfUrl = fileMember->ms("url");
                        long long logConfSize = fileMember->mi("size");
                        fprintf(listOfFiles, "%s::%s::%lld\r\n", logConfUrl.c_str(),
                                ("assets\\log_configs\\" + logConfId).c_str(), logConfSize);
                        createDirectoryRecursive("assets\\log_configs");
                    }
                }
            }

            const J* downloads = jsonFile.m("downloads");
            const J* client = downloads ? downloads->m("client") : nullptr;
            std::string clientUrl = client ? client->ms("url") : "";
            long long clientSize = client ? client->mi("size") : 0;
            fprintf(listOfFiles, "%s::%s::%lld\r\n", clientUrl.c_str(),
                    ("versions\\" + versionToDownload + "\\" + versionToDownload + ".jar").c_str(), clientSize);
        }

        if (jsonp::parse(fileRead("assets\\indexes\\" + assetsIndex + ".json"), jsonFile)) {
            const J* objects = jsonFile.m("objects");
            if (objects) {
                for (const auto& kv : objects->o) {
                    std::string fileHash = kv.second.ms("hash");
                    long long fsz = kv.second.mi("size");
                    fprintf(listOfFiles, "https://resources.download.minecraft.net/%s::assets\\objects\\%s\\%s::%lld\r\n",
                            (fileHash.substr(0, 2) + "/" + fileHash).c_str(),
                            fileHash.substr(0, 2).c_str(), fileHash.c_str(), fsz);
                }
            }
        }
        if (listOfFiles) fclose(listOfFiles);

        parseLibraries(versionToDownload, 1);

        disableGadget(g_playButton, true);

        openProgressWindow(versionToDownload);

        downloadThreadActive = true;
        long redownload = checkState(g_dlRedownload);
        std::thread([redownload]() { downloadFilesCore(redownload, true); }).detach();
    } else {
        messageRequester("Download Error", "Seems like you have no internet connection!");
    }
}

// ---------------------------------------------------------------------------
// Window procedures
// ---------------------------------------------------------------------------
static LRESULT CALLBACK MainProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_nameEdit = makeEdit(hWnd, IDC_NAME, 5, 5, 240, 25, "", false);
        SendMessageW(g_nameEdit, EM_LIMITTEXT, 16, 0);
        setGadgetText(g_nameEdit, prefStr(L"Name", playerNameDefault));

        g_ramEdit = makeEdit(hWnd, IDC_RAM, 5, 35, 240, 25, "", true);
        SendMessageW(g_ramEdit, EM_LIMITTEXT, 6, 0);
        setGadgetText(g_ramEdit, prefStr(L"Ram", ramAmountDefault));
        addTip(g_ramEdit, "Amount (megabytes) of memory to allocate for Minecraft");

        g_versionsCombo = makeCombo(hWnd, IDC_VERSIONS, 5, 65, 240, 25);
        g_javaCombo = makeCombo(hWnd, IDC_JAVA, 5, 95, 240, 25);

        g_playButton = makeButton(hWnd, IDC_PLAY, 5, 130, 240, 30, "Play");
        g_downloadButton = makeButton(hWnd, IDC_DOWNLOAD, 5, 165, 240, 30, "Downloader");
        g_settingsButton = makeButton(hWnd, IDC_SETTINGS, 5, 200, 240, 30, "Settings");

        if (fontBold) {
            SendMessageW(g_playButton, WM_SETFONT, (WPARAM)fontBold, TRUE);
            SendMessageW(g_downloadButton, WM_SETFONT, (WPARAM)fontBold, TRUE);
        }

        HWND author = makeStatic(hWnd, IDC_AUTHOR, 2, 240, 70, 20, (std::string("by ") + launcherDeveloper).c_str());
        HWND vtxt = makeStatic(hWnd, IDC_VERSIONTXT, 216, 240, 50, 20, (std::string("v") + launcherVersion).c_str());
        if (fontSmall) {
            SendMessageW(author, WM_SETFONT, (WPARAM)fontSmall, TRUE);
            SendMessageW(vtxt, WM_SETFONT, (WPARAM)fontSmall, TRUE);
        }

        findInstalledVersions();
        findJava();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_PLAY: onPlay();
            if (g_quitMain) DestroyWindow(hWnd);
            return 0;
        case IDC_DOWNLOAD: {
            std::string buf;
            if (httpGetMemory("https://launchermeta.mojang.com/mc/game/version_manifest.json", buf)) {
                versionsManifestString = buf; // PB: versionsManifestString.s = ReceiveHTTPMemory(...)
                hDownloader = createTopWindow(L"VLDownloader", L"Client Downloader", 200, 120);
                if (hDownloader) {
                    disableGadget(g_downloadButton, true);
                    ShowWindow(hDownloader, SW_SHOW);
                    UpdateWindow(hDownloader);
                }
            } else {
                messageRequester("Download Error", "Seems like you have no internet connection");
            }
            return 0;
        }
        case IDC_SETTINGS:
            disableGadget(g_settingsButton, true);
            hSettings = createTopWindow(L"VLSettings", L"Vortex Launcher Settings", 335, 255);
            if (hSettings) { ShowWindow(hSettings, SW_SHOW); UpdateWindow(hSettings); }
            return 0;
        }
        return 0;
    case WM_APP_REFRESHLIST:
        comboClear(g_versionsCombo);
        findInstalledVersions();
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        hMain = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static LRESULT CALLBACK DownloaderProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_dlCombo = makeCombo(hWnd, IDC_DW_COMBO, 5, 5, 190, 25);
        g_dlShowAll = makeCheck(hWnd, IDC_DW_SHOWALL, 5, 40, 130, 20, "Show all versions");
        setCheck(g_dlShowAll, prefInt(L"ShowAllVersions", versionsTypeDefault));
        g_dlRedownload = makeCheck(hWnd, IDC_DW_REDOWNLOAD, 5, 60, 130, 20, "Redownload all files");
        setCheck(g_dlRedownload, prefInt(L"RedownloadFiles", downloadAllFilesDefault));
        g_dlButton = makeButton(hWnd, IDC_DW_BUTTON, 5, 85, 190, 30, "Download");

        if (downloadThreadActive) disableGadget(g_dlButton, true);

        // versionsManifestString was already fetched by the caller.
        parseVersionsManifest((int)checkState(g_dlShowAll));
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_DW_SHOWALL: downloaderRefreshVersions(); return 0;
        case IDC_DW_BUTTON: onDownloadPressed(); return 0;
        }
        return 0;
    case WM_CLOSE:
        writePrefI(L"ShowAllVersions", checkState(g_dlShowAll));
        writePrefI(L"RedownloadFiles", checkState(g_dlRedownload));
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        hDownloader = nullptr; g_dlCombo = g_dlShowAll = g_dlRedownload = g_dlButton = nullptr;
        if (hMain) disableGadget(g_downloadButton, false);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static LRESULT CALLBACK SettingsProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        makeStatic(hWnd, 0, 5, 5, 80, 30, "Launch parameters:");
        g_setArgs = makeEdit(hWnd, IDC_SET_ARGS, 70, 5, 260, 25, "", false);
        setGadgetText(g_setArgs, prefStr(L"LaunchArguments", customLaunchArgumentsDefault));
        addTip(g_setArgs, "These parameters will be used to launch Minecraft");

        makeStatic(hWnd, 0, 5, 35, 80, 30, "Custom Java path:");
        g_setJavaPath = makeEdit(hWnd, IDC_SET_JAVAPATH, 70, 35, 260, 25, "", false);
        setGadgetText(g_setJavaPath, prefStr(L"JavaPath", javaBinaryPathDefault));
        addTip(g_setJavaPath, "Absolute path to Java executable");

        makeStatic(hWnd, 0, 5, 65, 80, 30, "Download threads:");
        g_setThreads = makeEdit(hWnd, IDC_SET_THREADS, 70, 65, 260, 25, "", true);
        SendMessageW(g_setThreads, EM_LIMITTEXT, 3, 0);
        setGadgetText(g_setThreads, std::to_string(prefInt(L"DownloadThreads", downloadThreadsAmountDefault)));
        addTip(g_setThreads, "Higher numbers may speedup downloads on fast internet connection (works only with multi-threads downloads)");

        g_setAsync = makeCheck(hWnd, IDC_SET_ASYNC, 5, 95, 300, 20, "Fast multithreaded downloading");
        setCheck(g_setAsync, prefInt(L"AsyncDownload", asyncDownloadDefault));

        g_setDlMissing = makeCheck(hWnd, IDC_SET_DLMISSING, 5, 115, 300, 20, "Download missing libraries on game start");
        setCheck(g_setDlMissing, prefInt(L"DownloadMissingLibs", downloadMissingLibrariesDefault));

        g_setSaveString = makeCheck(hWnd, IDC_SET_SAVESTRING, 5, 135, 300, 20, "Save the launch string to a file");
        addTip(g_setSaveString, "The full game launch string will be saved to launch_string.txt");
        setCheck(g_setSaveString, prefInt(L"SaveLaunchString", saveLaunchStringDefault));

        g_setUseCustomJava = makeCheck(hWnd, IDC_SET_USECUSTOMJAVA, 5, 155, 300, 20, "Use custom Java");
        addTip(g_setUseCustomJava, "Use custom Java instead of the installed one");
        setCheck(g_setUseCustomJava, prefInt(L"UseCustomJava", useCustomJavaDefault));

        g_setUseCustomParams = makeCheck(hWnd, IDC_SET_USECUSTOMPARAMS, 5, 175, 300, 20, "Use custom launch parameters");
        addTip(g_setUseCustomParams, "Set custom parameters to launch the game");
        setCheck(g_setUseCustomParams, prefInt(L"UseCustomParameters", useCustomParamsDefault));

        g_setKeepOpen = makeCheck(hWnd, IDC_SET_KEEPOPEN, 5, 195, 300, 20, "Keep the launcher open");
        addTip(g_setKeepOpen, "Keep the launcher open after launching the game");
        setCheck(g_setKeepOpen, prefInt(L"KeepLauncherOpen", keepLauncherOpenDefault));

        makeButton(hWnd, IDC_SET_SAVE, 5, 220, 325, 30, "Save and apply");

        disableGadget(g_setThreads, checkState(g_setAsync) != BST_CHECKED);
        disableGadget(g_setJavaPath, checkState(g_setUseCustomJava) != BST_CHECKED);
        disableGadget(g_setArgs, checkState(g_setUseCustomParams) != BST_CHECKED);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SET_USECUSTOMPARAMS:
            disableGadget(g_setArgs, checkState(g_setUseCustomParams) != BST_CHECKED); return 0;
        case IDC_SET_USECUSTOMJAVA:
            disableGadget(g_setJavaPath, checkState(g_setUseCustomJava) != BST_CHECKED); return 0;
        case IDC_SET_ASYNC:
            disableGadget(g_setThreads, checkState(g_setAsync) != BST_CHECKED); return 0;
        case IDC_SET_SAVE: {
            if (gadgetText(g_setThreads) == "0") setGadgetText(g_setThreads, "5");

            writePrefI(L"DownloadMissingLibs", checkState(g_setDlMissing));
            writePrefI(L"AsyncDownload", checkState(g_setAsync));
            writePrefI(L"SaveLaunchString", checkState(g_setSaveString));
            writePrefI(L"UseCustomJava", checkState(g_setUseCustomJava));
            writePrefI(L"UseCustomParameters", checkState(g_setUseCustomParams));
            writePrefI(L"KeepLauncherOpen", checkState(g_setKeepOpen));

            if (checkState(g_setUseCustomJava)) writePref(L"JavaPath", u2w(gadgetText(g_setJavaPath)));
            if (checkState(g_setAsync)) writePref(L"DownloadThreads", u2w(gadgetText(g_setThreads)));
            if (checkState(g_setUseCustomParams)) writePref(L"LaunchArguments", u2w(gadgetText(g_setArgs)));

            findJava();

            downloadThreadsAmount = val(gadgetText(g_setThreads));
            asyncDownload = (int)checkState(g_setAsync);
            return 0;
        }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        hSettings = nullptr;
        if (hMain) disableGadget(g_settingsButton, false);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static LRESULT CALLBACK ProgressProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_prText = makeStatic(hWnd, IDC_PR_TEXT, 5, 5, 220, 20, "");   // set right after creation
        g_prFilesLeft = makeStatic(hWnd, IDC_PR_FILESLEFT, 5, 25, 220, 20, "Files remaining: unknown");
        g_prBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                  5, 50, 220, 20, hWnd, (HMENU)(INT_PTR)IDC_PR_BAR,
                                  GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_prBar, PBM_SETRANGE32, 0, 100);
        g_prOk = makeButton(hWnd, IDC_PR_OK, 5, 50, 220, 30, "OK");
        hideGadget(g_prOk, true);
        SetTimer(hWnd, 1, 500, nullptr);
        return 0;
    }
    case WM_TIMER:
        refreshProgressUI();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_PR_OK) { DestroyWindow(hWnd); return 0; }
        return 0;
    case WM_CLOSE:
        if (!downloadThreadActive) DestroyWindow(hWnd);
        else messageRequester("Download in progress", "Wait for download to complete!");
        return 0;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        hProgress = nullptr;
        g_prText = g_prFilesLeft = g_prBar = g_prOk = nullptr;
        if (hMain) disableGadget(g_playButton, false);
        if (hDownloader) disableGadget(g_dlButton, false);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Entry point (PB lines 64-114 + event loop)
// ---------------------------------------------------------------------------
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    INITCOMMONCONTROLSEX icc{ sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    workingDirectory = w2u(exePath);
    size_t slash = workingDirectory.find_last_of('\\');
    if (slash != std::string::npos) workingDirectory.resize(slash);   // PB GetPathPart + RTrim
    // PB RTrim(x, "\")
    while (!workingDirectory.empty() && workingDirectory.back() == '\\') workingDirectory.pop_back();

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    tempDirectory = w2u(tmp);   // GetTempPath returns a trailing backslash, like PB's GetTemporaryDirectory

    // PB: programFilesDir(0)=ProgramW6432, (1)=PROGRAMFILES.
    // The original is a 32-bit process, so PROGRAMFILES is the WOW64 dir; in this
    // native x64 build we take "ProgramFiles(x86)" explicitly to keep the same behaviour.
    auto envGet = [](const wchar_t* name) -> std::string {
        wchar_t buf[4096];
        DWORD n = GetEnvironmentVariableW(name, buf, 4095);
        return n > 0 && n < 4095 ? w2u(buf) : std::string();
    };
    std::string pf64 = envGet(L"ProgramW6432");
    std::string pf32 = envGet(L"ProgramFiles(x86)");
    if (pf32.empty()) pf32 = envGet(L"PROGRAMFILES");
    programFilesDir[0] = pf64 + "\\";
    programFilesDir[1] = pf32 + "\\";

    SetCurrentDirectoryW(u2w(workingDirectory).c_str());
    g_confPath = u2w(workingDirectory + "\\vortex_launcher.conf");   // PB OpenPreferences("vortex_launcher.conf")

    downloadThreadsAmount = (int)prefInt(L"DownloadThreads", downloadThreadsAmountDefault);
    asyncDownload = (int)prefInt(L"AsyncDownload", asyncDownloadDefault);

    deleteFile(tempDirectory + "vlauncher_download_list.txt");
    SetEnvironmentVariableW(L"_JAVA_OPTIONS", nullptr);   // PB RemoveEnvironmentVariable

    httpInit();

    // Fonts: PB LoadFont(0,"Arial",10,#PB_Font_Bold), LoadFont(1,"Arial",7)
    HDC hdc = GetDC(nullptr);
    fontBold = CreateFontW(-MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72), 0, 0, 0, FW_BOLD,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    fontSmall = CreateFontW(-MulDiv(7, GetDeviceCaps(hdc, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL,
                            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    ReleaseDC(nullptr, hdc);

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = MainProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"VLMain";
    RegisterClassW(&wc);
    wc.lpfnWndProc = DownloaderProc; wc.lpszClassName = L"VLDownloader"; RegisterClassW(&wc);
    wc.lpfnWndProc = SettingsProc; wc.lpszClassName = L"VLSettings"; RegisterClassW(&wc);
    wc.lpfnWndProc = ProgressProc; wc.lpszClassName = L"VLProgress"; RegisterClassW(&wc);

    hMain = createTopWindow(L"VLMain", L"Vortex Minecraft Launcher", 250, 250);
    if (hMain) {
        ShowWindow(hMain, SW_SHOW);
        UpdateWindow(hMain);

        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        deleteFile(tempDirectory + "vlauncher_download_list.txt");   // PB after the event loop
    }
    return 0;
}
