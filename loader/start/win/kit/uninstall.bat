@echo off
setlocal EnableExtensions
title Dungeon Settlers Modded - uninstall
echo.
echo  Dungeon Settlers Modded - uninstall
echo  ===================================
echo.
set "ROOT=%LOCALAPPDATA%\Dungeon Settlers Modded"
rem the texts in the game's language (install.bat's way), read before the game copy (and its lang folder) goes
call :texts
call :lang "%ROOT%\Dungeon Settlers Modded.exe" "%ROOT%\game\lang"
if not exist "%ROOT%" (
  call :say U_NOT_INSTALLED
  goto done
)
tasklist /fi "imagename eq DungeonSettlers.exe" 2>nul | find /i "DungeonSettlers.exe" >nul
if not errorlevel 1 (
  call :say U_RUNNING
  goto fail
)
rem only what the install put there (install_files.log), then its empty folders (install_dirs.log, deepest first).
rem Saves and logs (profiles\), player mods and any other file stay.
if not exist "%ROOT%\install_files.log" (
  call :say U_NO_LIST
  call :say U_NO_LIST_2
  goto fail
)
rem the game copy (marker inside) and the auto-update folder are ours from top to bottom: every file in them goes
if exist "%ROOT%\game\dsmodded_build.txt" del /s /f /q "%ROOT%\game\*" >nul 2>nul
if exist "%ROOT%\update\" (
  del /s /f /q "%ROOT%\update\*" >nul 2>nul
  if exist "%ROOT%\RELEASE" del /f /q "%ROOT%\RELEASE"
)
rem so are the download cache (BepInEx / MelonLoader zips) and retired\ (old mod folders, rolled-back files)
for %%r in (cache retired) do if exist "%ROOT%\%%r\" del /s /f /q "%ROOT%\%%r\*" >nul 2>nul
rem the listed files: the lists are UTF-16 (any language), which for /f and type do not read reliably: the starter
rem deletes them (--unlist, only inside ROOT), then the empty folders below game mods update retired cache licenses
rem (a folder that still holds a file nobody of ours made stays; profiles\ is never touched); then its own exe goes.
if not exist "%ROOT%\Dungeon Settlers Modded.exe" goto oldway
start "" /wait "%ROOT%\Dungeon Settlers Modded.exe" --unlist
del /f /q "%ROOT%\Dungeon Settlers Modded.exe" 2>nul
goto unlisted
:oldway
rem without the starter: the old way (type, for /f; rd without /s removes a folder only when it is empty)
for /f "tokens=2 delims=:." %%c in ('chcp') do set "OLDCP=%%c"
chcp 65001 >nul
for /f "usebackq delims=" %%f in (`type "%ROOT%\install_files.log"`) do if exist "%%f" del /f /q "%%f"
if defined OLDCP chcp %OLDCP% >nul
for %%r in (game mods update retired cache licenses) do if exist "%ROOT%\%%r\" (
  for /l %%i in (1,1,12) do for /f "delims=" %%d in ('dir /s /b /ad "%ROOT%\%%r" 2^>nul') do rd "%%d" 2>nul
  rd "%ROOT%\%%r" 2>nul
)
:unlisted
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
  call :say U_LEFT
  goto fail
)
call :say U_DONE
set "UNDONE=1"
if not exist "%ROOT%\profiles\" goto done
call :say U_SAVES
echo  %ROOT%\profiles
call :say U_SAVES_2
:done
echo.
if not defined DSMODDED_NOPAUSE pause
if not defined UNDONE exit /b 0
rem the uninstall's own file goes too, from any start folder: ROOT\uninstall.bat (it may be this very file: "(goto)"
rem ends the batch first, the rest of the line still runs), then ROOT itself when it is empty (saves in profiles\ or
rem the player's own mods in mods\ keep it). Paths compared in their short form (one spelling for one file).
for %%a in ("%ROOT%\uninstall.bat") do set "ROOTBAT=%%~sa"
if /i "%~s0"=="%ROOTBAT%" goto selfdelete
cd /d "%TEMP%"
del /f /q "%ROOT%\uninstall.bat" 2>nul
rd "%ROOT%" 2>nul
exit /b 0
:selfdelete
rem "cmd /c exit 0": the exit code is 0 ("(goto)" set 1)
(goto) 2>nul & cd /d "%TEMP%" & del /f /q "%ROOT%\uninstall.bat" 2>nul & rd "%ROOT%" 2>nul & cmd /d /c exit 0
:fail
echo.
if not defined DSMODDED_NOPAUSE pause
exit /b 1

rem ---- texts: English here; game\lang\setup_<code>.bat has the other languages (made from setup_lang\<code>.json)
:texts
set "U_NOT_INSTALLED=Dungeon Settlers Modded is not installed."
set "U_RUNNING=Dungeon Settlers is running. Close the game, then run the uninstall again."
set "U_NO_LIST=This install has no file list (an older kit). Nothing was removed."
set "U_NO_LIST_2=Install this kit once over it, then uninstall again."
set "U_LEFT=Some files could not be removed. Close the game, then run the uninstall again."
set "U_DONE=Done. The modded game copy is removed. Steam's game was never changed."
set "U_SAVES=Your modded saves are kept in this folder:"
set "U_SAVES_2=You can delete that folder if you do not need them."
exit /b 0

rem :lang <starter> <lang folder>: as in install.bat (the starter's --conlang; the UTF-8 file read with code page 65001)
:lang
if not exist "%~1" exit /b 0
start "" /wait "%~1" --conlang "%ROOT%"
set "LN=%errorlevel%"
set "LC="
for %%a in (1.ko 2.fr 3.de 4.ru 5.zh-Hans 6.zh-Hant 7.ja 8.es 9.pt-BR) do for /f "tokens=1,2 delims=." %%b in ("%%a") do if "%%b"=="%LN%" set "LC=%%c"
if not defined LC exit /b 0
if not exist "%~2\setup_%LC%.bat" exit /b 0
set "OLDCP="
for /f "tokens=2 delims=:." %%c in ('chcp') do set "OLDCP=%%c"
chcp 65001 >nul
call "%~2\setup_%LC%.bat"
if defined OLDCP chcp %OLDCP% >nul
exit /b 0

rem :say <text name>: one line of text, shown as it is (delayed expansion)
:say
setlocal EnableDelayedExpansion
echo  !%~1!
endlocal
exit /b 0
