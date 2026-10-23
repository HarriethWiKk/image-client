# image-client

Windows 桌面图像生成客户端：C++20 + QML（Qt 6.8.3）。支持 OpenAI 兼容 Images API、xAI Grok Imagine 与 Google Gemini，提供文生图与图生图；同时构建一个 stdio MCP 服务端供本机 agent 拉起。

规格见 [`docs/SPEC-cpp-rebuild.md`](docs/SPEC-cpp-rebuild.md)。它是**唯一事实源**，标注 `[冻结]` 的条目是实现必须满足的行为，不要擅自改动。

## 当前状态

- **GUI**（`ImageClient.exe`）：生成、图生图、历史、设置、灯箱已实现。
- **MCP**（`image-client-mcp.exe`）：目前只有 `--version` / `--smoke`，stdio 协议与工具集尚未实现（SPEC §8）。
- **测试**：17 个套件。`tst_store` 的并发写、`tst_jobmanager` 的并发上限、`tst_assets` 的目录 ACL 三个用例在本机失败，前两个稳定复现，待查。

## 环境要求

- Windows 10/11 x64
- Qt 6.8.3 `msvc2022_64`，需包含 Core / Gui / Qml / Quick / QuickControls2 / Network / Sql / Test / Concurrent
- Visual Studio 2022 或更新（含「使用 C++ 的桌面开发」工作负载）
- CMake ≥ 3.22、Ninja

本机实测基线：Qt 6.8.3、MSVC 14.51（VS 2026）、CMake 4.4.3、Ninja 1.13.2。

## 构建

```bat
set QTDIR=D:\tools\Qt\6.8.3\msvc2022_64
scripts\build.cmd
```

脚本会用 `vswhere` 找到 MSVC 并 `vcvars64`，再以 Ninja + Release 配置构建。若 `cmake.exe` / `ninja.exe` 不在 `PATH` 上，用 `CMAKE_DIR` 指向它们所在目录。

产物直接落在 `build\` 根目录：

- `build\ImageClient.exe` —— GUI（WIN32 子系统）
- `build\image-client-mcp.exe` —— stdio MCP 服务端

注意：**不是** `build\bin`。

## 测试

```bat
scripts\test.cmd
```

等价于在 vcvars64 + Qt `bin` 环境下执行 `ctest --test-dir build --output-on-failure`。Qt 的 `bin` 必须在 `PATH` 上，测试进程需要加载 `Qt6Test.dll` 等运行库。

## 静态检查

```powershell
scripts\lint.ps1
```

对 QML 模块跑 `qmllint`。源目录没有 `qmldir`（构建时生成），脚本会把源码和构建产物里的类型清单拼成一个临时模块再检查，否则 `Theme` / `StatusMap` 会被当成普通类型，报出大量假的 missing-property。当前过滤 `[unqualified]`——控制器还是 context property，qmllint 无法给它们类型；等它们注册成 QML singleton 后应去掉这条过滤。

## 运行

```bat
scripts\run.cmd
```

应用数据（SQLite 库、生成图片、日志）写在 `%LOCALAPPDATA%\image-client\`：

- `library.sqlite3` —— 任务与历史
- `assets\` —— 生成图片
- `gui.log` —— GUI 运行日志（stderr 仍同步输出）

## 目录结构

```
src/core/    协议、网络、存储、凭据、任务状态机（Gui-free 静态库 oic-core）
src/app/     QML 侧控制器与 image:// provider（oic-app）
src/gui/     QML 界面与 main.cpp（ImageClient.exe）
src/mcp/     stdio MCP 服务端（image-client-mcp.exe）
tests/       Qt Test 套件
scripts/     build / test / run 脚本
docs/        规格文档
```

## 架构不变量

- **双可执行文件，不合并**：GUI 的 `qDebug` 一旦混入 MCP 的 stdout，JSON-RPC 帧就损坏了。
- **`mcp` 目标不链接 QtQuick / QtQML / QtGui**，只依赖 QtCore 与 `oic-core`；单实例锁与托盘只在 GUI 目标实现。
- **`core` 保持 Gui-free**（唯一例外是测试 `tst_imageprobe`，它用 QtGui 交叉校验头解析）。
- 源码文件头带 SPDX 标识；对外 Apache-2.0，Qt 按 LGPLv3 动态链接，第三方归属见 `THIRD_PARTY_NOTICES.md`。

## 许可

[Apache-2.0](LICENSE)
