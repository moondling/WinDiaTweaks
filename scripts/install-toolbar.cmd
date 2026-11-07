@echo off
setlocal
title WinDiaTweaks v1.2 + custom toolbar
cd /d "%~dp0"
echo WinDiaTweaks v1.2 + your custom toolbar
echo Dia will close and reopen during installation.
echo.
if not exist "%~dp0payload\diatweaks.exe" (
    echo Extract the entire ZIP first. Keep Install.cmd and payload together.
    pause
    exit /b 1
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -Command "if (-not [Environment]::Is64BitOperatingSystem -or $env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64' -or [Environment]::OSVersion.Version.Build -lt 22000) { Write-Host 'Windows 11 x64 is required.'; exit 1 }; if (-not (Get-AppxPackage TheBrowserCompany.Dia)) { Write-Host 'Install Dia from https://www.diabrowser.com and open it once, then try again.'; exit 1 }"
if errorlevel 1 goto failed
"%~dp0payload\diatweaks.exe" install --to "%USERPROFILE%\WinDiaTweaks-Toolbar\bin"
if errorlevel 1 goto failed
echo.
echo Installed. Open Dia (Tweaks), then Settings ^> Patches.
pause
exit /b 0
:failed
set "SETUP_RESULT=%ERRORLEVEL%"
echo.
echo Setup did not finish. See the error above, then try again.
pause
exit /b %SETUP_RESULT%
