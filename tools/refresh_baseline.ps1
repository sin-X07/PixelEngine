# ============================================================================
# refresh_baseline.ps1 —— 一条命令重刷确定性基准（baseline）
#
# 用途：当一次改动**被确认是期望行为**之后，把"当前代码的 60 局结果"写成新基线，
#       之后所有对比都以它为参照。
#
# 用法（在仓库根目录）：
#   powershell -ExecutionPolicy Bypass -File tools\refresh_baseline.ps1
#   powershell -ExecutionPolicy Bypass -File tools\refresh_baseline.ps1 -Tag v0.3.0
#   powershell -ExecutionPolicy Bypass -File tools\refresh_baseline.ps1 -Seeds 1-10
#
# 它会做四件事：
#   1) 检查 build-dev\dev_replay_runner.exe 是否存在（没有就提示怎么构建，不会自己编译）
#   2) 跑 60 局（3 图 × seed 1-20）到 build-evidence\baseline-refresh\
#   3) 把 summary.csv 复制到 tools\dev_replay\baseline\summary.csv（旧版备份为 summary.prev.csv）
#   4) 打印新旧基线关键指标对照 + 下一步该敲的 git 命令
# ============================================================================

param(
    [string]$Seeds   = "1-20",
    [string]$Maps    = "dust_2,mirage,ancient_ruins",
    [int]   $Ticks   = 150000,
    [string]$Tag     = "",
    [string]$Runner  = "build-dev\dev_replay_runner.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$baselineDir  = "tools\dev_replay\baseline"
$baselineFile = Join-Path $baselineDir "summary.csv"
$prevFile     = Join-Path $baselineDir "summary.prev.csv"
$outDir       = "build-evidence\baseline-refresh"

function Fail([string]$msg) {
    Write-Host ""
    Write-Host "✗ $msg" -ForegroundColor Red
    exit 1
}

# ---- 0) 前置检查 -----------------------------------------------------------
if (-not (Test-Path $Runner)) {
    Fail @"
找不到 $Runner。
baseline 必须用 dev 构建产出的 runner 跑。先构建：

  cmake -S PixelEngine -B build-dev -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ``
        "-DPE_VERSION=0.3.0" "-DPE_CHANNEL=dev" -DBUILD_DEV_REPLAY_TOOLS=ON ``
        "-DFETCHCONTENT_SOURCE_DIR_SDL2=`$PWD\build-release\_deps\sdl2-src"
  cmake --build build-dev --parallel 8
"@
}

if (-not $Tag) { $Tag = (git rev-parse --short HEAD).Trim() }

# ---- 1) 跑 60 局 -----------------------------------------------------------
Write-Host ""
Write-Host "==> 跑基准：$Maps × seeds $Seeds  (ticks=$Ticks)" -ForegroundColor Cyan
if (Test-Path $outDir) { Remove-Item $outDir -Recurse -Force }
New-Item -ItemType Directory -Force $outDir | Out-Null

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$log = Join-Path $outDir "run.log"
& $Runner --ticks=$Ticks --maps=$Maps --seeds=$Seeds --out=$outDir "--tag=$Tag" *> $log
if ($LASTEXITCODE -ne 0) { Fail "基准运行失败（exit=$LASTEXITCODE）" }
$sw.Stop()
Get-Content $log -Encoding UTF8 | Select-String -Pattern "batch games|avg rounds" | ForEach-Object { Write-Host ("    " + $_.Line) }
Write-Host ("    用时 {0:N0} 秒（完整日志: $log）" -f ($sw.Elapsed.TotalSeconds))

$newSummary = Join-Path $outDir "summary.csv"
if (-not (Test-Path $newSummary)) { Fail "没生成 $newSummary" }

# ---- 2) 备份旧基线并覆盖 ---------------------------------------------------
New-Item -ItemType Directory -Force $baselineDir | Out-Null
if (Test-Path $baselineFile) {
    Copy-Item $baselineFile $prevFile -Force
    Write-Host "    旧基线已备份到 $prevFile"
}
Copy-Item $newSummary $baselineFile -Force
Write-Host "    新基线已写入 $baselineFile" -ForegroundColor Green

# ---- 3) 新旧对照 -----------------------------------------------------------
function Read-Summary([string]$path) {
    if (-not (Test-Path $path)) { return $null }
    $lines = Get-Content $path -Encoding UTF8
    $tag = ""
    $header = $null
    $rows = @()
    foreach ($line in $lines) {
        if ($line.StartsWith("#")) {
            if ($line -match "tag=(\S+)") { $tag = $Matches[1] }
            continue
        }
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($null -eq $header) { $header = $line.Split(","); continue }
        $f = $line.Split(",")
        if ($f.Count -lt $header.Count) { continue }
        $map = @{}
        for ($i = 0; $i -lt $header.Count; $i++) { $map[$header[$i]] = $f[$i] }
        $rows += $map
    }
    if ($rows.Count -eq 0) { return $null }
    $n = $rows.Count
    function Avg([string]$key) {
        $sum = 0.0
        foreach ($r in $rows) { if ($r.ContainsKey($key)) { $sum += [double]$r[$key] } }
        return $sum / $n
    }
    $tWins = 0
    foreach ($r in $rows) { if ($r["winner"] -eq "T") { $tWins++ } }
    return [pscustomobject]@{
        Tag       = $tag
        N         = $n
        TWinRate  = $tWins / $n
        Plants    = Avg "plants"
        Defuses   = Avg "defuses"
        Still     = Avg "still_pct"
        Idle      = Avg "idle_pct"
        PathEff   = Avg "path_eff"
        Dmg       = Avg "dmg_per_round"
        Conflicts = Avg "ai_conflicts"
    }
}
$old = Read-Summary $prevFile
$new = Read-Summary $baselineFile

Write-Host ""
Write-Host "==> 新旧基线对照" -ForegroundColor Cyan
if (-not $old) {
    Write-Host "    （没有旧基线，这是第一次生成）"
} else {
    Write-Host ("    {0,-14} {1,12} {2,12} {3,10}" -f "指标", "旧 tag=$($old.Tag)", "新 tag=$($new.Tag)", "变化")
    $fmt = @(
        @{ Name = "局数";         Old = $old.N;          New = $new.N;          F = "{0}" },
        @{ Name = "T 胜率";       Old = $old.TWinRate;   New = $new.TWinRate;   F = "{0:P1}" },
        @{ Name = "下包/局";      Old = $old.Plants;     New = $new.Plants;     F = "{0:F2}" },
        @{ Name = "拆包/局";      Old = $old.Defuses;    New = $new.Defuses;    F = "{0:F2}" },
        @{ Name = "原地不动";     Old = $old.Still;      New = $new.Still;      F = "{0:F1}" },
        @{ Name = "发呆";         Old = $old.Idle;       New = $new.Idle;       F = "{0:F1}" },
        @{ Name = "路径效率";     Old = $old.PathEff;    New = $new.PathEff;    F = "{0:F2}" },
        @{ Name = "每回合伤害";   Old = $old.Dmg;        New = $new.Dmg;        F = "{0:F0}" },
        @{ Name = "AI 冲突/局";   Old = $old.Conflicts;  New = $new.Conflicts;  F = "{0:F2}" }
    )
    foreach ($r in $fmt) {
        $delta = [double]$r.New - [double]$r.Old
        $sign  = if ($delta -gt 0) { "+" } else { "" }
        Write-Host ("    {0,-14} {1,12} {2,12} {3,10}" -f `
            $r.Name, ($r.F -f $r.Old), ($r.F -f $r.New), ("{0}{1:N2}" -f $sign, $delta))
    }
}

# ---- 4) 下一步 -------------------------------------------------------------
Write-Host ""
Write-Host "==> 下一步：确认这是期望行为后，把新基线提交上去" -ForegroundColor Cyan
Write-Host @"

  git add tools/dev_replay/baseline/summary.csv
  git commit -m "chore(baseline): 刷新基准 tag=$Tag"
  git push

提示：
  · 基线只应该在"行为变化被确认是期望的"之后刷新；
  · 提交信息里写清"哪项指标变了、为什么"，方便以后追溯；
  · 旧基线在 $prevFile（未入库，可删）；
  · 完整流程见 tools/dev_replay/WORKFLOW.md 第 6 节。
"@
