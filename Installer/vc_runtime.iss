{ Prerequisite logic runs before Inno Setup copies any application files.
  Registry schema and switches:
  https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
  Official current download URL:
  https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist
  Do not pin a download hash: Microsoft updates this permalink in place.
  Instead validate Authenticode trust AND the Microsoft publisher before Exec. }

const
  MinimumVCRuntimeVersion = '{#MinimumVCRuntimeVersion}';
  VCRuntimeRegistryKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  VCRuntimeUrl = 'https://aka.ms/vc14/vc_redist.x64.exe';
  VCRuntimeFileName = 'vc_redist.x64.exe';

var
  RuntimeDownloadPage: TDownloadWizardPage;
  RuntimeProgressPage: TOutputProgressWizardPage;
  RuntimeRestartRequired: Boolean;

function VersionMeetsMinimum(const ActualVersion, RequiredVersion: String): Boolean;
var
  Actual, Required: Int64;
begin
  Result := StrToVersion(ActualVersion, Actual) and
    StrToVersion(RequiredVersion, Required);
  if Result then
    Result := ComparePackedVersion(Actual, Required) >= 0;
end;

function RuntimeVersionInRegistry(const RootKey: Integer; var Version: String): Boolean;
var
  Installed, Major, Minor, Build, Revision: Cardinal;
begin
  Version := '';
  Result := RegQueryDWordValue(RootKey, VCRuntimeRegistryKey, 'Installed', Installed);
  if not Result then
    Exit;
  if Installed <> 1 then begin
    Result := False;
    Exit;
  end;
  Result := RegQueryDWordValue(RootKey, VCRuntimeRegistryKey, 'Major', Major) and
    RegQueryDWordValue(RootKey, VCRuntimeRegistryKey, 'Minor', Minor) and
    RegQueryDWordValue(RootKey, VCRuntimeRegistryKey, 'Bld', Build) and
    RegQueryDWordValue(RootKey, VCRuntimeRegistryKey, 'Rbld', Revision);
  if Result then begin
    Version := Format('%d.%d.%d.%d', [Major, Minor, Build, Revision]);
    Log(Format('VC runtime x64 registry view %d: %s', [RootKey, Version]));
  end;
end;

function IsVCRuntimeInstalled: Boolean;
var
  Version32, Version64: String;
  Installed32, Installed64: Boolean;
begin
  { Releases of the redistributable have used both registry views. Read both;
    a stale record must never conceal a newer installed x64 runtime. }
  Installed32 := RuntimeVersionInRegistry(HKLM32, Version32);
  Installed64 := RuntimeVersionInRegistry(HKLM64, Version64);
  Result := (Installed32 and VersionMeetsMinimum(Version32, MinimumVCRuntimeVersion)) or
    (Installed64 and VersionMeetsMinimum(Version64, MinimumVCRuntimeVersion));
end;

function PowerShellString(const Value: String): String;
begin
  Result := Value;
  StringChangeEx(Result, '''', '''''', True);
  Result := '''' + Result + '''';
end;

function HasTrustedMicrosoftSignature(const FileName: String): Boolean;
var
  Command, PowerShellExe: String;
  ResultCode: Integer;
begin
  { Windows 10 includes Windows PowerShell. No profiles, PATH lookup, downloaded
    scripts or execution-policy overrides. A valid signature from another
    publisher is deliberately insufficient. Certificate rotation stays possible.
    Import the Windows module explicitly: a parent PowerShell 7 process can
    leave an incompatible PSModulePath in the inherited environment.
    https://learn.microsoft.com/powershell/module/microsoft.powershell.security/get-authenticodesignature }
  PowerShellExe := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  Command := '$ErrorActionPreference=''Stop''; try { ' +
    'Import-Module ($PSHOME + ''\Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1'') -ErrorAction Stop; ' +
    '$signature=Microsoft.PowerShell.Security\Get-AuthenticodeSignature -LiteralPath ' +
    PowerShellString(FileName) + '; ' +
    'if ($signature.Status -ne ''Valid'' -or $null -eq $signature.SignerCertificate) { exit 1 }; ' +
    'if ($signature.SignerCertificate.GetNameInfo(' +
    '[System.Security.Cryptography.X509Certificates.X509NameType]::SimpleName,$false) ' +
    '-cne ''Microsoft Corporation'') { exit 2 }; exit 0 } catch { exit 3 }';
  ResultCode := -1;
  Result := Exec(PowerShellExe, '-NoLogo -NoProfile -NonInteractive -Command "' +
    Command + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := Result and (ResultCode = 0);
  Log(Format('Microsoft Authenticode verification result: %d', [ResultCode]));
end;

procedure InitializeWizard;
begin
  RuntimeDownloadPage := CreateDownloadPage('Компоненты Microsoft Visual C++',
    'Загрузка необходимого компонента с сайта Microsoft.', nil);
  RuntimeDownloadPage.ShowBaseNameInsteadOfUrl := True;
  RuntimeProgressPage := CreateOutputProgressPage('Компоненты Microsoft Visual C++',
    'Подготовка среды выполнения для QtZpl Designer.');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine + MemoGroupInfo;
  if MemoTasksInfo <> '' then
    Result := Result + NewLine + NewLine + MemoTasksInfo;
  Result := Result + NewLine + NewLine +
    'Microsoft Visual C++ Runtime (x64), версия ' + MinimumVCRuntimeVersion + ' или новее:';
  if IsVCRuntimeInstalled then
    Result := Result + NewLine + Space + 'Подходящая версия уже установлена.'
  else
    Result := Result + NewLine + Space +
      'Будет загружен с сайта Microsoft и установлен. Требуется подключение к Интернету.';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  FileName, DownloadedVersion, PendingBefore, PendingAfter: String;
  ResultCode: Integer;
begin
  Result := '';
  if IsVCRuntimeInstalled then begin
    Log('The installed x64 VC runtime meets requirement ' + MinimumVCRuntimeVersion);
    Exit;
  end;
  if RuntimeRestartRequired then begin
    NeedsRestart := True;
    Result := 'Для завершения обновления Microsoft Visual C++ требуется перезагрузка.' + #13#10 +
      'Перезагрузите компьютер и снова запустите установщик QtZpl Designer.';
    Exit;
  end;

  FileName := ExpandConstant('{tmp}\' + VCRuntimeFileName);
  RuntimeDownloadPage.Clear;
  RuntimeDownloadPage.Add(VCRuntimeUrl, VCRuntimeFileName, '');
  RuntimeDownloadPage.Show;
  try
    try
      { Each retry downloads again, including after an interrupted transfer. }
      DeleteFile(FileName);
      RuntimeDownloadPage.Download;
    except
      if RuntimeDownloadPage.AbortedByUser then
        Result := 'Загрузка Microsoft Visual C++ отменена. Установка программы не началась.'
      else
        Result := 'Не удалось загрузить Microsoft Visual C++ с сайта Microsoft.' + #13#10 +
          GetExceptionMessage + #13#10 +
          'Проверьте подключение к Интернету и повторите установку.';
      Log(Result);
    end;
  finally
    RuntimeDownloadPage.Hide;
  end;
  if Result <> '' then
    Exit;

  RuntimeProgressPage.SetText('Проверка цифровой подписи Microsoft...', '');
  RuntimeProgressPage.Show;
  try
    if not HasTrustedMicrosoftSignature(FileName) then begin
      Result := 'Не удалось подтвердить действительную цифровую подпись Microsoft ' +
        'у загруженного компонента. Он не был запущен. Проверьте дату и время Windows, ' +
        'доступ к серверам сертификатов и Windows PowerShell, затем повторите установку.';
      DeleteFile(FileName);
      Exit;
    end;
    if not GetVersionNumbersString(FileName, DownloadedVersion) or
      not VersionMeetsMinimum(DownloadedVersion, MinimumVCRuntimeVersion) then begin
      Result := 'Загруженный Microsoft Visual C++ не соответствует требуемой версии ' +
        MinimumVCRuntimeVersion + '. Установка программы не началась.';
      Exit;
    end;

    RuntimeProgressPage.SetText('Установка Microsoft Visual C++ (x64)...',
      'Это может занять несколько минут. Не выключайте компьютер.');
    PendingBefore := MakePendingFileRenameOperationsChecksum;
    ResultCode := -1;
    if not Exec(FileName, '/install /quiet /norestart', '', SW_HIDE,
      ewWaitUntilTerminated, ResultCode) then begin
      Result := 'Не удалось запустить установку Microsoft Visual C++. Код Windows: ' +
        IntToStr(ResultCode) + '.';
      Exit;
    end;
    Log(Format('VC redistributable installer exit code: %d', [ResultCode]));
    PendingAfter := MakePendingFileRenameOperationsChecksum;
    RuntimeRestartRequired := (ResultCode = 3010) or (ResultCode = 1641) or
      (PendingBefore <> PendingAfter);

    { 1638 / 0x80070666 can mean another installer installed a newer version
      between our initial check and execution. Accept only after rechecking. }
    if (ResultCode <> 0) and (ResultCode <> 3010) and (ResultCode <> 1641) and
      (ResultCode <> 1638) and (ResultCode <> -2147023258) then begin
      Result := 'Установка Microsoft Visual C++ не завершена. Код: ' +
        IntToStr(ResultCode) + '. Установка программы не началась.' + #13#10 +
        'Подробности находятся в журнале dd_vcredist_*.log в папке %TEMP%.';
      Exit;
    end;
    if not IsVCRuntimeInstalled then begin
      if RuntimeRestartRequired then begin
        NeedsRestart := True;
        Result := 'Для завершения установки Microsoft Visual C++ требуется перезагрузка.' + #13#10 +
          'Перезагрузите компьютер и снова запустите установщик QtZpl Designer.';
      end else
        Result := 'После установки не обнаружен Microsoft Visual C++ (x64) версии ' +
          MinimumVCRuntimeVersion + ' или новее. Установка программы не началась.';
    end;
  finally
    RuntimeProgressPage.Hide;
    if Result <> '' then
      Log(Result);
  end;
end;

function NeedRestart: Boolean;
begin
  Result := RuntimeRestartRequired;
end;

function CanLaunchApplication: Boolean;
begin
  Result := not RuntimeRestartRequired;
end;
