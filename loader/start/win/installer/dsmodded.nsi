; Dungeon Settlers Modded - Windows installer (NSIS 3, per user, no admin). Built by make_installer.sh:
;   makensis -DKIT=<kit folder> -DOUT=<setup.exe> -DVER=<x.y.z> -DICON=<ico> dsmodded.nsi
; It runs the kit's own install.bat (the same game search, version check, game copy and mods as the zip kit) into the
; chosen folder (DSMODDED_ROOT), then adds the shortcuts, an uninstaller and the "Apps & features" entry.
; Silent (tests): Setup.exe /S [/GAME=<Steam game folder>] [/D=<install folder>]  (/D last, no quotes)
Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!define NAME "Dungeon Settlers Modded"
!define UKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\DungeonSettlersModded"
Name "${NAME}"
OutFile "${OUT}"
InstallDir "$LOCALAPPDATA\${NAME}"
ShowInstDetails show
ShowUninstDetails show
BrandingText "${NAME} ${VER}"
VIProductVersion "${VER}.0"
VIAddVersionKey "ProductName" "${NAME}"
VIAddVersionKey "CompanyName" "AmfiLogos"
VIAddVersionKey "FileDescription" "${NAME} installer"
VIAddVersionKey "FileVersion" "${VER}"
VIAddVersionKey "ProductVersion" "${VER}"
VIAddVersionKey "LegalCopyright" "MIT license"

Var GameDir

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "This installs ${NAME}: a mod loader for Dungeon Settlers, with the Multiplayer, Custom Modes, Hell difficulty and cooldown mods.$\r$\n$\r$\nIt plays a copy of the game. Steam's game, your normal saves and your settings do not change.$\r$\n$\r$\nQuit Dungeon Settlers before you continue."
!insertmacro MUI_PAGE_WELCOME

!define MUI_PAGE_HEADER_TEXT "Game folder"
!define MUI_PAGE_HEADER_SUBTEXT "Where Steam installed Dungeon Settlers."
!define MUI_DIRECTORYPAGE_TEXT_TOP "This is the folder of Dungeon Settlers in Steam (it has DungeonSettlers.exe). It is only read, never changed.$\r$\n$\r$\nTo find it: in Steam, right-click Dungeon Settlers, click Manage, then Browse local files."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "Dungeon Settlers game folder"
!define MUI_DIRECTORYPAGE_VARIABLE $GameDir
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE GameDirLeave
!insertmacro MUI_PAGE_DIRECTORY

!define MUI_PAGE_HEADER_TEXT "Install folder"
!define MUI_PAGE_HEADER_SUBTEXT "Where the modded copy of the game goes (about 1 GB)."
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${NAME}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Play now"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  InitPluginsDir
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/GAME=" $GameDir
  ${If} $GameDir == ""
    ; the kit's own search (Steam registry, library folders, usual drives)
    File "/oname=$PLUGINSDIR\find_game.bat" "${KIT}\files\find_game.bat"
    nsExec::ExecToStack '"$SYSDIR\cmd.exe" /v:on /c call "$PLUGINSDIR\find_game.bat" & echo.!GAME!'
    Pop $1
    Pop $2
    ${If} $1 == 0
      ; the last line of the output, without CR / LF
      ${Do}
        StrCpy $3 $2 1 -1
        ${If} $3 != "$\n"
        ${AndIf} $3 != "$\r"
          ${Break}
        ${EndIf}
        StrCpy $2 $2 -1
      ${Loop}
      StrCpy $GameDir $2
    ${EndIf}
  ${EndIf}
  ${If} ${Silent}
  ${AndIfNot} ${FileExists} "$GameDir\DungeonSettlers.exe"
    SetErrorLevel 2
    Abort
  ${EndIf}
FunctionEnd

Function GameDirLeave
  ${IfNot} ${FileExists} "$GameDir\DungeonSettlers.exe"
    MessageBox MB_OK|MB_ICONEXCLAMATION "DungeonSettlers.exe is not in this folder. Choose the Dungeon Settlers folder of Steam."
    Abort
  ${EndIf}
FunctionEnd

Section "Install"
  SetOutPath "$PLUGINSDIR\kit"
  File /r "${KIT}\*"
  CreateDirectory "$INSTDIR"
  System::Call 'kernel32::SetEnvironmentVariable(t "DSMODDED_NOPAUSE", t "1")'
  System::Call 'kernel32::SetEnvironmentVariable(t "DSMODDED_ROOT", t "$INSTDIR")'
  DetailPrint "Copying the game (about 700 MB). This takes a minute or two."
  nsExec::ExecToLog '"$SYSDIR\cmd.exe" /c call "$PLUGINSDIR\kit\install.bat" "$GameDir"'
  Pop $0
  ${If} $0 != 0
    SetDetailsView show
    MessageBox MB_OK|MB_ICONSTOP "The install did not finish. The reason is in the list above." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  SetOutPath "$INSTDIR"
  ; the installer's shortcuts and uninstaller replace the kit's (uninstall.bat only knows the default folder)
  Delete "$INSTDIR\uninstall.bat"
  ${If} ${FileExists} "$DESKTOP\${NAME}.bat"
    Delete "$DESKTOP\${NAME}.bat"
  ${EndIf}
  CreateShortcut "$DESKTOP\${NAME}.lnk" "$INSTDIR\${NAME}.exe" "" "$INSTDIR\${NAME}.exe" 0
  CreateShortcut "$SMPROGRAMS\${NAME}.lnk" "$INSTDIR\${NAME}.exe" "" "$INSTDIR\${NAME}.exe" 0
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${UKEY}" "DisplayName" "${NAME}"
  WriteRegStr HKCU "${UKEY}" "DisplayVersion" "${VER}"
  WriteRegStr HKCU "${UKEY}" "Publisher" "AmfiLogos"
  WriteRegStr HKCU "${UKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UKEY}" "DisplayIcon" "$INSTDIR\${NAME}.exe"
  WriteRegStr HKCU "${UKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UKEY}" "NoRepair" 1
  DetailPrint "Done. Double-click ${NAME} on your desktop to play."
SectionEnd

Section "Uninstall"
  Call un.TasklistGame
  Pop $0
  ${If} $0 == 1
    MessageBox MB_OK|MB_ICONEXCLAMATION "Dungeon Settlers is running. Close the game, then uninstall again." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  ; the game copy, the mods, the starter, the downloads and the auto-update folder go; saves and logs (profiles\) stay
  RMDir /r "$INSTDIR\game"
  RMDir /r "$INSTDIR\mods"
  RMDir /r "$INSTDIR\retired"
  RMDir /r "$INSTDIR\cache"
  RMDir /r "$INSTDIR\update"
  Delete "$INSTDIR\RELEASE"
  Delete "$INSTDIR\${NAME}.exe"
  Delete "$INSTDIR\uninstall.bat"
  Delete "$DESKTOP\${NAME}.lnk"
  Delete "$DESKTOP\${NAME}.bat"
  Delete "$SMPROGRAMS\${NAME}.lnk"
  DeleteRegKey HKCU "${UKEY}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  ${If} ${FileExists} "$INSTDIR\game\*.*"
    MessageBox MB_OK|MB_ICONEXCLAMATION "Some files could not be removed. Close the game, then uninstall again." /SD IDOK
    SetErrorLevel 1
  ${ElseIf} ${FileExists} "$INSTDIR\profiles\*.*"
    MessageBox MB_OK "${NAME} is removed. Steam's game was never changed.$\r$\nYour modded saves stay in $INSTDIR\profiles. You can delete that folder if you do not need them." /SD IDOK
  ${EndIf}
SectionEnd

Function un.TasklistGame
  nsExec::ExecToStack '"$SYSDIR\cmd.exe" /c tasklist /fi "imagename eq DungeonSettlers.exe" | find /i "DungeonSettlers.exe"'
  Pop $0
  Pop $1
  ${If} $0 == 0
    Push 1
  ${Else}
    Push 0
  ${EndIf}
FunctionEnd
