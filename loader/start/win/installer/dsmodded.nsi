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
!include "TextFunc.nsh"

!define NAME "Dungeon Settlers Modded"
!define UKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\DungeonSettlersModded"
Name "${NAME}"
OutFile "${OUT}"
InstallDir "$LOCALAPPDATA\${NAME}"
; the next Setup offers the folder of the last install
InstallDirRegKey HKCU "${UKEY}" "InstallLocation"
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
Var LangType                                  ; the game's LanguageType 0..9 of the texts (install.bat gets it too)

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "$(S_WELCOME)"
!insertmacro MUI_PAGE_WELCOME

!define MUI_PAGE_HEADER_TEXT "$(S_GAME_HEADER)"
!define MUI_PAGE_HEADER_SUBTEXT "$(S_GAME_SUB)"
!define MUI_DIRECTORYPAGE_TEXT_TOP "$(S_GAME_TOP)"
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "$(S_GAME_DEST)"
!define MUI_DIRECTORYPAGE_VARIABLE $GameDir
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE GameDirLeave
!insertmacro MUI_PAGE_DIRECTORY

!define MUI_PAGE_HEADER_TEXT "$(S_INST_HEADER)"
!define MUI_PAGE_HEADER_SUBTEXT "$(S_INST_SUB)"
!define MUI_DIRECTORYPAGE_TEXT_TOP "$(S_INST_TOP)"
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE InstDirLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${NAME}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "$(S_PLAY)"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
; the game's 10 languages (LanguageType order); the first is the default. The texts: lang.nsh, made by
; setup_lang/gen_setup_lang.py (make_installer.sh) from setup_lang/<code>.json
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "TradChinese"
!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "PortugueseBR"
!include "${LANGNSH}"

; SetLang <starter exe>: the language the way the game picks it (its saved language, else the Windows display language,
; else English): the starter's --lang <install folder> gives the game's LanguageType as its exit code
!macro SetLang EXE
  StrCpy $LangType 0
  ${If} ${FileExists} "${EXE}"
    ClearErrors
    ExecWait '"${EXE}" --lang "$INSTDIR"' $0
    ${IfNot} ${Errors}
    ${AndIf} $0 >= 0
    ${AndIf} $0 <= 9
      StrCpy $LangType $0
    ${EndIf}
  ${EndIf}
  ${Select} $LangType
    ${Case} 1
      StrCpy $LANGUAGE ${LANG_KOREAN}
    ${Case} 2
      StrCpy $LANGUAGE ${LANG_FRENCH}
    ${Case} 3
      StrCpy $LANGUAGE ${LANG_GERMAN}
    ${Case} 4
      StrCpy $LANGUAGE ${LANG_RUSSIAN}
    ${Case} 5
      StrCpy $LANGUAGE ${LANG_SIMPCHINESE}
    ${Case} 6
      StrCpy $LANGUAGE ${LANG_TRADCHINESE}
    ${Case} 7
      StrCpy $LANGUAGE ${LANG_JAPANESE}
    ${Case} 8
      StrCpy $LANGUAGE ${LANG_SPANISH}
    ${Case} 9
      StrCpy $LANGUAGE ${LANG_PORTUGUESEBR}
    ${CaseElse}
      StrCpy $LANGUAGE ${LANG_ENGLISH}
  ${EndSelect}
!macroend

Function .onInit
  InitPluginsDir
  File "/oname=$PLUGINSDIR\dsm_lang.exe" "${KIT}\files\${NAME}.exe"
  !insertmacro SetLang "$PLUGINSDIR\dsm_lang.exe"
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
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(S_NO_EXE)"
    Abort
  ${EndIf}
FunctionEnd

; the install always goes into its own folder: "<chosen>\${NAME}" unless the chosen folder already has that name.
; A folder that holds other files (no marker of ours) is refused: the uninstall then never meets a file it did not put there.
Function OwnFolder
  ${GetFileName} "$INSTDIR" $0
  ${If} $0 != "${NAME}"
    StrCpy $INSTDIR "$INSTDIR\${NAME}"
  ${EndIf}
  StrCpy $1 0                                 ; 1 = ours
  ${If} ${FileExists} "$INSTDIR\dsmodded_install.txt"
    StrCpy $1 1
  ${ElseIf} ${FileExists} "$INSTDIR\game\dsmodded_build.txt"
  ${AndIf} ${FileExists} "$INSTDIR\${NAME}.exe"
    StrCpy $1 1                               ; an install of an older kit (no marker yet)
  ${EndIf}
  StrCpy $2 0                                 ; 1 = holds something that is not ours
  ${If} $1 == 0
    ; what an uninstall of ours leaves (the saves in profiles\, the player's own mods in mods\) is not "other files":
    ; a reinstall into the same folder must work (older uninstallers removed the marker)
    FindFirst $3 $4 "$INSTDIR\*.*"
    ${DoWhile} $4 != ""
      ${If} $4 != "."
      ${AndIf} $4 != ".."
      ${AndIf} $4 != "profiles"
      ${AndIf} $4 != "mods"
        StrCpy $2 1
        ${Break}
      ${EndIf}
      FindNext $3 $4
    ${Loop}
    FindClose $3
  ${EndIf}
  ${If} $1 == 0
  ${AndIf} $2 == 1
    Push 0
  ${Else}
    Push 1
  ${EndIf}
FunctionEnd

; PathOk: Push 1 when the game can start from $INSTDIR, else 0. Letters outside the PC's ANSI code page (Chinese,
; Japanese ... on a Western PC) reach the game as "?": it does not start. A UTF-8 code page (65001) holds every letter.
Function PathOk
  System::Call 'kernel32::GetACP() i .r0'
  ${If} $0 == 65001
    Push 1
    Return
  ${EndIf}
  ; WC_NO_BEST_FIT_CHARS (0x400): a letter without an exact ANSI form sets the "default char used" flag ($1)
  System::Call 'kernel32::WideCharToMultiByte(i 0, i 0x400, w "$INSTDIR", i -1, m .r2, i ${NSIS_MAX_STRLEN}, p 0, *i 0 r1) i .r0'
  ${If} $0 == 0
  ${OrIf} $1 != 0
    Push 0
  ${Else}
    Push 1
  ${EndIf}
FunctionEnd

!define BADPATH_TEXT "$(S_BADPATH)"

Function InstDirLeave
  Call OwnFolder
  Pop $0
  Call PathOk
  Pop $1
  ${If} $1 == 0
    MessageBox MB_OK|MB_ICONEXCLAMATION "${BADPATH_TEXT}"
    Abort
  ${EndIf}
  ${If} $0 == 0
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(S_OTHER_FILES)"
    Abort
  ${EndIf}
FunctionEnd

Section "Install"
  Call OwnFolder                              ; also for /S /D=... (no pages in a silent install)
  Call PathOk
  Pop $1
  Pop $0
  ${If} $1 == 0
    DetailPrint "$(S_BADPATH_LOG)"
    MessageBox MB_OK|MB_ICONSTOP "${BADPATH_TEXT}" /SD IDOK
    SetErrorLevel 4
    Quit
  ${EndIf}
  ${If} $0 == 0
    DetailPrint "$(S_OTHER_FILES_LOG)"
    MessageBox MB_OK|MB_ICONSTOP "$(S_OTHER_FILES_STOP)" /SD IDOK
    SetErrorLevel 3
    Quit
  ${EndIf}
  SetOutPath "$PLUGINSDIR\kit"
  File /r "${KIT}\*"
  CreateDirectory "$INSTDIR"
  System::Call 'kernel32::SetEnvironmentVariable(t "DSMODDED_NOPAUSE", t "1")'
  System::Call 'kernel32::SetEnvironmentVariable(t "DSMODDED_ROOT", t "$INSTDIR")'
  System::Call 'kernel32::SetEnvironmentVariable(t "DSMODDED_LANG", t "$LangType")'
  DetailPrint "$(S_COPYING)"
  ; /U: install.bat's lines reach this list as UTF-16, whatever their language (nsExec reads UTF-16 output)
  nsExec::ExecToLog '"$SYSDIR\cmd.exe" /U /c call "$PLUGINSDIR\kit\install.bat" "$GameDir"'
  Pop $0
  ${If} $0 != 0
    SetDetailsView show
    MessageBox MB_OK|MB_ICONSTOP "$(S_FAILED)" /SD IDOK
    SetErrorLevel 1
    ${IfThen} ${Silent} ${|} Quit ${|}
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
  DetailPrint "$(S_DONE)"
SectionEnd

Function un.onInit
  !insertmacro SetLang "$INSTDIR\${NAME}.exe"
FunctionEnd

Section "Uninstall"
  Call un.TasklistGame
  Pop $0
  ${If} $0 == 1
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(S_UN_RUNNING)" /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  ; only what the install and the starter put there: the files of install_files.log (inside this folder only), then the
  ; empty folders of install_dirs.log (deepest first). Saves (profiles\) and files that nobody of ours made stay.
  ; Never RMDir /r.
  ${IfNot} ${FileExists} "$INSTDIR\install_files.log"
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(S_UN_NO_LIST)" /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  ; the game copy is ours from top to bottom (install.bat made it, marker inside) and so is the auto-update folder: every
  ; file in them goes, whatever the game, its loader or an update added later (Unicode-safe walk, no list needed)
  ${If} ${FileExists} "$INSTDIR\game\dsmodded_build.txt"
  ${OrIf} ${FileExists} "$INSTDIR\game\dsmodded_copy.txt"
    Push "$INSTDIR\game"
    Call un.DeleteFilesUnder
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\update\*.*"
    Push "$INSTDIR\update"
    Call un.DeleteFilesUnder
    Delete "$INSTDIR\RELEASE"
  ${EndIf}
  ; so are the download cache (BepInEx / MelonLoader zips) and retired\ (old mod folders, rolled-back files)
  ${If} ${FileExists} "$INSTDIR\cache\*.*"
    Push "$INSTDIR\cache"
    Call un.DeleteFilesUnder
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\retired\*.*"
    Push "$INSTDIR\retired"
    Call un.DeleteFilesUnder
  ${EndIf}
  ; the files of install_files.log (inside this folder only): UTF-16LE with a BOM (written by the starter), or the OEM
  ; text of an older install
  StrLen $5 "$INSTDIR\"
  ClearErrors
  FileOpen $6 "$INSTDIR\install_files.log" r
  FileReadByte $6 $0
  FileReadByte $6 $1
  StrCpy $9 0
  ${If} $0 == 255
  ${AndIf} $1 == 254
    StrCpy $9 1
  ${Else}
    FileSeek $6 0 SET
  ${EndIf}
  ClearErrors
  ${Do}
    ${If} $9 == 1
      FileReadUTF16LE $6 $7
    ${Else}
      FileRead $6 $7
    ${EndIf}
    ${If} ${Errors}
      ${Break}
    ${EndIf}
    ${un.TrimNewLines} $7 $7
    StrCpy $8 $7 $5
    ${If} $8 == "$INSTDIR\"
      Delete "$7"
    ${EndIf}
  ${Loop}
  FileClose $6
  ; then the folders our install made, deepest first; RMDir without /r removes a folder only when it is empty, so a
  ; folder that still holds a file nobody of ours made stays (profiles\ is never touched)
  Push "$INSTDIR\game"
  Call un.RemoveEmptyDirs
  Push "$INSTDIR\mods"
  Call un.RemoveEmptyDirs
  Push "$INSTDIR\update"
  Call un.RemoveEmptyDirs
  Push "$INSTDIR\retired"
  Call un.RemoveEmptyDirs
  Push "$INSTDIR\cache"
  Call un.RemoveEmptyDirs
  Push "$INSTDIR\licenses"
  Call un.RemoveEmptyDirs
  Delete "$INSTDIR\install_files.log"
  Delete "$INSTDIR\install_files.prev.log"
  Delete "$INSTDIR\install_dirs.log"
  ; the marker stays while our leftovers (saves, player mods) stay: a reinstall into this folder is accepted
  ${IfNot} ${FileExists} "$INSTDIR\profiles\*.*"
  ${AndIfNot} ${FileExists} "$INSTDIR\mods\*.*"
    Delete "$INSTDIR\dsmodded_install.txt"
  ${EndIf}
  Delete "$INSTDIR\uninstall.bat"
  Delete "$DESKTOP\${NAME}.lnk"
  Delete "$DESKTOP\${NAME}.bat"
  Delete "$SMPROGRAMS\${NAME}.lnk"
  DeleteRegKey HKCU "${UKEY}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  ${If} ${FileExists} "$INSTDIR\game\GameAssembly.dll"
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(S_UN_LEFT)" /SD IDOK
    SetErrorLevel 1
  ${ElseIf} ${FileExists} "$INSTDIR\profiles\*.*"
    MessageBox MB_OK "$(S_UN_DONE)" /SD IDOK
  ${EndIf}
SectionEnd

; un.DeleteFilesUnder <folder>: every file below <folder> (folders stay for un.RemoveEmptyDirs). Only called for folders
; that are ours from top to bottom (the marked game copy, the auto-update folder, the download cache, retired\).
Function un.DeleteFilesUnder
  Exch $R0
  Push $R1
  Push $R2
  ClearErrors
  FindFirst $R1 $R2 "$R0\*.*"
  ${DoUntil} ${Errors}
    ${If} $R2 != "."
    ${AndIf} $R2 != ".."
      ${If} ${FileExists} "$R0\$R2\*.*"
        Push "$R0\$R2"
        Call un.DeleteFilesUnder
      ${Else}
        Delete "$R0\$R2"
      ${EndIf}
    ${EndIf}
    ClearErrors
    FindNext $R1 $R2
  ${Loop}
  FindClose $R1
  Pop $R2
  Pop $R1
  Pop $R0
FunctionEnd

; un.RemoveEmptyDirs <folder>: every empty folder below <folder>, children before parents, then <folder> itself when
; empty. Never deletes a file.
Function un.RemoveEmptyDirs
  Exch $R0
  Push $R1
  Push $R2
  ClearErrors
  FindFirst $R1 $R2 "$R0\*.*"
  ${DoUntil} ${Errors}
    ${If} $R2 != "."
    ${AndIf} $R2 != ".."
    ${AndIf} ${FileExists} "$R0\$R2\*.*"
      Push "$R0\$R2"
      Call un.RemoveEmptyDirs
    ${EndIf}
    ClearErrors
    FindNext $R1 $R2
  ${Loop}
  FindClose $R1
  RMDir "$R0"
  Pop $R2
  Pop $R1
  Pop $R0
FunctionEnd

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
