; QtZpl Designer installer. Build with tools/build_installer.py.
; Inno Setup 6.4+; VERSION is the only source of the application version.
#if VER < EncodeVer(6, 4, 0)
  #error Inno Setup 6.4 or newer is required.
#endif
#define AppName "QtZpl Designer"
#define AppExeName "QtZplDesigner.exe"

#ifdef AppVersion
  #error AppVersion must be read from VERSION; remove the /DAppVersion override.
#endif
#define VersionHandle FileOpen(SourcePath + "..\VERSION")
#if !VersionHandle
  #error Cannot read the root VERSION.
#endif
#define AppVersion Trim(FileRead(VersionHandle))
#expr FileClose(VersionHandle)
#if AppVersion == ""
  #error The root VERSION is empty.
#endif

#ifndef PackageDir
  #error PackageDir is required. Run tools/build_installer.py.
#endif
#ifndef OutputDir
  #define OutputDir "..\dist\installer"
#endif
#ifndef MinimumVCRuntimeVersion
  #error MinimumVCRuntimeVersion must be derived from the build toolchain.
#endif
#if !FileExists(PackageDir + "\VERSION")
  #error The package must contain VERSION.
#endif
#define PackageVersionHandle FileOpen(PackageDir + "\VERSION")
#define PackageVersion Trim(FileRead(PackageVersionHandle))
#expr FileClose(PackageVersionHandle)
#if PackageVersion != AppVersion
  #error Packaged VERSION differs from the source VERSION. Rebuild the package.
#endif
#if !FileExists(PackageDir + "\" + AppExeName)
  #error The package is missing QtZplDesigner.exe.
#endif
#if GetVersionNumbersString(PackageDir + "\" + AppExeName) != AppVersion + ".0"
  #error Packaged EXE version differs from the source VERSION. Rebuild the package.
#endif

[Setup]
AppId={{A9C0550A-B137-40DD-BB1B-7F3E21E74E8A}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Руслан Багаутдинов
AppSupportURL=mailto:bagautdinovrf@ya.ru
DefaultDirName={autopf}\QtZpl Designer
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=QtZplDesigner-{#AppVersion}-Setup
SetupIconFile=assets\setup.ico
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\setup.ico
VersionInfoProductName={#AppName}
VersionInfoCompany=Руслан Багаутдинов
VersionInfoDescription=Установщик {#AppName}
VersionInfoVersion={#AppVersion}
WizardStyle=modern
WizardImageFile=assets\wizard-large.bmp
WizardSmallImageFile=assets\wizard-small.bmp
WizardImageStretch=yes
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[LangOptions]
DialogFontName=Segoe UI
DialogFontSize=9
WelcomeFontName=Segoe UI
WelcomeFontSize=14

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"; GroupDescription: "Ярлыки:"

[Files]
; Explicit entries prevent successful compilation of an incomplete deployment.
Source: "{#PackageDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\Qt6Core.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\Qt6Gui.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\Qt6Widgets.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\platforms\qwindows.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion
Source: "assets\setup.ico"; DestDir: "{app}"; Flags: ignoreversion
; VC runtime belongs to Windows and is downloaded only when required. Never
; embed its EXE or app-local DLLs, even if a supplied package contains them.
Source: "{#PackageDir}\*"; DestDir: "{app}"; Excludes: "QtZplDesigner.exe,Qt6Core.dll,Qt6Gui.dll,Qt6Widgets.dll,platforms\qwindows.dll,vc_redist*.exe,vcredist*.exe,msvcp1*.dll,msvcr1*.dll,vcruntime1*.dll,vccorlib1*.dll,concrt1*.dll,vcomp1*.dll,ucrtbase.dll,api-ms-win-crt-*.dll,*.pdb,*.log"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\setup.ico"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\setup.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Description: "Запустить {#AppName}"; Flags: postinstall nowait skipifsilent; Check: CanLaunchApplication

[Code]
#include "vc_runtime.iss"
