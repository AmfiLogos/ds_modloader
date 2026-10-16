@echo off
setlocal EnableExtensions
title Dungeon Settlers Modded - install
cd /d "%~dp0"
echo.
echo  Dungeon Settlers Modded - install
echo  =================================
echo.
if not exist "files\baselib.dll" goto nokit
if not exist "files\Dungeon Settlers Modded.exe" goto nokit
set /p BUILD=<files\BUILD.txt

call files\find_game.bat %1
if defined GAME goto found
echo  The game folder was not found automatically.
echo  1. Open Steam. Right-click Dungeon Settlers. Click Manage, then Browse local files.
echo  2. Click the address bar at the top of that folder window. Press Ctrl+C.
echo  3. Click this window. Press Ctrl+V, then press Enter.
echo.
set "ASK="
set /p "ASK=Game folder: "
if not defined ASK goto fail
set "ASK=%ASK:"=%"
call files\find_game.bat "%ASK%"
if not defined GAME (
  echo.
  echo  DungeonSettlers.exe is not in that folder.
  goto fail
)

:found
echo  Steam game folder: %GAME%
rem Steam's folder is only read. The game version must be the one this kit is for.
for %%F in ("%GAME%\GameAssembly.dll") do set "GASIZE=%%~zF"
set /p WANTSIZE=<files\GASIZE.txt
if not "%GASIZE%"=="%WANTSIZE%" (
  echo.
  echo  Your game version is not the version this kit is for ^(Steam build %BUILD%^).
  echo  Nothing was changed. Look for a newer kit on the download page.
  goto fail
)
tasklist /fi "imagename eq DungeonSettlers.exe" 2>nul | find /i "DungeonSettlers.exe" >nul
if not errorlevel 1 (
  echo.
  echo  Dungeon Settlers is running. Close the game, then run install.bat again.
  goto fail
)

rem the game's own baselib.dll is about 230 KB. An older multiplayer kit put a bigger one in Steam's folder and kept
rem the game's file as baselib_orig.dll: then that one is the original.
set "STOCKBL=%GAME%\baselib.dll"
set "BLSIZE=0"
for %%F in ("%GAME%\baselib.dll") do if exist "%%~fF" set "BLSIZE=%%~zF"
if %BLSIZE% GTR 1000000 set "STOCKBL=%GAME%\baselib_orig.dll"
if not exist "%STOCKBL%" (
  echo.
  echo  The game file baselib.dll is missing. In Steam, right-click Dungeon Settlers,
  echo  click Properties, Installed Files, Verify integrity of game files. Then run install.bat again.
  goto fail
)
set "BLSIZE=0"
for %%F in ("%STOCKBL%") do set "BLSIZE=%%~zF"
if %BLSIZE% GTR 1000000 (
  echo.
  echo  The game file baselib.dll is not the original one. In Steam, right-click Dungeon Settlers,
  echo  click Properties, Installed Files, Verify integrity of game files. Then run install.bat again.
  goto fail
)

rem DSMODDED_ROOT (the installer's chosen folder) replaces the default folder
set "ROOT=%LOCALAPPDATA%\Dungeon Settlers Modded"
if defined DSMODDED_ROOT set "ROOT=%DSMODDED_ROOT%"
set "COPY=%ROOT%\game"
rem only into a new folder, an empty one or our own (marker file, or the game copy of an older kit): never mixed with
rem other files, so the uninstall can never touch anything it did not put there
set "OURS="
if exist "%ROOT%\dsmodded_install.txt" set "OURS=1"
if exist "%COPY%\dsmodded_build.txt" if exist "%ROOT%\Dungeon Settlers Modded.exe" set "OURS=1"
if not defined OURS if exist "%ROOT%\" (
  dir /b /a "%ROOT%" 2>nul | findstr "^" >nul && goto notours
)
if not exist "%ROOT%" mkdir "%ROOT%"
> "%ROOT%\dsmodded_install.txt" echo Dungeon Settlers Modded install folder. The uninstall removes only the files listed in install_files.log.

rem one full copy of the game (about 700 MB). Not again when the copy is already this build.
set "HAVE="
if exist "%COPY%\dsmodded_build.txt" set /p HAVE=<"%COPY%\dsmodded_build.txt"
if "%HAVE%"=="%BUILD%" (
  echo  The game copy is already there ^(build %BUILD%^).
  goto files
)
rem an older or unfinished copy of ours (marker files) holds only game files and mod files (saves live in profiles\):
rem replaced. A "game" folder without our marker is never removed.
if exist "%COPY%\" if not exist "%COPY%\dsmodded_build.txt" if not exist "%COPY%\dsmodded_copy.txt" goto notours
if exist "%COPY%" rd /s /q "%COPY%"
if exist "%COPY%" goto copyfail
mkdir "%COPY%"
> "%COPY%\dsmodded_copy.txt" echo Dungeon Settlers Modded game copy
echo  Copying the game ^(about 700 MB, this takes a minute or two^) ...
rem xcopy (in every Windows): the files of an older multiplayer kit in Steam's folder are left out (files\copy_exclude.txt)
xcopy "%GAME%" "%COPY%\" /E /I /H /Q /Y /EXCLUDE:files\copy_exclude.txt >nul
if errorlevel 1 goto copyfail
copy /y "%STOCKBL%" "%COPY%\baselib_orig.dll" >nul || goto copyfail
for %%F in ("%COPY%\GameAssembly.dll") do set "CSIZE=%%~zF"
if not "%CSIZE%"=="%WANTSIZE%" goto copyfail

:files
copy /y "files\baselib.dll" "%COPY%\baselib.dll" >nul || goto copyfail
> "%COPY%\steam_appid.txt" echo 2798330
> "%COPY%\dsmodded_build.txt" echo %BUILD%
copy /y "files\Dungeon Settlers Modded.exe" "%ROOT%\Dungeon Settlers Modded.exe" >nul || goto copyfail
copy /y "uninstall.bat" "%ROOT%\uninstall.bat" >nul
if not exist "%ROOT%\mods" mkdir "%ROOT%\mods"
xcopy "files\mods" "%ROOT%\mods\" /E /I /Q /Y >nul
if errorlevel 2 goto copyfail
rem what this install put there (the uninstall deletes exactly these files, then the empty folders in the list)
if exist "%ROOT%\install_files.log" copy /y "%ROOT%\install_files.log" "%ROOT%\install_files.prev.log" >nul
(
  dir /s /b /a-d "%COPY%" "%ROOT%\mods"
  echo %ROOT%\Dungeon Settlers Modded.exe
) > "%ROOT%\install_files.log"
dir /s /b /ad "%COPY%" "%ROOT%\mods" > "%ROOT%\install_dirs.log"

rem shortcuts "Dungeon Settlers Modded" on the desktop and in the Start menu
set "LNKTARGET=%ROOT%\Dungeon Settlers Modded.exe"
set "DESK="
for /f "tokens=2,*" %%a in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders" /v Desktop 2^>nul') do set "DESK=%%b"
if not defined DESK set "DESK=%USERPROFILE%\Desktop"
set "STARTM=%APPDATA%\Microsoft\Windows\Start Menu\Programs"
for %%D in ("%DESK%" "%STARTM%") do (
  set "LNKDIR=%%~D"
  powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut($env:LNKDIR+'\Dungeon Settlers Modded.lnk'); $s.TargetPath=$env:LNKTARGET; $s.WorkingDirectory=$env:ROOT; $s.IconLocation=$env:LNKTARGET+',0'; $s.Save()" >nul 2>&1
)
if not exist "%DESK%\Dungeon Settlers Modded.lnk" (
  > "%DESK%\Dungeon Settlers Modded.bat" echo @start "" "%LNKTARGET%"
)

echo.
echo  Done. Dungeon Settlers Modded is installed.
echo.
echo  To play with mods: double-click "Dungeon Settlers Modded" on your desktop.
echo  To play the normal game: start it from Steam as usual. Steam's game is not changed.
echo.
if not defined DSMODDED_NOPAUSE pause
exit /b 0

:nokit
echo  The kit files are missing.
echo  Right-click the zip file, click "Extract All", then open the extracted folder
echo  and double-click install.bat there.
goto fail

:notours
echo.
echo  The folder %ROOT% already holds other files.
echo  Nothing was changed. Choose an empty folder, or a new one.
goto fail

:copyfail
echo.
echo  The game could not be copied into %ROOT%.
echo  Close the game and check that the disk has 1 GB free. Then run install.bat again.
:fail
echo.
if not defined DSMODDED_NOPAUSE pause
exit /b 1
