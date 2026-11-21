@echo off
rem Builds and runs the UI string test. Run from a VS x64 developer prompt, or let it call VsDevCmd.
setlocal
where cl.exe >nul 2>&1 || call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
set SRC=%~dp0..\src
set OUT=%TEMP%\FreeMenuCursorTests
if not exist "%OUT%" mkdir "%OUT%"
cd /d "%OUT%" || exit /b 1
cl.exe /nologo /std:c++latest /utf-8 /EHsc /I"%SRC%" "%~dp0MenuUIStringsTest.cpp" /Fe"%OUT%\MenuUIStringsTest.exe" || exit /b 1
"%OUT%\MenuUIStringsTest.exe"
exit /b %errorlevel%
