@echo off
rem ============================================================================
rem  PixelEngine / menu 3D model update  (just double-click this file)
rem
rem  What it does: bakes tools\menu-models\CharacterLab.blend into GLB files and
rem                copies them next to build-release / build-dev executables.
rem
rem  Before running: save the .blend in Blender (Ctrl+S).
rem  Optional args : update-models.cmd -Launch
rem                 update-models.cmd -SyncOnly
rem ============================================================================
setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\update-menu-models.ps1" %*
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo [FAILED] Read the red message above, fix it, then run this file again.
) else (
    echo [DONE] Start the game to check: build-release\EngineApp.exe
)
echo.
pause
exit /b %RC%
