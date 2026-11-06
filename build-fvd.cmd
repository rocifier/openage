@echo off
rem One-command FVD Windows native release: build, package, audit.
rem Run from anywhere, or double-click in Explorer.
setlocal
pushd "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-fvd.ps1" %*
set "result=%ERRORLEVEL%"
popd
if not "%result%"=="0" (
    echo.
    echo FVD native build failed. See the error above.
    pause
)
exit /b %result%
