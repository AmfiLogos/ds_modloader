@echo off
setlocal EnableExtensions
title Dungeon Settlers Modded - uninstall
echo.
echo  Dungeon Settlers Modded - uninstall
echo  ===================================
echo.
set "ROOT=%LOCALAPPDATA%\Dungeon Settlers Modded"
if not exist "%ROOT%" (
  echo  Dungeon Settlers Modded is not installed.
  goto done
)
tasklist /fi "imagename eq DungeonSettlers.exe" 2>nul | find /i "DungeonSettlers.exe" >nul
if not errorlevel 1 (
  echo  Dungeon Settlers is running. Close the game, then run the uninstall again.
  goto fail
)
rem only what the install put there (install_files.log), then its empty folders (install_dirs.log, deepest first).
rem Saves and logs (profiles\), player mods and any other file stay.
if not exist "%ROOT%\install_files.log" (
  echo  This install has no file list ^(an older kit^). Nothing was removed.
  echo  Install this kit once over it, then uninstall again.
  goto fail
)
rem the game copy (marker inside) and the auto-update folder are ours from top to bottom: every file in them goes
if exist "%ROOT%\game\dsmodded_build.txt" del /s /f /q "%ROOT%\game\*" >nul 2>nul
if exist "%ROOT%\update\" (
  del /s /f /q "%ROOT%\update\*" >nul 2>nul
  if exist "%ROOT%\RELEASE" del /f /q "%ROOT%\RELEASE"
)
rem the listed files: the lists are UTF-16 (any language); with code page 65001 "type" gives them to for /f as UTF-8
for /f "tokens=2 delims=:." %%c in ('chcp') do set "OLDCP=%%c"
chcp 65001 >nul
for /f "usebackq delims=" %%f in (`type "%ROOT%\install_files.log"`) do if exist "%%f" del /f /q "%%f"
if defined OLDCP chcp %OLDCP% >nul
rem then the folders our install made, deepest first: rd without /s removes a folder only when it is empty (each pass
rem removes one level), so a folder that still holds a file nobody of ours made stays; profiles\ is never touched
for %%r in (game mods update retired cache licenses) do if exist "%ROOT%\%%r\" (
  for /l %%i in (1,1,12) do for /f "delims=" %%d in ('dir /s /b /ad "%ROOT%\%%r" 2^>nul') do rd "%%d" 2>nul
  rd "%ROOT%\%%r" 2>nul
)
for %%f in (install_files.log install_files.prev.log install_dirs.log) do if exist "%ROOT%\%%f" del /q "%ROOT%\%%f"
rem the marker stays while our leftovers (saves, player mods) stay: a reinstall into this folder is accepted
if not exist "%ROOT%\profiles\" if not exist "%ROOT%\mods\" if exist "%ROOT%\dsmodded_install.txt" del /q "%ROOT%\dsmodded_install.txt"
set "DESK="
for /f "tokens=2,*" %%a in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders" /v Desktop 2^>nul') do set "DESK=%%b"
if not defined DESK set "DESK=%USERPROFILE%\Desktop"
for %%f in (lnk bat) do if exist "%DESK%\Dungeon Settlers Modded.%%f" del /q "%DESK%\Dungeon Settlers Modded.%%f"
set "STARTM=%APPDATA%\Microsoft\Windows\Start Menu\Programs"
if exist "%STARTM%\Dungeon Settlers Modded.lnk" del /q "%STARTM%\Dungeon Settlers Modded.lnk"
if exist "%ROOT%\game\GameAssembly.dll" (
  echo  Some files could not be removed. Close the game, then run the uninstall again.
  goto fail
)
echo  Done. The modded game copy is removed. Steam's game was never changed.
echo  Your modded saves are kept in %ROOT%\profiles
echo  You can delete that folder if you do not need them.
:done
echo.
if not defined DSMODDED_NOPAUSE pause
if /i "%~dp0"=="%ROOT%\" (goto) 2>nul & del "%~f0"
exit /b 0
:fail
echo.
if not defined DSMODDED_NOPAUSE pause
exit /b 1
