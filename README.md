# PowerShift V1.1

一个 C++17 编写的 Windows 原生电源计划托盘工具。右键托盘图标选择计划，首次运行自动配置当前用户登录后启动。

## 下载

请前往 [Releases] 下载所需版本的 `PowerShift-V版本号-x64.zip`，解压后运行 `PowerShift.exe`。

## 使用

1. 将 `PowerShift.exe` 放在固定位置，例如 `C:\Tools\PowerShift\`，双击运行。
2. 在任务栏右下角找到闪电图标；若在折叠菜单中，可自行拖到任务栏显示。
3. 右键图标，选择电脑现有的电源计划。圆点表示当前计划，悬停图标可查看计划名称。
4. 「开机启动」默认开启，可在菜单中关闭。关闭后再次运行不会自动开启。
5. 「退出」结束程序；当前电源计划继续生效。取消开机启动后退出并删除 EXE 即可停止使用。

程序首次运行会写入当前用户的 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\PowerShift`，并在 `HKCU\Software\PowerShift` 保存首次配置标记。不会创建服务或计划任务。菜单中的开机启动标记表示启动项已登记；若在 Windows 任务管理器中禁用了该项目，还需在那里重新启用。

移动 EXE 后应手动运行新位置的副本，已开启的启动项会更新路径。Windows 的 Run 启动命令有 260 字符限制，路径过长时会报告错误，电源切换仍可使用。

## 功能与兼容目标

- Windows 10/11 x64，普通用户权限运行。
- 使用 `PowerEnumerate` / `PowerReadFriendlyName` / `PowerGetActiveScheme` / `PowerSetActiveScheme`，读取系统已有计划及自定义计划，不创建或修改计划内容。
- 菜单每次打开重新查询；切换后读取当前 GUID 验证结果。
- 使用 `Shell_NotifyIconW` 和原生菜单；资源管理器重启后重新添加图标，登录时托盘尚未就绪会短暂重试。
- Unicode 路径和名称、多尺寸图标、DPI 感知、单实例。
- 电源设置通知和睡眠恢复时更新提示文字；同一类型的自定义计划在外部切换后，提示文字会在下次打开菜单时刷新。

Windows「电源计划」与设置中的「电源模式」是不同的控制项。本版切换电源计划。支持 Modern Standby 的设备可能只提供平衡及其派生计划；菜单以 Windows 返回的实际列表为准。组织策略、用户权限或厂商软件可能限制、覆盖切换结果，程序会报告 API 返回的错误。

## 使用微软工具构建

安装 Visual Studio 2022 的「使用 C++ 的桌面开发」、Windows SDK 和 CMake。打开 PowerShell，在项目目录执行：

```powershell
.\scripts\build.ps1
```

输出为 `dist\PowerShift.exe`。采用 MSVC 静态运行库，不要求额外安装 Visual C++ 运行库。

也可直接执行：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

## 验证

在 Windows 上构建并运行集成测试：

```powershell
.\scripts\build.ps1 -Test
```

测试检查真实电源 API 的读取结果，以及隔离测试注册表路径中的首次启动、中文空格路径引用、移动路径更新、关闭设置的持久性、外部移除和不修改其他项目。默认测试不切换电源计划，也不修改真实开机启动项。

需要测试实际切换时，在测试机上运行以下命令。它会逐个切换可用计划，并在完成或失败时尝试恢复原计划：

```powershell
.\build\Release\PowerShiftTests.exe --switch
```

手动验收：右键切换及控制面板确认、关闭/开启开机启动并重新登录、重复运行、重启资源管理器、睡眠恢复、不同缩放比例、中文自定义名称、普通用户与受策略限制的环境。

## Linux 交叉编译

安装 CMake 和 MinGW-w64：

```sh
cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

MinGW 构建静态链接 GCC/C++ 运行库，运行依赖为 Windows 自带 DLL。各版本的构建工具与验证范围见 `VALIDATION.md`。Windows 上的 MSVC 构建是推荐的后续发布流程。

项目使用 Windows runner 和 MSVC 构建并执行集成测试。发布版本标签后，会生成对应版本的运行 ZIP 并上传到 Release；发布说明仅列出相对上一版本的改动。发布流程见 `RELEASING.md`。

## 官方接口文档

- [电源计划管理](https://learn.microsoft.com/en-us/windows/win32/power/managing-power-schemes)
- [托盘图标](https://learn.microsoft.com/en-us/windows/win32/shell/notification-area)
- [Run / RunOnce 启动项](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys)
