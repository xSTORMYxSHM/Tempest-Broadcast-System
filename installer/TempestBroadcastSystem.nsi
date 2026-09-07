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

!macro ProbeWritableDirectory Directory NotWritableLabel LabelPrefix
  ClearErrors
  CreateDirectory "${Directory}"
  IfErrors ${NotWritableLabel} 0
  ClearErrors
  FileOpen $R4 "${Directory}\.tempest-install-write-test.tmp" w
  IfErrors ${NotWritableLabel} 0
  FileWrite $R4 "Tempest Broadcast System install write test"
  IfErrors ${LabelPrefix}_write_failed 0
  FileClose $R4
  ClearErrors
  Delete "${Directory}\.tempest-install-write-test.tmp"
  IfErrors ${NotWritableLabel} 0
  Goto ${LabelPrefix}_complete
${LabelPrefix}_write_failed:
  FileClose $R4
  Delete "${Directory}\.tempest-install-write-test.tmp"
  Goto ${NotWritableLabel}
${LabelPrefix}_complete:
!macroend

!macro ValidateInstallDirectory SafeLabel UnsafeLabel LabelPrefix
  StrCpy $InstallDirectoryError ""
  ; $INSTDIR is already absolute. GetFullPathName returns an empty string when
  ; its target does not exist, which made a fresh folder compare equal to an
  ; empty root and caused the 1.1.1 installer to reject every new destination.
  StrCpy $R0 "$INSTDIR"
  ${GetRoot} "$R0" $R1
  ${If} $R0 == $R1
    StrCpy $InstallDirectoryError "Drive roots cannot be used as the application folder."
    Goto ${UnsafeLabel}
  ${EndIf}
  StrCpy $R3 "$R1\"
  ${If} $R0 == $R3
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

  ; An existing Tempest application folder is a valid update or repair target
  ; regardless of its final folder name. Probe its critical child directories
  ; before overwriting any files so inherited ACL problems fail safely.
  IfFileExists "$R0\${PRODUCT_EXECUTABLE}" ${LabelPrefix}_existing_install 0

  ; Create and write-test a new destination before checking its contents so a
  ; path that does not exist yet is handled as a normal fresh installation.
  StrCpy $FreshInstall "1"
  !insertmacro ProbeWritableDirectory "$R0" ${LabelPrefix}_not_writable ${LabelPrefix}_new_root_probe

  ; A new custom folder can use any name, but it must not already contain
  ; unrelated content. This keeps uninstall cleanup away from shared folders.
  ClearErrors
  FindFirst $R2 $R3 "$R0\*"
  IfErrors ${SafeLabel} 0
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
  Goto ${SafeLabel}

${LabelPrefix}_existing_install:
  StrCpy $FreshInstall "0"
  !insertmacro ProbeWritableDirectory "$R0" ${LabelPrefix}_not_writable ${LabelPrefix}_existing_root_probe
  !insertmacro ProbeWritableDirectory "$R0\bin\64bit" ${LabelPrefix}_not_writable ${LabelPrefix}_bin_probe
  !insertmacro ProbeWritableDirectory "$R0\data\obs-studio\themes" ${LabelPrefix}_not_writable ${LabelPrefix}_theme_probe
  !insertmacro ProbeWritableDirectory "$R0\obs-plugins\64bit" ${LabelPrefix}_not_writable ${LabelPrefix}_plugin_probe
  Goto ${SafeLabel}
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
  Delete "$TEMP\TempestBroadcastSystem-Installer.log"

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
  Call WriteInstallerFailureLog
  MessageBox MB_ICONEXCLAMATION|MB_OK "$InstallDirectoryError"
  Abort
valid_install_directory:
  Return
FunctionEnd

Function WriteInstallerFailureLog
  ClearErrors
  FileOpen $R9 "$TEMP\TempestBroadcastSystem-Installer.log" w
  IfErrors failure_log_complete 0
  FileWrite $R9 "Tempest Broadcast System ${PRODUCT_VERSION}$\r$\n"
  FileWrite $R9 "Destination: $INSTDIR$\r$\n"
  FileWrite $R9 "Validated destination: $R0$\r$\n"
  FileWrite $R9 "Validation root: $R1$\r$\n"
  FileWrite $R9 "Error: $InstallDirectoryError$\r$\n"
  FileClose $R9
failure_log_complete:
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
  Call WriteInstallerFailureLog
  MessageBox MB_ICONSTOP|MB_OK "$InstallDirectoryError" /SD IDOK
  SetErrorLevel 20
  Abort
valid_section_install_directory:
  ClearErrors
  SetOutPath "$INSTDIR"
  IfErrors install_output_directory_failed 0
  ClearErrors
  File /r "${PAYLOAD_DIR}\*"
  IfErrors install_extraction_failed 0

  ; Do not register a partial installation. The application, updater, and both
  ; fallback themes are required for a valid first launch and recovery path.
  IfFileExists "$INSTDIR\${PRODUCT_EXECUTABLE}" 0 install_application_missing
  IfFileExists "$INSTDIR\${PRODUCT_UPDATER}" 0 install_updater_missing
  IfFileExists "$INSTDIR\data\obs-studio\themes\Yami.obt" 0 install_yami_theme_missing
  IfFileExists "$INSTDIR\data\obs-studio\themes\System.obt" 0 install_system_theme_missing
  IfFileExists "$INSTDIR\data\obs-studio\themes\Tempest.ovt" 0 install_tempest_theme_missing
  IfFileExists "$INSTDIR\data\obs-studio\locale\en-US.ini" 0 install_locale_missing

  ClearErrors
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  IfErrors install_uninstaller_failed 0

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
    SetOutPath "$INSTDIR\bin\64bit"
    CreateShortcut "$SMPROGRAMS\$StartMenuFolder\${PRODUCT_NAME}.lnk" "$INSTDIR\${PRODUCT_EXECUTABLE}" "" "$INSTDIR\${PRODUCT_EXECUTABLE}" 0
    CreateShortcut "$SMPROGRAMS\$StartMenuFolder\Check for Updates.lnk" "$INSTDIR\${PRODUCT_UPDATER}" "" "$INSTDIR\${PRODUCT_UPDATER}" 0
    SetOutPath "$INSTDIR"
    CreateShortcut "$SMPROGRAMS\$StartMenuFolder\Uninstall ${PRODUCT_NAME}.lnk" "$INSTDIR\Uninstall.exe"
  !insertmacro MUI_STARTMENU_WRITE_END
  SetOutPath "$INSTDIR\bin\64bit"
  Goto install_payload_ready

install_output_directory_failed:
  StrCpy $InstallDirectoryError "Windows could not open the selected installation folder."
  SetErrorLevel 21
  Goto install_payload_failed
install_extraction_failed:
  StrCpy $InstallDirectoryError "Windows could not extract every application file into the selected folder."
  SetErrorLevel 22
  Goto install_payload_failed
install_application_missing:
  StrCpy $InstallDirectoryError "The application executable is missing after extraction."
  SetErrorLevel 23
  Goto install_payload_failed
install_updater_missing:
  StrCpy $InstallDirectoryError "The manual updater is missing after extraction."
  SetErrorLevel 24
  Goto install_payload_failed
install_yami_theme_missing:
  StrCpy $InstallDirectoryError "The Yami fallback theme is missing after extraction."
  SetErrorLevel 25
  Goto install_payload_failed
install_system_theme_missing:
  StrCpy $InstallDirectoryError "The System fallback theme is missing after extraction."
  SetErrorLevel 26
  Goto install_payload_failed
install_tempest_theme_missing:
  StrCpy $InstallDirectoryError "The Tempest theme is missing after extraction."
  SetErrorLevel 27
  Goto install_payload_failed
install_locale_missing:
  StrCpy $InstallDirectoryError "The default locale file is missing after extraction."
  SetErrorLevel 28
  Goto install_payload_failed
install_uninstaller_failed:
  StrCpy $InstallDirectoryError "Windows could not create the application uninstaller."
  SetErrorLevel 29
  Goto install_payload_failed
install_payload_failed:
  Call WriteInstallerFailureLog
  MessageBox MB_ICONSTOP|MB_OK "Installation could not be completed. $InstallDirectoryError No shortcuts were created. If this was a new installation, the partial application files will be removed. Choose a writable folder and try again." /SD IDOK
  Abort
install_payload_ready:
SectionEnd

Section /o "Desktop shortcut" SectionDesktop
  SetOutPath "$INSTDIR\bin\64bit"
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
