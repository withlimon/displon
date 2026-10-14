// Native file dialogs + process enumeration helpers (Windows UI only).
#pragma once
#include <string>
#include <vector>

#ifdef _WIN32

// Open-file picker filtered to *.exe. Returns empty on cancel/error.
std::wstring PickExecutableFile(void* hwndOwner /* HWND */);

// Open-file picker for whitelist import (*.txt;*.ftw).
std::wstring PickOpenTextFile(void* hwndOwner);

// Save-file picker for whitelist export.
std::wstring PickSaveTextFile(void* hwndOwner, const std::wstring& defaultName = L"whitelist.txt");

// Currently running processes suitable for whitelist (display name + full path).
struct RunningApp {
    std::wstring name;  // e.g. "notepad.exe"
    std::wstring path;  // full image path when available
};
std::vector<RunningApp> EnumerateRunningApps(std::size_t maxCount = 64);

#endif

// Portable whitelist file I/O (UTF-8, one path per line; lines starting with # ignored).
bool ExportWhitelistFile(const std::wstring& path,
                         const std::vector<std::pair<std::wstring, std::wstring>>& namePathPairs);
bool ImportWhitelistFile(const std::wstring& path,
                         std::vector<std::pair<std::wstring, std::wstring>>& outNamePath);
