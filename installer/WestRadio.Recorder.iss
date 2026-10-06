; WestRadio Recorder - Inno Setup 6 script.
; Build via scripts/package.ps1 (passes /DAppVersion=...), or manually:
;   ISCC.exe /DAppVersion=0.1.0 installer\WestRadio.Recorder.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{5F6307C9-A10C-40C0-977C-21FCBE33E8D5}
AppName=WestRadio Recorder
AppVersion={#AppVersion}
AppVerName=WestRadio Recorder {#AppVersion}
AppPublisher=WestRadio
AppPublisherURL=https://github.com/MediaNerdStudio/WestRadio.Recorder
DefaultDirName={autopf}\WestRadio Recorder
DefaultGroupName=WestRadio Recorder
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\WestRadio.Recorder.exe
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=WestRadio.Recorder-{#AppVersion}-setup
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "..\dist\WestRadio.Recorder-{#AppVersion}-win64\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\WestRadio Recorder"; Filename: "{app}\WestRadio.Recorder.exe"
Name: "{group}\Uninstall WestRadio Recorder"; Filename: "{uninstallexe}"
Name: "{autodesktop}\WestRadio Recorder"; Filename: "{app}\WestRadio.Recorder.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\WestRadio.Recorder.exe"; Description: "Launch WestRadio Recorder"; Flags: postinstall nowait skipifsilent
