# make-resources.ps1 -- build the RCDATA blobs embedded in CS2ESP.exe
#
#   res\kdmapper.bin  = XOR-decoded dist_release_v16\driver\kdm.dat
#                       (kdmapper build with the embedded signed trampoline)
#   res\driver.bin    = core\driver\build\cs2core.sys (current build)
#
# Both are embedded as RCDATA so the shipped exe is self-contained: double-click
# -> UAC -> map driver -> overlay. Nothing else needs to sit next to it.
param(
    [string]$Driver = '',
    [string]$KdmDat = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Driver)) { $Driver = Join-Path $root 'core\driver\build\cs2core.sys' }
if ([string]::IsNullOrEmpty($KdmDat)) { $KdmDat = Join-Path $root 'dist_release_v16\driver\kdm.dat' }
$outDir = Join-Path $root 'res'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (-not (Test-Path $Driver)) { throw "missing driver: $Driver (run tools\build-driver.ps1 -Link)" }
if (-not (Test-Path $KdmDat)) { throw "missing kdm.dat: $KdmDat" }

# kdm.dat is a single-byte XOR blob; key = (0x5A + len % 251) & 0xFF
$blob = [IO.File]::ReadAllBytes($KdmDat)
$k = [byte]((0x5A + ($blob.Length % 251)) -band 0xFF)
$dec = New-Object byte[] $blob.Length
for ($i = 0; $i -lt $blob.Length; $i++) { $dec[$i] = $blob[$i] -bxor $k }
if ($dec[0] -ne 0x4D -or $dec[1] -ne 0x5A) { throw 'kdmapper blob has no MZ header after decode' }

[IO.File]::WriteAllBytes((Join-Path $outDir 'kdmapper.bin'), $dec)
Copy-Item $Driver (Join-Path $outDir 'driver.bin') -Force

foreach ($f in 'kdmapper.bin', 'driver.bin') {
    $p = Join-Path $outDir $f
    $h = (Get-FileHash $p -Algorithm SHA256).Hash
    "{0,-14} {1,8} bytes  sha256 {2}" -f $f, (Get-Item $p).Length, $h
}
