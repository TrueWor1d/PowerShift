#include "power_plans.h"
#include "startup.h"
#include "ui_text.h"
#include "win32_util.h"
#include "resource.h"

#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <powrprof.h>
#include <algorithm>
#include <iterator>
#include <new>
#include <string_view>

namespace powershift {
namespace {

constexpr wchar_t kWindowClass[] = L"PowerShift.TrayWindow";
constexpr wchar_t kMutexName[] = L"Local\\PowerShift.SingleInstance";
constexpr UINT kTrayCallback = WM_APP + 1;
constexpr UINT kShowMenu = WM_APP + 2;
constexpr UINT_PTR kTrayRetryTimer = 1;
constexpr UINT_PTR kPowerRefreshTimer = 2;
constexpr UINT kInitialRetryDelay = 2000;
constexpr UINT kMaximumRetryDelay = 30000;
constexpr UINT kStartupCommand = 1;
constexpr UINT kExitCommand = 2;
constexpr UINT kPowerErrorCommand = 3;
constexpr UINT kPowerOptionsCommand = 4;
constexpr UINT kAboutCommand = 5;
constexpr UINT kFirstPlanCommand = 1000;
constexpr size_t kMaxMenuPlans = 50000;
constexpr GUID kTrayGuid{0xa059d10e, 0x8b80, 0x4a37,
    {0x9b, 0x8c, 0x57, 0x95, 0x41, 0x18, 0x34, 0xe2}};

struct MenuScope {
    bool& open;
    explicit MenuScope(bool& value) noexcept : open(value) { open = true; }
    ~MenuScope() { open = false; }
    MenuScope(const MenuScope&) = delete;
    MenuScope& operator=(const MenuScope&) = delete;
};

class TrayApp {
public:
    TrayApp(HINSTANCE instance, UINT taskbarMessage, std::wstring executable)
        : instance_(instance), taskbarMessage_(taskbarMessage), executable_(std::move(executable)) {}
    ~TrayApp() noexcept {
        // Destroy the window while all members referenced by its procedure are alive.
        if (window_) DestroyWindow(window_);
        Stop();
    }
    TrayApp(const TrayApp&) = delete;
    TrayApp& operator=(const TrayApp&) = delete;

    bool Start() {
        HICON icon = nullptr;
        const HRESULT loaded = LoadIconMetric(instance_, MAKEINTRESOURCEW(IDI_POWERSHIFT), LIM_SMALL, &icon);
        icon_.reset(icon);
        if (FAILED(loaded) || !icon_) {
            ShowError(window_, L"无法加载托盘图标。", ERROR_RESOURCE_DATA_NOT_FOUND);
            return false;
        }
        notification_.cbSize = sizeof(notification_);
        notification_.hWnd = window_;
        notification_.uID = 1;
        notification_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_GUID | NIF_SHOWTIP;
        notification_.uCallbackMessage = kTrayCallback;
        notification_.hIcon = icon_.get();
        notification_.guidItem = kTrayGuid;
        UpdateTooltip();
        TryAddIcon();
        powerNotification_ = RegisterPowerSettingNotification(window_, &GUID_ACTIVE_POWERSCHEME,
            DEVICE_NOTIFY_WINDOW_HANDLE);
        if (!powerNotification_) {
            powerNotification_ = RegisterPowerSettingNotification(window_, &GUID_POWERSCHEME_PERSONALITY,
                DEVICE_NOTIFY_WINDOW_HANDLE);
            // Personality notifications alone miss switches between custom plans of the same type.
            if (!SetTimer(window_, kPowerRefreshTimer, 10000, nullptr))
                OutputDebugStringW(L"PowerShift: could not schedule fallback power status refresh.\n");
        }

        const DWORD startupError = startup_.Initialize(executable_);
        if (startupError != ERROR_SUCCESS)
            ShowError(window_, L"未能配置开机启动。可从托盘菜单重试；电源计划切换仍可使用。", startupError);
        return true;
    }

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
        auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            app = static_cast<TrayApp*>(creation->lpCreateParams);
            app->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (app) {
            try { return app->OnMessage(message, wParam, lParam); }
            catch (const std::bad_alloc&) {
                MessageBoxW(window, L"内存不足，无法完成操作。", kAppTitle, MB_OK | MB_ICONERROR);
            } catch (...) {
                MessageBoxW(window, L"程序遇到异常，请退出后重试。", kAppTitle, MB_OK | MB_ICONERROR);
            }
            if (message == WM_NCCREATE) return FALSE;
            if (message == WM_CREATE) return -1;
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

private:
    void UpdateTooltip(const GUID* knownActive = nullptr, const std::vector<PowerPlan>* knownPlans = nullptr) {
        GUID active{};
        std::wstring name;
        if (knownActive) active = *knownActive;
        const bool readable = knownActive || ReadActivePlan(active) == ERROR_SUCCESS;
        if (readable && knownPlans) {
            const auto found = std::find_if(knownPlans->begin(), knownPlans->end(), [&](const PowerPlan& plan) {
                return IsEqualGUID(plan.id, active) != FALSE;
            });
            if (found != knownPlans->end()) name = found->name;
        }
        if (!readable || (name.empty() && ReadPlanName(active, name) != ERROR_SUCCESS))
            name = L"暂时无法读取";
        std::wstring text = std::wstring(kAppTitle) + L"\n当前电源计划：" + name;
        size_t size = std::min(text.size(), std::size(notification_.szTip) - 1);
        // Avoid cutting a UTF-16 surrogate pair at the tooltip's size limit.
        if (size > 0 && text[size - 1] >= 0xD800 && text[size - 1] <= 0xDBFF) --size;
        lastTooltipRefresh_ = GetTickCount64();
        if (std::wstring_view(notification_.szTip) == std::wstring_view(text.data(), size)) return;
        std::copy_n(text.data(), size, notification_.szTip);
        notification_.szTip[size] = L'\0';
        if (iconAdded_) Shell_NotifyIconW(NIM_MODIFY, &notification_);
    }

    void TryAddIcon() {
        bool added = Shell_NotifyIconW(NIM_ADD, &notification_) != FALSE;
        if (!added && (notification_.uFlags & NIF_GUID)) {
            // An unsigned EXE moved to a new path cannot reuse its old GUID registration.
            NOTIFYICONDATAW fallback = notification_;
            fallback.uFlags &= ~NIF_GUID;
            fallback.guidItem = {};
            if (Shell_NotifyIconW(NIM_ADD, &fallback)) {
                notification_ = fallback;
                added = true;
            }
        }
        if (added) {
            iconAdded_ = true;
            notification_.uVersion = NOTIFYICON_VERSION_4;
            version4_ = Shell_NotifyIconW(NIM_SETVERSION, &notification_) != FALSE;
            retryDelay_ = kInitialRetryDelay;
            KillTimer(window_, kTrayRetryTimer);
        } else {
            iconAdded_ = false;
            if (!SetTimer(window_, kTrayRetryTimer, retryDelay_, nullptr)) {
                const DWORD error = GetLastError();
                ShowError(window_, L"无法等待 Windows 托盘就绪。", error);
                PostMessageW(window_, WM_CLOSE, 0, 0);
            }
            retryDelay_ = std::min(retryDelay_ * 2, kMaximumRetryDelay);
        }
    }

    POINT MenuPosition(POINT point, TPMPARAMS& parameters, UINT& popupFlags) const noexcept {
        NOTIFYICONIDENTIFIER identifier{};
        identifier.cbSize = sizeof(identifier);
        identifier.hWnd = window_;
        identifier.uID = notification_.uID;
        if (notification_.uFlags & NIF_GUID) identifier.guidItem = notification_.guidItem;
        RECT iconRectangle{};
        const bool hasIconRectangle = SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &iconRectangle)) &&
            !IsRectEmpty(&iconRectangle);
        POINT iconPoint = point;
        if (hasIconRectangle) {
            iconPoint = {iconRectangle.left + (iconRectangle.right - iconRectangle.left) / 2,
                iconRectangle.top + (iconRectangle.bottom - iconRectangle.top) / 2};
        } else if (point.x == -1 && point.y == -1) {
            iconPoint = {};
            GetCursorPos(&iconPoint);
        }
        if (point.x == -1 && point.y == -1) point = iconPoint;

        parameters.cbSize = sizeof(parameters);
        parameters.rcExclude = hasIconRectangle ? iconRectangle :
            RECT{iconPoint.x, iconPoint.y, iconPoint.x + 1, iconPoint.y + 1};
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        const bool hasMonitor = GetMonitorInfoW(MonitorFromPoint(iconPoint, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE;

        // Capture the icon's top-level host before SetForegroundWindow can dismiss it.
        // Exclude the whole overflow panel/taskbar, rather than only the icon itself.
        const HWND host = GetAncestor(WindowFromPoint(iconPoint), GA_ROOT);
        RECT hostRectangle{};
        RECT exclusion{};
        if (hasMonitor && host && host != window_ && host != GetDesktopWindow() && host != GetShellWindow() &&
            IsWindowVisible(host) && GetWindowRect(host, &hostRectangle) &&
            PtInRect(&hostRectangle, iconPoint) &&
            IntersectRect(&exclusion, &hostRectangle, &monitor.rcMonitor) &&
            !(exclusion.left <= monitor.rcWork.left && exclusion.top <= monitor.rcWork.top &&
                exclusion.right >= monitor.rcWork.right && exclusion.bottom >= monitor.rcWork.bottom)) {
            parameters.rcExclude = exclusion;
        }

        // Anchor at a panel edge so the complete native menu can open outside it.
        // Windows resolves limited space and side-mounted taskbars using rcExclude.
        point.x = std::clamp(point.x, parameters.rcExclude.left, parameters.rcExclude.right - 1);
        const bool openAbove = !hasMonitor ||
            parameters.rcExclude.top - monitor.rcWork.top >= monitor.rcWork.bottom - parameters.rcExclude.bottom;
        point.y = openAbove ? parameters.rcExclude.top : parameters.rcExclude.bottom;
        popupFlags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_VERTICAL | TPM_WORKAREA |
            (openAbove ? TPM_BOTTOMALIGN : TPM_TOPALIGN) |
            (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
        return point;
    }

    void ShowMenu(POINT point) {
        if (menuOpen_) return;
        MenuScope menuScope(menuOpen_);
        TPMPARAMS parameters{};
        UINT popupFlags = 0;
        point = MenuPosition(point, parameters, popupFlags);
        UniqueMenu menu(CreatePopupMenu());
        if (!menu) { ShowError(window_, L"无法创建托盘菜单。", GetLastError()); return; }
        bool built = true;
        auto append = [&](UINT flags, UINT_PTR id, const wchar_t* label) {
            if (!AppendMenuW(menu.get(), flags, id, label)) built = false;
        };
        append(MF_STRING | MF_GRAYED, 0, kAppTitle);
        append(MF_SEPARATOR, 0, nullptr);

        // This snapshot belongs to this menu, even if notifications arrive while it is open.
        std::vector<PowerPlan> plans;
        DWORD powerError = EnumeratePlans(plans);
        GUID active{};
        const DWORD activeError = ReadActivePlan(active);
        if (powerError == ERROR_SUCCESS) {
            if (plans.empty()) append(MF_STRING | MF_GRAYED, 0, L"系统没有可用的电源计划");
            const size_t count = std::min(plans.size(), kMaxMenuPlans);
            for (size_t index = 0; index < count; ++index) {
                const UINT command = kFirstPlanCommand + static_cast<UINT>(index);
                const std::wstring label = MenuLabel(plans[index].name);
                UINT flags = MF_STRING;
                if (activeError == ERROR_SUCCESS && IsEqualGUID(active, plans[index].id)) flags |= MF_CHECKED;
                append(flags, command, label.c_str());
                MENUITEMINFOW item{};
                item.cbSize = sizeof(item);
                item.fMask = MIIM_FTYPE;
                item.fType = MFT_STRING | MFT_RADIOCHECK;
                if (!SetMenuItemInfoW(menu.get(), command, FALSE, &item)) built = false;
            }
            if (count < plans.size()) append(MF_STRING | MF_GRAYED, 0, L"电源计划过多，部分项目未显示");
            if (activeError != ERROR_SUCCESS) powerError = activeError;
        }
        if (powerError != ERROR_SUCCESS)
            append(MF_STRING, kPowerErrorCommand, L"电源计划读取失败，查看详情…");

        append(MF_SEPARATOR, 0, nullptr);
        bool startupEnabled = false;
        const DWORD startupError = startup_.IsEnabled(startupEnabled);
        UINT startupFlags = MF_STRING | (startupEnabled ? MF_CHECKED : 0);
        if (startupError != ERROR_SUCCESS) startupFlags |= MF_GRAYED;
        append(startupFlags, kStartupCommand,
            startupError == ERROR_SUCCESS ? L"开机启动" : L"开机启动（无法读取）");
        append(MF_STRING, kPowerOptionsCommand, L"电源选项…");
        append(MF_STRING, kAboutCommand, L"关于 PowerShift…");
        append(MF_SEPARATOR, 0, nullptr);
        append(MF_STRING, kExitCommand, L"退出");
        if (!built) { ShowError(window_, L"无法完成托盘菜单。", ERROR_GEN_FAILURE); return; }

        UpdateTooltip(activeError == ERROR_SUCCESS ? &active : nullptr,
            powerError == ERROR_SUCCESS ? &plans : nullptr);
        SetForegroundWindow(window_);
        const UINT selected = static_cast<UINT>(TrackPopupMenuEx(menu.get(), popupFlags,
            point.x, point.y, window_, &parameters));
        PostMessageW(window_, WM_NULL, 0, 0);
        if (iconAdded_) Shell_NotifyIconW(NIM_SETFOCUS, &notification_);

        if (selected >= kFirstPlanCommand) {
            const size_t index = selected - kFirstPlanCommand;
            if (index < std::min(plans.size(), kMaxMenuPlans)) {
                const DWORD error = ActivatePlan(plans[index].id);
                if (error != ERROR_SUCCESS)
                    ShowError(window_, L"未能确认切换到「" + plans[index].name + L"」。请检查当前计划后重试。", error);
                UpdateTooltip();
            }
        } else if (selected == kStartupCommand) {
            bool enabled = false;
            DWORD error = startup_.IsEnabled(enabled);
            if (error == ERROR_SUCCESS) error = startup_.SetEnabled(executable_, !enabled);
            if (error != ERROR_SUCCESS) ShowError(window_, L"无法更改开机启动设置。", error);
        } else if (selected == kPowerErrorCommand) {
            ShowError(window_, L"无法读取 Windows 电源计划。", powerError);
        } else if (selected == kPowerOptionsCommand) {
            wchar_t directory[MAX_PATH]{};
            const UINT length = GetSystemDirectoryW(directory, static_cast<UINT>(std::size(directory)));
            if (!length || length >= std::size(directory)) {
                ShowError(window_, L"无法定位 Windows 电源选项。", length ? ERROR_FILENAME_EXCED_RANGE : GetLastError());
                return;
            }
            const std::wstring control = std::wstring(directory) + L"\\control.exe";
            const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window_, L"open", control.c_str(),
                L"/name Microsoft.PowerOptions", nullptr, SW_SHOWNORMAL));
            if (result <= 32) {
                const DWORD error = result == SE_ERR_FNF ? ERROR_FILE_NOT_FOUND :
                    result == SE_ERR_PNF ? ERROR_PATH_NOT_FOUND :
                    result == SE_ERR_ACCESSDENIED ? ERROR_ACCESS_DENIED : ERROR_OPEN_FAILED;
                ShowError(window_, L"无法打开 Windows 电源选项。", error);
            }
        } else if (selected == kAboutCommand) {
            const std::wstring text = std::wstring(kAppTitle) +
                L"\n\nWindows 原生电源计划托盘工具\n构建标识：" + kBuildRevision;
            MessageBoxW(window_, text.c_str(), kAppTitle, MB_OK | MB_ICONINFORMATION);
        } else if (selected == kExitCommand) {
            DestroyWindow(window_);
        }
    }

    void Stop() noexcept {
        if (window_) {
            KillTimer(window_, kTrayRetryTimer);
            KillTimer(window_, kPowerRefreshTimer);
        }
        if (powerNotification_) {
            UnregisterPowerSettingNotification(powerNotification_);
            powerNotification_ = nullptr;
        }
        if (iconAdded_) {
            Shell_NotifyIconW(NIM_DELETE, &notification_);
            iconAdded_ = false;
        }
    }

    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == taskbarMessage_) {
            iconAdded_ = false;
            version4_ = false;
            retryDelay_ = kInitialRetryDelay;
            UpdateTooltip();
            if (icon_) TryAddIcon();
            return 0;
        }
        switch (message) {
        case kTrayCallback: {
            const UINT event = version4_ ? LOWORD(lParam) : static_cast<UINT>(lParam);
            if (event == WM_MOUSEMOVE && GetTickCount64() - lastTooltipRefresh_ >= 1000)
                UpdateTooltip();
            if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP || event == NIN_KEYSELECT) {
                POINT point{-1, -1};
                if (version4_) point = {GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)};
                else GetCursorPos(&point);
                ShowMenu(point);
            }
            return 0;
        }
        case kShowMenu:
            ShowMenu({-1, -1});
            return 0;
        case WM_TIMER:
            if (wParam == kTrayRetryTimer && !iconAdded_) TryAddIcon();
            if (wParam == kPowerRefreshTimer) UpdateTooltip();
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_POWERSETTINGCHANGE || wParam == PBT_APMRESUMEAUTOMATIC ||
                wParam == PBT_APMRESUMESUSPEND) UpdateTooltip();
            return TRUE;
        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
        case WM_THEMECHANGED:
            if (icon_) {
                HICON replacement = nullptr;
                if (SUCCEEDED(LoadIconMetric(instance_, MAKEINTRESOURCEW(IDI_POWERSHIFT), LIM_SMALL, &replacement))) {
                    icon_.reset(replacement);
                    notification_.hIcon = icon_.get();
                    if (iconAdded_) Shell_NotifyIconW(NIM_MODIFY, &notification_);
                }
            }
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wParam) DestroyWindow(window_);
            return 0;
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;
        case WM_DESTROY:
            Stop();
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY: {
            const HWND destroyed = window_;
            SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
            window_ = nullptr;
            return DefWindowProcW(destroyed, message, wParam, lParam);
        }
        default:
            return DefWindowProcW(window_, message, wParam, lParam);
        }
    }

    HINSTANCE instance_;
    UINT taskbarMessage_;
    std::wstring executable_;
    HWND window_ = nullptr;
    UniqueIcon icon_;
    NOTIFYICONDATAW notification_{};
    HPOWERNOTIFY powerNotification_ = nullptr;
    StartupManager startup_;
    bool iconAdded_ = false;
    bool version4_ = false;
    UINT retryDelay_ = kInitialRetryDelay;
    ULONGLONG lastTooltipRefresh_ = 0;
    bool menuOpen_ = false;
};

int Run(HINSTANCE instance) {
    UniqueHandle singleInstance(CreateMutexW(nullptr, FALSE, kMutexName));
    const DWORD mutexError = GetLastError();
    if (!singleInstance) { ShowError(nullptr, L"无法初始化 PowerShift。", mutexError); return 1; }
    if (mutexError == ERROR_ALREADY_EXISTS) {
        const HWND existing = FindWindowW(kWindowClass, nullptr);
        if (existing) PostMessageW(existing, kShowMenu, 0, 0);
        return 0;
    }

    std::wstring executable;
    const DWORD pathError = ExecutablePath(executable);
    if (pathError != ERROR_SUCCESS) { ShowError(nullptr, L"无法读取程序路径。", pathError); return 1; }
    const UINT taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
    if (!taskbarMessage) { ShowError(nullptr, L"无法监听 Windows 托盘。", GetLastError()); return 1; }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = TrayApp::WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&windowClass)) { ShowError(nullptr, L"无法注册托盘窗口。", GetLastError()); return 1; }

    TrayApp app(instance, taskbarMessage, std::move(executable));
    // A hidden top-level window receives TaskbarCreated broadcasts; HWND_MESSAGE does not.
    const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, kAppTitle,
        WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, &app);
    if (!window) { ShowError(nullptr, L"无法创建托盘窗口。", GetLastError()); return 1; }
    if (!app.Start()) { DestroyWindow(window); return 1; }

    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result == -1) {
        const DWORD error = GetLastError();
        DestroyWindow(window);
        ShowError(nullptr, L"Windows 消息处理失败。", error);
        return 1;
    }
    return static_cast<int>(message.wParam);
}

} // namespace
} // namespace powershift

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try { return powershift::Run(instance); }
    catch (const std::bad_alloc&) {
        MessageBoxW(nullptr, L"内存不足，无法运行 PowerShift。", powershift::kAppTitle, MB_OK | MB_ICONERROR);
    } catch (...) {
        MessageBoxW(nullptr, L"PowerShift 初始化失败，请重新运行。", powershift::kAppTitle, MB_OK | MB_ICONERROR);
    }
    return 1;
}
