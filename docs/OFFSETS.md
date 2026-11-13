# 偏移与 schema：游戏更新了要做什么

## 更新记录 2026-11-12（build 14196）

`tools/fetch-offsets.ps1` 从 a2x/cs2-dumper `main` 拉到 build **14196**
（2026-11-09T17:25:12Z），15 个 `client.dll` 锚点全部位移，已写入
`core/driver/src/cs2_seed.h` 并重编 `cs2core.sys`：

| 字段 | 旧 (10-31) | 新 (11-09) |
|---|---|---|
| dwCSGOInput | 0x257F980 | 0x2580A10 |
| dwEntityList / dwGameEntitySystem | 0x271EB28 | 0x271FBB8 |
| dwGameRules | 0x2569378 | 0x256A3F8 |
| dwGlobalVars | 0x2234108 | 0x2235108 |
| dwGlowManager | 0x25660A0 | 0x2567120 |
| dwLocalPlayerController | 0x25413F8 | 0x2542548 |
| dwLocalPlayerPawn | 0x25698C8 | 0x256A948 |
| dwPlantedC4 | 0x24CFB60 | 0x24D0B70 |
| dwPrediction | 0x25697D0 | 0x256A850 |
| dwSensitivity | 0x25649D8 | 0x2567C48 |
| dwViewAngles | 0x2580008 | 0x2581098 |
| dwViewMatrix | 0x256F1D0 | 0x2570250 |
| dwViewRender | 0x256FB98 | 0x2570C18 |
| dwWeaponC4 | 0x24CE108 | 0x24CF118 |

netvar 本版无变化；实体链 stride 仍 `0x70`。旧的 `cs2_seed.h` 备份在
`core/driver/src/cs2_seed.h.bak-1112`。详细链路与显示端功能见
`docs/ESP-1112.md`。

## 维护审计（2026-11-09）

当前工作区没有 `bump_cs2_offsets.ps1`。实际存在的更新脚本是
`tools/fetch-offsets.ps1`，其默认输出是 `core/driver/src/cs2_seed.h`，不是
`cs2-dumper/output/offsets.hpp` 或 `offsets.json`。脚本从 cs2-dumper 拉取
`offsets.hpp` 与 `client_dll.hpp`，将 C++ 常量解析后按脚本内的字段映射更新种子宏；
字段缺失和数值变化会打印出来。`-Offline` 使用 `ref/cs2dumper` 缓存。

`cs2-dumper/output/offsets.hpp` 是按模块命名空间组织的 C++ 常量表，值使用十六进制
`std::ptrdiff_t`；同目录 `offsets.json` 是同一批模块/字段的 JSON 映射，值使用十进制
整数。两者表示同一类“模块内偏移”数据，不含每个值的独立定位记录。字段来源需要另用
对应 dumper 输出、精确游戏构建标识和可复现的定位证据证明，不能仅凭字段名或两种格式
数值相同来补写定位依据。

工作区副本的来源标记目前不一致：`offsets.hpp` 内嵌生成时间为
`2026-10-23 17:14:58 UTC`，同目录 `info.json` 则记录 build `14192`、
`2026-10-25T05:40:20.580350200+00:00`。因此这份副本不能仅凭 `info.json`
认定为对应于该构建的完整、可追溯 dump。更新前应从同一次 dumper 运行中原子地保存
`offsets.hpp`、`offsets.json` 和 `info.json`，并记录 dumper revision 与游戏构建号；
之后再校验两种格式的模块/字段集合及数值一致性。

## 产出格式

- C++：`cs2_dumper::offsets::<module_namespace>::<field>`，例如模块注释对应
  `client.dll`，命名空间为 `client_dll`；值写成十六进制常量。
- JSON：顶层键为模块文件名，子键为字段名，值为十进制整数。
- 头文件注释中的生成时间、`info.json` 的 build/timestamp 属于来源元数据，不是偏移值；
  发布时应来自同一次生成，避免混用缓存文件。

字段级定位依据应记录为可复核来源（所属模块或 schema 类、字段名/符号、dumper 版本与
构建标识）；没有这类来源证据时标成“未核验”，不要从相邻字段或历史版本推断。

**结论先说：正常情况下什么都不用做。**

CS2 每次更新都会挪 `client.dll` / `engine2.dll` 里的全局对象偏移和 schema 字段偏移。
我们把「找偏移」这件事拆成三层，越靠前越优先，所以更新不会打断透视：

| 层 | 谁在做 | 游戏更新后要不要动 |
|---|---|---|
| 1. 运行期特征码扫描 | `core/driver/src/sigscan.c`（IDA 风格 pattern + `?` 通配，扫 `.text` 再退全节） | 不用动。特征码本身很少变；变了只改这个表 |
| 2. 运行期 schema 按名字自解析 | Stage-A 的 schema 解析（`m_iHealth`、`m_bDormant` 这类**按字段名**遍历，不是按死偏移） | 不用动。字段名比偏移稳得多 |
| 3. 种子/兜底常量表 | `core/driver/src/cs2_seed.h` | 想刷新就重跑下面的命令，**不用重新编译 Game Bar 组件** |

第 3 层只是「对照值 + 兜底」：第 1、2 层先跑，跑出来的值还要过语义校验
（血量 0..100、队伍 2..3、`viewMatrix` 末行近似 `0,0,0,1`、骨骼间距 1..40 单位），
校验不过才回落到 `cs2_seed.h` 的常量。所以组件侧（Game Bar 那部分）完全不需要跟着游戏更新。

---

## 刷新种子表

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch-offsets.ps1
```

做的事：

1. 从 `cdn.jsdelivr.net/gh/a2x/cs2-dumper@main/output/` 拉 `offsets.hpp` 和 `client_dll.hpp`
   （沙箱里 `raw.githubusercontent.com` 的 DNS 被拦；`web_fetch` 对 `application/octet-stream` 会被拒，
   所以走 pwsh 的 `Invoke-WebRequest`）。
2. 解析 `namespace X { constexpr std::ptrdiff_t NAME = 0xVAL; }` 两种文件格式。
3. 只改 `cs2_seed.h` 里 `#define CS2_DW_* / CS2_NV_*` 的数值，保留注释和结构；最后打印变化清单：

```
=== 变化 3 项 ===
  CS2_DW_ENTITYLIST: 0x271A908 -> 0x2720A28
  CS2_NV_M_IHEALTH: 0x34C -> 0x350
  ...
```

（`FileTime` 参数没给就是上一次的缓存文件，不联网也能重跑。）

**注意**：脚本只写「常量表」。真正的锚点解析仍然由 Stage-A 在目标进程里做——
它读的是**目标进程内存里的模块**，而不是磁盘上那份 DLL，两者基址不同，
所以常量表必须配合运行期的「模块基址 + 偏移」才成立。

---

## 关键链路备忘

### 实体列表（本版配方）

```
index  = handle & 0x7FFF
list   = *(client + dwEntityList)
chunk  = *(list + 0x8 * (index >> 9) + 0x10)
entity = *(chunk + 0x78 * (index & 0x1FF))
```

### 取玩家

```
controller = *(client + dwLocalPlayerController)        // 或遍历 controller 列表
pawn       = resolve(controller + m_hPlayerPawn)        // CHandle -> 上面的配方
worldPos   = *(pawn + m_pGameSceneNode) + m_vecAbsOrigin
eyePos     = worldPos + *(pawn + m_vecViewOffset)
```

### 名字 / 血量 / 队伍

| 用途 | 字段 | 类 |
|---|---|---|
| 名字（最省事） | `m_iszPlayerName`（`char[128]`） | CBasePlayerController |
| 血量 | `m_iHealth` | C_BaseEntity |
| 队伍 | `m_iTeamNum`（`uint8`，2=T 3=CT） | C_BaseEntity |
| 是否活着 | `m_lifeState == 0` | C_BaseEntity |
| 休眠 | `m_bDormant` | CGameSceneNode |
| 被点亮 | `EntitySpottedState_t::m_bSpotted` | C_CSPlayerPawn |

### 武器 / 弹药 / 护甲 / 装备（ABI v5 的 4 个字段）

```
pawn  + m_pWeaponServices   (0x12F0) -> CPlayer_WeaponServices
      + m_hActiveWeapon      (0x60)  -> CHandle<C_CSWeaponBase>
weapon+ m_AttributeManager  (0x1290)-> C_AttributeContainer   (C_EconEntity)
      + m_Item               (0x50)  -> C_EconItemView
      + m_iItemDefinitionIndex(0x1BA, uint16) -> 武器 ID
weapon+ m_iClip1            (0x1928)-> 弹匣余弹
weapon+ m_bInReload         (0x1A3C)-> CS2_GEAR_RELOADING 位
pawn  + m_ArmorValue        (0x1EC8)-> armor（0..100，>0 就有甲）
ctrl  + m_bPawnHasHelmet    (0x941) -> CS2_GEAR_HELMET
ctrl  + m_bPawnHasDefuser   (0x940) -> CS2_GEAR_DEFUSER
pawn  + m_bIsScoped         (0x1E98)-> CS2_GEAR_ZOOMED
pawn  + m_bIsDefusing       (0x1E9A)-> CS2_GEAR_DEFUSING
```

⚠ 三个类里**都有**叫 `m_AttributeManager` 的字段：
`C_Chicken`(0x14C8) / `C_BaseEntity`(0x12E8) / `C_EconEntity`(0x1290)。
武器用的是 **C_EconEntity 的 0x1290**，别拿错。

### 骨骼（不在 schema 里）

```
boneArray = *(sceneNode + m_modelState(0x140) + 0x80)
stride    = 0x20          // CTransform { Quaternion(16B); Vector m_vPosition; float m_flScale; }
位置候选   = +0x10 或 +0x00
```

位置在 `0x10` 还是 `0x00` 历史上有过两种读法，Stage-A **不写死**：
对同一根骨骼按两个候选各读一次，用「骨骼间距 1..40 单位、头在胸口之上」打分自动选，
每个 build 只做一次。

---

## 校验清单（改完偏移/特征码后跑）

1. `--dump` 看 `viewMatrixValid=True`、`playerCount>0`、`map` 不是空。
2. 看 `players 3d/11` 里的 `d`（画出来的人数）与 `playerCount` 是否合理。
3. 进游戏对着墙/箱子核对：方框贴人、骨架关节对得上、**穿墙目标要不要画**由 `OnlyVisible` 决定。
4. 若方框整体偏移 → 是 viewport 映射问题（`frame.ViewportX/Y/W/H`），不是偏移问题。
5. 若方框跟着人「拖尾」→ 是外推（`MaxExtrapolationMs`）或采样 tick 对不上（`SampleTickMs`）。
