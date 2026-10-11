#pragma once

#include <string>

namespace powershift {

// A plan's name is data, not a menu accelerator or a shortcut description.
inline std::wstring MenuLabel(const std::wstring& name) {
    std::wstring result;
    result.reserve(name.size());
    for (const wchar_t character : name) {
        if (character == L'&') result += L"&&";
        else if (character == L'\t' || character == L'\r' || character == L'\n') result += L' ';
        else result += character;
    }
    return result;
}

} // namespace powershift
