@echo off
REM Build Sentinel-IDE, then compile the Inno Setup installer.
REM   output: build\installer\Sentinel-IDE-<ver>-setup.exe
REM Requires Inno Setup 6:  winget install JRSoftware.InnoSetup
setlocal
call "%~dp0build.bat" || exit /b 1
set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=ISCC.exe"
"%ISCC%" "%~dp0..\packaging\Sentinel-IDE.iss" || (echo Inno Setup ^(ISCC^) not found or failed - install: winget install JRSoftware.InnoSetup & exit /b 1)
echo INSTALLER_OK
