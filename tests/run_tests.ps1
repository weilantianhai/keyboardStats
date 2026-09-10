# Regression tests: data layer (test_query) + autostart (test_autostart) + record management (test_records)
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

# 2.5) 默认数据文件夹解析（不调用 StorageInit，不会碰偏好文件；无环境变量时应落在 exe 同级 keyboardstats）
& $gxx -std=c++17 -O2 "$root\tests\test_default_dir.cpp" "$root\src\storage.cpp" "$root\src\timeutil.cpp" -o "$out\test_default_dir.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_default_dir compile failed"; exit 1 }
Write-Host "--- test_default_dir ---"
& "$out\test_default_dir.exe"

# 3) record management: describe / export jsonl+csv / import / clear
# 该测试最后会 StorageClearAll()，因此必须在独立临时目录运行：
# 用 KEYBOARDSTATS_DIR 指向临时目录，只从真实数据目录"复制"一份种子事件，绝不写入。
Write-Host "--- test_records ---"
$tmpDir = Join-Path $env:TEMP "kbstats-records-test"
New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null
Get-ChildItem -Path $tmpDir -File -ErrorAction SilentlyContinue | Remove-Item -Force

$seed = Join-Path $env:APPDATA "KeyboardStats\events-$((Get-Date).ToString('yyyyMM')).jsonl"
if (Test-Path $seed) { Copy-Item $seed $tmpDir -Force }

& $gxx -std=c++17 -O2 "$root\tests\test_records.cpp" "$root\src\storage.cpp" "$root\src\timeutil.cpp" -o "$out\test_records.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_records compile failed"; exit 1 }

$env:KEYBOARDSTATS_DIR = $tmpDir
# 保险丝：该测试会 StorageClearAll()，若环境变量没生效就会清空真实记录。
# 运行前再确认一次它确实指向临时目录，否则直接中止。
if ($env:KEYBOARDSTATS_DIR -ne $tmpDir) {
    Remove-Item Env:\KEYBOARDSTATS_DIR -ErrorAction SilentlyContinue
    Write-Error "KEYBOARDSTATS_DIR 未生效，拒绝运行 test_records（避免清空真实记录）"
    exit 1
}
try {
    & "$out\test_records.exe"
} finally {
    Remove-Item Env:\KEYBOARDSTATS_DIR -ErrorAction SilentlyContinue
}
