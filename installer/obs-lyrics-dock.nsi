; ============================================================
; OBS Lyrics Dock — NSIS Installer  v12.10.1
; ============================================================
; Targets: Windows 10/11 x64
; Installs into the OBS Studio plugin directories, auto-detected
; from the registry or the default path.
; ============================================================

Unicode true
SetCompressor /SOLID lzma

; ── Plugin metadata ─────────────────────────────────────────
!define PLUGIN_NAME    "OBS Lyrics Dock"
!define PLUGIN_VERSION "12.10.1"
; v12.10.1: Paragraph mode, audio/video mutual exclusion, pinyin decimal spacing, lyrics padding
!define PLUGIN_AUTHOR  "OBS Lyrics Dock"
!define PLUGIN_DLL     "obs-lyrics-dock.dll"
!define PLUGIN_ID      "obs-lyrics-dock"
!define UNINST_KEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PLUGIN_ID}"

; ── MUI2 ────────────────────────────────────────────────────
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

Name              "${PLUGIN_NAME} v${PLUGIN_VERSION}"
OutFile           "OBS-Lyrics-Dock-v12.10.1-Setup.exe"
InstallDir        "$PROGRAMFILES64\obs-studio"
InstallDirRegKey  HKLM "Software\OBS Studio" ""

; Request admin rights (needed to write into Program Files)
RequestExecutionLevel admin

; ── MUI Pages ───────────────────────────────────────────────
!define MUI_ABORTWARNING
!define MUI_ICON   "..\installer\icon.ico"
!define MUI_UNICON "..\installer\icon.ico"

!define MUI_WELCOMEPAGE_TITLE  "Welcome to ${PLUGIN_NAME} Setup"
!define MUI_WELCOMEPAGE_TEXT   "This wizard will install ${PLUGIN_NAME} v${PLUGIN_VERSION} into your OBS Studio installation.$\r$\n$\r$\nOBS Studio must already be installed. Please close OBS before continuing."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE   "..\installer\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TEXT     "${PLUGIN_NAME} has been installed successfully.$\r$\n$\r$\nStart OBS Studio to begin using the Lyrics Dock."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Launch OBS Studio"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchOBS
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; ── Version info ────────────────────────────────────────────
VIProductVersion "12.10.1.0"
VIAddVersionKey "ProductName"      "${PLUGIN_NAME}"
VIAddVersionKey "ProductVersion"   "${PLUGIN_VERSION}"
VIAddVersionKey "FileVersion"      "12.10.1.0"
VIAddVersionKey "FileDescription"  "${PLUGIN_NAME} Installer"
VIAddVersionKey "LegalCopyright"   "© 2026 ${PLUGIN_AUTHOR}"

; ── Installer section ────────────────────────────────────────
Section "Plugin (required)" SecPlugin
  SectionIn RO   ; always installed

  ; Verify the selected directory looks like OBS
  IfFileExists "$INSTDIR\bin\64bit\obs64.exe" obsFound 0
  IfFileExists "$INSTDIR\obs64.exe"           obsFound 0
    MessageBox MB_ICONEXCLAMATION|MB_OKCANCEL \
      "OBS Studio does not appear to be installed in:$\r$\n$INSTDIR$\r$\n$\r$\nInstall anyway?" \
      IDOK obsFound
    Abort

  obsFound:

  ; -- DLL → obs-plugins/64bit/
  SetOutPath "$INSTDIR\obs-plugins\64bit"
  File "..\build\Release\${PLUGIN_DLL}"

  ; -- Data files → data/obs-plugins/obs-lyrics-dock/
  SetOutPath "$INSTDIR\data\obs-plugins\${PLUGIN_ID}"
  File "..\data\remote.html"

  SetOutPath "$INSTDIR\data\obs-plugins\${PLUGIN_ID}\locale"
  File "..\data\locale\en-US.ini"

  ; -- Write uninstall registry entries
  WriteRegStr   HKLM "${UNINST_KEY}" "DisplayName"     "${PLUGIN_NAME}"
  WriteRegStr   HKLM "${UNINST_KEY}" "DisplayVersion"  "${PLUGIN_VERSION}"
  WriteRegStr   HKLM "${UNINST_KEY}" "Publisher"       "${PLUGIN_AUTHOR}"
  WriteRegStr   HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr   HKLM "${UNINST_KEY}" "UninstallString" \
      '"$INSTDIR\data\obs-plugins\${PLUGIN_ID}\Uninstall.exe"'
  WriteRegStr   HKLM "${UNINST_KEY}" "DisplayIcon"     \
      "$INSTDIR\obs-plugins\64bit\${PLUGIN_DLL}"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1

  ; Estimate installed size (in KB)
  ${GetSize} "$INSTDIR\obs-plugins\64bit" "/S=0K" $0 $1 $2
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" "$0"

  ; -- Write uninstaller executable into the data folder
  WriteUninstaller "$INSTDIR\data\obs-plugins\${PLUGIN_ID}\Uninstall.exe"

SectionEnd

; ── Uninstaller section ──────────────────────────────────────
Section "Uninstall"

  ; DLL
  Delete "$INSTDIR\obs-plugins\64bit\${PLUGIN_DLL}"

  ; Data files
  Delete "$INSTDIR\data\obs-plugins\${PLUGIN_ID}\remote.html"
  Delete "$INSTDIR\data\obs-plugins\${PLUGIN_ID}\locale\en-US.ini"
  RMDir  "$INSTDIR\data\obs-plugins\${PLUGIN_ID}\locale"
  Delete "$INSTDIR\data\obs-plugins\${PLUGIN_ID}\Uninstall.exe"
  RMDir  "$INSTDIR\data\obs-plugins\${PLUGIN_ID}"

  ; Registry
  DeleteRegKey HKLM "${UNINST_KEY}"

SectionEnd

; ── Launch OBS helper ────────────────────────────────────────
Function LaunchOBS
  IfFileExists "$INSTDIR\bin\64bit\obs64.exe" 0 tryRoot
    Exec '"$INSTDIR\bin\64bit\obs64.exe"'
    Return
  tryRoot:
  IfFileExists "$INSTDIR\obs64.exe" 0 done
    Exec '"$INSTDIR\obs64.exe"'
  done:
FunctionEnd

; ── Directory page validation ────────────────────────────────
; Warn if chosen dir doesn't look like OBS, but allow it anyway.
Function .onVerifyInstDir
  IfFileExists "$INSTDIR\obs-plugins\64bit\*.*" +2
    MessageBox MB_ICONINFORMATION \
      "The selected folder does not contain an 'obs-plugins\64bit' subfolder.$\r$\nMake sure you are pointing at the root OBS Studio folder.$\r$\n$\r$\nDefault: C:\Program Files\obs-studio" \
      /SD IDOK
FunctionEnd
