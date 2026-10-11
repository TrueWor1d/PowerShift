# PowerShift 验证记录

## V1.1：替换发布

- 用户提供 V1.1 截图，确认托盘折叠面板仍遮住「开机启动」下半部分及「退出」。V1.1 的临时置顶方案未解决此场景。
- 移除隐藏主窗口的临时置顶处理。在切换前台窗口前，读取托盘图标及其所在顶层窗口的屏幕范围。
- 使用 `TrackPopupMenuEx` 的 `TPMPARAMS.rcExclude` 避开整个托盘折叠面板或任务栏，从面板边缘展开菜单；使用 `TPM_WORKAREA` 限制在当前显示器工作区内。
- 已通过 MinGW-w64 Release 编译及 `-Wall -Wextra -Wpedantic -Wconversion -Werror` 检查。正式 ZIP 由 Windows 发布工作流使用 MSVC 构建，执行集成测试并核对 EXE 版本后替换原附件。
- 按用户要求保持 V1.1 / 1.1.0.0，更新原 `v1.1` 标签和 Release。发布说明仅列出相对 V1.0 的菜单定位修改。
- 当前环境无法复现 Windows 10/11 的真实托盘折叠面板，界面效果需要桌面实测。

## V1.1：首次发布方案

- 用户反馈 V1.0 右键菜单底部的「开机启动」「退出」被部分遮挡，并建议采用置顶方式处理。
- 修改为在菜单显示期间使用 `SetWindowPos(HWND_TOPMOST)` 临时置顶菜单所属的隐藏窗口；菜单关闭后恢复之前的层级，再执行所选命令。
- 源码版本信息已更新为 V1.1 / 1.1.0.0。
- 已使用 MinGW-w64 GCC 14-posix 交叉编译，并通过 `-Wall -Wextra -Wpedantic -Wconversion -Werror` 检查。
- 使用 Wine 执行隔离注册表测试，通过首次运行配置启动、中文及空格路径引用、移动路径更新、关闭后保持关闭及外部删除等检查。
- Release 工作流使用 Windows runner 和 MSVC 构建对应标签，执行真实电源 API 读取及隔离注册表集成测试，核对 EXE 产品版本后生成并上传 ZIP。每次运行结果见仓库 Actions 页面。
- 用户桌面截图已确认：托盘折叠面板中的菜单仍被遮挡。编译及自动化测试通过不代表该界面问题修复成功。

## V1.0

### 已完成

- 用户已试用本次交付 EXE，并反馈「使用正常」。此反馈作为基本使用验证；未提供具体 Windows 版本或各项专项测试记录。
- 用户验证的 `PowerShift.exe` SHA-256：`757358506ca71d0125add33e5a0b3a4a388e4b5d6039589fe4b28eb86f84a53b`。
- 在 Linux x64 环境使用 MinGW-w64 GCC 14-posix 和 CMake 4.4.4 交叉编译 Windows x64 GUI 程序及 Windows 集成测试程序。
- 使用 `-Wall -Wextra -Wpedantic -Wconversion -Werror` 构建通过，交付程序采用 Release 优化及静态 GCC/C++ 运行库。
- 检查 PE 文件：x64、GUI 子系统、文件版本 `1.0.0.0`、产品版本 `V1.0`。
- 检查内嵌清单：普通用户权限 `asInvoker`、Common Controls v6、Per-Monitor V2 DPI 设置。
- 检查多尺寸图标资源和程序导入表：依赖仅为 Windows 系统 DLL，没有额外 GCC、C++ 或线程运行库 DLL。
- 使用 Wine 10.0 执行 `PowerShiftTests.exe --registry-only`，通过：首次运行配置启动、中文及空格路径正确引用、已启用项目移动路径更新、关闭后不自动恢复、外部删除后不恢复、无关项目保持不变、非法路径不会更改状态。测试只写入隔离测试注册表路径。
- [Windows CI 构建](https://github.com/TrueWor1d/PowerShift/actions/runs/38154937428)已通过 MSVC 构建及默认 Windows 集成测试。补齐的 V1.0 Release ZIP 由发布工作流从原始 `v1.0` 标签重新构建。

### 尚未完成

- 不同 Windows 版本覆盖、重新登录启动、资源管理器重启、睡眠恢复及缩放比例等专项验收，尚未收到逐项测试记录。
- Wine 完整测试在 `PowerGetActiveScheme` 返回 `ERROR_CALL_NOT_IMPLEMENTED`（120）后停止。Wine 不提供本环境所需的真实 Windows 电源管理功能，此结果不作为 Windows 实机验证通过的证据。

Windows 上运行 `scripts/build.ps1 -Test` 可执行默认的真实 API 读取及隔离注册表测试；在测试机执行 `PowerShiftTests.exe --switch` 可检查切换并恢复原计划。专项验收方法见 README。
