# External overlay research: kernel memory sampling → shared-memory frame → D3D11 surface

一个**外部渲染**方向的工程实践项目，把下面三件事串成一条可测量的数据通路：

1. 内核侧按固定节奏采样目标进程的**结构化数据**（不注入、不修改目标内存）；
2. 通过一块**共享内存 section** 做零 IPC 调用的帧传递（seqlock + 校验和）；
3. 用户态用 **D3D11 + ImGui** 在独立透明窗口里渲染这块帧。

显示端不打开目标进程、不注入、不挂钩；驱动只用
`PsLookupProcessByProcessId` + `MmCopyVirtualMemory` 读内存，不创建进程句柄。
项目里的"目标进程"是本地运行的 CS2，用来验证这条通路在真实高频数据下的表现。

> 仅用于逆向工程、Windows 内核驱动、图形覆盖层与进程间通信技术的学习与研究。
> 使用者需自行确认所在地区的法律法规与目标软件的服务条款。作者不鼓励、不支持将其用于
> 任何违反服务条款或侵害他人权益的场景。

## 架构

```
cs2.exe
  │  MmCopyVirtualMemory（内核读，无句柄、无注入）
cs2core.sys  （手工映射，无 DriverObject / 无 DeviceObject / 无服务）
  │  60 Hz 采样 → 组帧 → seqlock 发布
Global\{GUID}  共享内存 section（名字由目标 pid 派生）
  │  OpenFileMapping + MapViewOfFile（只读）+ XOR 解码
CS2ESP.exe      D3D11 + ImGui 透明覆盖层
  └─ 方框 / 骨骼 / 血条 / 护甲 / 名字 / 距离 / 武器弹药 / 装备 / C4 / 物品 / 投掷物 / 雷达
```

帧格式（`shared/cs2core_abi.h`）：

| 区间 | 内容 |
|---|---|
| `[0, 10280)` | 头 168 B + 32 × 316 B 玩家（队伍/血量/护甲/装备/名字/坐标/眼位/角度/距离/17 骨骼/速度/武器/弹药） |
| `[10280, 12604)` | 碰撞盒掩码 + `item[64]`（掉落物 / C4 / 拆弹器） |
| `[12604, 13968)` | `gren[24]`（飞行投掷物 + 燃烧区，含半径与起爆时间） |

每段独立 magic + FNV-1a 校验；`seq` 奇数表示正在写，读端丢弃。

## 目录

```
core/driver/            内核采集端（C，WDK）
  src/                  DriverEntry / 采样 / 锚点解析 / 共享内存通道 / 特征码
  inc/                  内部公共头
shared/                 帧 ABI（C 头，两端共用）
cs2ext_port/            显示端（C++17，D3D11 + ImGui）
  CS2_External/         数据解析 + 渲染 + 菜单
res/                    内嵌资源脚本（kdmapper / driver 的 RCDATA 定义 + manifest）
tools/                  构建与诊断脚本
docs/                   架构、偏移、测试、检测面评估
```

## 构建

依赖：Visual Studio 2022（MSVC v143）+ Windows SDK 10 + WDK 头（内核）。

```powershell
# 1) 内核驱动
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-driver.ps1 -Link

# 2) 内嵌资源（需要自备 kdmapper 构建与 cs2core.sys）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\make-resources.ps1

# 3) 单文件 exe（cl/link + rc，静态 CRT）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-cs2ext.ps1
```

产物：`core\driver\build\cs2core.sys`、`cs2ext_port\CS2_External-master\x64\Release\CS2ESP.exe`

偏移更新：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch-offsets.ps1
```

## 运行

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run-esp.ps1
```

详见 `docs/USAGE.md`。

## 诊断

| 命令 | 作用 |
|---|---|
| `CS2ESP.exe --selftest` | 环境 / 内嵌资源 / 驱动 / 帧状态 |
| `CS2ESP.exe --frame-check` | 打印当前帧玩家、物品、投掷物 |
| `python tools\synth-frame.py out.db` | 生成合成帧（校验解析链） |
| `python tools\bone-probe.py <pid>` | 打印原始骨骼数组（按高度排序） |
| `python tools\watch-vm.py <pid> <sec>` | 采样视角矩阵与 FOV 变化 |

## 文档

- `docs/ARCH-1113.md` — 架构、数据流、通道实现
- `docs/OFFSETS.md` — 偏移来源与更新流程
- `docs/TEST-1113.md` — 实机测试步骤
- `docs/USAGE.md` — 使用说明
- `docs/PACKAGE-1113.md` — 打包说明

## 第三方

| 组件 | 许可证 | 说明 |
|---|---|---|
| [CS2_External](https://github.com/TKazer/CS2-External) | MIT | 显示端渲染与菜单基础 |
| [ImGui](https://github.com/ocornut/imgui) | MIT | 界面 |
| [OS-ImGui](https://github.com/TKazer/OS-ImGui) | MIT | D3D11 覆盖层封装 |
| kdmapper | MIT | 驱动映射（二进制不随仓库分发） |

## 免责声明

本项目按 MIT 许可「按原样」提供，不含任何担保。作者不对使用本项目造成的任何后果负责，
包括但不限于账号处罚、数据丢失、系统不稳定或法律纠纷。请勿用于违反目标平台服务条款或
所在地区法律法规的场景。
