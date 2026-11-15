; Windows-Installer (Inno Setup).
;
; Installiert zwei Teile, jeder abwählbar:
;   NAMplify.exe   nach Programme\NAMplify
;   NAMplify.vst3  nach Programme\Common Files\VST3
;
; Kein AU (nur macOS), kein VST2 (Steinberg vergibt keine Lizenzen mehr).
;
; Die Version kommt von außen (ISCC /DVersion=2.0.0), sie steht in
; CMakeLists.txt.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Bin
  #define Bin "..\build\plugin\NAMplify_artefacts\Release"
#endif

[Setup]
AppId={{7A2E6C10-4B31-4E5D-9E77-NAMPLIFY0001}
AppName=NAMplify
AppVersion={#Version}
AppPublisher=Martin Maiwald
DefaultDirName={autopf}\NAMplify
DefaultGroupName=NAMplify
DisableProgramGroupPage=yes
; Programm und Plugin liegen unter Programme, dafür sind Adminrechte nötig.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\release
OutputBaseFilename=NAMplify-{#Version}-Windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=NAMplify {#Version}
; Lizenz und Hinweise zu Fremdcode gehören in jede Kopie (AGPL, MIT).
LicenseFile=..\LICENSE

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "voll";  Description: "Alles"
Name: "eigen"; Description: "Auswahl"; Flags: iscustom

[Components]
Name: "app";  Description: "NAMplify (Programm für sich allein)"; Types: voll eigen
Name: "vst3"; Description: "VST3-Plugin";                          Types: voll eigen

[Files]
Source: "{#Bin}\Standalone\NAMplify.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
; Das VST3 ist unter Windows ein Ordner.
Source: "{#Bin}\VST3\NAMplify.vst3\*"; DestDir: "{commoncf}\VST3\NAMplify.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
; WebView2-Laufzeit für die UI. Ohne sie startet das Programm mit leerem
; Fenster. Windows 11 bringt sie mit, Windows 10 nicht immer. Der
; Bootstrapper wird nur ausgepackt und ausgeführt, wenn sie fehlt.
Source: "redist\MicrosoftEdgeWebview2Setup.exe"; DestDir: "{tmp}"; \
    Flags: deleteafterinstall; Check: WebView2Fehlt

Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; URL-Schema für die TONE3000-Anmeldung (namplify://t3k-callback). Auf macOS
; steht es in der Info.plist, unter Windows hier. Es zeigt auf die
; Standalone, ein Plugin im Host kann kein Schema anmelden. Die Standalone
; speichert das Ergebnis in denselben Einstellungen, die das Plugin liest.
Root: HKCR; Subkey: "namplify"; ValueType: string; ValueName: ""; ValueData: "URL:NAMplify"; Flags: uninsdeletekey; Components: app
Root: HKCR; Subkey: "namplify"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""; Components: app
Root: HKCR; Subkey: "namplify\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\NAMplify.exe,0"; Components: app
Root: HKCR; Subkey: "namplify\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\NAMplify.exe"" ""%1"""; Components: app

[Icons]
Name: "{group}\NAMplify"; Filename: "{app}\NAMplify.exe"; Components: app
Name: "{autodesktop}\NAMplify"; Filename: "{app}\NAMplify.exe"; Components: app; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Components: app; Flags: unchecked

[Run]
; Erst die Laufzeit, dann das Programm.
Filename: "{tmp}\MicrosoftEdgeWebview2Setup.exe"; Parameters: "/silent /install"; \
    StatusMsg: "Edge-WebView2 wird nachinstalliert…"; Check: WebView2Fehlt
Filename: "{app}\NAMplify.exe"; Description: "NAMplify starten"; \
    Flags: nowait postinstall skipifsilent; Components: app

[Code]
// Ist die WebView2-Laufzeit installiert? Je nach Installation für alle oder
// nur für diesen Nutzer steht sie unter HKLM oder HKCU.
function WebView2Fehlt: Boolean;
var
  s: String;
begin
  Result := True;
  if RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', s) and (s <> '') and (s <> '0.0.0.0') then
    Result := False
  else if RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', s) and (s <> '') and (s <> '0.0.0.0') then
    Result := False;
end;
