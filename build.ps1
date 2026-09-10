# 一键构建：产出单个静态链接的 KeyboardStats.exe（Win64）
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$files = Get-ChildItem "$root\src" -Filter *.cpp | ForEach-Object { $_.FullName }
if (-not $files) { Write-Error "src 目录下没有 .cpp 文件" }
$out = Join-Path $root 'KeyboardStats.exe'

& g++ -std=c++17 -O2 -municode -mwindows -static -s `
    -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 `
    -o $out @files `
    -lgdi32 -luser32 -lshell32 -ladvapi32 -lcomctl32

if ($LASTEXITCODE -ne 0) {
    Write-Error "编译失败，退出码 $LASTEXITCODE"
    exit 1
}
Write-Host "BUILD OK: $out"
