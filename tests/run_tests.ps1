# Regression tests: 数据层(test_query) + 手柄记录(test_gamepad) + 自启动(test_autostart)
#                  + 默认数据目录(test_default_dir) + 记录管理(test_records)
# 说明：test_autostart 采用非破坏式流程——检测到已开启的自启动任务时只做只读校验，绝不删除。
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

# 1.5) 手柄记录回归（键码段隔离 / 分组判定 / 四组归一化 / 数据层往返）
# 不需要真的接手柄；用独立临时目录，避免污染真实数据
Write-Host "--- test_gamepad ---"
$padDir = Join-Path $env:TEMP "kbstats-gamepad-test"
New-Item -ItemType Directory -Force -Path $padDir | Out-Null
Get-ChildItem -Path $padDir -File -ErrorAction SilentlyContinue | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }

& $gxx -std=c++17 -O2 -DUNICODE -D_UNICODE "$root\tests\test_gamepad.cpp" "$root\src\storage.cpp" "$root\src\heatnorm.cpp" "$root\src\timeutil.cpp" -o "$out\test_gamepad.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_gamepad compile failed"; exit 1 }
$env:KEYBOARDSTATS_DIR = $padDir
if ($env:KEYBOARDSTATS_DIR -ne $padDir) { Write-Error "KEYBOARDSTATS_DIR 未生效"; exit 1 }
try { & "$out\test_gamepad.exe" } finally { Remove-Item Env:\KEYBOARDSTATS_DIR -ErrorAction SilentlyContinue }
if (Test-Path Env:\KEYBOARDSTATS_DIR) { Write-Error "KEYBOARDSTATS_DIR 未能清除，会影响后续用例"; exit 1 }

# 1.7) 手柄模拟量实时性：SharedPadAnalogAlive 与 (KeyAlive||PadAnalogAlive) 重绘条件
# 链接 hook.cpp + gamepad.cpp，走真实共享内存，不做桩替换
Write-Host "--- test_padanalog_alive ---"
& $gxx -std=c++17 -O2 -DUNICODE -D_UNICODE "$root\tests\test_padanalog_alive.cpp" "$root\src\hook.cpp" "$root\src\gamepad.cpp" "$root\src\storage.cpp" "$root\src\timeutil.cpp" -o "$out\test_padanalog_alive.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_padanalog_alive compile failed"; exit 1 }
& "$out\test_padanalog_alive.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_padanalog_alive failed"; exit 1 }

# 1.8) 共享内存版本回退必须能自愈（v2 残留 → v3 出现后要能自动升上来）
# 这条守的是"摇杆为什么不跟着动"的根因：旧实现命中 v2 后永不重试，
# 等于永久降级——面板读不到模拟量，而分数仍走磁盘照涨，极易误判成渲染问题。
# 用例会临时造一个 Local\KeyboardStats.KeyState2 模拟残留旧进程，结束时自动销毁。
Write-Host "--- test_shared_upgrade ---"
& $gxx -std=c++17 -O2 -DUNICODE -D_UNICODE "$root\tests\test_shared_upgrade.cpp" "$root\src\hook.cpp" "$root\src\gamepad.cpp" "$root\src\storage.cpp" "$root\src\timeutil.cpp" -o "$out\test_shared_upgrade.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_shared_upgrade compile failed"; exit 1 }
& "$out\test_shared_upgrade.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_shared_upgrade failed"; exit 1 }

# 1.9) 摇杆/扳机必须取自"第一个已连接手柄"、且必须按值留下来
# 这条守的是本轮真正的根因：旧代码在轮询循环里存 &st.Gamepad（指向循环内每轮
# 重新构造的局部变量），循环结束后读到的恒为全 0 —— 面板内圈与行程柱永远不动，
# 而按键计数与行程累计都在循环内当场算完，于是"按键有反应、分数照涨、摇杆不动"，
# 极具误导性。PollOnceWith 可注入假 XInput，故无手柄机器也能端到端验证。
Write-Host "--- test_padfirst ---"
& $gxx -std=c++17 -O2 -DUNICODE -D_UNICODE "$root\tests\test_padfirst.cpp" "$root\src\gamepad.cpp" "$root\src\hook.cpp" "$root\src\storage.cpp" "$root\src\timeutil.cpp" -o "$out\test_padfirst.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_padfirst compile failed"; exit 1 }
& "$out\test_padfirst.exe"
if ($LASTEXITCODE -ne 0) { Write-Error "test_padfirst failed"; exit 1 }

# 2) autostart regression (new win/autostart.cpp)
& $gxx -std=c++17 -O2 -DUNICODE -D_UNICODE "$root\tests\test_autostart.cpp" "$root\src\win\autostart.cpp" "$root\src\win\adminmode.cpp" -o "$out\test_autostart.exe"
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
Get-ChildItem -Path $tmpDir -File -ErrorAction SilentlyContinue | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }

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
