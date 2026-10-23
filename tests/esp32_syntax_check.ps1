# Compiles a sketch (syntax / type check only, no linking) with the REAL Xtensa compiler and the REAL Arduino-ESP32
# core headers installed by the Arduino IDE - the same defines, include paths and flag files that platform.txt uses for
# the "ESP32 Dev Module" board. Use it when arduino-cli / the IDE is not at hand: it catches everything the IDE's
# compile step would catch (wrong String/WiFi/WebServer/Serial calls, missing headers, name clashes with the core).
# It does NOT link and does NOT run anything.
#   powershell -File esp32_syntax_check.ps1 [-Sketch <path to .ino>] [-Warnings]
param(
  [string]$Sketch = (Join-Path $PSScriptRoot '..\firmware\smart_parking_v9\smart_parking_v9.ino'),
  [switch]$Warnings                     # show -Wall -Wextra warnings that come from the sketch itself
)
$ErrorActionPreference = 'Stop'
$Sketch = (Resolve-Path $Sketch).Path
$sketchDir = Split-Path $Sketch
$pk = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32'
$core = Join-Path $pk 'hardware\esp32\3.0.1'
$sdk = Get-ChildItem (Join-Path $pk 'tools\esp32-arduino-libs') -Directory | Select-Object -First 1
$sdk = Join-Path $sdk.FullName 'esp32'
$gpp = Get-ChildItem (Join-Path $pk 'tools\esp-x32\2302\bin') -Filter 'xtensa-esp32-elf-g++.exe' | Select-Object -First 1 -ExpandProperty FullName
if (-not (Test-Path $gpp)) { throw "Xtensa compiler not found: $gpp" }

# what the IDE compiles: <Arduino.h> + the sketch (it also inserts function prototypes; this sketch needs none)
$work = Join-Path $env:TEMP 'esp32_syntax_check'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
$main = Join-Path $work 'sketch.ino.cpp'
Set-Content -Path $main -Encoding ASCII -Value @("#include <Arduino.h>", ('#include "' + ($Sketch -replace '\\', '/') + '"'))

$sdkF = $sdk -replace '\\', '/'
$coreF = $core -replace '\\', '/'
$args = @(
  '-c', "@$sdkF/flags/cpp_flags",
  $(if ($Warnings) { '-Wall' } else { '-w' }), $(if ($Warnings) { '-Wextra' } else { '-Os' }), '-Os',
  '-DF_CPU=240000000L', '-DARDUINO=10607', '-DARDUINO_ESP32_DEV', '-DARDUINO_ARCH_ESP32', '-DARDUINO_BOARD="ESP32_DEV"',
  '-DARDUINO_VARIANT="esp32"', '-DARDUINO_PARTITION_default', '-DARDUINO_HOST_OS="windows"', '-DARDUINO_FQBN="esp32:esp32:esp32"',
  '-DESP32', '-DCORE_DEBUG_LEVEL=0', '-DARDUINO_USB_CDC_ON_BOOT=0',
  "@$sdkF/flags/defines", "-I$($sketchDir -replace '\\','/')", '-iprefix', "$sdkF/include/", "@$sdkF/flags/includes", "-I$sdkF/dio_qspi/include",
  "-I$coreF/cores/esp32", "-I$coreF/variants/esp32"
)
foreach ($lib in 'WiFi', 'WebServer', 'Network', 'FS') { $args += "-I$coreF/libraries/$lib/src" }
$args += @('-fsyntax-only', ($main -replace '\\', '/'))

$ErrorActionPreference = 'Continue'     # the compiler writes diagnostics to stderr; that must not abort the script
$out = (& $gpp @args 2>&1 | ForEach-Object { "$_" }) -join "`n"
$code = $LASTEXITCODE
if (-not $Warnings) {
  if ($code -eq 0) { Write-Output "OK: $(Split-Path $Sketch -Leaf) compiles for ESP32 Dev Module (core 3.0.1, xtensa-esp32-elf-g++ 2302), syntax-only." }
  else { Write-Output $out; Write-Output "FAILED (exit $code)"; exit 1 }
} else {
  $mine = ($out -split "`n") | Where-Object { $_ -match [regex]::Escape((Split-Path $Sketch -Leaf)) -or $_ -match 'parking_page' }
  if ($mine) { $mine | ForEach-Object { Write-Output $_ } } else { Write-Output 'no warnings from the sketch itself' }
  Write-Output "exit $code"
  if ($code -ne 0) { exit 1 }
}
