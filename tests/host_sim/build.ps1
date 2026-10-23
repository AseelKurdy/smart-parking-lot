# Builds host_sim.exe from a sketch folder. The sketch itself is never modified: variants are made in a temp copy.
#   -Sketch   path to the .ino (default: ..\..\firmware\smart_parking_v9\smart_parking_v9.ino)
#   -Out      exe to produce
#   -TextMap  build with TEXT_MAP = true   (the Serial Monitor draws the text picture even though the web page runs;
#                                           the automated tests read that picture, because `status` prints nothing else)
#   -NoWeb    build with ENABLE_WEB_VIEW = 0 (the WiFi-off fallback)
#   -ExitLane build with the optional two-lane entrance sensor switched on (exit lane ~22 cm, entry lane ~26 cm)
param(
  [string]$Sketch = "",
  [Parameter(Mandatory = $true)][string]$Out,
  [switch]$TextMap, [switch]$NoWeb, [switch]$ExitLane
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($Sketch -eq "") { $Sketch = Join-Path $here '..\..\firmware\smart_parking_v9\smart_parking_v9.ino' }
$Sketch = (Resolve-Path $Sketch).Path
$work = Join-Path $env:TEMP ("parking_host_sim_" + [IO.Path]::GetFileNameWithoutExtension($Out))
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
Copy-Item (Join-Path (Split-Path $Sketch) '*') $work -Include '*.ino', '*.h'
$ino = Join-Path $work (Split-Path $Sketch -Leaf)
$text = [IO.File]::ReadAllText($ino, [Text.Encoding]::UTF8)
if ($TextMap) {
  $n = $text.Replace('const bool TEXT_MAP = false;', 'const bool TEXT_MAP = true;')
  if ($n -eq $text) { throw 'TEXT_MAP line not found in the sketch' }
  $text = $n
}
if ($NoWeb) {
  $n = $text.Replace('#define ENABLE_WEB_VIEW 1', '#define ENABLE_WEB_VIEW 0')
  if ($n -eq $text) { throw 'ENABLE_WEB_VIEW line not found in the sketch' }
  $text = $n
}
if ($ExitLane) {
  # wall 30, entry lane 26, exit lane 22 -> a car is anything below 28
  $n = $text
  foreach ($kv in @(@('GATE_ENTRY_CM', '26.0'), @('GATE_EXIT_CM', '22.0'), @('GATE_CAR_BELOW_CM', '28.0'))) {
    $re = "const float $($kv[0])\s*=\s*[\d.]+;"
    if (-not [regex]::IsMatch($n, $re)) { throw "$($kv[0]) line not found in the sketch" }
    $n = [regex]::Replace($n, $re, "const float $($kv[0]) = $($kv[1]);")
  }
  $text = $n
}
[IO.File]::WriteAllText($ino, $text, (New-Object Text.UTF8Encoding($false)))
$inc = ($ino -replace '\\', '/')
Set-Content -Path (Join-Path $work 'sketch_include.h') -Value ('#include "' + $inc + '"') -Encoding ASCII
$src = $here
& g++ -std=gnu++17 -O1 -Wall -Wextra -Wno-unused-parameter -I $src -I $work `
  (Join-Path $src 'host_sim.cpp') (Join-Path $src 'os_io.cpp') -o $Out
if ($LASTEXITCODE -ne 0) { throw "g++ failed ($LASTEXITCODE)" }
Write-Output "built $Out"
