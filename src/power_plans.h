#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace powershift {

struct PowerPlan {
    GUID id{};
    std::wstring name;
};

DWORD ReadPlanName(const GUID& id, std::wstring& name);
DWORD ReadActivePlan(GUID& id);
DWORD EnumeratePlans(std::vector<PowerPlan>& plans);
DWORD ActivatePlan(const GUID& id);

} // namespace powershift
