<#
.SYNOPSIS
    把 CharacterLab.blend 的修改导出成游戏用的 GLB，并同步到构建目录。

.DESCRIPTION
    一条命令完成「保存 .blend 之后」的全部更新动作：

      1. 用 Blender 后台模式运行 tools\menu-models\export_models.py
      2. 校验导出真的完成（日志尾行 + GLB 里的动画名/节点名 + 文件时间戳）
      3. 把 assets\models\menu\*.glb 复制到 build-release / build-dev 的 exe 旁
         （引擎优先读 exe 旁的 assets，不同步就会看到旧模型）
      4. 可选：直接启动游戏（-Launch）

    只改模型 / 颜色 / 姿势时，跑完这个脚本就够了，不需要重新编译 C++。
    只有改了「姿势名、武器名、新增选项」之类，才需要再改 Menu3D.cpp 并重新构建。

    已知现象：t.glb / ct.glb 每次导出的字节会有细微差异（配件合并时 UV 层顺序不固定），
    肉眼看不出区别，游戏行为一致；脚本校验的是结构、动画名和关键节点，不比对字节。

.PARAMETER Blender
    blender.exe 的完整路径。默认自动查找：PATH -> D:\Blender -> Program Files。

.PARAMETER Source
    编辑后的 .blend 路径。默认优先使用 CharacterLab_Reconstruction.blend。

.PARAMETER Build
    要同步到哪个构建目录：release / dev / both（默认 both）。
    目录里存在 EngineApp.exe 才会同步，不存在就跳过。

.PARAMETER SyncOnly
    不重新导出，只把现有 GLB 同步到构建目录（快速救急用）。

.PARAMETER Launch
    同步完成后启动游戏（优先 build-release）。

.PARAMETER Quiet
    不逐行打印 Blender 日志；失败时只打印日志尾部。

.EXAMPLE
    .\tools\update-menu-models.ps1

.EXAMPLE
    .\tools\update-menu-models.ps1 -Launch

.EXAMPLE
    .\tools\update-menu-models.ps1 -SyncOnly

.EXAMPLE
    .\tools\update-menu-models.ps1 -Blender 'D:\Blender\blender.exe' -Build release
#>
[CmdletBinding()]
param(
    [string]$Blender = '',
    [string]$Source = '',
    [ValidateSet('release', 'dev', 'both')]
    [string]$Build = 'both',
    [switch]$SyncOnly,
    [switch]$Launch,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# 统一把任何中断错误变成一行红色提示 + 退出码 1，不甩调用栈。
trap {
    Write-Host ''
    Write-Host "✘ $($_.Exception.Message)" -ForegroundColor Red
    Write-Host '  修好后再跑一次即可；已经生成的旧 GLB 不会被破坏。' -ForegroundColor DarkGray
    exit 1
}

# ---------------------------------------------------------------- 路径与常量

$scriptDir    = $PSScriptRoot
$root         = Split-Path -Parent $scriptDir
$exportScript = Join-Path $scriptDir 'menu-models\export_models.py'
$blendFile    = Join-Path $scriptDir 'menu-models\CharacterLab.blend'
if ($Source) {
    $blendFile = (Resolve-Path -LiteralPath $Source).Path
}
elseif (Test-Path -LiteralPath (Join-Path $scriptDir 'menu-models\CharacterLab_Reconstruction.blend')) {
    $blendFile = Join-Path $scriptDir 'menu-models\CharacterLab_Reconstruction.blend'
}
$assetDir     = Join-Path $root 'assets\models\menu'
$logPath      = Join-Path $env:TEMP 'pixelengine-menu-export.log'
$exportOutput = Join-Path $root ('build-menu-export\' + [guid]::NewGuid().ToString('N'))

# 角色 -> GLB 文件 -> 期望内容。
# Animations 缺失 = 硬错误（游戏会报「缺少持久姿势」）；数量不符只提示。
$humanAnimations = @(
    foreach ($pose in @('Idle', 'Crouch', 'HandsOnHips', 'FistPalm')) {
        foreach ($weapon in @('AK', 'SMG', 'Sniper', 'Shotgun', 'Pistol', 'Knife_1')) {
            "Menu_${pose}_${weapon}"
        }
    }
)
$roles = @(
    [pscustomobject]@{
        Actor = 'T'
        File  = 't.glb'
        Count = 24
        Animations = $humanAnimations
        Nodes = @('T_RIG', 'Hands', 'accessory_headwear', 'accessory_eyewear', 'accessory_back', 'weapon_Idle_AK')
    },
    [pscustomobject]@{
        Actor = 'CT'
        File  = 'ct.glb'
        Count = 24
        Animations = $humanAnimations
        Nodes = @('CT_RIG', 'Hands', 'accessory_headwear', 'accessory_eyewear', 'accessory_back', 'weapon_Idle_AK')
    },
    [pscustomobject]@{
        Actor = '小鸡'
        File  = 'chicken.glb'
        Count = 4
        Animations = @('Menu_Idle', 'Menu_Look', 'Menu_Walk', 'Menu_Rest')
        Nodes = @('CHICKEN_RIG', 'accessory_headwear', 'accessory_eyewear')   # 小鸡没有背包，所以没有 accessory_back
    }
)

# ---------------------------------------------------------------- 输出助手

function Write-Step([string]$Text) { Write-Host ''; Write-Host "▶ $Text" -ForegroundColor Cyan }
function Write-Info([string]$Text) { Write-Host "  $Text" -ForegroundColor Gray }
function Write-Ok([string]$Text)   { Write-Host "  ✔ $Text" -ForegroundColor Green }
function Write-Warn([string]$Text) { Write-Host "  ! $Text" -ForegroundColor Yellow }

# ---------------------------------------------------------------- 工具函数

function Find-Blender {
    param([string]$Explicit)

    if ($Explicit) {
        if (Test-Path -LiteralPath $Explicit) { return (Resolve-Path -LiteralPath $Explicit).Path }
        $cmd = Get-Command $Explicit -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
        throw "找不到你指定的 Blender：$Explicit"
    }

    $cmd = Get-Command blender -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = New-Object System.Collections.Generic.List[string]
    [void]$candidates.Add('D:\Blender\blender.exe')
    foreach ($base in @('C:\Program Files\Blender Foundation', 'C:\Program Files (x86)\Blender Foundation')) {
        if (Test-Path -LiteralPath $base) {
            Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending | ForEach-Object {
                    [void]$candidates.Add((Join-Path $_.FullName 'blender.exe'))
                }
        }
    }
    foreach ($p in $candidates) {
        if (Test-Path -LiteralPath $p) { return $p }
    }
    throw "找不到 blender.exe。请用 -Blender 'C:\...\blender.exe' 指定路径。"
}

function Get-GlbInfo {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 20) { throw "文件太小，不是有效 GLB：$Path" }

    $magic = [System.BitConverter]::ToUInt32($bytes, 0)
    if ($magic -ne 0x46546C67) { throw "GLB 标识不对（不是 glTF 二进制）：$Path" }

    $jsonLength = [System.BitConverter]::ToUInt32($bytes, 12)
    if ((20 + $jsonLength) -gt $bytes.Length) { throw "GLB 的 JSON 块长度越界，文件可能写了一半：$Path" }

    $json = [System.Text.Encoding]::UTF8.GetString($bytes, 20, [int]$jsonLength)
    $doc  = $json | ConvertFrom-Json

    $animations = @()
    if ($doc.animations) { $animations = @($doc.animations | ForEach-Object { [string]$_.name }) }
    $nodes = @()
    if ($doc.nodes) { $nodes = @($doc.nodes | ForEach-Object { [string]$_.name }) }

    [pscustomobject]@{
        Name       = [System.IO.Path]::GetFileName($Path)
        Path       = $Path
        Bytes      = [int64]$bytes.Length
        Animations = $animations
        Nodes      = $nodes
    }
}

function Invoke-Export {
    param([string]$BlenderExe)

    Write-Step '1/4 导出模型（Blender 后台模式）'
    Write-Info "Blender : $BlenderExe"
    Write-Info "脚本    : $exportScript"

    $log  = New-Object System.Collections.Generic.List[string]
    $code = 1
    Push-Location -LiteralPath $root
    try {
        & $BlenderExe --background --python-exit-code 1 --python $exportScript -- --source $blendFile --output-dir $exportOutput 2>&1 | ForEach-Object {
            $line = [string]$_
            [void]$log.Add($line)
            if (-not $Quiet) { Write-Host $line }
        }
        $code = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    try {
        Set-Content -LiteralPath $logPath -Value ($log -join [Environment]::NewLine) -Encoding UTF8
    }
    catch { }

    [pscustomobject]@{ ExitCode = $code; Log = $log }
}

function Show-LogTail {
    param($Lines, [int]$Count = 30)
    if (-not $Lines -or $Lines.Count -eq 0) { return }
    $start = [Math]::Max(0, $Lines.Count - $Count)
    Write-Host '  ---- 日志尾部 ----' -ForegroundColor DarkGray
    for ($i = $start; $i -lt $Lines.Count; $i++) { Write-Host "  $($Lines[$i])" -ForegroundColor DarkGray }
}

# ---------------------------------------------------------------- 主流程

$started = Get-Date

Write-Host ''
Write-Host 'PixelEngine / 主菜单 3D 模型更新' -ForegroundColor White
Write-Host '================================' -ForegroundColor DarkGray

if (-not (Test-Path -LiteralPath $exportScript)) { throw "找不到导出脚本：$exportScript" }
if (-not (Test-Path -LiteralPath $blendFile))    { throw "找不到工程文件：$blendFile" }

$blenderExe = Find-Blender -Explicit $Blender

# --- 1. 导出 ---------------------------------------------------------------
if ($SyncOnly) {
    Write-Step '1/4 跳过导出（-SyncOnly）'
    Write-Info '直接使用 assets\models\menu 下现有的 GLB。'
}
else {
    $result = Invoke-Export -BlenderExe $blenderExe

    if ($result.ExitCode -ne 0) {
        Show-LogTail -Lines $result.Log
        throw "Blender 退出码 $($result.ExitCode)，导出失败。完整日志：$logPath"
    }

    $joined = $result.Log -join "`n"
    if (-not $joined.Contains('Menu model export complete')) {
        Show-LogTail -Lines $result.Log
        throw "导出没有跑到最后一行 'Menu model export complete'，GLB 可能不完整。完整日志：$logPath"
    }

    $bakeCount = @($result.Log | Where-Object { $_ -like 'Baking *' }).Count
    if ($bakeCount -eq 48) {
        Write-Ok "导出完成，共烘焙 $bakeCount 条动画（T 24 + CT 24；小鸡 4 条不打印）"
    }
    else {
        Write-Warn "导出完成，但烘焙条数是 $bakeCount（预期 48）。若你增删过姿势，这是正常的。"
    }
    Write-Info "完整日志：$logPath"
}

# --- 2. 校验 GLB -----------------------------------------------------------
Write-Step '2/4 校验 GLB 内容'

$infos    = @()
$problems = New-Object System.Collections.Generic.List[string]

foreach ($role in $roles) {
    $validationDir = if ($SyncOnly) { $assetDir } else { $exportOutput }
    $path = Join-Path $validationDir $role.File
    if (-not (Test-Path -LiteralPath $path)) {
        $problems.Add("缺少文件：$($role.File)")
        continue
    }

    $info = Get-GlbInfo -Path $path
    $item = Get-Item -LiteralPath $path
    $info | Add-Member -NotePropertyName 'Actor'   -NotePropertyValue $role.Actor
    $info | Add-Member -NotePropertyName 'Updated' -NotePropertyValue (-not $SyncOnly)

    # 时间戳：导出后应该比本次运行开始时间新（-SyncOnly 时不检查）
    if (-not $SyncOnly -and $item.LastWriteTime -lt $started.AddSeconds(-2)) {
        $info.Updated = $false
        $problems.Add("$($role.File) 没有被本次导出更新（时间戳 $($item.LastWriteTime)）")
    }

    if ($info.Animations.Count -ne $role.Count) {
        Write-Warn "$($role.File) 动画数 $($info.Animations.Count)，预期 $($role.Count)（改过姿势表就会不同）"
    }
    foreach ($need in $role.Animations) {
        if ($info.Animations -notcontains $need) {
            $problems.Add("$($role.File) 缺少动画 '$need' —— 游戏会打印「缺少持久姿势」")
        }
    }
    foreach ($need in $role.Nodes) {
        if ($info.Nodes -notcontains $need) {
            $problems.Add("$($role.File) 缺少节点 '$need' —— 换装/换色会失效")
        }
    }

    $infos += $info
    Write-Ok "$($role.File)：$($info.Animations.Count) 条动画 / $($info.Nodes.Count) 个节点"
}

if ($problems.Count -gt 0) {
    Write-Host ''
    foreach ($p in $problems) { Write-Host "  ✘ $p" -ForegroundColor Red }
    throw 'GLB 校验未通过，先别启动游戏。'
}

if (-not $SyncOnly) {
    $previousDir = Join-Path $exportOutput 'previous'
    New-Item -ItemType Directory -Path $previousDir -Force | Out-Null
    $existingFiles = @()
    foreach ($role in $roles) {
        $current = Join-Path $assetDir $role.File
        if (Test-Path -LiteralPath $current) {
            Copy-Item -LiteralPath $current -Destination $previousDir -Force
            $existingFiles += $role.File
        }
    }
    try {
        New-Item -ItemType Directory -Path $assetDir -Force | Out-Null
        foreach ($role in $roles) {
            $sourceFile = Join-Path $exportOutput $role.File
            $destinationFile = Join-Path $assetDir $role.File
            Copy-Item -LiteralPath $sourceFile -Destination $destinationFile -Force
            if ((Get-FileHash -LiteralPath $sourceFile).Hash -ne (Get-FileHash -LiteralPath $destinationFile).Hash) {
                throw "发布校验失败：$($role.File)"
            }
        }
    }
    catch {
        foreach ($role in $roles) {
            $destinationFile = Join-Path $assetDir $role.File
            if ($existingFiles -contains $role.File) {
                Copy-Item -LiteralPath (Join-Path $previousDir $role.File) -Destination $destinationFile -Force
            }
            elseif (Test-Path -LiteralPath $destinationFile) {
                Remove-Item -LiteralPath $destinationFile
            }
        }
        throw
    }
    Write-Ok '三份模型均校验通过，已更新游戏资源。'
}

# --- 3. 同步到构建目录 -----------------------------------------------------
Write-Step '3/4 同步到可执行文件旁'

$buildDirs = @()
if ($Build -eq 'release' -or $Build -eq 'both') { $buildDirs += 'build-release' }
if ($Build -eq 'dev'     -or $Build -eq 'both') { $buildDirs += 'build-dev' }

$synced = @()
foreach ($name in $buildDirs) {
    $buildDir = Join-Path $root $name
    $exePath  = Join-Path $buildDir 'EngineApp.exe'
    if (-not (Test-Path -LiteralPath $exePath)) {
        Write-Info "$name：没有 EngineApp.exe，跳过"
        continue
    }

    $destDir = Join-Path $buildDir 'assets\models\menu'
    if (-not (Test-Path -LiteralPath $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }

    foreach ($role in $roles) {
        $src = Join-Path $assetDir $role.File
        $dst = Join-Path $destDir $role.File
        Copy-Item -LiteralPath $src -Destination $dst -Force

        $srcHash = (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash
        $dstHash = (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash
        if ($srcHash -ne $dstHash) { throw "复制校验失败：$dst" }
    }

    Write-Ok "$name：已复制 3 个 GLB 并校验哈希一致"
    $synced += $name
}

if ($synced.Count -eq 0) {
    Write-Warn '没有任何构建目录被更新。是不是还没 cmake 构建过？'
}

# --- 4. 汇总 ---------------------------------------------------------------
Write-Step '4/4 汇总'

foreach ($info in $infos) {
    $state = if ($SyncOnly) { '仅同步，未重新导出' } elseif ($info.Updated) { '已重新导出' } else { '未更新（时间戳没变）' }
    $size  = '{0:N0} KB' -f ($info.Bytes / 1KB)
    Write-Host "  $($info.Actor) · $($info.Name) · $size · $($info.Animations.Count) 条动画 / $($info.Nodes.Count) 个节点 · $state"
}
if ($synced.Count -gt 0) {
    Write-Ok "已同步到：$($synced -join '、')"
}
else {
    Write-Warn '没有同步到任何构建目录。'
}

Write-Host ''
Write-Host '下一步：' -ForegroundColor White
Write-Host '  · 只改了模型 / 颜色 / 姿势 → 重启游戏即可看到效果（模型只在菜单初始化时加载一次）。' -ForegroundColor Gray
Write-Host '  · 改了姿势名 / 武器名 / 新增选项 → 还要改 PixelEngine\Graphics\Menu3D.cpp，然后：' -ForegroundColor Gray
Write-Host '      cmake --build build-release --parallel' -ForegroundColor DarkGray
Write-Host '  · 游戏日志里看这三行确认加载成功：' -ForegroundColor Gray
Write-Host '      [Menu3D] glTF 角色已加载: T=Menu_Idle_AK CT=Menu_Idle_AK 小鸡=3' -ForegroundColor DarkGray

if ($Launch) {
    $launchDir = Join-Path $root 'build-release'
    if (-not (Test-Path -LiteralPath (Join-Path $launchDir 'EngineApp.exe'))) {
        $launchDir = Join-Path $root 'build-dev'
    }
    $launchExe = Join-Path $launchDir 'EngineApp.exe'
    if (Test-Path -LiteralPath $launchExe) {
        Write-Host ''
        Write-Host "启动 $launchExe" -ForegroundColor Cyan
        Start-Process -FilePath $launchExe -WorkingDirectory $launchDir | Out-Null
    }
    else {
        Write-Warn '找不到可启动的 EngineApp.exe'
    }
}

Write-Host ''
Write-Host '完成。' -ForegroundColor Green
