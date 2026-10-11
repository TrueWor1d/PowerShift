#include "power_plans.h"
#include "win32_util.h"

#include <powrprof.h>
#include <powersetting.h>
#include <algorithm>
#include <memory>

namespace powershift {

DWORD ReadPlanName(const GUID& id, std::wstring& name) {
    DWORD required = 0;
    DWORD error = PowerReadFriendlyName(nullptr, &id, nullptr, nullptr, nullptr, &required);
    if (error != ERROR_SUCCESS && error != ERROR_MORE_DATA) return error;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (required == 0 || required > 1024 * 1024) return ERROR_INVALID_DATA;
        std::vector<wchar_t> buffer((required + sizeof(wchar_t) - 1) / sizeof(wchar_t) + 1, L'\0');
        DWORD capacity = static_cast<DWORD>((buffer.size() - 1) * sizeof(wchar_t));
        error = PowerReadFriendlyName(nullptr, &id, nullptr, nullptr,
            reinterpret_cast<UCHAR*>(buffer.data()), &capacity);
        if (error == ERROR_MORE_DATA) { required = capacity; continue; }
        if (error != ERROR_SUCCESS) return error;
        name.assign(buffer.begin(), std::find(buffer.begin(), buffer.end(), L'\0'));
        if (name.empty()) name = L"未命名电源计划 " + GuidText(id);
        return ERROR_SUCCESS;
    }
    return ERROR_MORE_DATA;
}

DWORD ReadActivePlan(GUID& id) {
    GUID* raw = nullptr;
    const DWORD error = PowerGetActiveScheme(nullptr, &raw);
    std::unique_ptr<GUID, LocalMemoryDeleter> memory(raw);
    if (error != ERROR_SUCCESS) return error;
    if (!memory) return ERROR_INVALID_DATA;
    id = *memory;
    return ERROR_SUCCESS;
}

DWORD EnumeratePlans(std::vector<PowerPlan>& plans) {
    std::vector<PowerPlan> snapshot;
    for (ULONG index = 0; ; ++index) {
        PowerPlan plan;
        DWORD size = sizeof(plan.id);
        const DWORD error = PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME,
            index, reinterpret_cast<UCHAR*>(&plan.id), &size);
        if (error == ERROR_NO_MORE_ITEMS) break;
        if (error != ERROR_SUCCESS) return error;
        if (size != sizeof(plan.id)) return ERROR_INVALID_DATA;
        if (ReadPlanName(plan.id, plan.name) != ERROR_SUCCESS)
            plan.name = L"电源计划 " + GuidText(plan.id);
        snapshot.push_back(std::move(plan));
    }
    plans = std::move(snapshot);
    return ERROR_SUCCESS;
}

DWORD ActivatePlan(const GUID& id) {
    const DWORD error = PowerSetActiveScheme(nullptr, &id);
    if (error != ERROR_SUCCESS) return error;
    GUID active{};
    const DWORD query = ReadActivePlan(active);
    if (query != ERROR_SUCCESS) return query;
    return IsEqualGUID(active, id) ? ERROR_SUCCESS : ERROR_RETRY;
}

} // namespace powershift
