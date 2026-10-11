# 发布版本

每个版本都在 GitHub Release 中提供可运行的 ZIP，命名为 `PowerShift-V版本号-x64.zip`。README 只引导用户前往 Releases 页面下载。

1. 更新源码、CMake、清单、资源文件及构建脚本中的版本信息。
2. 在 `.github/release-notes/v版本号.md` 中只列出相对上一版本的修改。首版说明首次提供的功能。
3. 完成构建检查及与修改相关的测试后，提交源码，创建对应版本标签并推送，例如 `v1.1`。
4. `release.yml` 在 Windows runner 上检出对应标签，使用 MSVC 构建、运行集成测试、核对 EXE 版本并打包。
5. 工作流将 ZIP 上传到该标签的 Release，并使用对应说明文件作为发布说明。ZIP 上传完成后发布 Release。

流程也会检查历史版本标签，为尚未提供运行 ZIP 的版本补齐 Release。已有正式 Release 的同名 ZIP 会保留。`main` 分支更新也会触发检查，便于补齐遗漏的附件。

发布后核对工作流完成状态、Release 标签、发布说明和 ZIP 附件。最新版本使用最大的版本号标签，历史版本补包不会覆盖其最新版本标记。
