# C# 断点调试说明（CoreCLR / 函数指针表模式）

本项目已完成 C# 运行时从 **MonoVM internal call** 到 **CoreCLR 函数指针表**（Godot 4 模式）的迁移：

- 桌面（Windows / Linux / macOS）：**自包含（self-contained）部署**——`dotnet publish -r <rid> --self-contained` 产出的完整 CoreCLR 运行时（hostfxr/hostpolicy/coreclr + 全套 BCL，约 71MB）随游戏部署到 exe 目录，**玩家无需安装 .NET**。宿主用 `hostfxr_initialize_for_dotnet_command_line`（命令行模式，Godot 4 导出版的做法）从 exe 目录启动运行时。
- Android：libmonosgen 的 `coreclr_initialize` + `coreclr_create_delegate`（Godot fallback 路径，需真机验证）。
- 797 个 native 调用走"按名字查函数指针表"，C# 入口是 `[UnmanagedCallersOnly]`（UCO），桌面/Android 共用同一套入口（代码统一）。

## 桌面：Visual Studio F5 直接断点（推荐）

因为程序集跑在真 CoreCLR 的标准 ALC 里，VS 的 `NativeWithManagedCore`（托管 .NET Core）引擎能正确看到 `JoyGame.dll` 模块、绑定 PDB，所以**直接 F5 即可打断点**。

1. 构建 Debug 配置（无优化，断点定位最准）：
   ```powershell
   cmake --build build --config Debug
   ```
2. 用 VS 打开 `build/project_proxy3d.sln`，选 **Debug | x64**，启动项目 `project_proxy3d`。
3. 确认 `JoyGame.dll` 与 `JoyGame.pdb` 在 `build/Debug/`（CMake 自动部署），`Joy.Bindings.dll`/`.pdb` 也在同目录。
4. 在 `CSharp/Runtime/GameApplication.cs` 的 `Update()` 里打断点，直接 **F5**。
5. 调试 → 窗口 → 模块，应能看到 `JoyGame.dll` 且符号状态为"已加载符号"。

调试环境由 CMake 写入 `.vcxproj`（`LocalDebuggerEnvironment`），F5 时自动带上：
```
DOTNET_EnableDiagnostics=1
COMPlus_EnableDiagnostics=1
```
调试器类型为 `NativeWithManagedCore`。ICorDebug 诊断通道由 CoreCLR 自动激活，无需额外设置。

### 运行时前置条件：无（自包含）

桌面**不依赖玩家机器安装任何 .NET**。构建时 CMake 自动执行 `dotnet publish -r <rid> --self-contained`，把完整运行时铺到 exe 目录：

- `hostfxr.dll` / `hostpolicy.dll` / `coreclr.dll` + 全套 `System.*.dll`（约 71MB）与 exe 同目录。
- `JoyGame.runtimeconfig.json` 是**自包含版**（无 `framework` 节点）+ `JoyGame.deps.json`，都在 exe 目录。
- `JoyGame.dll` / `Joy.Bindings.dll` 用 `dotnet build` 的版本（不是 publish 的），保证 PDB 与源码一致、断点可用。

验证过"零系统 .NET 依赖"：把 exe 目录的 `coreclr.dll` 临时改名后，游戏立即报 `Failed to run as a self-contained app` 而启动失败——证明它只用自己的运行时。

**关键机制**：宿主用 `hostfxr_initialize_for_dotnet_command_line`（命令行模式，argv[0] = JoyGame.dll 路径）而非组件模式（`initialize_for_runtime_config`）。只有命令行模式支持自包含；组件模式会报 `Initialization for self-contained components is not supported`。`jcsx.c` 优先命令行模式，失败时回退组件模式（兼容框架依赖部署）。

## 关键实现要点（排障用）

- **单一程序集实例**：宿主必须从 `JoyGame.dll` 取 `GameApplication.Bootstrap`（UCO）来调用 `NativeFuncs.Initialize`，让它与 `Initialize`/`Update`/`Shutdown` 落在同一个 ALC 实例。若从 `Joy.Bindings.dll` 单独取入口，会出现"绑定填在 A、业务调用落在 B，函数指针全 NULL"的 AV（已踩坑并修复）。
- **UCO 不能直接 C# 调用**：`Bootstrap` 是 UCO 供 native 取函数指针；它转调的 `NativeFuncs.Initialize` 是普通 C# 方法。
- **查表名字须 null 结尾**：`NativeFuncs.Lookup` 用 `Marshal.StringToCoTaskMemUTF8`（带 `\0`），不能用 `Encoding.GetBytes`（无终止符）。
- **类型名须程序集限定**：hostfxr 取入口用 `"JoyGame.GameApplication, JoyGame"`。

## Android（需真机验证）

Android 无 hostfxr，走 libmonosgen 的 `coreclr_*` API（`jcsx.c` 的 `csx_init_android`）。C# 入口与桌面共用 UCO（代码统一）。此路径在当前开发环境无法真机验证，以下为必须逐项确认的清单：

1. 确认 `libmonosgen-2.0.so` 导出 `coreclr_initialize` / `coreclr_create_delegate` / `coreclr_shutdown`（已在 Windows MonoVM DLL 上确认导出，Android .so 待核对）。
2. 确认 TPA（`JOY_CLR_LIB_DIR`）包含 System.Private.CoreLib.dll / Joy.Bindings.dll / JoyGame.dll，路径为解压后的真实文件路径。
3. **关键风险点**：`coreclr_create_delegate` 对 `[UnmanagedCallersOnly]` 方法的支持。早期在 Windows MonoVM DLL 上的验证中，`coreclr_create_delegate` 取 UCO 方法曾崩溃（`monovm.c` 断言）——该验证环境的 TPA 配置不完整，结论不成立但未经复验。若真机上 `create_delegate` 取 `Bootstrap`/`Initialize` 崩溃，需改用 Godot 做法：入口退化为普通 `public static` + 配套 delegate 类型（用条件编译区分，桌面仍用 UCO）。
4. 确认 Bootstrap → NativeFuncs.Initialize → 797 查表在 Android ALC 下同样落在单实例。

Android 上的托管断点：CoreCLR/MonoVM 的 diagnostics 在移动端需额外配置（目前未验证），不在本文档保证范围。

## 常见问题

- **VS 模块列表看不到 JoyGame.dll**：确认走的是真 CoreCLR 路径（启动日志有 `CoreCLR initialized via hostfxr`），而非旧的 mono_jit_init。
- **`Failed to run as a self-contained app` / `hostpolicy.dll not found`**：自包含运行时不完整。确认 exe 目录有 `hostfxr.dll`/`hostpolicy.dll`/`coreclr.dll` + 全套 `System.*.dll` + `JoyGame.deps.json` + 自包含版 `JoyGame.runtimeconfig.json`（无 framework 节点）。重新 `cmake --build build --config <Cfg>` 会重跑 publish 部署。
- **`No frameworks were found`**：runtimeconfig 里有 `framework` 节点但没装系统 .NET，且 exe 目录自包含运行时不全。自包含版 runtimeconfig 必须**没有** framework 节点。
- **`Initialization for self-contained components is not supported`**：宿主误用了组件模式（`initialize_for_runtime_config`）。确认 `jcsx.c` 优先走 `hostfxr_initialize_for_dotnet_command_line`。
- **AV at 某个 JmathFp*/Bindings 调用**：函数指针表填到了另一个 ALC 实例。确认 Bootstrap 转发路径与 exe 目录只有一份 `Joy.Bindings.dll`。
- **断点空心 / 未加载符号**：确认 PDB 与 DLL 同目录且同一次构建；Debug 优先；确认 `DOTNET_EnableDiagnostics=1` 生效。
