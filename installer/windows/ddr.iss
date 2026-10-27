; Inno Setup script for the DDR Windows installer.
; Built by CI: ISCC /DArch=x64|arm64 /DVersion=0.1.0 /DSourceDir=<folder with the .vst3> ddr.iss

#ifndef Arch
  #define Arch "x64"
#endif
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist"
#endif

[Setup]
AppId={{7B0C5E2A-4D8E-4E5B-9C55-DD0D21DE0001}
AppName=DDR Drive Simulator
AppVersion={#Version}
AppVerName=DDR Drive Simulator {#Version}
AppPublisher=salleq
AppPublisherURL=https://github.com/salleq/ddr-drive-simulator
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallDisplayName=DDR Drive Simulator (VST3)
CreateUninstallRegKey=yes
PrivilegesRequired=admin
LicenseFile=..\..\LICENSE
OutputDir=..\..\installers
#if Arch == "arm64"
OutputBaseFilename=DDR-{#Version}-Windows-ARM64-Setup
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
OutputBaseFilename=DDR-{#Version}-Windows-x64-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Messages]
FinishedLabel=DDR is installed in %n{commoncf64}\VST3%n%nRescan plugins in your DAW and put DDR last on your master bus. Remember to bypass it before you bounce!

[Files]
Source: "{#SourceDir}\DDR Drive Simulator.vst3\*"; DestDir: "{commoncf64}\VST3\DDR Drive Simulator.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\DDR Drive Simulator.vst3"
