# 打包 release 压缩包。
#
# 为什么需要这个脚本：运行时 assets 缺失是**静默**失败的——框架在
#   resolveDefaultUiFontPath() 里找不到 <exe目录>\assets\JingNanJunJunTi-*.ttf 时
#   会退回系统字体（Segoe UI / simhei，组件默认族名干脆就是 "Microsoft YaHei"），
#   于是"开发机能跑、别人机器上字体变微软雅黑"。手工打包很容易只带上项目自己的
#   assets\（里面只有 icon.png），把框架 assets\ 里的字体漏掉——v1.0/v1.0.1 就是这么漏的。
#
# 所以本脚本：① 整份复制 build\assets（开发机验证过能跑的那一套，不做裁剪）；
#            ② 打包前**强制校验**关键运行时资源存在，缺任何一个直接失败；
#            ③ 打印包内清单与体积，方便核对。
param(
    [string]$Version = 'v1.0.1',
    [string]$Name = '',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot      # 本脚本就在仓库根目录（与 build.ps1 同级）
$utf8 = New-Object Text.UTF8Encoding($false)

if (-not $Name) { $Name = "KeyboardStats-$Version-win64" }

# ── 1) 构建（可选）──
if (-not $SkipBuild) {
    $cmake = "D:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (-not (Test-Path $cmake)) { throw "未找到 CMake: $cmake" }
    & $cmake --build "$root\build" --parallel 8
    if ($LASTEXITCODE -ne 0) { throw "构建失败" }
}

$exe     = Join-Path $root 'build\KeyboardStats.exe'
$assets  = Join-Path $root 'build\assets'
if (-not (Test-Path $exe))    { throw "找不到产物: $exe" }
if (-not (Test-Path $assets)) { throw "找不到运行时资源目录: $assets" }

# ── 2) 关键运行时资源校验（缺了就打包失败，而不是等用户看到微软雅黑）──
# 名称来自框架 core/render/text.cpp 的 kDefaultUiFontFile / kDefaultIconFontFile
# 与 src/recorder.cpp 的托盘图标 assets\icon.png
$required = @(
    'JingNanJunJunTi-JinNanJunJunTi-Bold-2.ttf',   # kDefaultUiFontFile：界面默认字体（中文）
    'Font Awesome 7 Free-Solid-900.otf',          # kDefaultIconFontFile：图标字体
    'icon.png'                                    # 托盘/窗口图标
)
$missing = @()
foreach ($f in $required) {
    if (-not (Test-Path (Join-Path $assets $f))) { $missing += $f }
}
if ($missing.Count) {
    throw ("运行时资源缺失，拒绝打包：`n  " + ($missing -join "`n  ") +
           "`n（这些文件由 CMake 的 eui_neo_copy_assets 从 .vendor/eui-neo/assets 部署到 build/assets；" +
           "先完整构建一次再打包）")
}

# ── 3) 组装暂存目录（扁平结构，与历史 release 一致：exe + assets + 文档）──
$stage = Join-Path $root "build\release\$Name"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item $exe (Join-Path $stage 'KeyboardStats.exe')
Copy-Item $assets (Join-Path $stage 'assets') -Recurse
foreach ($doc in 'THIRD-PARTY-NOTICES.md', 'README.md') {
    $p = Join-Path $root $doc
    if (Test-Path $p) { Copy-Item $p $stage }
}
$guide = Join-Path $root 'docs\GUIDE.md'
if (Test-Path $guide) { Copy-Item $guide $stage }

# ── 4) 打包后再校验一次包内容（防止压缩环节出岔子）──
# 自己写条目而不是 ZipFile.CreateFromDirectory：后者在 .NET Framework 下把分隔符写成
# 反斜杠（不合 ZIP 规范），部分解压工具会把 "assets\icon.png" 当成单个文件名丢在根目录。
$zip = Join-Path $root "$Name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Add-Type -AssemblyName System.IO.Compression            # ZipArchive
Add-Type -AssemblyName System.IO.Compression.FileSystem # ZipFile（读回校验用）

$zipStream = [IO.File]::Open($zip, [IO.FileMode]::Create)
$archive = New-Object IO.Compression.ZipArchive($zipStream, [IO.Compression.ZipArchiveMode]::Create)
foreach ($f in (Get-ChildItem $stage -Recurse -File)) {
    $rel = ($f.FullName.Substring($stage.Length + 1)) -replace '\\', '/'
    $entry = $archive.CreateEntry($rel, [IO.Compression.CompressionLevel]::Optimal)
    $es = $entry.Open()
    $fs = [IO.File]::OpenRead($f.FullName)
    try { $fs.CopyTo($es) } finally { $fs.Dispose(); $es.Dispose() }
}
$archive.Dispose()
$zipStream.Dispose()

$z = [IO.Compression.ZipFile]::OpenRead($zip)
$entries = $z.Entries | Sort-Object FullName
$names = @($entries | ForEach-Object { $_.FullName })
$sizes = @{}
foreach ($e in $entries) { $sizes[$e.FullName] = $e.Length }
$z.Dispose()

$gone = @()
foreach ($f in $required) {
    if ($names -notcontains "assets/$f") { $gone += $f }
}
$assetCount = ($names | Where-Object { $_ -like 'assets/*' }).Count
if ($gone.Count) { throw ("压缩包内缺少运行时资源：" + ($gone -join ', ')) }

# ── 5) 报告 ──
$zi = Get-Item $zip
Write-Host ""
Write-Host "包内清单（assets 共 $assetCount 项）："
$names | Where-Object { $_ -notlike 'assets/shaders/*' } |
    ForEach-Object { '  {0,10:N0}  {1}' -f $sizes[$_], $_ }
$shaderN = ($names | Where-Object { $_ -like 'assets/shaders/*' }).Count
if ($shaderN) { Write-Host ("  ...另有 assets/shaders/ 下 {0} 项（框架资源，一并带上）" -f $shaderN) }
Write-Host ""
Write-Host ("ZIP : {0}" -f $zip)
Write-Host ("大小: {0:N0} bytes ({1:N2} MB)" -f $zi.Length, ($zi.Length / 1MB))
Write-Host ("SHA256: {0}" -f (Get-FileHash $zip -Algorithm SHA256).Hash)
