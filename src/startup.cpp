#include "startup.h"
#include "win32_util.h"

#include <limits>
#include <vector>

namespace powershift {

StartupManager::StartupManager(std::wstring runKey, std::wstring settingsKey, std::wstring valueName)
    : runKey_(std::move(runKey)), settingsKey_(std::move(settingsKey)), valueName_(std::move(valueName)) {}

DWORD StartupManager::ReadCommand(std::wstring& command, bool& exists) const {
    exists = false;
    command.clear();
    DWORD required = 0;
    LSTATUS error = RegGetValueW(HKEY_CURRENT_USER, runKey_.c_str(), valueName_.c_str(),
        RRF_RT_REG_SZ, nullptr, nullptr, &required);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return ERROR_SUCCESS;
    if (error != ERROR_SUCCESS && error != ERROR_MORE_DATA) return static_cast<DWORD>(error);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (required == 0 || required > 1024 * 1024) return ERROR_INVALID_DATA;
        std::vector<wchar_t> buffer((required + sizeof(wchar_t) - 1) / sizeof(wchar_t) + 1, L'\0');
        DWORD capacity = static_cast<DWORD>((buffer.size() - 1) * sizeof(wchar_t));
        error = RegGetValueW(HKEY_CURRENT_USER, runKey_.c_str(), valueName_.c_str(),
            RRF_RT_REG_SZ, nullptr, buffer.data(), &capacity);
        if (error == ERROR_MORE_DATA) { required = capacity; continue; }
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return ERROR_SUCCESS;
        if (error != ERROR_SUCCESS) return static_cast<DWORD>(error);
        command = buffer.data();
        exists = true;
        return ERROR_SUCCESS;
    }
    return ERROR_MORE_DATA;
}

DWORD StartupManager::WriteCommand(const std::wstring* command) const {
    HKEY raw = nullptr;
    LSTATUS error = command
        ? RegCreateKeyExW(HKEY_CURRENT_USER, runKey_.c_str(), 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &raw, nullptr)
        : RegOpenKeyExW(HKEY_CURRENT_USER, runKey_.c_str(), 0, KEY_SET_VALUE, &raw);
    UniqueRegistryKey key(raw);
    if (!command && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
        return ERROR_SUCCESS;
    if (error != ERROR_SUCCESS) return static_cast<DWORD>(error);
    if (command) {
        const size_t bytes = (command->size() + 1) * sizeof(wchar_t);
        if (bytes > std::numeric_limits<DWORD>::max()) return ERROR_INVALID_PARAMETER;
        error = RegSetValueExW(key.get(), valueName_.c_str(), 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command->c_str()), static_cast<DWORD>(bytes));
    } else {
        error = RegDeleteValueW(key.get(), valueName_.c_str());
        if (error == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    }
    return static_cast<DWORD>(error);
}

DWORD StartupManager::IsEnabled(bool& enabled) const {
    std::wstring command;
    bool exists = false;
    const DWORD error = ReadCommand(command, exists);
    enabled = exists && !command.empty();
    return error;
}

DWORD StartupManager::SetEnabled(const std::wstring& executable, bool enabled) {
    if (enabled && (executable.empty() || executable.find(L'"') != std::wstring::npos))
        return ERROR_INVALID_PARAMETER;
    std::wstring previous;
    bool existed = false;
    DWORD error = ReadCommand(previous, existed);
    if (error != ERROR_SUCCESS) return error;
    const std::wstring command = L"\"" + executable + L"\"";
    // Run values are limited to 260 characters, including command-line arguments.
    if (enabled && command.size() > 260) return ERROR_FILENAME_EXCED_RANGE;
    error = WriteCommand(enabled ? &command : nullptr);
    if (error != ERROR_SUCCESS) return error;

    HKEY raw = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, settingsKey_.c_str(), 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &raw, nullptr);
    UniqueRegistryKey settings(raw);
    if (status == ERROR_SUCCESS) {
        const DWORD initialized = 1;
        status = RegSetValueExW(settings.get(), L"StartupInitialized", 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&initialized), sizeof(initialized));
    }
    if (status != ERROR_SUCCESS) {
        // Keep the previous startup item if saving the first-run marker failed.
        const DWORD rollback = WriteCommand(existed ? &previous : nullptr);
        return rollback == ERROR_SUCCESS ? static_cast<DWORD>(status) : rollback;
    }
    return ERROR_SUCCESS;
}

DWORD StartupManager::Initialize(const std::wstring& executable) {
    DWORD initialized = 0;
    DWORD size = sizeof(initialized);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, settingsKey_.c_str(),
        L"StartupInitialized", RRF_RT_REG_DWORD, nullptr, &initialized, &size);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND ||
        (status == ERROR_SUCCESS && initialized == 0))
        return SetEnabled(executable, true);
    if (status != ERROR_SUCCESS) return static_cast<DWORD>(status);

    std::wstring command;
    bool exists = false;
    const DWORD error = ReadCommand(command, exists);
    if (error != ERROR_SUCCESS) return error;
    // Preserve a disabled or externally removed item. Repair an enabled item's path
    // when the user deliberately starts a moved copy of the executable.
    if (exists && !command.empty() && command != L"\"" + executable + L"\"")
        return SetEnabled(executable, true);
    return ERROR_SUCCESS;
}

} // namespace powershift
