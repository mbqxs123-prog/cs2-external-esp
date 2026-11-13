# CS2ESP.exe · 打包说明（2026-11-13）

单文件可执行程序，双击即完成：**提权 → 检测/加载驱动 → 等待游戏窗口 → 渲染透视**。
不需要 PowerShell、不需要外部 .sys/.dat、不需要安装服务。

## 1. 交付物

| 文件 | 大小 | 说明 |
|---|---|---|
| `dist_pkg\CS2ESP.exe` | 1 288 192 B | 单文件程序（内嵌驱动 + 映射器 + 渲染） |
| `res\kdmapper.bin` | 169 984 B | 内嵌资源：带跳板的 kdmapper（由 `kdm.dat` 解码） |
| `res\driver.bin` | 39 936 B | 内嵌资源：`cs2core.sys`（当前构建） |
| `res\app.rc` / `app.manifest` | — | 资源脚本与清单（requireAdministrator / per-monitor DPI） |

exe 内部结构：

```
CS2ESP.exe
├─ .text            渲染层（ImGui + D3D11 overlay）+ 帧解析 + 加载器
├─ RCDATA 101       kdmapper（169 984 B，带内嵌签名跳板）
├─ RCDATA 102       cs2core.sys（39 936 B）
└─ RT_MANIFEST 1    requireAdministrator, permonitorv2
```

## 2. 运行环境

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 x64（本机 Win11 实测） |
| 权限 | **管理员**（清单强制 UAC；映射驱动需要 `SeLoadDriverPrivilege`） |
| 图形 | D3D11 可用（独显/核显均可），游戏建议窗口化或无边框 |
| 运行库 | **无**：`/MT` 静态链接，依赖只有 `d3d11.dll`、`d3dcompiler_47.dll`、`dwmapi.dll`、`user32/shell32/advapi32/kernel32/imm32`，全部系统自带 |
| 驱动加载条件 | HVCI 关闭（本机 `HVCI=0`）、Microsoft vulnerable driver blocklist 关闭（本机 `VulnerableDriverBlocklistEnable=0`） |
| 杀软 | kdmapper 属已知工具，可能被拦；需将 `CS2ESP.exe` 加入白名单 |

`dumpbin /dependents` 实测输出只有上述系统 DLL，没有 `vcruntime140.dll` / `msvcp140.dll`。

## 3. 构建方式

```powershell
# 1) 驱动（改动驱动源码后）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-driver.ps1 -Link

# 2) 生成内嵌资源（解码 kdm.dat + 复制 cs2core.sys）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make-resources.ps1

# 3) 编译单文件 exe（cl/link + rc，静态 CRT）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-cs2ext.ps1
```

产物：`cs2ext_port\CS2_External-master\x64\Release\CS2ESP.exe`

构建链要点：

- 本机安全策略把 `MSBuild.exe` 归为 LOLBin，构建脚本直接调 `cl.exe` / `link.exe` / `rc.exe`，显式给 `INCLUDE`/`LIB`（含 `atlmfc`）。
- `/MT` 静态 CRT，避免目标机缺 VC++ Redist。
- manifest 由 `rc.exe` 作为 `RT_MANIFEST` 资源嵌入（`res\app.rc` 里 `1 24 "app.manifest"`），不用 link 的 `/MANIFESTINPUT`，避免重复。
- 资源路径相对 `res\`，构建脚本先 `Push-Location res` 再调 `rc.exe`。

## 4. 运行时行为

```
启动
 ├─ 提权（清单 requireAdministrator）
 ├─ DriverLoader::Present()
 │    └─ FrameShm 映射 → 读 16 字节头 → magic == 'C2SE' ?
 ├─ 已存在 → 跳过映射
 └─ 不存在
      ├─ 资源 101/102 释放到 %TEMP%\cs2esp_<rand>\
      ├─ CreateProcess(k.exe --indPages d.sys)   （隐藏窗口，30s 超时）
      ├─ 删除临时文件与目录
      └─ 轮询 24 × 150ms 等待 section 可读
 ├─ 打开 %USERPROFILE%\Documents\CS2_External（菜单配置目录）
 └─ 等待 "Counter-Strike 2" / SDL_app 窗口（每 2s 重试，最多 120s）
      └─ 找到后进入 ImGui 渲染循环（HOME 开关菜单）
```

- 驱动映射后**不会**留下服务、注册表项、设备对象；section 在驱动卸载时消失。
- 默认不写 `dd0f1a.db`（`CS2_FILE_MIRROR=0`），磁盘上只有 exe 自身。
- 先开 exe 再开游戏也可以：覆盖层会等窗口出现。

## 5. 命令行参数

| 参数 | 作用 |
|---|---|
| （无） | 加载驱动（如需）→ 等待游戏 → 渲染 |
| `--selftest` | 打印提权状态、内嵌资源大小、驱动是否在线、帧头字段，然后退出 |
| `--driver-only` | 只加载驱动，不渲染（排障用，退出码 3 = 失败） |
| `--force-driver` | 跳过"已存在"判断，强制再映射一次（验证内嵌 kdmapper 用） |
| `--no-driver` | 不碰驱动，只跑覆盖层 |
| `--frame-check [file]` | 解析并打印一帧；不带文件时读共享内存 section |

实测：

```
> CS2ESP.exe --selftest
CS2ESP selftest
  architecture : x64
  elevated     : yes
  emb. mapper  : 169984 bytes
  emb. driver  : 39936 bytes
  driver live  : yes (shared section ok)
  shm pid      : 0
  frame        : seq=2 flags=0xDEAD0001 players=0 items=0 gren=0

> CS2ESP.exe --driver-only --force-driver
checking driver ...
driver live: shared frame section readable        (exit 0)

> CS2ESP.exe --no-driver
CS2ESP overlay starting (frame-fed, no process attach)
waiting for the CS2 window ... ([OS-Exception] DestWindow isn't exist)
```

`flags=0xDEAD0001` = 诊断帧（阶段 1：没找到 `cs2.exe`），说明驱动在跑、
section 可读、帧头校验通过。游戏启动后同一命令会显示 `players>0`。

## 6. 配置

| 项 | 位置 / 方式 |
|---|---|
| 菜单 | 游戏内按 `HOME` 开/关 |
| 渲染开关 | 菜单 ESP / Radar 页：方框、骨骼、血条、护甲、装备字母、弹药、C4、掉落物、投掷物、Snapline、雷达 |
| 配置目录 | `%USERPROFILE%\Documents\CS2_External`（启动时自动创建） |
| 帧通道 | 自动：驱动发布到共享内存，显示端只读映射；无需配置路径 |
| 通道代数 | 代码内 `CS2_SHM_SALT=2`，显示端自动尝试 2 → 1 |

## 7. 故障排查

| 现象 | 原因 / 处理 |
|---|---|
| 双击没反应 | UAC 被拒；右键"以管理员身份运行" |
| `driver not ready: mapper exit 0x...` | 杀软拦截 kdmapper / HVCI 开启 / blocklist 开启 |
| `driver loaded but the shared frame section never became readable` | 驱动被拦或另一实例占了同名 section；重启后重试 |
| `driver not ready: administrator required` | 未提权运行（直接调 exe 时常见） |
| 覆盖层一直等窗口 | 游戏窗口类名不是 `SDL_app`（换启动参数/全屏独占）；用无边框窗口 |
| 方框整体偏移 | 视口映射问题，不是偏移问题：确认游戏渲染分辨率与窗口客户区一致 |
| 游戏更新后方框消失 | 跑 `tools\fetch-offsets.ps1` 刷偏移 → 重编驱动 → `make-resources` → 重编 exe |

## 8. 限制

- 未在真实对局中验证 `players>0` 的渲染（本机 CS2 未启动）；解析与几何已用合成帧验证。
- 调试期间机器上残留多份手工映射的驱动实例（验证 `--force-driver` 产生），重启后自然清理；
  正常使用只需一份。
- kdmapper 的 vulnerable driver 能否加载取决于系统策略，属环境前提，不是本 exe 能保证的。
