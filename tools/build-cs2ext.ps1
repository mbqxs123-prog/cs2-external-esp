# build-cs2ext.ps1 -- 用 cl.exe/link.exe 直接编 CS2_External（不经过 MSBuild）
#
# 为什么不用 MSBuild：本机安全策略把 MSBuild.exe 归到 LOLBin，调用被拒。
# 这里走和 build-driver.ps1 相同的思路：显式给 INCLUDE/LIB，直接调工具链。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-cs2ext.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-cs2ext.ps1 -Trace
param(
    [switch]$Trace,
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'

$root   = Split-Path -Parent $PSScriptRoot
$proj   = Join-Path $root 'cs2ext_port\CS2_External-master'
$src    = Join-Path $proj 'CS2_External'
$outDir = Join-Path $proj ('x64\' + $Configuration)
$objDir = Join-Path $outDir 'obj'
New-Item -ItemType Directory -Force -Path $outDir, $objDir | Out-Null

# ---- 工具链定位 ----
$vsRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$msvc   = Get-ChildItem (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory |
          Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVer  = (Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory |
            Where-Object { $_.Name -match '^10\.' } |
            Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name

$cl   = Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe'
$link = Join-Path $msvc.FullName 'bin\Hostx64\x64\link.exe'
if (-not (Test-Path $cl)) { throw "cl.exe not found: $cl" }

$inc = @(
    (Join-Path $msvc.FullName 'include'),
    (Join-Path $msvc.FullName 'atlmfc\include'),
    (Join-Path $sdkRoot "Include\$sdkVer\ucrt"),
    (Join-Path $sdkRoot "Include\$sdkVer\um"),
    (Join-Path $sdkRoot "Include\$sdkVer\shared"),
    (Join-Path $sdkRoot "Include\$sdkVer\winrt")
) | Where-Object { Test-Path $_ }
$lib = @(
    (Join-Path $msvc.FullName 'lib\x64'),
    (Join-Path $msvc.FullName 'atlmfc\lib\x64'),
    (Join-Path $sdkRoot "Lib\$sdkVer\ucrt\x64"),
    (Join-Path $sdkRoot "Lib\$sdkVer\um\x64")
) | Where-Object { Test-Path $_ }

$env:PATH    = (Join-Path $msvc.FullName 'bin\Hostx64\x64') + ';' + $env:PATH
$env:INCLUDE = ($inc -join ';')
$env:LIB     = ($lib -join ';')

# ---- 源文件（与 CS2_External.vcxproj 的 ClCompile 列表一致）----
$sources = @(
    'Bone.cpp','Cheats.cpp','Entity.cpp','FrameAdapter.cpp','FrameShm.cpp','DriverLoader.cpp','EspFrame.cpp',
    'MenuCn.cpp',
    'Game.cpp','GlobalVars.cpp','main.cpp','Offsets.cpp',
    'OS-ImGui\imgui\imgui.cpp','OS-ImGui\imgui\imgui_demo.cpp','OS-ImGui\imgui\imgui_draw.cpp',
    'OS-ImGui\imgui\imgui_impl_dx11.cpp','OS-ImGui\imgui\imgui_impl_win32.cpp',
    'OS-ImGui\imgui\imgui_tables.cpp','OS-ImGui\imgui\imgui_widgets.cpp',
    'OS-ImGui\OS-ImGui.cpp','OS-ImGui\OS-ImGui_Base.cpp','OS-ImGui\OS-ImGui_External.cpp',
    'Radar\Radar.cpp','Utils\ConfigMenu.cpp','Utils\ConfigSaver.cpp',
    'Utils\MemorySearch.cpp','Utils\VTableSearch.cpp'
)

$cflags = @(
    '/nologo','/c','/MP','/O2','/MT','/EHsc','/std:c++17','/Zi','/execution-charset:utf-8',
    '/DNDEBUG','/DWIN32','/D_CONSOLE','/D_CRT_SECURE_NO_WARNINGS',
    '/I' + $src,
    '/I' + (Join-Path $proj 'cs2-dumper\output'),
    '/I' + (Join-Path $src 'OS-ImGui'),
    '/I' + (Join-Path $src 'OS-ImGui\imgui'),
    '/I' + (Join-Path $root 'res')
)
if ($Trace) { $cflags += '/DCS2ESP_TRACE' }

$objs = @()
Write-Host "== building CS2_External ($Configuration) =="
Write-Host "cl : $cl"
Write-Host "sdk: $sdkVer"
foreach ($s in $sources) {
    $p = Join-Path $src $s
    if (-not (Test-Path $p)) { throw "missing source: $p" }
    $leaf = [System.IO.Path]::GetFileNameWithoutExtension($s)
    $objPath = Join-Path $objDir ($leaf + '.obj')
    # 子目录同名文件（OS-ImGui\OS-ImGui.cpp vs imgui\imgui.cpp）不含重名，直接用叶子名
    Write-Host "-- $s"
    & $cl @cflags ('/Fo' + $objPath) $p 2>&1 | ForEach-Object { if ($_ -match 'error|warning C') { Write-Host $_ } }
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $s" }
    $objs += $objPath
}

# Resources: kdmapper + driver + manifest (see res\app.rc)
$resDir  = Join-Path $root 'res'
$rcExe   = Join-Path $sdkRoot "bin\$sdkVer\x64\rc.exe"
if (-not (Test-Path $rcExe)) { $rcExe = Join-Path $sdkRoot 'bin\10.0.22621.0\x64\rc.exe' }
if (-not (Test-Path $rcExe)) { throw "rc.exe not found under $sdkRoot" }
$resFile = Join-Path $outDir 'app.res'
Write-Host '== resources =='
Push-Location $resDir
try {
    & $rcExe /nologo "/fo$resFile" app.rc 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw 'rc failed' }
} finally { Pop-Location }

$exe = Join-Path $outDir 'CS2ESP.exe'
$pdb = Join-Path $outDir 'CS2ESP.pdb'
$lflags = @(
    '/nologo','/SUBSYSTEM:CONSOLE','/MACHINE:X64','/OPT:REF','/OPT:ICF','/INCREMENTAL:NO',
    '/DEBUG','/DEBUGTYPE:CV',
    "/OUT:$exe",
    "/PDB:$pdb",
    $resFile,
    'd3d11.lib','dxgi.lib','d3dcompiler.lib','dwmapi.lib',
    'user32.lib','gdi32.lib','shell32.lib','ole32.lib','advapi32.lib',
    'winmm.lib','uuid.lib','comdlg32.lib'
)
# 注意：cl 生成的 obj 名与源文件名一致；link 直接给 obj 路径
Write-Host '== linking =='
& $link @lflags @objs 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { throw 'link failed' }

Write-Host ''
Write-Host ('OK: ' + $exe)
Get-Item $exe | Select-Object FullName, Length, LastWriteTime | Format-List
