; KumAmp - instalador de Windows (Inno Setup 6)
;
; Compilar:  ISCC.exe /DBuildDir="<repo>\build-win\KumAmp_artefacts\Release" installer\KumAmp.iss
; (build_windows.bat y el workflow de GitHub Actions lo hacen automaticamente)

#ifndef AppVersion
  #define AppVersion "0.1.6"
#endif
#ifndef BuildDir
  #define BuildDir "..\build-win\KumAmp_artefacts\Release"
#endif

[Setup]
; AppId identifica la instalacion para actualizaciones y desinstalacion: no cambiarlo.
AppId={{B02D6486-15F7-487B-9C6C-157981A4CE19}
AppName=KumAmp
AppVersion={#AppVersion}
AppVerName=KumAmp {#AppVersion}
AppPublisher=KumAmp Audio
DefaultDirName={autopf}\KumAmp
DefaultGroupName=KumAmp
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=KumAmp-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=KumAmp {#AppVersion}
UninstallDisplayIcon={app}\KumAmp.exe
MinVersion=10.0
LicenseFile=..\LICENSE

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "Instalacion completa"
Name: "plugin"; Description: "Solo el plugin VST3"
Name: "custom"; Description: "Personalizada"; Flags: iscustom

[Components]
Name: "vst3";       Description: "Plugin VST3 (para REAPER, Ableton, Cubase, FL Studio...)"; Types: full plugin custom
Name: "standalone"; Description: "Aplicacion independiente (sin DAW)";                      Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Crear acceso directo en el escritorio"; Components: standalone; Flags: unchecked

[Files]
; El VST3 es una carpeta (bundle): va entera a la carpeta estandar de VST3 de 64 bits.
Source: "{#BuildDir}\VST3\KumAmp.vst3\*"; DestDir: "{commoncf64}\VST3\KumAmp.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#BuildDir}\Standalone\KumAmp.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone
Source: "..\README.md"; DestDir: "{app}"; DestName: "LEEME.md"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\KumAmp";                 Filename: "{app}\KumAmp.exe"; Components: standalone
Name: "{group}\Desinstalar KumAmp";     Filename: "{uninstallexe}"
Name: "{autodesktop}\KumAmp";           Filename: "{app}\KumAmp.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\KumAmp.exe"; Description: "Abrir KumAmp ahora"; Flags: nowait postinstall skipifsilent unchecked; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\KumAmp.vst3"

[Messages]
spanish.FinishedLabel=KumAmp esta instalado.%n%nEl plugin VST3 esta en "C:\Program Files\Common Files\VST3". Si tu DAW no lo muestra, vuelve a escanear los plugins.
