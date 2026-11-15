; Jotter's Windows installer (built by build-windows.sh with NSIS). It copies
; the folder laid out there into the user's own programs folder, so it needs no
; administrator rights, and adds a Start menu entry and an uninstaller.
Unicode true
!include "MUI2.nsh"

Name "Jotter"
OutFile "${OUT}"
InstallDir "$LOCALAPPDATA\Programs\Jotter"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ManifestDPIAware true

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\jotter.exe"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!define UNINSTALL "Software\Microsoft\Windows\CurrentVersion\Uninstall\Jotter"
!define OPENWITH "Software\Classes\Applications\jotter.exe"

; Only what Jotter put there: never the folder wholesale.
!macro RemoveFiles
  RMDir /r "$INSTDIR\python"
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\share"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\jotter.exe"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
!macroend

Section
  !insertmacro RemoveFiles              ; an earlier version, if there is one
  SetOutPath "$INSTDIR"
  File /r "${APP}/*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\Jotter.lnk" "$INSTDIR\jotter.exe"

  WriteRegStr HKCU "${UNINSTALL}" "DisplayName" "Jotter"
  WriteRegStr HKCU "${UNINSTALL}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL}" "DisplayIcon" "$INSTDIR\jotter.exe"
  WriteRegStr HKCU "${UNINSTALL}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTALL}" "EstimatedSize" ${SIZE_KB}
  WriteRegDWORD HKCU "${UNINSTALL}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL}" "NoRepair" 1

  ; Offered under "Open with" for Markdown and text files (it takes over nothing).
  WriteRegStr HKCU "${OPENWITH}" "FriendlyAppName" "Jotter"
  WriteRegStr HKCU "${OPENWITH}\shell\open\command" "" '"$INSTDIR\jotter.exe" "%1"'
  WriteRegStr HKCU "${OPENWITH}\SupportedTypes" ".md" ""
  WriteRegStr HKCU "${OPENWITH}\SupportedTypes" ".markdown" ""
  WriteRegStr HKCU "${OPENWITH}\SupportedTypes" ".txt" ""
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\Jotter.lnk"
  !insertmacro RemoveFiles
  DeleteRegKey HKCU "${UNINSTALL}"
  DeleteRegKey HKCU "${OPENWITH}"
SectionEnd
