# fetch-offsets.ps1 -- 游戏更新后一条命令刷新 Stage-A 的偏移种子表
#
# 背景
#   CS2 每次更新都会挪 client.dll/engine2.dll 里的全局对象偏移和 schema 字段偏移。
#   我们的策略是三层：
#     1) 运行期特征码扫描 (core/driver/src/sigscan.c) -- 主路径, 不依赖任何常量
#     2) 运行期 schema 按名字自解析 (netvar 优先按字段名走)
#     3) 本文件生成的 cs2_seed.h -- 种子值 / 兜底值 / 特征码失效时的对照
#   所以游戏更新后只要重跑这个脚本刷新第 3 层, **不用重新编译 Game Bar 组件**。
#
# 数据源
#   cs2-dumper (a2x) 的产物。沙箱里 raw.githubusercontent.com 的 DNS 被拦,
#   但 cdn.jsdelivr.net 走 pwsh 的 Invoke-WebRequest 能拿到 (web_fetch 对
#   application/octet-stream 会被拒), 所以下面用 jsDelivr。
#
# 用法
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch-offsets.ps1
#   可选: -OutFile <路径>  -Ref <git ref, 默认 main>  -Quiet
#
# 产物
#   ref\cs2dumper\offsets.hpp / client_dll.hpp          原始 dump (留档)
#   core\driver\src\cs2_seed.h                          刷新后的种子表
#   控制台打印每个变化的字段 (旧值 -> 新值), 便于判断这次更新动了什么

param(
    [string]$OutFile = '',
    [string]$Ref = 'main',
    [switch]$Offline,
    [string]$CacheDir = '',
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($OutFile)) { $OutFile = Join-Path $root 'core\driver\src\cs2_seed.h' }
if ([string]::IsNullOrEmpty($CacheDir)) { $CacheDir = Join-Path $root 'ref\cs2dumper' }
New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null

$base = 'https://cdn.jsdelivr.net/gh/a2x/cs2-dumper@' + $Ref + '/output/'
$files = @{ 'offsets.hpp' = 'offsets.hpp'; 'client_dll.hpp' = 'client_dll.hpp' }

if ($Offline) {
    Write-Host ('离线模式：直接用 ' + $CacheDir + ' 里的 dump（不下载）')
} else {
foreach ($k in $files.Keys) {
    $dest = Join-Path $CacheDir $k
    try {
        Invoke-WebRequest -UseBasicParsing -Uri ($base + $files[$k]) -OutFile $dest -TimeoutSec 60
        Write-Host ("downloaded {0} ({1} bytes)" -f $k, (Get-Item $dest).Length)
    } catch {
        if (Test-Path $dest) {
            Write-Host ("download {0} failed ({1}); 用本地缓存那份" -f $k, $_.Exception.Message)
        } else {
            throw ("拿不到 {0}, 也没有本地缓存: {1}" -f $k, $_.Exception.Message)
        }
    }
}
}

# ---- 解析: "namespace X { constexpr std::ptrdiff_t NAME = 0xVAL; ... }" ----
function Parse-Dump([string]$path) {
    $map = @{}
    $ns = ''
    foreach ($line in [System.IO.File]::ReadAllLines($path)) {
        $t = $line.Trim()
        if ($t -match '^namespace\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{') { $ns = $Matches[1]; continue }
        if ($t -match '^constexpr\s+std::ptrdiff_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;') {
            $name = $Matches[1]
            $val = $Matches[2]
            if ($val -like '0x*') { $v = [uint64]$val } else { $v = [uint64]$val }
            $key = ($ns + '!' + $name)
            if (-not $map.ContainsKey($key)) { $map[$key] = $v }
        }
    }
    return $map
}

$anchorFields = @{}
$netvars = @{}
foreach ($f in 'offsets.hpp', 'client_dll.hpp') {
    $p = Join-Path $CacheDir $f
    $m = Parse-Dump $p
    foreach ($k in $m.Keys) {
        if ($m[$k] -lt 0x100) { $netvars[$k] = $m[$k] } else { $anchorFields[$k] = $m[$k] }
    }
}
$all = @{}
foreach ($k in $anchorFields.Keys) { $all[$k] = $anchorFields[$k] }
foreach ($k in $netvars.Keys) { if (-not $all.ContainsKey($k)) { $all[$k] = $netvars[$k] } }
Write-Host ("parsed: {0} anchors + {1} small offsets" -f $anchorFields.Count, $netvars.Count)

# ---- 宏名 -> dump 来源 ("命名空间!字段") ----
$map = @{
    # ==== 模块锚点 ====
    'CS2_DW_CSGOINPUT'                    = 'client_dll!dwCSGOInput'
    'CS2_DW_ENTITYLIST'                   = 'client_dll!dwEntityList'
    'CS2_DW_GAMEENTITYSYSTEM'             = 'client_dll!dwGameEntitySystem'
    'CS2_DW_GAMEENTITYSYSTEM_HIGHESTIDX'  = 'client_dll!dwGameEntitySystem_highestEntityIndex'
    'CS2_DW_GAMERULES'                    = 'client_dll!dwGameRules'
    'CS2_DW_GLOBALVARS'                   = 'client_dll!dwGlobalVars'
    'CS2_DW_GLOWMANAGER'                  = 'client_dll!dwGlowManager'
    'CS2_DW_LOCALPLAYERCONTROLLER'        = 'client_dll!dwLocalPlayerController'
    'CS2_DW_LOCALPLAYERPAWN'              = 'client_dll!dwLocalPlayerPawn'
    'CS2_DW_PLANTEDC4'                    = 'client_dll!dwPlantedC4'
    'CS2_DW_PREDICTION'                   = 'client_dll!dwPrediction'
    'CS2_DW_SENSITIVITY'                  = 'client_dll!dwSensitivity'
    'CS2_DW_SENSITIVITY_SENSITIVITY'      = 'client_dll!dwSensitivity_sensitivity'
    'CS2_DW_VIEWANGLES'                   = 'client_dll!dwViewAngles'
    'CS2_DW_VIEWMATRIX'                   = 'client_dll!dwViewMatrix'
    'CS2_DW_VIEWRENDER'                   = 'client_dll!dwViewRender'
    'CS2_DW_WEAPONC4'                     = 'client_dll!dwWeaponC4'
    'CS2_DW_BUILDNUMBER'                  = 'engine2_dll!dwBuildNumber'
    'CS2_DW_NETWORKGAMECLIENT'            = 'engine2_dll!dwNetworkGameClient'
    'CS2_DW_NGC_LOCALPLAYER'              = 'engine2_dll!dwNetworkGameClient_localPlayer'
    'CS2_DW_NGC_MAXCLIENTS'               = 'engine2_dll!dwNetworkGameClient_maxClients'
    'CS2_DW_NGC_SIGNONSTATE'              = 'engine2_dll!dwNetworkGameClient_signOnState'
    'CS2_DW_NGC_CLIENTTICKCOUNT'          = 'engine2_dll!dwNetworkGameClient_clientTickCount'
    'CS2_DW_NGC_SERVERTICKCOUNT'          = 'engine2_dll!dwNetworkGameClient_deltaTick'
    'CS2_DW_WINDOWWIDTH'                  = 'engine2_dll!dwWindowWidth'
    'CS2_DW_WINDOWHEIGHT'                 = 'engine2_dll!dwWindowHeight'
    'CS2_DW_INPUTSYSTEM'                  = 'inputsystem_dll!dwInputSystem'
    'CS2_DW_GAMETYPES'                    = 'matchmaking_dll!dwGameTypes'
    'CS2_DW_SOUNDSYSTEM'                  = 'soundsystem_dll!dwSoundSystem'
    # ==== C_BaseEntity ====
    'CS2_NV_M_PGAMESCENENODE'             = 'C_BaseEntity!m_pGameSceneNode'
    'CS2_NV_M_IHEALTH'                    = 'C_BaseEntity!m_iHealth'
    'CS2_NV_M_IMAXHEALTH'                 = 'C_BaseEntity!m_iMaxHealth'
    'CS2_NV_M_LIFESTATE'                  = 'C_BaseEntity!m_lifeState'
    'CS2_NV_M_ITEAMNUM'                   = 'C_BaseEntity!m_iTeamNum'
    'CS2_NV_M_FLAGS'                      = 'C_BaseEntity!m_fFlags'
    'CS2_NV_M_VECABSVELOCITY'             = 'C_BaseEntity!m_vecAbsVelocity'
    'CS2_NV_M_VECVELOCITY'                = 'C_BaseEntity!m_vecVelocity'
    'CS2_NV_M_HOWNERENTITY'               = 'C_BaseEntity!m_hOwnerEntity'
    'CS2_NV_M_MOVETYPE'                   = 'C_BaseEntity!m_MoveType'
    'CS2_NV_M_FLSIMULATIONTIME'           = 'C_BaseEntity!m_flSimulationTime'
    # ==== C_BaseModelEntity / CCollisionProperty ====
    'CS2_NV_M_COLLISION'                  = 'C_BaseModelEntity!m_Collision'
    'CS2_NV_M_VECVIEWOFFSET'              = 'C_BaseModelEntity!m_vecViewOffset'
    'CS2_NV_M_VECMINS'                    = 'CCollisionProperty!m_vecMins'
    'CS2_NV_M_VECMAXS'                    = 'CCollisionProperty!m_vecMaxs'
    'CS2_NV_M_FLBOUNDINGRADIUS'           = 'CCollisionProperty!m_flBoundingRadius'
    # ==== C_BasePlayerPawn ====
    'CS2_NV_M_HCONTROLLER'                = 'C_BasePlayerPawn!m_hController'
    'CS2_NV_M_PWEAPONSERVICES'            = 'C_BasePlayerPawn!m_pWeaponServices'
    'CS2_NV_M_VOLDORIGIN'                 = 'C_BasePlayerPawn!m_vOldOrigin'
    # ==== C_CSPlayerPawn ====
    'CS2_NV_M_ANGEYEANGLES'               = 'C_CSPlayerPawn!m_angEyeAngles'
    'CS2_NV_M_BISSCOPED'                  = 'C_CSPlayerPawn!m_bIsScoped'
    'CS2_NV_M_BISDEFUSING'                = 'C_CSPlayerPawn!m_bIsDefusing'
    'CS2_NV_M_BISWALKING'                 = 'C_CSPlayerPawn!m_bIsWalking'
    'CS2_NV_M_ENTITYSPOTTEDSTATE'         = 'C_CSPlayerPawn!m_entitySpottedState'
    'CS2_NV_M_ISHOTSFIRED'                = 'C_CSPlayerPawn!m_iShotsFired'
    'CS2_NV_M_ARMORVALUE'                 = 'C_CSPlayerPawn!m_ArmorValue'
    'CS2_NV_M_SZLASTPLACENAME'            = 'C_CSPlayerPawn!m_szLastPlaceName'
    'CS2_NV_M_FLFLASHMAXALPHA'            = 'C_CSPlayerPawnBase!m_flFlashMaxAlpha'
    'CS2_NV_M_FLFLASHDURATION'            = 'C_CSPlayerPawnBase!m_flFlashDuration'
    # ==== controller ====
    'CS2_NV_M_HPLAYERPAWN'                = 'CCSPlayerController!m_hPlayerPawn'
    'CS2_NV_M_BPAWNISALIVE'               = 'CCSPlayerController!m_bPawnIsAlive'
    'CS2_NV_M_IPAWNHEALTH'                = 'CCSPlayerController!m_iPawnHealth'
    'CS2_NV_M_BPAWNHASDEFUSER'            = 'CCSPlayerController!m_bPawnHasDefuser'
    'CS2_NV_M_BPAWNHASHELMET'             = 'CCSPlayerController!m_bPawnHasHelmet'
    'CS2_NV_M_IPING'                      = 'CCSPlayerController!m_iPing'
    'CS2_NV_M_ISCORE'                     = 'CCSPlayerController!m_iScore'
    'CS2_NV_M_HOBSERVERPAWN'              = 'CCSPlayerController!m_hObserverPawn'
    'CS2_NV_M_SSANITIZEDNAME'             = 'CCSPlayerController!m_sSanitizedPlayerName'
    'CS2_NV_M_ISZPLAYERNAME'              = 'CBasePlayerController!m_iszPlayerName'
    'CS2_NV_M_NTICKBASE'                  = 'CBasePlayerController!m_nTickBase'
    'CS2_NV_M_BISLOCALPLAYERCTRL'         = 'CBasePlayerController!m_bIsLocalPlayerController'
    # ==== 武器/弹药链 ====
    'CS2_NV_M_ATTRIBUTEMANAGER'           = 'C_EconEntity!m_AttributeManager'
    'CS2_NV_M_ITEM'                       = 'C_AttributeContainer!m_Item'
    'CS2_NV_M_ITEMDEFINITIONINDEX'        = 'C_EconItemView!m_iItemDefinitionIndex'
    'CS2_NV_M_ICLIP1'                     = 'C_BasePlayerWeapon!m_iClip1'
    'CS2_NV_M_BINRELOAD'                  = 'C_CSWeaponBase!m_bInReload'
    # ==== scene node / 骨骼容器 ====
    'CS2_NV_M_POWNER'                     = 'CGameSceneNode!m_pOwner'
    'CS2_NV_M_VECORIGIN'                  = 'CGameSceneNode!m_vecOrigin'
    'CS2_NV_M_VECABSORIGIN'               = 'CGameSceneNode!m_vecAbsOrigin'
    'CS2_NV_M_ANGABSROTATION'             = 'CGameSceneNode!m_angAbsRotation'
    'CS2_NV_M_FLSCALE'                    = 'CGameSceneNode!m_flScale'
    'CS2_NV_M_BDORMANT'                   = 'CGameSceneNode!m_bDormant'
    'CS2_NV_M_MODELSTATE'                 = 'CSkeletonInstance!m_modelState'
    'CS2_NV_M_NHITBOXSET'                 = 'CSkeletonInstance!m_nHitboxSet'
    'CS2_NV_M_HMODEL'                     = 'CModelState!m_hModel'
    'CS2_NV_M_MODELNAME'                  = 'CModelState!m_ModelName'
    'CS2_NV_ES_M_BSPOTTED'                = 'EntitySpottedState_t!m_bSpotted'
    'CS2_NV_ES_M_BSPOTTEDBYMASK'          = 'EntitySpottedState_t!m_bSpottedByMask'
    # ==== 运动/武器服务/C4 ====
    'CS2_NV_M_FLDUCKAMOUNT'               = 'CCSPlayer_MovementServices!m_flDuckAmount'
    'CS2_NV_M_BDUCKING'                   = 'CCSPlayer_MovementServices!m_bDucking'
    'CS2_NV_M_HACTIVEWEAPON'              = 'CPlayer_WeaponServices!m_hActiveWeapon'
    'CS2_NV_M_BBOMBTICKING'               = 'C_PlantedC4!m_bBombTicking'
    'CS2_NV_M_FLCA4BLOW'                  = 'C_PlantedC4!m_flC4Blow'
    'CS2_NV_M_ISLOCALPLAYER'              = 'CCSPlayerBase_CameraServices!m_iFOV'
}

# ---- 生成新的 cs2_seed.h: 只改 #define 行里的数值, 保留结构与注释 ----
if (-not (Test-Path $OutFile)) { throw ("找不到 $OutFile, 请先有基线文件") }
$lines = [System.IO.File]::ReadAllLines($OutFile, [System.Text.Encoding]::UTF8)
$changed = @()
$miss = @()
$used = @{}
for ($i = 0; $i -lt $lines.Count; $i++) {
    $L = $lines[$i]
    if ($L -match '^\s*#define\s+(CS2_(?:DW|NV|ES)_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)u?\s*(.*)$') {
        $def = $Matches[1]; $old = $Matches[2]; $tail = $Matches[3]
        if ($map.ContainsKey($def)) {
            $src = $map[$def]
            if ($all.ContainsKey($src)) {
                $new = $all[$src]
                $used[$def] = $true
                if ([uint64]$old -ne $new) {
                    $changed += ('{0}: 0x{1:X} -> 0x{2:X}' -f $def, [uint64]$old, $new)
                }
                $lines[$i] = ('#define {0,-36} 0x{1:X}u{2}' -f $def, $new, $(if ($tail) { '   ' + $tail } else { '' }))
                continue
            } else {
                $miss += ('{0}  (dump 里没找到 {1})' -f $def, $src)
            }
        }
    }
}
foreach ($k in $map.Keys) { if (-not $used.ContainsKey($k)) { $miss += ('{0}  (原文件里没有这个 #define)' -f $k) } }

# 更新时间戳/来源注释
for ($i = 0; $i -lt 40 -and $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^#define\s+CS2_SEED_GENERATED') {
        $lines[$i] = ('#define CS2_SEED_GENERATED  "' + (Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm:ss') + ' UTC (fetch-offsets.ps1)"')
    }
}

[System.IO.File]::WriteAllLines($OutFile, $lines, (New-Object System.Text.UTF8Encoding($false)))

if (-not $Quiet) {
    Write-Host ''
    Write-Host ('=== 变化 {0} 项 ===' -f $changed.Count)
    $changed | ForEach-Object { Write-Host ('  ' + $_) }
    if ($miss.Count -gt 0) {
        Write-Host ''
        Write-Host ('=== 需要注意 {0} 项 (dump 改名/字段没了 -> 运行期靠特征码或 schema 名字解析兜底) ===' -f $miss.Count)
        $miss | ForEach-Object { Write-Host ('  ' + $_) }
    }
    Write-Host ''
    Write-Host ('已写入 ' + $OutFile)
}
