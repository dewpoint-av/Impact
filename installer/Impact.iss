; Impact — Inno Setup installer

#define MyAppName "Impact"
#define MyAppVersion "1.1.3"
#define MyAppPublisher "Dewpoint AV"
#define MyAppURL "https://dewpoint.live"

[Setup]
AppId={{5E1A8C62-3F5D-4B7A-9E2C-7F0A3B4C5D6E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\Dewpoint AV\{#MyAppName}
DefaultGroupName=Dewpoint AV\{#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=output
OutputBaseFilename={#MyAppName}-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
SetupLogging=yes
UninstallDisplayName={#MyAppName} (Dewpoint AV)

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\build\dist\Impact.vst3\*"; DestDir: "{commoncf}\VST3\{#MyAppName}.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\build\dist\Impact.clap"; DestDir: "{commoncf}\CLAP"; Flags: ignoreversion

[InstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\Impact.vst3"
Type: files; Name: "{commoncf}\CLAP\Impact.clap"

[Icons]
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

[Messages]
WelcomeLabel1=Impact Multiband Transient Shaper
WelcomeLabel2=This will install Impact (VST3 + CLAP) on your computer.%n%nClick Next to continue.
