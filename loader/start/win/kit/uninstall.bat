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
rem the game copy, the mods and the starter go. Saves and logs (profiles\) stay.
if exist "%ROOT%\game" rd /s /q "%ROOT%\game"
if exist "%ROOT%\mods" rd /s /q "%ROOT%\mods"
if exist "%ROOT%\Dungeon Settlers Modded.exe" del /q "%ROOT%\Dungeon Settlers Modded.exe"
if exist "%ROOT%\RELEASE" del /q "%ROOT%\RELEASE"
if exist "%ROOT%\update" rd /s /q "%ROOT%\update"
set "DESK="
for /f "tokens=2,*" %%a in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders" /v Desktop 2^>nul') do set "DESK=%%b"
if not defined DESK set "DESK=%USERPROFILE%\Desktop"
for %%f in (lnk bat) do if exist "%DESK%\Dungeon Settlers Modded.%%f" del /q "%DESK%\Dungeon Settlers Modded.%%f"
set "STARTM=%APPDATA%\Microsoft\Windows\Start Menu\Programs"
if exist "%STARTM%\Dungeon Settlers Modded.lnk" del /q "%STARTM%\Dungeon Settlers Modded.lnk"
if exist "%ROOT%\game" (
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
