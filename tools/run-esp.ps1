# run-esp.ps1 -- convenience launcher for the packaged exe.
#
# CS2ESP.exe already carries the driver and the mapper, so this script only
# starts it (the exe raises its own UAC prompt).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\run-esp.ps1
#   powershell ... -File tools\run-esp.ps1 -SelfTest
#   powershell ... -File tools\run-esp.ps1 -DriverOnly
param(
    [switch]$SelfTest,
    [switch]$DriverOnly,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'cs2ext_port\CS2_External-master\x64\Release\CS2ESP.exe'

if (-not (Test-Path $exe)) {
    throw "missing $exe -- build it with tools\build-cs2ext.ps1"
}

$argv = @()
if ($SelfTest)   { $argv += '--selftest' }
if ($DriverOnly) { $argv += '--driver-only' }
if ($Force)      { $argv += '--force-driver' }

if ($argv.Count -gt 0) {
    & $exe @argv
    exit $LASTEXITCODE
}

Write-Host "starting $exe (UAC prompt follows if not elevated)"
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe -Parent)
Write-Host 'HOME toggles the menu. Start CS2 if it is not running yet.'
