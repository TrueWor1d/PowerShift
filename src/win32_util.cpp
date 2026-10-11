#include "win32_util.h"

#include <objbase.h>
#include <algorithm>
#include <iterator>
#include <memory>
#include <vector>

namespace powershift {

std::wstring ErrorText(DWORD error) {
    wchar_t* raw = nullptr;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
    std::unique_ptr<wchar_t, LocalMemoryDeleter> memory(raw);
    std::wstring text = count && raw ? std::wstring(raw, count) : L"系统未提供错误说明。";
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
        text.pop_back();
    return text;
}

void ShowError(HWND owner, const std::wstring& action, DWORD error) {
    std::wstring message = action + L"\n\n" + ErrorText(error)
        + L"\n错误代码：" + std::to_wstring(error);
    if (error == ERROR_ACCESS_DENIED)
        message += L"\n\n此操作可能受到 Windows 权限或组织策略限制。";
    MessageBoxW(owner, message.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
}

DWORD ExecutablePath(std::wstring& path) {
    for (DWORD capacity = MAX_PATH; ; capacity = std::min<DWORD>(capacity * 2, 32768)) {
        std::vector<wchar_t> buffer(capacity);
        SetLastError(ERROR_SUCCESS);
        const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (size == 0) return GetLastError();
        if (size < capacity) {
            path.assign(buffer.data(), size);
            return ERROR_SUCCESS;
        }
        if (capacity == 32768) break;
    }
    return ERROR_FILENAME_EXCED_RANGE;
}

std::wstring GuidText(const GUID& guid) {
    wchar_t text[40]{};
    StringFromGUID2(guid, text, static_cast<int>(std::size(text)));
    return text;
}

} // namespace powershift
