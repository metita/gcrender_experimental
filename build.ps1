# Compila gcrender.asi (Win32) con VS2022 BuildTools.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
cmake -S $root -B $build -G "Visual Studio 17 2022" -A Win32
if ($LASTEXITCODE -ne 0) { throw "cmake configure fallo ($LASTEXITCODE)" }
cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "cmake build fallo ($LASTEXITCODE)" }
$out = Join-Path $build 'Release\gcrender.asi'
if (Test-Path $out) { Write-Host "OK -> $out ($((Get-Item $out).Length) bytes)" }
else { throw "no se genero gcrender.asi" }
