# ============================================================================
# package.ps1 —— 打一个可分发的 Windows 包（release / beta）
#
# 做四件事：
#   1) 用指定版本与渠道配置 build-release 并构建
#   2) 构建并运行 gameplay_tests（必须全绿）
#   3) 跑 60 局基准与基线对比（可选 -SkipBenchmark）
#   4) 收集 exe + assets + README + version.txt，打 zip
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Version 0.4.0 -Channel beta
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -SkipBenchmark
# ============================================================================

param(
    [string]$Version  = "0.3.0",
    [ValidateSet("release", "beta")]
    [string]$Channel  = "release",
    [string]$OutDir   = "dist",
    [string]$Cmake    = "",
    [switch]$SkipBenchmark
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# 依次尝试：显式参数 → PATH → 本机常见安装位置
$cmake = $Cmake
if (-not $cmake) {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { $cmake = $onPath.Source }
}
if (-not $cmake) {
    $candidates = @(
        "D:\cmake\cmake-4.3.2-windows-x86_64\bin\cmake.exe",
        "C:\Program Files\CMake\bin\cmake.exe"
    )
    foreach ($c in $candidates) { if (Test-Path $c) { $cmake = $c; break } }
}
if (-not $cmake -or -not (Test-Path $cmake)) {
    throw "找不到 cmake，请用 -Cmake 指定完整路径"
}
Write-Host "cmake: $cmake"

function Step([string]$text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }

# ---- 1) 配置 + 构建 release ------------------------------------------------
Step "配置 build-release ($Version-$Channel)"
& $cmake -S PixelEngine -B build-release -G "MinGW Makefiles" `
    -DCMAKE_BUILD_TYPE=Release "-DPE_VERSION=$Version" "-DPE_CHANNEL=$Channel" `
    "-DFETCHCONTENT_SOURCE_DIR_SDL2=$PWD\build-release\_deps\sdl2-src" `
    "-DFETCHCONTENT_SOURCE_DIR_SDL2_MIXER=$PWD\build-release\_deps\sdl2_mixer-src" `
    "-DFETCHCONTENT_SOURCE_DIR_SDL2_IMAGE=$PWD\build-release\_deps\sdl2_image-src"
if ($LASTEXITCODE -ne 0) { throw "配置失败" }

Step "构建 EngineApp"
& $cmake --build build-release --parallel 8
if ($LASTEXITCODE -ne 0) { throw "构建失败" }

# ---- 2) 回归测试 ------------------------------------------------------------
Step "构建并运行 gameplay_tests"
& $cmake -S PixelEngine -B build-tests -G "MinGW Makefiles" `
    -DCMAKE_BUILD_TYPE=Release "-DPE_VERSION=$Version" "-DPE_CHANNEL=release" `
    -DBUILD_GAMEPLAY_TESTS=ON `
    "-DFETCHCONTENT_SOURCE_DIR_SDL2=$PWD\build-release\_deps\sdl2-src" `
    "-DFETCHCONTENT_SOURCE_DIR_SDL2_MIXER=$PWD\build-release\_deps\sdl2_mixer-src" `
    "-DFETCHCONTENT_SOURCE_DIR_SDL2_IMAGE=$PWD\build-release\_deps\sdl2_image-src"
& $cmake --build build-tests --parallel 8 --target gameplay_tests
$testsExe = "build-tests\gameplay_tests.exe"
if (-not (Test-Path $testsExe)) { throw "找不到 $testsExe" }
$testOut = & $testsExe 2>&1
$testOut | Select-String -Pattern "\[FAIL\]|结果:" | ForEach-Object { Write-Host $_.Line }
if ($LASTEXITCODE -ne 0 -or ($testOut | Select-String -Pattern "\[FAIL\]")) {
    throw "回归测试未全绿，停止打包"
}

# ---- 3) 基准对比（可选）----------------------------------------------------
if (-not $SkipBenchmark) {
    Step "跑 60 局基准并对比基线"
    $benchDir = "build-evidence\package-bench"
    if (Test-Path $benchDir) { Remove-Item $benchDir -Recurse -Force }
    & "build-dev\dev_replay_runner.exe" --ticks=150000 `
        --maps=dust_2,mirage,ancient_ruins --seeds=1-20 `
        --out=$benchDir "--tag=$Version-$Channel" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "基准运行失败" }
    $env:PYTHONIOENCODING = "utf-8"
    python tools\dev_replay\compare_benchmark.py `
        tools\dev_replay\baseline\summary.csv "$benchDir\summary.csv"
    $compareExit = $LASTEXITCODE
    if ($compareExit -ne 0) {
        Write-Host ""
        Write-Host "警告：基准对比触发了告警（见上）。确认是预期变化后再打包。" -ForegroundColor Yellow
        $answer = Read-Host "仍然继续打包？(y/N)"
        if ($answer -ne "y") { throw "已取消打包" }
    }
}

# ---- 4) 收集并打 zip -------------------------------------------------------
Step "收集产物"
$sha = (git rev-parse --short HEAD).Trim()
$name = "PixelEngine-$Version-$Channel-$sha"
$stage = Join-Path $OutDir $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item "build-release\EngineApp.exe" $stage
Copy-Item "build-release\assets" $stage -Recurse
if (Test-Path "README.md") { Copy-Item "README.md" $stage }

@(
    "PixelEngine $Version-$Channel"
    "commit: $sha"
    "built:  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    ""
    "运行: EngineApp.exe"
    "存档: cs2d_save.ini（与渠道同名后缀，dev/beta 为 cs2d_save_dev.ini / cs2d_save_beta.ini）"
) | Set-Content -Path (Join-Path $stage "version.txt") -Encoding UTF8

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force

$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ""
Write-Host "打包完成: $zip ($sizeMb MB)" -ForegroundColor Green
Write-Host "记得：给 tag 打上 v$Version（beta 用 v$Version-beta.N），并核对 docs/release-checklist.md"
