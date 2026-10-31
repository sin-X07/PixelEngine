# Windows 验收脚本（PIX-15）：启动游戏、模拟输入、截图留证
#
# 用法（在仓库根目录）：
#   pwsh -File tools/win_accept.ps1                     # 默认输出到 build-evidence/
#   pwsh -File tools/win_accept.ps1 -OutDir build-evidence
#
# 产物：01-main-menu.png / 02-ingame-hud.png / 03-scope.png / 04-buy-menu.png
# 以及每次运行的 stdout/stderr 日志。截图目录默认被 .gitignore 的 /build-*/ 忽略。
param(
    [string]$OutDir = "build-evidence",
    [string]$Exe = "build-release/EngineApp.exe",
    [string]$Title = "CS2D Pixel"
)

$ErrorActionPreference = "Stop"
$root = (Get-Location).Path
$out = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Input {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, ref RECT rect);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, UIntPtr e);
    public const uint MOUSEEVENTF_RIGHTDOWN = 0x0008;
    public const uint MOUSEEVENTF_RIGHTUP   = 0x0010;
    public const uint KEYEVENTF_KEYUP       = 0x0002;
}
"@

# DPI 感知：否则截图只能拿到「逻辑坐标」大小的裁切，看不到真实的 2560x1600 画面
[Win32Input]::SetProcessDPIAware() | Out-Null

function Save-Shot([string]$name) {
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $path = Join-Path $out $name
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Host "[shot] $name"
}

function Focus-Game($proc) {
    for ($i = 0; $i -lt 20; ++$i) {
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) {
            [Win32Input]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
            Start-Sleep -Milliseconds 200
            return
        }
        Start-Sleep -Milliseconds 200
    }
}

function Click-Right() {
    [Win32Input]::mouse_event([Win32Input]::MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [Win32Input]::mouse_event([Win32Input]::MOUSEEVENTF_RIGHTUP, 0, 0, 0, [UIntPtr]::Zero)
}

function Focus-Game($proc) {
    for ($i = 0; $i -lt 20; ++$i) {
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) {
            [Win32Input]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
            # 把光标真正挪进游戏窗口中心：DPI 缩放下鼠标常停在窗口左上角
            $rect = New-Object Win32Input+RECT
            if ([Win32Input]::GetWindowRect($proc.MainWindowHandle, [ref]$rect)) {
                $cx = [int](($rect.Left + $rect.Right) / 2)
                $cy = [int](($rect.Top + $rect.Bottom) / 2)
                [Win32Input]::SetCursorPos($cx, $cy) | Out-Null
            }
            Start-Sleep -Milliseconds 300
            return
        }
        Start-Sleep -Milliseconds 200
    }
}

function Press-Key([byte]$vk) {
    [Win32Input]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [Win32Input]::keybd_event($vk, 0, [Win32Input]::KEYEVENTF_KEYUP, [UIntPtr]::Zero)
}

# ---- 1. 主菜单 ----
Write-Host "== 主菜单 =="
$p1 = Start-Process -FilePath $Exe -ArgumentList "--coach=rule" -PassThru `
    -RedirectStandardOutput (Join-Path $out "menu.out") `
    -RedirectStandardError  (Join-Path $out "menu.err")
Focus-Game $p1
Start-Sleep -Seconds 9
Save-Shot "01-main-menu.png"
if (-not $p1.HasExited) { $p1.Kill() }
Start-Sleep -Seconds 1

# ---- 2. 对局：HUD + 开镜（自带 AWP 便于验证开镜）----
Write-Host "== 对局 =="
$p2 = Start-Process -FilePath $Exe `
    -ArgumentList "--coach=rule --autostart --mode=1 --side=ct --loadout=AWP" -PassThru `
    -RedirectStandardOutput (Join-Path $out "ingame.out") `
    -RedirectStandardError  (Join-Path $out "ingame.err")
Focus-Game $p2
Start-Sleep -Seconds 6
Save-Shot "02-ingame-hud.png"

Write-Host "== 开镜（右键）=="
Click-Right
Start-Sleep -Milliseconds 500
Save-Shot "04-scope.png"

if (-not $p2.HasExited) { $p2.Kill() }
Start-Sleep -Seconds 1

# ---- 3. 购买菜单：购买窗口只有开局 15 秒，用 --buymenu 打开一次（可复现）----
Write-Host "== 购买菜单 =="
$p3 = Start-Process -FilePath $Exe `
    -ArgumentList "--coach=rule --autostart --mode=1 --side=ct --loadout=AWP --buymenu" -PassThru `
    -RedirectStandardOutput (Join-Path $out "buymenu.out") `
    -RedirectStandardError  (Join-Path $out "buymenu.err")
Focus-Game $p3
Start-Sleep -Seconds 7
Save-Shot "03-buy-menu.png"
if (-not $p3.HasExited) { $p3.Kill() }

Write-Host "完成：截图与日志在 $out"
