@echo off
setlocal

set OBS_PLUGINS=C:\Program Files\obs-studio\obs-plugins\64bit
set OBS_DATA=C:\Program Files\obs-studio\data\obs-plugins\obs-lyrics-dock
set REPO=%~dp0
set BUILD=%REPO%build
set NSI=%REPO%installer\obs-lyrics-dock.nsi

:: NSIS default install path — adjust if installed elsewhere
set MAKENSIS="C:\Program Files (x86)\NSIS\makensis.exe"
cls
echo ==============================================
echo  OBS Lyrics Dock — Build + Package + Install
echo ==============================================

echo.
echo [1/3] Building DLL...
cmake --build "%BUILD%" --config Release
if errorlevel 1 (
    echo BUILD FAILED.
    pause
    exit /b 1
)

echo.
echo [2/3] Creating NSIS installer...
if not exist %MAKENSIS% (
    echo WARNING: makensis not found at %MAKENSIS% -- skipping installer creation.
    echo          Install NSIS from https://nsis.sourceforge.io/ to enable this step.
) else (
    %MAKENSIS% /V2 "%NSI%"
    if errorlevel 1 (
        echo NSIS build FAILED.
        pause
        exit /b 1
    )
    echo Installer created: %REPO%installer\OBS-Lyrics-Dock-v10.7.0-Setup.exe
)

echo.
echo [3/3] Installing to local OBS...
copy /y "%BUILD%\Release\obs-lyrics-dock.dll" "%OBS_PLUGINS%\obs-lyrics-dock.dll"
if errorlevel 1 (
    echo COPY FAILED -- run this script as Administrator.
    pause
    exit /b 1
)
xcopy /y /e /i "%REPO%data\*" "%OBS_DATA%\"

echo.
echo Done. Restart OBS to load the updated plugin.
pause
