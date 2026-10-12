#pragma once

#include <windows.h>
#include <string>
#include <utility>
#include "powershift_version.h"

namespace powershift {

template<typename T, auto Close>
class UniqueResource {
public:
    UniqueResource() noexcept = default;
    explicit UniqueResource(T value) noexcept : value_(value) {}
    ~UniqueResource() { reset(); }
    UniqueResource(const UniqueResource&) = delete;
    UniqueResource& operator=(const UniqueResource&) = delete;
    UniqueResource(UniqueResource&& other) noexcept : value_(other.release()) {}
    UniqueResource& operator=(UniqueResource&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    T get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
    T release() noexcept { return std::exchange(value_, nullptr); }
    void reset(T value = nullptr) noexcept {
        if (value_ != nullptr) Close(value_);
        value_ = value;
    }
private:
    T value_ = nullptr;
};

using UniqueHandle = UniqueResource<HANDLE, CloseHandle>;
using UniqueMenu = UniqueResource<HMENU, DestroyMenu>;
using UniqueIcon = UniqueResource<HICON, DestroyIcon>;
using UniqueRegistryKey = UniqueResource<HKEY, RegCloseKey>;

struct LocalMemoryDeleter {
    void operator()(void* memory) const noexcept { if (memory) LocalFree(memory); }
};

std::wstring ErrorText(DWORD error);
void ShowError(HWND owner, const std::wstring& action, DWORD error);
DWORD ExecutablePath(std::wstring& path);
std::wstring GuidText(const GUID& guid);

} // namespace powershift
