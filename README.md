<div align="center">

# 🌀 Vortex Launcher — C++ Port

**A complete, dependency-free C++17 rewrite of [Vortex Minecraft Launcher][original] by [Kron4ek][kron].**

Pure Win32 API · no frameworks · no third-party libraries · built by `build.bat`

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Language: C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows_10/11-blue.svg)](#build)
[![Dependencies: none](https://img.shields.io/badge/Dependencies-none-brightgreen.svg)](#build)

**English** · [Русский](README.ru.md)

</div>

---

This repository is a **port of the original PureBasic sources** of Vortex Launcher **v1.1.20** to modern C++.
The port is written from the [original source code][source-pb] (`vlauncher_windows.pb`, tag `1.1.20`) — procedure by
procedure, string by string — **not** from disassembly or decompiled output. Its goal is a 1:1 behavioral clone:
the same UI layout, the same dialogs, the same configuration file format, the same defaults, and — deliberately —
the same quirks as the original 1.1.20.

## Screenshots

| Main window | Client Downloader | Settings |
|:---:|:---:|:---:|
| ![Main window](docs/screenshots/main.png) | ![Client Downloader](docs/screenshots/downloader.png) | ![Settings](docs/screenshots/settings.png) |

## Features

* Offline-mode launcher: player name, RAM amount, installed-version selector, one-click **Play**.
* Built-in **Client Downloader**: fetches the Mojang version manifest, lists every release/snapshot, downloads the
  selected version with its libraries, client jar and assets (MD5-verified, multithreaded).
* Automatic **Java discovery** (common JDK/JRE locations), or a custom Java path.
* Generates the full `launcher_profiles.json` / `launcher_log.txt` and launches the game; optional launch-string saving.
* Rich settings: custom launch parameters, keep-launcher-open, redownload-all, download-missing-libraries-on-start, etc.
* Config stored in `vortex_launcher.conf` — the **same file and format the original uses**.

## What the port uses instead of PureBasic

| PureBasic (original) | C++ (this port) |
|---|---|
| PB gadgets / forms | raw Win32: `user32`, `gdi32`, `comctl32` v6 |
| `ReceiveHTTPMemory` / `ReceiveHTTPFile` | `winhttp` (redirects, TLS 1.2/1.3, same timeouts) |
| `ParseJSON` + `ExamineJSONMembers` | small JSON parser that **preserves member order** like PB's |
| `OpenPack` / `ExtractPackFile` (natives jars) | own ZIP reader + RFC-1951 inflate |
| `CryptCreateHashData(#PB_Hash_MD5)` | `crypt32` `CALG_MD5` |
| PB preferences → Windows registry-style prefs | the same `vortex_launcher.conf` file & section |
| PB runtime | none: static CRT (`/MT`), result is a single ~350 KB exe |

All strings inside are UTF-8, wide Win32 APIs are used at the UI boundary — non-ASCII nicknames and paths work.

## Faithfulness notes

The port reproduces the original's behavior exactly where it matters — window sizes (250×250 main, 200×120
downloader, 335×255 settings, 230×85 progress), control layout, message-box texts, order of operations, and even the
original's known quirks (e.g. async download uses `threads + 1` slots; the sync retry counter is shared between
files; `findJava` stops at the first non-directory entry; versions with spaces are renamed on disk). These are kept
on purpose so that the C++ build is a drop-in replacement for Vortex Launcher 1.1.20.
Runtime behavior was verified against the reference: HTTP flow returns the live manifest (≈210 KB, 103 modern
releases in manifest order), settings round-trip through `vortex_launcher.conf` matches the original keys and
defaults, and all window close/reopen semantics behave like the PureBasic original.

## Build

Requirements: **Windows 10/11 x64**, [Visual Studio 2017+ Build Tools][buildtools] with the
*Desktop development with C++* workload. Nothing else.

```bat
build.bat
```

The script locates MSVC via `vswhere`, compiles and links `VLauncher_cpp.exe` — one static executable, no installer.
Manual equivalent (inside a *x64 Native Tools* prompt):

```bat
rc /nologo /fo app.res res\app.rc
cl /nologo /EHsc /std:c++17 /O2 /MT /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
   src\app.cpp src\http.cpp src\zip.cpp /Fe:VLauncher_cpp.exe /Fo:obj\ ^
   /link /SUBSYSTEM:WINDOWS app.res ^
   winhttp.lib crypt32.lib advapi32.lib user32.lib gdi32.lib comctl32.lib
```

## Project layout

```
vortex-launcher-cpp/
├─ build.bat              one-shot MSVC build
├─ src/
│  ├─ app.cpp             all windows + launcher logic
│  ├─ http.cpp|hpp        WinHTTP fetch-to-memory / download-to-file
│  ├─ zip.cpp|hpp         ZIP reader + inflate (natives jars)
│  ├─ json.hpp            order-preserving JSON parser
│  └─ util.hpp            strings, config file, MD5, process helpers
├─ res/
│  ├─ app.rc              icon + Common-Controls manifest
│  ├─ app.manifest        comctl32 v6 activation
│  └─ launcher_icon.ico   icon (from the original project)
└─ docs/screenshots/      screenshots used above
```

## Configuration

All settings live in `vortex_launcher.conf` next to the executable, section `[PureBasic-Preferences]`
(the section name is kept for compatibility with the original's config files):
`Name`, `Ram`, `ChosenVer`, `DownloadThreads`, `AsyncDownload`, `DownloadMissingLibs`, `ShowAllVersions`,
`RedownloadFiles`, `SaveLaunchString`, `UseCustomJava`, `JavaPath`, `UseCustomParameters`, `LaunchArguments`,
`KeepLauncherOpen`. A config written by the original Vortex Launcher 1.1.20 works here unchanged, and vice versa.

## Disclaimer & trademark

This is **not an official launcher**. *Minecraft* is a trademark of Mojang AB / Microsoft; this project is
unaffiliated with and not endorsed by them. It downloads game files only from Mojang's public endpoints
(`launchermeta.mojang.com`, `resources.download.minecraft.net`) and requires you to own a legal copy of the game.

## License

The C++ port is licensed under **GPL-3.0** — see [LICENSE](LICENSE).

This is a derivative work of [Vortex Launcher][original] by [Kron4ek][kron] (the original PureBasic sources are
also GPL-3.0). The launcher icon comes from the original project.
The original author(s) receive attribution as required by the license; nothing here is a re-licensing of the
original work.

[original]: https://github.com/Kron4ek/minecraft-vortex-launcher
[kron]: https://github.com/Kron4ek
[source-pb]: https://github.com/Kron4ek/minecraft-vortex-launcher/blob/1.1.20/vlauncher_windows.pb
[buildtools]: https://visualstudio.microsoft.com/visual-cpp-build-tools/
