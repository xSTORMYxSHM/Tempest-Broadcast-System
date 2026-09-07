Unicode true

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!ifndef PRODUCT_VERSION
  !error "PRODUCT_VERSION must be supplied by the release builder."
!endif
!ifndef NUMERIC_FILE_VERSION
  !error "NUMERIC_FILE_VERSION must be supplied by the release builder."
!endif
!ifndef PAYLOAD_DIR
  !error "PAYLOAD_DIR must be supplied by the release builder."
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE must be supplied by the release builder."
!endif
!ifndef PROJECT_ROOT
  !error "PROJECT_ROOT must be supplied by the release builder."
!endif
!ifndef INSTALL_SIZE_KB
  !error "INSTALL_SIZE_KB must be supplied by the release builder."
!endif

!define PRODUCT_NAME "Tempest Broadcast System"
!define PRODUCT_PUBLISHER "Tempest Mainframe"
!define PRODUCT_WEBSITE "https://github.com/xSTORMYxSHM/Tempest-Broadcast-System"
!define PRODUCT_EXECUTABLE "bin\64bit\tempest-broadcast-system.exe"
!define PRODUCT_UPDATER "bin\64bit\tempest-broadcast-updater.exe"
!define PRODUCT_REGISTRY_KEY "Software\Tempest Mainframe\Tempest Broadcast System"
!define UNINSTALL_REGISTRY_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tempest Broadcast System"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Tempest Broadcast System"
InstallDirRegKey HKCU "${PRODUCT_REGISTRY_KEY}" "InstallLocation"
RequestExecutionLevel user
ManifestDPIAware true
CRCCheck force
SetCompressor /SOLID lzma
SetDatablockOptimize on
ShowInstDetails show
ShowUninstDetails show
BrandingText "Tempest Mainframe"

VIProductVersion "${NUMERIC_FILE_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey /LANG=1033 "FileDescription" "${PRODUCT_NAME} Installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (C) Tempest Mainframe contributors"

!define MUI_ABORTWARNING
!define MUI_ICON "${PROJECT_ROOT}\frontend\cmake\windows\tempest-broadcast-system.ico"
!define MUI_UNICON "${PROJECT_ROOT}\frontend\cmake\windows\tempest-broadcast-system.ico"
!define MUI_WELCOMEPAGE_TITLE "Install ${PRODUCT_NAME} ${PRODUCT_VERSION}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${PRODUCT_EXECUTABLE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${PRODUCT_NAME}"
!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose an empty folder or an existing ${PRODUCT_NAME} installation. This location will be remembered for future updates and removal."
!define MUI_STARTMENUPAGE_REGISTRY_ROOT HKCU
!define MUI_STARTMENUPAGE_REGISTRY_KEY "${PRODUCT_REGISTRY_KEY}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "StartMenuFolder"
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "Tempest Broadcast System"

Var StartMenuFolder
Var UpdateMode
Var FreshInstall
Var InstallDirectoryError

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PROJECT_ROOT}\COPYING"
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryPageLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

!macro ValidateInstallDirectory SafeLabel UnsafeLabel LabelPrefix
  StrCpy $InstallDirectoryError ""
  GetFullPathName $R0 "$INSTDIR"
  ${GetRoot} "$R0" $R1
  ${If} $R0 == $R1
    StrCpy $InstallDirectoryError "Drive roots cannot be used as the application folder."
    Goto ${UnsafeLabel}
  ${EndIf}

  ; Never place application files in the settings directory or one of its
  ; children. Settings must remain independent from install/update cleanup.
  GetFullPathName $R1 "$APPDATA\tempest-broadcast-system"
  StrLen $R2 "$R1"
  StrCpy $R3 "$R0" $R2
  ${If} $R3 == $R1
    StrCpy $R3 "$R0" 1 $R2
    ${If} $R3 == ""
      StrCpy $InstallDirectoryError "The Broadcast settings folder cannot be used as the application folder."
      Goto ${UnsafeLabel}
    ${ElseIf} $R3 == "\"
      StrCpy $InstallDirectoryError "A folder inside the Broadcast settings folder cannot be used as the application folder."
      Goto ${UnsafeLabel}
    ${EndIf}
  ${EndIf}

  ; An existing Tempest application folder is a valid update target regardless
  ; of its final folder name, but it must still be writable.
  IfFileExists "$R0\${PRODUCT_EXECUTABLE}" ${LabelPrefix}_existing_install 0

  ; A new custom folder can use any name, but it must not already contain
  ; unrelated content. This keeps uninstall cleanup away from shared folders.
  StrCpy $FreshInstall "1"
  ClearErrors
  FindFirst $R2 $R3 "$R0\*"
  IfErrors ${LabelPrefix}_write_probe 0
${LabelPrefix}_scan_entry:
  StrCmp $R3 "." ${LabelPrefix}_next_entry
  StrCmp $R3 ".." ${LabelPrefix}_next_entry
  FindClose $R2
  StrCpy $InstallDirectoryError "The selected folder contains files that do not belong to an existing ${PRODUCT_NAME} installation."
  Goto ${UnsafeLabel}
${LabelPrefix}_next_entry:
  ClearErrors
  FindNext $R2 $R3
  IfErrors ${LabelPrefix}_empty_directory 0
  Goto ${LabelPrefix}_scan_entry
${LabelPrefix}_empty_directory:
  FindClose $R2
  Goto ${LabelPrefix}_write_probe

${LabelPrefix}_existing_install:
  StrCpy $FreshInstall "0"

  ; Confirm Windows can create and write the chosen location before any payload
  ; is extracted. This prevents a protected folder from leaving a launchable
  ; executable without its required data and themes.
${LabelPrefix}_write_probe:
  ClearErrors
  CreateDirectory "$R0"
  IfErrors ${LabelPrefix}_not_writable 0
  ClearErrors
  FileOpen $R2 "$R0\.tempest-install-write-test.tmp" w
  IfErrors ${LabelPrefix}_not_writable 0
  FileWrite $R2 "Tempest Broadcast System install write test"
  IfErrors ${LabelPrefix}_write_failed 0
  FileClose $R2
  ClearErrors
  Delete "$R0\.tempest-install-write-test.tmp"
  IfErrors ${LabelPrefix}_not_writable 0
  Goto ${SafeLabel}
${LabelPrefix}_write_failed:
  FileClose $R2
  Delete "$R0\.tempest-install-write-test.tmp"
${LabelPrefix}_not_writable:
  StrCpy $InstallDirectoryError "Windows cannot create or write this folder. Choose a location owned by your account, such as the default folder under Local AppData. Program Files normally requires an administrator installer."
  Goto ${UnsafeLabel}
!macroend

!macro RemoveApplicationPayload
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\data"
  RMDir /r "$INSTDIR\licenses"
  RMDir /r "$INSTDIR\obs-plugins"
  Delete "$INSTDIR\AUTHORS"
  Delete "$INSTDIR\COPYING"
  Delete "$INSTDIR\NOTICE.txt"
  Delete "$INSTDIR\PUBLIC_RELEASE.md"
  Delete "$INSTDIR\RELEASE_NOTES_*.md"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
!macroend

!ifdef SIGN_SCRIPT
  !ifndef TRUSTED_SIGNING_ENDPOINT
    !error "TRUSTED_SIGNING_ENDPOINT is required when SIGN_SCRIPT is supplied."
  !endif
  !ifndef TRUSTED_SIGNING_ACCOUNT
    !error "TRUSTED_SIGNING_ACCOUNT is required when SIGN_SCRIPT is supplied."
  !endif
  !ifndef TRUSTED_SIGNING_PROFILE
    !error "TRUSTED_SIGNING_PROFILE is required when SIGN_SCRIPT is supplied."
  !endif

  !define SIGN_COMMAND `pwsh.exe -NoProfile -File "${SIGN_SCRIPT}" "%1" -TrustedSigningEndpoint "${TRUSTED_SIGNING_ENDPOINT}" -TrustedSigningAccount "${TRUSTED_SIGNING_ACCOUNT}" -TrustedSigningProfile "${TRUSTED_SIGNING_PROFILE}"`
  !uninstfinalize `${SIGN_COMMAND}` = 0
  !finalize `${SIGN_COMMAND}` = 0
!endif

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP|MB_OK "${PRODUCT_NAME} requires 64-bit Windows."
    Abort
  ${EndIf}
  SetShellVarContext current
  SetRegView 64

  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/UPDATE" $R1
  ${IfNot} ${Errors}
    StrCpy $UpdateMode "1"
  ${EndIf}
FunctionEnd

Function DirectoryPageLeave
  !insertmacro ValidateInstallDirectory valid_install_directory invalid_install_directory directory_page
invalid_install_directory:
  MessageBox MB_ICONEXCLAMATION|MB_OK "$InstallDirectoryError"
  Abort
valid_install_directory:
  Return
FunctionEnd

Function .onInstFailed
  ; Never remove an existing installation after a failed update. For a fresh
  ; install, remove only the known application payload that this installer may
  ; have copied. User configuration lives outside $INSTDIR and is untouched.
  ${If} $FreshInstall == "1"
    !insertmacro RemoveApplicationPayload
  ${EndIf}
FunctionEnd

Function .onInstSuccess
  ${If} $UpdateMode == "1"
    SetOutPath "$INSTDIR\bin\64bit"
    ExecShell "open" "$INSTDIR\${PRODUCT_EXECUTABLE}"
  ${EndIf}
FunctionEnd

Function un.onInit
  SetShellVarContext current
  SetRegView 64
  GetFullPathName $R0 "$INSTDIR"
  ${GetRoot} "$R0" $R1
  ${If} $R0 == $R1
    Goto unsafe_uninstall_directory
  ${EndIf}
  ReadRegStr $R2 HKCU "${PRODUCT_REGISTRY_KEY}" "InstallLocation"
  ${If} $R2 == ""
    Goto unsafe_uninstall_directory
  ${EndIf}
  GetFullPathName $R2 "$R2"
  ${If} $R0 != $R2
    Goto unsafe_uninstall_directory
  ${EndIf}
  Return
unsafe_uninstall_directory:
  MessageBox MB_ICONSTOP|MB_OK "For safety, ${PRODUCT_NAME} will only uninstall the exact application folder recorded during installation. User settings were not changed."
  Abort
FunctionEnd

Section "${PRODUCT_NAME}" SectionMain
  SectionIn RO
  !insertmacro ValidateInstallDirectory valid_section_install_directory invalid_section_install_directory section_install
invalid_section_install_directory:
  MessageBox MB_ICONSTOP|MB_OK "$InstallDirectoryError" /SD IDOK
  SetErrorLevel 2
  Abort
valid_section_install_directory:
  ClearErrors
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD_DIR}\*"
  IfErrors install_payload_failed 0

  ; Do not register a partial installation. The application, updater, and both
  ; fallback themes are required for a valid first launch and recovery path.
  IfFileExists "$INSTDIR\${PRODUCT_EXECUTABLE}" 0 install_payload_failed
  IfFileExists "$INSTDIR\${PRODUCT_UPDATER}" 0 install_payload_failed
  IfFileExists "$INSTDIR\data\obs-studio\themes\Yami.obt" 0 install_payload_failed
  IfFileExists "$INSTDIR\data\obs-studio\themes\System.obt" 0 install_payload_failed

  ClearErrors
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  IfErrors install_payload_failed 0

  WriteRegStr HKCU "${PRODUCT_REGISTRY_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${PRODUCT_REGISTRY_KEY}" "Version" "${PRODUCT_VERSION}"

  WriteRegStr HKCU "${UNINSTALL_REGISTRY_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKCU "${UNINSTALL_REGISTRY_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_REGISTRY_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKCU "${UNINSTALL_REGISTRY_KEY}" "URLInfoAbout" "${PRODUCT_WEBSITE}"
  WriteRegStr HKCU "${UNINSTALL_REGISTRY_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_REGISTRY_KEY}" "DisplayIcon" "$INSTDIR\${PRODUCT_EXECUTABLE}"
  WriteRegStr HKCU "${UNINSTALL_REGISTRY_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "${UNINSTALL_REGISTRY_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegStr HKCU "${UNINSTALL_REGISTRY_KEY}" "ModifyPath" '$\"$INSTDIR\${PRODUCT_UPDATER}$\"'
  WriteRegDWORD HKCU "${UNINSTALL_REGISTRY_KEY}" "EstimatedSize" ${INSTALL_SIZE_KB}
  WriteRegDWORD HKCU "${UNINSTALL_REGISTRY_KEY}" "NoModify" 0
  WriteRegDWORD HKCU "${UNINSTALL_REGISTRY_KEY}" "NoRepair" 1

  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
    CreateShortcut "$SMPROGRAMS\$StartMenuFolder\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXECUTABLE}" "" "$INSTDIR\${PRODUCT_EXECUTABLE}" 0
    CreateShortcut "$SMPROGRAMS\$StartMenuFolder\Check for Updates.lnk" "$INSTDIR\${PRODUCT_UPDATER}" "" "$INSTDIR\${PRODUCT_UPDATER}" 0
    CreateShortcut "$SMPROGRAMS\$StartMenuFolder\Uninstall ${PRODUCT_NAME}.lnk" "$INSTDIR\Uninstall.exe"
  !insertmacro MUI_STARTMENU_WRITE_END
  Goto install_payload_ready

install_payload_failed:
  MessageBox MB_ICONSTOP|MB_OK "Installation could not be completed. No shortcuts were created. If this was a new installation, the partial application files will be removed. Choose a writable folder and try again." /SD IDOK
  SetErrorLevel 2
  Abort
install_payload_ready:
SectionEnd

Section /o "Desktop shortcut" SectionDesktop
  CreateShortcut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXECUTABLE}" "" "$INSTDIR\${PRODUCT_EXECUTABLE}" 0
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
  Delete "$SMPROGRAMS\$StartMenuFolder\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\$StartMenuFolder\Check for Updates.lnk"
  Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall ${PRODUCT_NAME}.lnk"
  RMDir "$SMPROGRAMS\$StartMenuFolder"

  DeleteRegKey HKCU "${UNINSTALL_REGISTRY_KEY}"
  DeleteRegKey HKCU "${PRODUCT_REGISTRY_KEY}"

  ; Remove only known application payload. Deliberately leave an unexpected
  ; config folder or any other user-created content untouched.
  !insertmacro RemoveApplicationPayload
SectionEnd
