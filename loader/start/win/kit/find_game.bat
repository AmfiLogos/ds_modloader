@echo off
rem Finds the Steam folder of Dungeon Settlers and sets GAME (empty if not found).
rem Called by install.bat and uninstall.bat. Usage: call find_game.bat [folder]
rem FG_SEEN: the Steam folders that exist and were looked in (quoted, one each), for install.bat's "not found" message
set "GAME="
set "STEAM="
set "FG_SEEN="
if not "%~1"=="" set "FG_SEEN="%~f1""
if not "%~1"=="" (
  if exist "%~1\DungeonSettlers.exe" set "GAME=%~f1"
  goto :eof
)
for /f "tokens=2,*" %%a in ('reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul') do set "STEAM=%%b"
if not defined STEAM for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul') do set "STEAM=%%b"
if not defined STEAM for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\Valve\Steam" /v InstallPath 2^>nul') do set "STEAM=%%b"
if defined STEAM set "STEAM=%STEAM:/=\%"
if defined STEAM call :try "%STEAM%"
if defined GAME goto :eof
rem other Steam library folders (other drives)
if defined STEAM if exist "%STEAM%\steamapps\libraryfolders.vdf" (
  for /f "usebackq tokens=1,*" %%a in ("%STEAM%\steamapps\libraryfolders.vdf") do (
    if /i "%%~a"=="path" if not defined GAME call :trylib "%%~b"
  )
)
if defined GAME goto :eof
for %%d in (C D E F G H) do if not defined GAME (
  call :try "%%d:\Program Files (x86)\Steam"
  call :try "%%d:\Program Files\Steam"
  call :try "%%d:\Steam"
  call :try "%%d:\SteamLibrary"
)
goto :eof

:trylib
set "LIB=%~1"
set "LIB=%LIB:\\=\%"
call :try "%LIB%"
goto :eof

:try
if defined GAME goto :eof
if exist "%~1\steamapps\" call :seen "%~1"
if exist "%~1\steamapps\common\Dungeon Settlers\DungeonSettlers.exe" set "GAME=%~1\steamapps\common\Dungeon Settlers"
goto :eof

rem :seen <folder>: add it to FG_SEEN once (cmd's :x=y replace ignores case)
:seen
setlocal EnableDelayedExpansion
set "S=.!FG_SEEN!"
set "T=!S:"%~1"=!"
if not "!T!"=="!S!" (endlocal & goto :eof)
endlocal & set "FG_SEEN=%FG_SEEN% "%~1""
goto :eof
