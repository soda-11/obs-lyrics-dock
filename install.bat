@echo off
echo Installing OBS Lyrics Dock v10.2.0...

set DLL_SRC=C:\Users\boon\OBS Lyrics Plugin\build\Release\obs-lyrics-dock.dll
set DLL_DST=C:\Program Files\obs-studio\obs-plugins\64bit\obs-lyrics-dock.dll
set HTML_SRC=C:\Users\boon\OBS Lyrics Plugin\data\remote.html
set HTML_DST=C:\Program Files\obs-studio\data\obs-plugins\obs-lyrics-dock\remote.html

copy /Y "%DLL_SRC%" "%DLL_DST%"
if errorlevel 1 (
    echo ERROR: Could not copy DLL. Make sure OBS is closed and run as Administrator.
    pause
    exit /b 1
)
echo   [OK] obs-lyrics-dock.dll

copy /Y "%HTML_SRC%" "%HTML_DST%"
if errorlevel 1 (
    echo ERROR: Could not copy remote.html.
    pause
    exit /b 1
)
echo   [OK] remote.html

echo.
echo Install complete. You can now launch OBS.
pause
