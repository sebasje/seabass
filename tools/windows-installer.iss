; Inno Setup script for Seabass (Windows testing builds).
;
; Prerequisite: build-win\ must already contain a fresh Release build plus
; deploy-windows.ps1's output (windeployqt + DLL closure walk). Run, in order:
;   cmake --build build-win --target seabass-cli
;   cmake --build build-win --target seabass
;   .\tools\deploy-windows.ps1
;   & "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" tools\windows-installer.iss
;
; Only the runtime files deploy-windows.ps1 actually produces are listed
; explicitly below (exes, DLLs, qt.conf, Qt plugin dirs) -- build-win\ also
; contains CMake/Ninja intermediates and ~20 unit-test .exe files that must
; NOT ship.

#define MyAppName "Seabass"
#define MyAppVersion "0.1.0-9c20aa5"
#define MyAppPublisher "Sebastian Kugler"
#define BuildDir "..\build-win"

[Setup]
AppId={{5B1E9F2B-6C3E-4B2E-9C6D-6B3E7B9F0E01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\installer-out
OutputBaseFilename=Seabass-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\src\gui\win\app_icon.ico
UninstallDisplayIcon={app}\seabass.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; Executables
Source: "{#BuildDir}\seabass.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\seabass-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\qt.conf"; DestDir: "{app}"; Flags: ignoreversion

; Runtime DLLs (mingw runtime, Qt, ffmpeg codec graph, sqlcipher, etc. --
; the full closure deploy-windows.ps1 already resolved)
Source: "{#BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion

; Qt plugin directories windeployqt populated
Source: "{#BuildDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\multimedia\*"; DestDir: "{app}\multimedia"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\qml\*"; DestDir: "{app}\qml"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\qmltooling\*"; DestDir: "{app}\qmltooling"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\seabass.exe"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\seabass.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\seabass.exe"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
