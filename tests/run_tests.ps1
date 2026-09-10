# Regression tests: data layer (test_query) + autostart (test_autostart)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot   # project root (this script lives in tests/)
$gxx = "D:\Program Files\mingw64\bin\g++.exe"
$out = "$root\build\tests"
New-Item -ItemType Directory -Force -Path $out | Out-Null

# 1) data layer regression (unchanged storage/timeutil; same command as test_query.cpp header)
& $gxx -std=c++17 -O2 -DTEST_MAIN=1 "$root\tests\test_query.cpp" "$root\src\storage.cpp" "$root\src\timeutil.cpp" -o "$out\test_query.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_query compile failed"; exit 1 }
Write-Host "--- test_query ---"
& "$out\test_query.exe"

# 2) autostart regression (new win/autostart.cpp)
& $gxx -std=c++17 -O2 -DUNICODE -D_UNICODE "$root\tests\test_autostart.cpp" "$root\src\win\autostart.cpp" -o "$out\test_autostart.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_autostart compile failed"; exit 1 }
Write-Host "--- test_autostart ---"
& "$out\test_autostart.exe"
