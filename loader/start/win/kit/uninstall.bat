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
rem the game copy is ours from top to bottom (marker inside): what the game or its loader added there joins the lists
if exist "%ROOT%\game\dsmodded_build.txt" (
  dir /s /b /a-d "%ROOT%\game" >> "%ROOT%\install_files.log"
  dir /s /b /ad "%ROOT%\game" >> "%ROOT%\install_dirs.log"
)
rem the auto-update folder (downloads, old versions, journal) is made only by our starter and loader
if exist "%ROOT%\update\" (
  dir /s /b /a-d "%ROOT%\update" >> "%ROOT%\install_files.log"
  dir /s /b /ad "%ROOT%\update" >> "%ROOT%\install_dirs.log"
  >> "%ROOT%\install_dirs.log" echo %ROOT%\update
)
for /f "usebackq delims=" %%f in ("%ROOT%\install_files.log") do if exist "%%f" del /f /q "%%f"
rem then the folders our install made, deepest first: rd without /s removes a folder only when it is empty (each pass
rem removes one level), so a folder that still holds a file nobody of ours made stays; profiles\ is never touched
for %%r in (game mods update retired cache) do if exist "%ROOT%\%%r\" (
  for /l %%i in (1,1,12) do for /f "delims=" %%d in ('dir /s /b /ad "%ROOT%\%%r" 2^>nul') do rd "%%d" 2>nul
  rd "%ROOT%\%%r" 2>nul
)
for %%f in (install_files.log install_files.prev.log install_dirs.log dsmodded_install.txt) do if exist "%ROOT%\%%f" del /q "%ROOT%\%%f"
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
