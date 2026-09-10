# KeyboardStats 一键构建（CMake + MinGW + EUI-NEO）
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$cmake = "D:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { Write-Error "未找到 CMake: $cmake"; exit 1 }
$make = "D:\Program Files\mingw64\bin\mingw32-make.exe"
if (-not (Test-Path $make)) { Write-Error "未找到 mingw32-make: $make"; exit 1 }

& $cmake -S $root -B "$root\build" -G "MinGW Makefiles" `
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="$make"
if ($LASTEXITCODE -ne 0) { exit 1 }

& $cmake --build "$root\build" --parallel 8
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host ""
Write-Host "OK -> $root\build\KeyboardStats.exe"
