{ Inno Setup deletes an existing [Icons] entry before creating it again.
  Keep a matching desktop link untouched, including user-customized properties. }
function DesktopShortcutNeedsUpdate(const ShortcutPath, TargetPath: String): Boolean;
var
  Shell, Shortcut: Variant;
  ExistingTarget: String;
begin
  Result := True;
  if not FileExists(ShortcutPath) then
    Exit;

  try
    Shell := CreateOleObject('WScript.Shell');
    Shortcut := Shell.CreateShortcut(ShortcutPath);
    ExistingTarget := Shortcut.TargetPath;
    if ExistingTarget = '' then
    begin
      Log('Cannot read desktop shortcut target; keeping it: ' + ShortcutPath);
      Result := False;
      Exit;
    end;
    if CompareText(ExistingTarget, TargetPath) = 0 then
    begin
      Log('Keeping existing desktop shortcut: ' + ShortcutPath);
      Result := False;
    end;
  except
    { A failed inspection is not a reason to destroy an existing shortcut. }
    Log('Cannot inspect desktop shortcut; keeping it: ' + GetExceptionMessage);
    Result := False;
  end;
end;
