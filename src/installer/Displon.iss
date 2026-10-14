; Displon installer (Inno Setup 6.x).
;
; Build:   iscc Displon.iss /DBuildDir=C:\path\to\cmake\build
;          (BuildDir must contain DisplonUI.exe, DisplonService.exe, DisplonWatchdog.exe)
; Output:  installer\Output\Displon-Setup.exe
;
; The wizard shows the GNU GPL v3 (../LICENSE) on a license page; the user must accept it to continue.
; The same text is installed as {app}\LICENSE.txt. An NSIS equivalent of this installer: Installer.nsis.
;
; Key behaviour: while a lock is active this installer REFUSES to install over it and REFUSES to uninstall
; (it reads the same lock.state files the engine writes). An administrator can still bypass any installer by
; hand (sc delete, killing processes) - this is a speed bump, the real protection is the service + guard.

#ifndef BuildDir
  #define BuildDir "..\build"
#endif
#define AppName "Displon"
#define AppVersion "v0.1-alpha"

[Setup]
AppId={{6F1B3C52-8A47-4D0E-9B7A-3E5D2C91A0F4}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Displon
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=Output
OutputBaseFilename=Displon-Setup
SetupIconFile=..\assets\icons\displon.ico
LicenseFile=..\LICENSE
UninstallDisplayIcon={app}\DisplonUI.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "service"; Description: "Install the background service (recommended: the lock is enforced from boot, before sign-in)"; GroupDescription: "Protection:"
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#BuildDir}\DisplonUI.exe";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\DisplonService.exe";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\DisplonWatchdog.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\assets\fonts\*"; DestDir: "{app}\assets\fonts"; Flags: ignoreversion recursesubdirs
Source: "..\assets\icons\*"; DestDir: "{app}\assets\icons"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\{#AppName}";       Filename: "{app}\DisplonUI.exe"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\DisplonUI.exe"; Tasks: desktopicon

[Run]
; Service (only if the user kept the task ticked). Any leftover from an upgrade was removed in PrepareToInstall.
Filename: "{sys}\sc.exe"; Parameters: "create Displon binPath= ""{app}\DisplonService.exe"" start= auto DisplayName= ""{#AppName}"""; Flags: runhidden waituntilterminated; Tasks: service; StatusMsg: "Registering the service..."
Filename: "{sys}\sc.exe"; Parameters: "description Displon ""Displon lock enforcement (keeps a block active across restarts)."""; Flags: runhidden waituntilterminated; Tasks: service
Filename: "{sys}\sc.exe"; Parameters: "failure Displon reset= 0 actions= restart/1000/restart/1000/restart/1000"; Flags: runhidden waituntilterminated; Tasks: service
Filename: "{sys}\sc.exe"; Parameters: "start Displon"; Flags: runhidden waituntilterminated; Tasks: service; StatusMsg: "Starting the service..."
Filename: "{app}\DisplonUI.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{sys}\schtasks.exe"; Parameters: "/Delete /TN ""Displon Resume"" /F"; Flags: runhidden; RunOnceId: "DelTask"
Filename: "{sys}\sc.exe"; Parameters: "stop Displon";   Flags: runhidden waituntilterminated; RunOnceId: "StopSvc"
Filename: "{sys}\sc.exe"; Parameters: "delete Displon"; Flags: runhidden waituntilterminated; RunOnceId: "DelSvc"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM DisplonWatchdog.exe"; Flags: runhidden waituntilterminated; RunOnceId: "KillDog"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM DisplonUI.exe"; Flags: runhidden waituntilterminated; RunOnceId: "KillUi"

[UninstallDelete]
; Only reached when no lock is active (see InitializeUninstall), so these hold no live lock state.
Type: filesandordirs; Name: "{localappdata}\Displon"
Type: filesandordirs; Name: "{commonappdata}\Displon"
Type: filesandordirs; Name: "{userappdata}\Displon"

[Code]
type
  TFtTime = record
    dwLowDateTime: Cardinal;
    dwHighDateTime: Cardinal;
  end;

procedure GetSystemTimeAsFileTime(var lpFileTime: TFtTime);
  external 'GetSystemTimeAsFileTime@kernel32.dll stdcall';

// Seconds since 1970-01-01 UTC, the same clock the engine stores in lock.state ("unlock=").
function NowEpoch(): Int64;
var
  ft: TFtTime;
  t: Int64;
begin
  GetSystemTimeAsFileTime(ft);
  t := Int64(ft.dwHighDateTime) * 65536 * 65536 + Int64(ft.dwLowDateTime);
  // 11644473600 s between 1601 and 1970, built from small literals to stay inside 32-bit constants
  Result := t div 10000000 - (Int64(11644) * 1000000 + 473600);
end;

// True when this lock.state describes a lock that has not expired yet. Unreadable / malformed files count as
// "no lock", exactly like the engine (it ignores files whose checksum or format is wrong).
function LockFileActive(const Path: String): Boolean;
var
  Raw: AnsiString;
  S: String;
  P: Integer;
  Unlock: Int64;
begin
  Result := False;
  if not FileExists(Path) then Exit;
  if not LoadStringFromFile(Path, Raw) then Exit;
  S := String(Raw);
  P := Pos('unlock=', S);
  if P = 0 then Exit;
  S := Copy(S, P + 7, 24);
  P := Pos(#10, S);
  if P > 0 then S := Copy(S, 1, P - 1);
  Unlock := StrToInt64Def(Trim(S), 0);
  Result := Unlock > NowEpoch();
end;

// Checks every place a lock can live: the signed-in user's UI engine, and the service (SYSTEM profile).
function AnyLockActive(): Boolean;
begin
  Result :=
    LockFileActive(ExpandConstant('{localappdata}\Displon\lock.state')) or
    LockFileActive(ExpandConstant('{commonappdata}\Displon\lock.state')) or
    LockFileActive(ExpandConstant('{userappdata}\Displon\lock.state')) or
    LockFileActive(ExpandConstant('{sys}\config\systemprofile\AppData\Local\Displon\lock.state')) or
    LockFileActive(ExpandConstant('{sys}\config\systemprofile\AppData\Roaming\Displon\lock.state'));
end;

procedure TellLocked(const What: String);
begin
  MsgBox('Displon has an active lock, so it cannot be ' + What + ' right now.' + #13#10 + #13#10 +
         'Wait until the lock ends and try again. (If this is an "until restart" lock, restart the ' +
         'computer and open Displon once so it can clear itself.)',
         mbError, MB_OK);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if AnyLockActive() then
  begin
    TellLocked('installed or updated');
    Result := False;
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  if AnyLockActive() then
  begin
    TellLocked('uninstalled');
    Result := False;
  end;
end;

// Upgrade over an older version: stop and remove what the previous install registered (no lock is active here).
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Result := '';
  Exec(ExpandConstant('{sys}\schtasks.exe'), '/Delete /TN "Displon Resume" /F', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\sc.exe'), 'stop Displon', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\sc.exe'), 'delete Displon', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM DisplonWatchdog.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM DisplonUI.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Sleep(800);   // let the OS release the exe files before they are overwritten
end;
