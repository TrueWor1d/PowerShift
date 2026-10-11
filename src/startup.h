#pragma once

#include <windows.h>
#include <string>

namespace powershift {

// Paths can be overridden by the integration tests, which never touch real startup items.
class StartupManager {
public:
    StartupManager(
        std::wstring runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        std::wstring settingsKey = L"Software\\PowerShift",
        std::wstring valueName = L"PowerShift");

    DWORD Initialize(const std::wstring& executable);
    DWORD IsEnabled(bool& enabled) const;
    DWORD SetEnabled(const std::wstring& executable, bool enabled);

private:
    DWORD ReadCommand(std::wstring& command, bool& exists) const;
    DWORD WriteCommand(const std::wstring* command) const;

    std::wstring runKey_;
    std::wstring settingsKey_;
    std::wstring valueName_;
};

} // namespace powershift
