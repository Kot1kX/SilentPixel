@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%build_release_run.ps1"
exit /b %ERRORLEVEL%
