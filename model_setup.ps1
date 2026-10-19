# ============================================================================
# PixelEngine LLM BOT 环境安装脚本
# 
# 下载内容:
#   1. llama.cpp 预编译二进制 (Vulkan GPU 后端)
#   2. Qwen2.5-7B-Instruct GGUF 量化模型 (推荐)
#   3. Qwen2.5-3B-Instruct GGUF 量化模型 (低配备选)
#
# 用法:
#   .\model_setup.ps1              # 下载 7B 模型 (默认, ~4.4 GB)
#   .\model_setup.ps1 -Model 3b    # 下载 3B 模型 (~2.0 GB, 速度更快)
#   .\model_setup.ps1 -Model both  # 两个都下载
# ============================================================================

param(
    [ValidateSet("7b", "3b", "both")]
    [string]$Model = "7b"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

# ---- 配置 ----
$LlamaCppVersion = "b11669"
$LlamaCppUrl     = "https://github.com/ngosang/llama.cpp-wheels/releases/download/llama-cpp-${LlamaCppVersion}-vulkan/llama-cpp-${LlamaCppVersion}-vulkan-x64.zip"
$LlamaCppDir     = "llm_bot\llama.cpp"

$Model7b = @{
    Name = "qwen2.5-7b-instruct-q4_k_m"
    Url  = "https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/main/qwen2.5-7b-instruct-q4_k_m.gguf"
    File = "qwen2.5-7b-instruct-q4_k_m.gguf"
}

$Model3b = @{
    Name = "qwen2.5-3b-instruct-q4_k_m"
    Url  = "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf"
    File = "qwen2.5-3b-instruct-q4_k_m.gguf"
}

$ModelDir = "llm_bot\models"

$ConfigTemplate = @'
; PixelEngine LLM BOT 配置 — 由 model_setup.ps1 自动生成
[server]
url=http://127.0.0.1:9931

[launcher]
auto_launch=1
exe={EXE_PATH}
model={MODEL_PATH}
ngl=99
startup_wait=20
'@

# ---- 辅助函数 ----
function Write-Step([string]$msg) {
    Write-Host ">>> $msg" -ForegroundColor Cyan
}
function Download-File([string]$url, [string]$dest) {
    $dir = Split-Path $dest -Parent
    if (!(Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

    if (Test-Path $dest) {
        Write-Host "  已存在, 跳过: $dest" -ForegroundColor Gray
        return
    }

    Write-Host "  下载: $([math]::Round((Invoke-WebRequest -Uri $url -Method Head).ContentLength / 1MB, 1)) MB" -ForegroundColor Yellow
    Write-Host "  保存到: $dest"
    Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    Write-Host "  完成!" -ForegroundColor Green
}

# ============================================================================
# 1. 下载 llama.cpp
# ============================================================================
Write-Step "1/3 下载 llama.cpp (Vulkan 后端)"

$ZipFile = "$env:TEMP\llama-cpp-${LlamaCppVersion}-vulkan.zip"

# 如果目录已存在且有 llama-server.exe, 跳过
if ((Test-Path "$LlamaCppDir\llama-server.exe")) {
    Write-Host "  llama.cpp 已存在, 跳过下载" -ForegroundColor Gray
} else {
    if (!(Test-Path $ZipFile)) {
        Write-Host "  从 GitHub 下载: $LlamaCppUrl"
        Invoke-WebRequest -Uri $LlamaCppUrl -OutFile $ZipFile -UseBasicParsing
    }

    Write-Host "  解压..."
    New-Item -ItemType Directory -Force -Path $LlamaCppDir | Out-Null
    Expand-Archive -Path $ZipFile -DestinationPath $LlamaCppDir -Force -ErrorAction Stop

    # 如果解压出来有个子目录, 把里面的文件提到第一层
    $subDirs = Get-ChildItem $LlamaCppDir -Directory
    if ($subDirs.Count -eq 1 -and $subDirs[0].Name -notlike "models") {
        Write-Host "  提平嵌套目录..."
        $inner = $subDirs[0].FullName
        Get-ChildItem $inner | Move-Item -Destination $LlamaCppDir -Force
        Remove-Item $inner -Force
    }

    Remove-Item $ZipFile -Force -ErrorAction SilentlyContinue
    Write-Host "  llama.cpp 下载完成" -ForegroundColor Green
}

# ============================================================================
# 2. 下载模型
# ============================================================================
Write-Step "2/3 下载模型文件"

New-Item -ItemType Directory -Force -Path $ModelDir | Out-Null

$modelConfigPath = ""
$modelConfigExe  = ""

if ($Model -eq "7b" -or $Model -eq "both") {
    $dest = Join-Path $ModelDir $Model7b.File
    Download-File $Model7b.Url $dest
    $modelConfigPath = $dest
    $modelConfigExe  = (Join-Path $LlamaCppDir "llama-server.exe")
}

if ($Model -eq "3b" -or $Model -eq "both") {
    $dest = Join-Path $ModelDir $Model3b.File
    Download-File $Model3b.Url $dest
    if ($Model -eq "3b") {
        $modelConfigPath = $dest
        $modelConfigExe  = (Join-Path $LlamaCppDir "llama-server.exe")
    }
}

# ============================================================================
# 3. 生成配置文件
# ============================================================================
Write-Step "3/3 生成 llm.ini"

$configContent = $ConfigTemplate.Replace("{EXE_PATH}", $modelConfigExe.Replace('\', '\\'))
$configContent = $configContent.Replace("{MODEL_PATH}", $modelConfigPath.Replace('\', '\\'))

$configDest = "assets\config\llm.ini"
New-Item -ItemType Directory -Force -Path "assets\config" | Out-Null
Set-Content -Path $configDest -Value $configContent -Encoding utf8

Write-Host "  已生成: $configDest" -ForegroundColor Green

# ============================================================================
# 完成
# ============================================================================
Write-Host ""
Write-Host "==============================================" -ForegroundColor Green
Write-Host "  安装完成!" -ForegroundColor Green
Write-Host "==============================================" -ForegroundColor Green
Write-Host ""
Write-Host "  CMake 构建:" -ForegroundColor White
Write-Host "    cd build-release && cmake ../PixelEngine -G `"MinGW Makefiles`" -DCMAKE_BUILD_TYPE=Release && mingw32-make -j12"
Write-Host ""
Write-Host "  运行:" -ForegroundColor White
Write-Host "    .\build-release\EngineApp.exe"
Write-Host ""
Write-Host "  注意: 首次启动时自动拉起 llama-server 需要 10-15 秒加载模型 (RTX 5060)"
Write-Host "        如果不想用 LLM, 在 assets/config/llm.ini 里把 auto_launch 改成 0"
Write-Host ""