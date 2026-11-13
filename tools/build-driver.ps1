# build-driver.ps1 -- build Stage-A kernel driver with MSVC + Windows Kits 10 headers.
# Does NOT use the VS WDK project system (no WDK VSIX installed here); calls cl/link directly.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-driver.ps1
#   powershell ... -File tools\build-driver.ps1 -Link      # also link cs2core.sys
#
# Note: the Windows SDK on this machine is only partially installed, so
# tools\wdk-shim provides the missing SAL headers (specstrings.h / driverspecs.h).
# Keep this file ASCII-only: PowerShell 5.1 mis-parses UTF-8 without BOM.
param(
    [switch]$Link,
    [switch]$Cave,          # include cs2_cave.c (probe only, read-only, safe)
    [switch]$CaveClaim,     # also write a stub into the cave and execute it (may bugcheck)
    [string]$Config = 'Release'
)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot          # esp\
$src  = Join-Path $root 'core\driver\src'
$inc  = Join-Path $root 'core\driver\inc'
$shar = Join-Path $root 'shared'
$shim = Join-Path $PSScriptRoot 'wdk-shim'
$kits = 'C:\Program Files (x86)\Windows Kits\10'
$kver = '10.0.22621.0'
$vs   = 'C:\Program Files\Microsoft Visual Studio\2022\Community'

$toolset = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
if (-not $toolset) { throw 'MSVC toolset not found (VS2022 C++ workload missing?)' }
$clExe  = "$vs\VC\Tools\MSVC\$($toolset.Name)\bin\Hostx64\x64\cl.exe"
$lnkExe = "$vs\VC\Tools\MSVC\$($toolset.Name)\bin\Hostx64\x64\link.exe"
if (-not (Test-Path $clExe))  { throw "cl.exe not found: $clExe" }
if (-not (Test-Path $lnkExe)) { throw "link.exe not found: $lnkExe" }

$kmInc   = "$kits\Include\$kver\km"
$kmCrt   = "$kits\Include\$kver\km\crt"
$shared  = "$kits\Include\$kver\shared"
$ucrt    = "$kits\Include\$kver\ucrt"
$kmLib   = "$kits\Lib\$kver\km\x64"
$kmCrtLib= "$kits\Lib\$kver\km\crt\x64"
$ucrtLib = "$kits\Lib\$kver\ucrt\x64"

$outDir = Join-Path $root 'core\driver\build'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# cs2_cave.c is diagnostic-only (it wrote a marker file to disk at load = a PAC dish-scan hit) -> no longer compiled.
# cs2_strs.c holds the ciphertext literal table produced by tools\gen-strs.ps1.
$sources = @('cs2_main.c','cs2_auto.c','cs2_sampler.c','cs2_channel.c','cs2_file.c','cs2_shm.c','cs2_strs.c','cs2_hide.c',
             'kmem.c','cs2_ptread.c','sigscan.c','cs2_env_stub.c','cs2_lic.c')
# -Cave: diagnostic image used to answer "is there a usable code cave in a signed driver,
# and can we write+execute+restore it". Adds IoAllocateMdl/MmProbeAndLockPages imports and
# a report file, so this image must never be the one that goes onto a game machine.
if ($Cave) { $sources += 'cs2_cave.c' }
# Diagnostic build (optional): swap cs2_env_stub.c for 'cs2_env.c','cs2_env_util.c','cs2_env_clean.c'

$cflags = @(
    '/nologo','/c','/kernel','/GS-','/GR-','/W3','/WX-','/O2','/Oy-','/utf-8',
    '/D_AMD64_','/DAMD64','/D_WIN64','/DPOOL_NX_OPTIN=1',
    "/FI$shared\guiddef.h",
    "/I$kmInc","/I$kmCrt","/I$shared","/I$ucrt",

    "/I$inc","/I$src","/I$shar","/I$shim"
)
if ($Cave) { $cflags += '/DCS2_ENABLE_CAVE'; $cflags += '/DCS2_ENABLE_DIAG' }
if ($CaveClaim) { $cflags += '/DCS2_ENABLE_CAVE_CLAIM' }   # requires -Cave

function Q([string]$a) { if ($a -match '\s') { '"' + $a + '"' } else { $a } }

Write-Host "== building Stage-A kernel driver ($Config) =="
Write-Host "cl  : $clExe"
Write-Host "wdk : $kver"
$objs = @()
$failed = 0
$logPath = Join-Path $outDir 'build-driver.log'
if (Test-Path $logPath) { Remove-Item $logPath -Force }
foreach ($s in $sources) {
    $p = Join-Path $src $s
    $objPath = Join-Path $outDir ([IO.Path]::GetFileNameWithoutExtension($s) + '.obj')
    if (-not (Test-Path $p)) { Write-Host ('  [skip] ' + $s); continue }
    Write-Host ('-- ' + $s)
    $rsp = Join-Path $outDir ([IO.Path]::GetFileNameWithoutExtension($s) + '.rsp')
    $lines = @()
    foreach ($a in $cflags) { $lines += (Q $a) }
    $lines += (Q ('/Fo' + $objPath))
    $lines += (Q $p)
    Set-Content -Path $rsp -Value $lines -Encoding ASCII
    $o = & $clExe ('@' + $rsp) 2>&1
    $code = $LASTEXITCODE
    $o | ForEach-Object { $_; $_ | Out-File -Append -Encoding UTF8 $logPath }
    if ($code -ne 0) { $failed++ }
    $objs += $objPath
}

if ($Link -and $failed -eq 0) {
    $sys = Join-Path $outDir 'cs2core.sys'
    Write-Host "== linking $sys =="
    $lflags = @('/nologo','/DRIVER','/SUBSYSTEM:NATIVE','/ENTRY:DriverEntry',
                '/NODEFAULTLIB','/MACHINE:X64','/OPT:REF','/OPT:ICF','/INCREMENTAL:NO')
    # Merge the small read-only sections into .text so the image occupies fewer
    # pages (SizeOfImage 0xA000 -> 0x7000). Module stomping needs a contiguous
    # zero gap inside a live module; every page we drop makes that far likelier.
    # .data stays separate on purpose: its page must remain RW (the driver writes
    # its globals), and merged sections inherit the first section's protection.
    $lflags += @('/MERGE:.rdata=.text','/MERGE:.pdata=.text','/MERGE:INIT=.text')
    $lflags += ('/LIBPATH:' + $kmLib)
    $lflags += ('/LIBPATH:' + $kmCrtLib)
    $lflags += ('/LIBPATH:' + $ucrtLib)
    $lflags += ('/OUT:' + $sys)
    foreach ($ob in $objs) { $lflags += $ob }
    $lflags += @('ntoskrnl.lib','hal.lib','ntstrsafe.lib','bufferoverflowfastfailk.lib')
    $lrsp = Join-Path $outDir 'link.rsp'
    Set-Content -Path $lrsp -Value ($lflags | ForEach-Object { Q $_ }) -Encoding ASCII
    $o = & $lnkExe ('@' + $lrsp) 2>&1
    $code = $LASTEXITCODE
    $o | ForEach-Object { $_; $_ | Out-File -Append -Encoding UTF8 $logPath }
    if ($code -eq 0) { Write-Host "OK: $sys"; Get-Item $sys | Select-Object FullName,Length }
    else { Write-Host "link failed exit=$code" }
}

Write-Host ""
Write-Host ("files failed: " + $failed)
Write-Host ("log: " + $logPath)




