; Uninstall (not an update): remove what the launcher made in its own folder that costs disk: the game copies
; (games\, about 700 MB per build), downloaded mods (cache\) and replaced copies (retired\). The player's
; multiplayer saves (profiles\), units (builder\) and mod choices (state.json) stay, like the friend kit's uninstall.
; "Delete the application data" ticked: the whole launcher folder goes. Steam's game folder is never touched.
!macro NSIS_HOOK_POSTUNINSTALL
  ${If} $UpdateMode <> 1
    SetShellVarContext current
    RMDir /r "$LOCALAPPDATA\DSModLauncher\games"
    RMDir /r "$LOCALAPPDATA\DSModLauncher\cache"
    RMDir /r "$LOCALAPPDATA\DSModLauncher\retired"
    ${If} $DeleteAppDataCheckboxState = 1
      RMDir /r "$LOCALAPPDATA\DSModLauncher"
    ${EndIf}
  ${EndIf}
!macroend
