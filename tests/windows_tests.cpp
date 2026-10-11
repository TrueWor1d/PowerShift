#include "power_plans.h"
#include "startup.h"
#include "ui_text.h"
#include "win32_util.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void Success(DWORD error, const char* message) {
    if (error != ERROR_SUCCESS)
        throw std::runtime_error(std::string(message) + " (Windows error " + std::to_string(error) + ")");
}

struct TestRegistry {
    std::wstring root = L"Software\\PowerShiftTests\\" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(GetTickCount64());
    ~TestRegistry() { RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str()); }
};

std::wstring ReadValue(const std::wstring& path, const wchar_t* name) {
    wchar_t value[1024]{};
    DWORD bytes = sizeof(value);
    Success(static_cast<DWORD>(RegGetValueW(HKEY_CURRENT_USER, path.c_str(), name,
        RRF_RT_REG_SZ, nullptr, value, &bytes)), "Read registry value");
    return value;
}

void StartupTests() {
    using powershift::StartupManager;
    TestRegistry registry;
    const std::wstring run = registry.root + L"\\Run";
    const std::wstring settings = registry.root + L"\\Settings";
    const std::wstring first = L"C:\\PowerShift 中文 空格\\PowerShift.exe";
    const std::wstring moved = L"C:\\Moved PowerShift\\PowerShift.exe";
    StartupManager startup(run, settings);
    bool enabled = true;
    Success(startup.IsEnabled(enabled), "Read missing startup item");
    Check(!enabled, "A missing item must be disabled");

    Success(startup.Initialize(first), "First-run startup registration");
    Success(startup.IsEnabled(enabled), "Read enabled startup item");
    Check(enabled, "First run must enable startup");
    Check(ReadValue(run, L"PowerShift") == L"\"" + first + L"\"", "Quote the entire Unicode executable path");
    Success(startup.Initialize(moved), "Repair a moved executable's path");
    Check(ReadValue(run, L"PowerShift") == L"\"" + moved + L"\"", "Update an enabled item's path");

    HKEY raw = nullptr;
    Success(static_cast<DWORD>(RegOpenKeyExW(HKEY_CURRENT_USER, run.c_str(), 0, KEY_SET_VALUE, &raw)), "Open test key");
    powershift::UniqueRegistryKey key(raw);
    constexpr wchar_t other[] = L"C:\\OtherApp.exe";
    Success(static_cast<DWORD>(RegSetValueExW(key.get(), L"OtherApplication", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(other), sizeof(other))), "Create unrelated test item");

    Success(startup.SetEnabled(moved, false), "Disable startup");
    Success(startup.Initialize(first), "Restart after disabling startup");
    Success(startup.IsEnabled(enabled), "Read disabled startup item");
    Check(!enabled, "A restart must preserve disabled startup");
    Check(ReadValue(run, L"OtherApplication") == other, "Do not modify unrelated startup items");

    Success(startup.SetEnabled(first, true), "Re-enable startup");
    Success(static_cast<DWORD>(RegDeleteValueW(key.get(), L"PowerShift")), "Simulate external startup removal");
    Success(startup.Initialize(first), "Restart after external removal");
    Success(startup.IsEnabled(enabled), "Read externally removed startup item");
    Check(!enabled, "An externally removed item must stay disabled");

    Check(startup.SetEnabled(L"C:\\bad\"path.exe", true) == ERROR_INVALID_PARAMETER, "Reject an invalid executable path");
    Check(startup.SetEnabled(std::wstring(261, L'x'), true) == ERROR_FILENAME_EXCED_RANGE, "Report Run command length limit");
    Success(startup.IsEnabled(enabled), "Read state after invalid path");
    Check(!enabled, "Invalid paths must not change startup state");
    std::cout << "PASS: first-run startup, quoted Unicode paths, moved paths, disabled persistence, external removal, isolated writes\n";
}

void PowerTests(bool switchPlans) {
    using namespace powershift;
    GUID active{};
    Success(ReadActivePlan(active), "Read active power plan");
    std::vector<PowerPlan> plans;
    Success(EnumeratePlans(plans), "Enumerate power plans");
    Check(!plans.empty(), "At least one power plan must be present");
    Check(std::any_of(plans.begin(), plans.end(), [&](const PowerPlan& plan) {
        return IsEqualGUID(plan.id, active) != FALSE;
    }), "The active plan must appear in the enumerated plans");
    for (const auto& plan : plans) Check(!plan.name.empty(), "Plan names must be nonempty");
    std::cout << "PASS: official power plan enumeration, active GUID, friendly names (" << plans.size() << " plans)\n";

    if (switchPlans) {
        try {
            for (const auto& plan : plans) Success(ActivatePlan(plan.id), "Switch and verify power plan");
        } catch (...) {
            const DWORD restore = ActivatePlan(active);
            if (restore != ERROR_SUCCESS) std::cerr << "RESTORE FAILED: Windows error " << restore << '\n';
            throw;
        }
        Success(ActivatePlan(active), "Restore original power plan");
        std::cout << "PASS: switch available plans and restore the original\n";
    }
}

} // namespace

int main(int argc, char* argv[]) {
    const bool registryOnly = argc == 2 && std::string(argv[1]) == "--registry-only";
    const bool switchPlans = argc == 2 && std::string(argv[1]) == "--switch";
    if (argc > 1 && !registryOnly && !switchPlans) {
        std::cerr << "Usage: PowerShiftTests [--registry-only | --switch]\n";
        return 2;
    }
    try {
        Check(powershift::MenuLabel(L"AC & DC\t模式\n") == L"AC && DC 模式 ", "Escape menu accelerators and control characters");
        StartupTests();
        if (!registryOnly) PowerTests(switchPlans);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
