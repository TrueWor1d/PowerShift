// Exercise the real tray message loop with controlled Win32 failures, without
// changing the machine's tray, startup registration, or power plan.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <powrprof.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace fixture {
enum class Scenario { InitializationFailure, DelayedShell, MovedExecutable, NotificationFallback,
    ExternalSwitch, ExplorerRestart, PowerOptions, About };
struct State {
    Scenario scenario = Scenario::InitializationFailure;
    WNDPROC procedure = nullptr;
    LONG_PTR userData = 0;
    bool windowAlive = false, iconAlive = false, notificationAlive = false, quit = false;
    bool errorAfterCleanup = false, legacyQuery = false, menuOutsidePanel = false;
    bool foreground = false, powerOptionsOpened = false;
    unsigned ticks = 0, adds = 0, deletes = 0, popups = 0, activePlan = 1;
    ULONGLONG clock = 0;
    std::map<UINT_PTR, UINT> timers;
    std::vector<UINT> retryDelays;
    std::vector<GUID> subscriptions;
    std::vector<DWORD> iconFlags;
    std::wstring tooltip, aboutText;
    RECT icon{-300, 900, -276, 924};
    RECT panel{-320, 880, -40, 1040};
    std::vector<std::string> failures;
};
State state;
const HWND window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x100));
const HWND panelWindow = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x101));
void Need(bool condition, const char* message) { if (!condition) state.failures.emplace_back(message); }
bool UsesLegacy() { return state.scenario == Scenario::MovedExecutable || state.scenario == Scenario::ExplorerRestart; }
}

BOOL TestCloseHandle(HANDLE) { return TRUE; }
BOOL TestDestroyIcon(HICON) { return TRUE; }
HANDLE TestCreateMutexW(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR) {
    SetLastError(ERROR_SUCCESS);
    return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(0x200));
}
UINT TestRegisterWindowMessageW(LPCWSTR) { return WM_APP + 50; }
HCURSOR TestLoadCursorW(HINSTANCE, LPCWSTR) { return nullptr; }
ATOM TestRegisterClassExW(const WNDCLASSEXW* value) { fixture::state.procedure = value->lpfnWndProc; return 1; }
LONG_PTR TestGetWindowLongPtrW(HWND, int) { return fixture::state.userData; }
LONG_PTR TestSetWindowLongPtrW(HWND, int, LONG_PTR value) {
    const auto previous = fixture::state.userData;
    fixture::state.userData = value;
    return previous;
}
LRESULT TestDefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return TRUE; }
HWND TestCreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int,
    HWND, HMENU, HINSTANCE, LPVOID parameter) {
    fixture::state.windowAlive = true;
    CREATESTRUCTW creation{};
    creation.lpCreateParams = parameter;
    fixture::state.procedure(fixture::window, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&creation));
    return fixture::window;
}
HRESULT TestLoadIconMetric(HINSTANCE, LPCWSTR, int, HICON* icon) {
    *icon = reinterpret_cast<HICON>(static_cast<UINT_PTR>(0x300)); return S_OK;
}
BOOL TestNotifyIcon(DWORD action, PNOTIFYICONDATAW value) {
    auto& state = fixture::state;
    state.iconFlags.push_back(value->uFlags);
    if (action == NIM_ADD) {
        ++state.adds;
        if (state.scenario == fixture::Scenario::DelayedShell && state.ticks < 32) return FALSE;
        if (fixture::UsesLegacy() && (value->uFlags & NIF_GUID)) return FALSE;
        state.iconAlive = true;
    } else if (action == NIM_DELETE) { state.iconAlive = false; ++state.deletes; }
    if (action == NIM_ADD || action == NIM_MODIFY) state.tooltip = value->szTip;
    if (fixture::UsesLegacy() && action != NIM_ADD)
        fixture::Need(!(value->uFlags & NIF_GUID), "All updates/focus/deletion must retain the fallback identity");
    return TRUE;
}
HRESULT TestGetIconRect(const NOTIFYICONIDENTIFIER* value, RECT* rectangle) {
    if (fixture::UsesLegacy()) {
        fixture::state.legacyQuery = IsEqualGUID(value->guidItem, GUID{}) && value->uID == 1 && value->hWnd == fixture::window;
        if (!fixture::state.legacyQuery) return E_FAIL;
    }
    *rectangle = fixture::state.icon; return S_OK;
}
HPOWERNOTIFY TestRegisterNotification(HANDLE, LPCGUID guid, DWORD) {
    fixture::state.subscriptions.push_back(*guid);
    if (fixture::state.scenario == fixture::Scenario::NotificationFallback &&
        IsEqualGUID(*guid, GUID_ACTIVE_POWERSCHEME)) return nullptr;
    fixture::state.notificationAlive = true;
    return reinterpret_cast<HPOWERNOTIFY>(static_cast<UINT_PTR>(0x400));
}
BOOL TestUnregisterNotification(HPOWERNOTIFY) { fixture::state.notificationAlive = false; return TRUE; }
UINT_PTR TestSetTimer(HWND, UINT_PTR id, UINT interval, TIMERPROC) {
    fixture::state.timers[id] = interval;
    if (id == 1) fixture::state.retryDelays.push_back(interval);
    return id;
}
BOOL TestKillTimer(HWND, UINT_PTR id) { fixture::state.timers.erase(id); return TRUE; }
ULONGLONG TestTickCount() { return fixture::state.clock; }
void TestPostQuitMessage(int) { fixture::state.quit = true; }
BOOL TestDestroyWindow(HWND window) {
    fixture::Need(fixture::state.windowAlive, "Window must not be destroyed twice");
    fixture::state.procedure(window, WM_DESTROY, 0, 0);
    fixture::state.procedure(window, WM_NCDESTROY, 0, 0);
    fixture::state.windowAlive = false;
    return TRUE;
}
BOOL TestGetMessageW(LPMSG message, HWND, UINT, UINT) {
    auto& state = fixture::state;
    *message = {};
    if (state.quit) { message->message = WM_QUIT; return FALSE; }
    ++state.ticks;
    message->hwnd = fixture::window;
    message->message = WM_CLOSE;
    if (state.ticks > 100) { fixture::Need(false, "Tray loop did not terminate"); return FALSE; }
    if (state.scenario == fixture::Scenario::DelayedShell && !state.iconAlive) {
        state.clock += state.timers[1];
        message->message = WM_TIMER; message->wParam = 1;
    } else if (state.ticks == 1 && state.scenario == fixture::Scenario::ExplorerRestart) {
        state.iconAlive = false; message->message = WM_APP + 50;
    } else if ((state.ticks == 1 || (state.ticks == 2 && state.scenario == fixture::Scenario::ExplorerRestart)) &&
        (fixture::UsesLegacy() || state.scenario == fixture::Scenario::About || state.scenario == fixture::Scenario::PowerOptions)) {
        message->message = WM_APP + 1;
        message->lParam = MAKELPARAM(WM_CONTEXTMENU, 1);
        message->wParam = static_cast<WPARAM>(MAKELPARAM(-1, -1)); // Keyboard invocation.
    } else if (state.ticks == 1 && state.scenario == fixture::Scenario::ExternalSwitch) {
        state.activePlan = 2; message->message = WM_POWERBROADCAST; message->wParam = PBT_POWERSETTINGCHANGE;
    } else if (state.ticks == 1 && state.scenario == fixture::Scenario::NotificationFallback) {
        fixture::Need(state.timers.count(2) && state.timers[2] == 10000, "Subscription failure needs a bounded fallback refresh");
        state.activePlan = 2; message->message = WM_TIMER; message->wParam = 2;
    }
    return TRUE;
}
BOOL TestTranslateMessage(const MSG*) { return TRUE; }
LRESULT TestDispatchMessageW(const MSG* message) {
    return fixture::state.procedure(message->hwnd, message->message, message->wParam, message->lParam);
}
int TestMessageBoxW(HWND, LPCWSTR text, LPCWSTR, UINT flags) {
    if (flags & MB_ICONERROR) fixture::state.errorAfterCleanup = !fixture::state.windowAlive &&
        !fixture::state.notificationAlive && !fixture::state.iconAlive && fixture::state.userData == 0;
    else fixture::state.aboutText = text;
    return IDOK;
}
HMENU TestCreatePopupMenu() { return reinterpret_cast<HMENU>(static_cast<UINT_PTR>(0x500)); }
BOOL TestDestroyMenu(HMENU) { return TRUE; }
BOOL TestAppendMenuW(HMENU, UINT, UINT_PTR, LPCWSTR) { return TRUE; }
BOOL TestSetMenuItemInfoW(HMENU, UINT, BOOL, LPCMENUITEMINFOW) { return TRUE; }
HWND TestWindowFromPoint(POINT) { fixture::Need(!fixture::state.foreground, "Capture the panel before switching foreground"); return fixture::panelWindow; }
HWND TestGetAncestor(HWND value, UINT) { return value; }
HWND TestGetDesktopWindow() { return nullptr; }
HWND TestGetShellWindow() { return nullptr; }
BOOL TestIsWindowVisible(HWND) { return TRUE; }
BOOL TestGetWindowRect(HWND, LPRECT value) { *value = fixture::state.panel; return TRUE; }
HMONITOR TestMonitorFromPoint(POINT, DWORD) { return reinterpret_cast<HMONITOR>(static_cast<UINT_PTR>(0x600)); }
BOOL TestGetMonitorInfoW(HMONITOR, LPMONITORINFO value) {
    value->rcMonitor = {-1920, 0, 0, 1080}; value->rcWork = {-1920, 0, 0, 1040}; return TRUE;
}
BOOL TestSetForegroundWindow(HWND) { fixture::state.foreground = true; return TRUE; }
BOOL TestPostMessageW(HWND, UINT, WPARAM, LPARAM) { return TRUE; }
BOOL TestTrackPopupMenuEx(HMENU, UINT flags, int x, int y, HWND, LPTPMPARAMS parameters) {
    auto& state = fixture::state;
    ++state.popups;
    state.menuOutsidePanel = parameters && EqualRect(&parameters->rcExclude, &state.panel) &&
        (flags & TPM_WORKAREA) && (flags & TPM_BOTTOMALIGN) && y == state.panel.top &&
        x >= state.panel.left && x < state.panel.right;
    // A second instance requests the menu while TrackPopupMenuEx pumps messages.
    state.procedure(fixture::window, WM_APP + 2, 0, 0);
    if (state.scenario == fixture::Scenario::PowerOptions) return 4;
    if (state.scenario == fixture::Scenario::About) return 5;
    return 0;
}
HINSTANCE TestShellExecuteW(HWND, LPCWSTR, LPCWSTR file, LPCWSTR arguments, LPCWSTR, int) {
    fixture::state.powerOptionsOpened = std::wstring(file) == L"control.exe" &&
        std::wstring(arguments) == L"/name Microsoft.PowerOptions";
    return reinterpret_cast<HINSTANCE>(static_cast<UINT_PTR>(33));
}

#define CloseHandle TestCloseHandle
#define DestroyIcon TestDestroyIcon
#define CreateMutexW TestCreateMutexW
#define RegisterWindowMessageW TestRegisterWindowMessageW
#define LoadCursorW TestLoadCursorW
#define RegisterClassExW TestRegisterClassExW
#define GetWindowLongPtrW TestGetWindowLongPtrW
#define SetWindowLongPtrW TestSetWindowLongPtrW
#define DefWindowProcW TestDefWindowProcW
#define CreateWindowExW TestCreateWindowExW
#define LoadIconMetric TestLoadIconMetric
#define Shell_NotifyIconW TestNotifyIcon
#define Shell_NotifyIconGetRect TestGetIconRect
#define RegisterPowerSettingNotification TestRegisterNotification
#define UnregisterPowerSettingNotification TestUnregisterNotification
#define SetTimer TestSetTimer
#define KillTimer TestKillTimer
#define GetTickCount64 TestTickCount
#define PostQuitMessage TestPostQuitMessage
#define DestroyWindow TestDestroyWindow
#define GetMessageW TestGetMessageW
#define TranslateMessage TestTranslateMessage
#define DispatchMessageW TestDispatchMessageW
#define MessageBoxW TestMessageBoxW
#define CreatePopupMenu TestCreatePopupMenu
#define DestroyMenu TestDestroyMenu
#define AppendMenuW TestAppendMenuW
#define SetMenuItemInfoW TestSetMenuItemInfoW
#define WindowFromPoint TestWindowFromPoint
#define GetAncestor TestGetAncestor
#define GetDesktopWindow TestGetDesktopWindow
#define GetShellWindow TestGetShellWindow
#define IsWindowVisible TestIsWindowVisible
#define GetWindowRect TestGetWindowRect
#define MonitorFromPoint TestMonitorFromPoint
#define GetMonitorInfoW TestGetMonitorInfoW
#define SetForegroundWindow TestSetForegroundWindow
#define PostMessageW TestPostMessageW
#define TrackPopupMenuEx TestTrackPopupMenuEx
#define ShellExecuteW TestShellExecuteW
#include "../src/main.cpp"

namespace powershift {
StartupManager::StartupManager(std::wstring run, std::wstring settings, std::wstring value)
    : runKey_(std::move(run)), settingsKey_(std::move(settings)), valueName_(std::move(value)) {}
DWORD StartupManager::Initialize(const std::wstring&) {
    if (fixture::state.scenario == fixture::Scenario::InitializationFailure) throw std::bad_alloc();
    return ERROR_SUCCESS;
}
DWORD StartupManager::IsEnabled(bool& enabled) const { enabled = false; return ERROR_SUCCESS; }
DWORD StartupManager::SetEnabled(const std::wstring&, bool) { return ERROR_SUCCESS; }
DWORD ReadActivePlan(GUID& id) { id = {}; id.Data1 = fixture::state.activePlan; return ERROR_SUCCESS; }
DWORD ReadPlanName(const GUID& id, std::wstring& name) { name = L"Plan " + std::to_wstring(id.Data1); return ERROR_SUCCESS; }
DWORD EnumeratePlans(std::vector<PowerPlan>& plans) {
    plans.clear();
    for (unsigned id = 1; id <= 2; ++id) { PowerPlan plan; plan.id.Data1 = id; plan.name = L"Plan " + std::to_wstring(id); plans.push_back(plan); }
    return ERROR_SUCCESS;
}
DWORD ActivatePlan(const GUID&) { return ERROR_SUCCESS; }
DWORD ExecutablePath(std::wstring& path) { path = L"C:\\Moved PowerShift\\PowerShift.exe"; return ERROR_SUCCESS; }
void ShowError(HWND, const std::wstring&, DWORD) { fixture::Need(false, "Unexpected error while exercising the tray"); }
}

int main() {
    try {
        using fixture::Scenario;
        const Scenario scenarios[] = {Scenario::InitializationFailure, Scenario::DelayedShell, Scenario::MovedExecutable,
            Scenario::NotificationFallback, Scenario::ExternalSwitch, Scenario::ExplorerRestart, Scenario::PowerOptions, Scenario::About};
        for (const auto scenario : scenarios) {
            fixture::state = {};
            auto& state = fixture::state;
            state.scenario = scenario;
            const int result = wWinMain(nullptr, nullptr, nullptr, 0);
            fixture::Need(result == (scenario == Scenario::InitializationFailure ? 1 : 0), "Unexpected tray exit status");
            fixture::Need(!state.windowAlive && !state.notificationAlive && !state.iconAlive && state.userData == 0 && state.timers.empty(),
                "All resources must be cleaned up on normal and exceptional exit");
            if (scenario == Scenario::InitializationFailure) fixture::Need(state.errorAfterCleanup, "The error dialog must run after window cleanup");
            if (scenario == Scenario::DelayedShell) {
                fixture::Need(state.ticks == 33 && state.clock > 60000, "Keep waiting for a Shell that starts after one minute");
                fixture::Need(state.retryDelays.front() == 2000 && state.retryDelays.back() == 30000 &&
                    *std::max_element(state.retryDelays.begin(), state.retryDelays.end()) == 30000, "Use bounded exponential backoff");
            }
            if (fixture::UsesLegacy()) {
                fixture::Need(state.legacyQuery && state.menuOutsidePanel && state.popups == 1,
                    "Fallback identity must support keyboard positioning, full panel exclusion and nonrecursive menus");
                if (scenario == Scenario::ExplorerRestart) fixture::Need(state.adds == 3, "Restore the selected legacy identity after Explorer restart");
            }
            if (scenario == Scenario::NotificationFallback || scenario == Scenario::ExternalSwitch)
                fixture::Need(state.tooltip.find(L"Plan 2") != std::wstring::npos, "Refresh the externally selected plan");
            if (scenario == Scenario::PowerOptions) fixture::Need(state.powerOptionsOpened, "Open the official Windows power options");
            if (scenario == Scenario::About) fixture::Need(state.aboutText.find(powershift::kAppTitle) != std::wstring::npos &&
                state.aboutText.find(powershift::kBuildRevision) != std::wstring::npos, "About must show the generated version and build identity");
            if (!state.failures.empty()) throw std::runtime_error(state.failures.front());
            if (scenario != Scenario::NotificationFallback &&
                (state.subscriptions.empty() || !IsEqualGUID(state.subscriptions.front(), GUID_ACTIVE_POWERSCHEME)))
                throw std::runtime_error("Subscribe to actual plan changes, including plans of the same personality");
        }
        std::cout << "PASS: initialization cleanup, slow Shell recovery, moved EXE fallback, keyboard panel exclusion, menu reentrancy, external plan refresh, Explorer restart and native menu actions\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
