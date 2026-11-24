param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$FixTool, [switch]$AllowDisplay, [switch]$Foreground)
# CHK "Duzelt" UIA turu (gercek cihaz): Oyun Hazirligi > geri alinabilir otomatik duzeltme satiri > Duzelt >
# onay penceresinde (onay sirasinda degisiklik yok) Evet > kayit defteri geri okumasi degisti + "fixes" journal'inda
# yeni Active islem (eski deger kayitli) > geri al > kayit defteri ozgun ham degere dondu + journal "restored".
# Tercih sirasi (dusuk risk, kullanici kapsami HKCU, yonetici/UAC yok):
#   1) Arka planda kayit (GameDVR HistoricalCaptureEnabled 1 -> 0)
#   2) Oyun Modu (GameBar AutoGameModeEnabled 0 -> 1)
#   3) Pencereli oyun iyilestirmeleri (DirectXUserGlobalSettings SwapEffectUpgradeEnable -> 1)
#   4) Yalniz -AllowDisplay ile: yenileme hizi (Hz). Uygulamanin 15 sn "Koru / Geri al" penceresinde Geri al basilir.
# Uygun satir yoksa hicbir sey degistirilmez (cikis 77 = atlandi).
# Geri alma yolu (1-3): urun ici "Geri al" (AutomationId ^readiness\.undo\.N; N = bulgu indeksi, kalici kimlik
# DEGIL, yeniden taramada kayabilir). Hedef = duzeltmeden SONRAKI undo id'leri eksi duzeltmeden ONCEKI id'ler
# ($undoBefore); tam olarak bir tane olmali. Ogenin adi ("Geri al: <bulgu metni>") veya onay metni secilen
# bulguyu ya da ayni hedefin duzeltme sonrasi metnini (undoMatch) icermeli; aksi halde basilmaz / Hayir.
# Urun geri almasi ayni islemi yerinde geri alir (TransactionEngine::rollback, yeni islem yok) -> o islem
# "restored"; onceden var olan islemlerin durumu degismemeli. Yeni undo id yoksa (o dugmeden onceki surumler)
# geri alma, uygulama kapatildiktan sonra ef_fix_journal (ayni uretim adaptorleri + rollback, yalniz o islem,
# yalniz "gaming:") ile yapilir. Gunlukte undo_path=product|helper.
# Hz yolunda geri alma uygulamanin kendi "Geri al" dugmesidir (Koru/Geri al penceresi).
# Emniyet: Invoke oncesi satir adi secilenle ayni olmali; onay metni secilen bulguyu icermiyorsa Hayir basilir.
# Onay pencereleri uygulama ici kip penceresidir (spec 2026-11-12 E): ana pencerenin UIA agacinda AutomationId "modal"
# (Window + IsModal), dugmeleri "modal.no" / "modal.yes" (UIA Invoke). Windows TaskDialog (#32770) yoktur.
# Her hata yolunda acik kip penceresinde once "modal.no" (Hayir / Geri al) basilir, sonra uygulama kapatilir; ekran modu
# farkli kalirsa yuksek sesle basarisiz olur. Journal/arac hatasi son care RegWriteBack'i engellemez.
# Opt-in: EF_UIA_TOUR=1 ve EF_ALLOW_MUTATION=1 (aksi halde cikis 77). EF_TEST_BACKGROUND=1 varsayilan (-Foreground kapatir).
# Oyun calisiyorsa uygulama degisikligi engeller (onay penceresi gelmez) -> tur hatasiz durur, hicbir sey degismez.
if ($env:EF_UIA_TOUR -ne '1' -or $env:EF_ALLOW_MUTATION -ne '1') {
    Write-Host 'readiness_fix_tour: skipped (set EF_UIA_TOUR=1 and EF_ALLOW_MUTATION=1; opens the app window and changes one setting, then restores it)'
    exit 77
}
if ($Foreground) { Remove-Item Env:EF_TEST_BACKGROUND -ErrorAction SilentlyContinue } else { $env:EF_TEST_BACKGROUND = '1' }
$ErrorActionPreference = 'Stop'
$procName = [IO.Path]::GetFileNameWithoutExtension($Exe)
if (Get-Process -Name $procName -ErrorAction SilentlyContinue) {
    Write-Host "readiness_fix_tour: skipped ($procName already running; close it first so the journal has one writer)"; exit 77
}
if (-not (Test-Path $FixTool)) { throw "FIX: tool not found: $FixTool" }
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class WFx {
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static void Wheel(IntPtr h, int delta) { PostMessage(h, 0x020A, (IntPtr)((delta & 0xFFFF) << 16), IntPtr.Zero); }  // WM_MOUSEWHEEL
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct DD { public int cb;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string Name; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string Str;
    public int Flags; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string Id; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string Key; }
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct DM {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string Dev; public short SpecV, DrvV, Size, DrvExtra; public int Fields;
    public int PosX, PosY, Orient, Fixed; public short Color, Duplex, YRes, TT, Collate;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string Form; public short LogPx; public int Bpp, W, H, DFlags, Freq; }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern bool EnumDisplayDevicesW(string d, int i, ref DD dd, int f);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern bool EnumDisplaySettingsW(string d, int mode, ref DM dm);
  // Etkin ekranlarin "\\.\DISPLAYn=WxH@Hz" listesi (salt okuma)
  public static string Modes() { var sb = new System.Text.StringBuilder();
    for (int i = 0; ; i++) { var dd = new DD(); dd.cb = Marshal.SizeOf(typeof(DD)); if (!EnumDisplayDevicesW(null, i, ref dd, 0)) break;
      if ((dd.Flags & 1) == 0) continue; var dm = new DM(); dm.Size = (short)Marshal.SizeOf(typeof(DM));
      if (EnumDisplaySettingsW(dd.Name, -1, ref dm)) sb.Append(dd.Name + "=" + dm.W + "x" + dm.H + "@" + dm.Freq + ";"); }
    return sb.ToString(); }
}
"@
function Fail($m) { throw "FIX: $m" }
$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$HKCU = [Microsoft.Win32.Registry]::CurrentUser

# Duzeltme hedefleri: UIA adi (Turkce veya Ingilizce katalog; ASCII disi harfler '.' ile) + kayit defteri konumu.
# undoMatch: ayni hedefin duzeltme SONRASI bulgu metni (chk.win.*_on/_off). Urun "Geri al" adi ve onay metni
# check_text(c)'yi o anki (duzeltilmis) durumla kurar; ozgun bulgu metni orada artik gecmez.
$cands = @(
    @{ key = 'dvr';      target = 'gaming:dvr_background'; match = 'Olanlar. kaydet|Record what happened'
       undoMatch = 'Arka planda kay.t kapal.|Background recording is off'
       sub = 'Software\Microsoft\Windows\CurrentVersion\GameDVR'; name = 'HistoricalCaptureEnabled'; expect = { param($v) "$v" -eq 'DWord:0' } },
    @{ key = 'gamemode'; target = 'gaming:game_mode';      match = 'Oyun Modu kapal.|Game Mode is off'
       undoMatch = 'Oyun Modu a..k|Game Mode is on'
       sub = 'Software\Microsoft\GameBar'; name = 'AutoGameModeEnabled'; expect = { param($v) "$v" -eq 'DWord:1' } },
    @{ key = 'windowed'; target = 'gaming:windowed_opt';   match = 'Pencereli oyunlar i.in iyile.tirmeler kapal.|windowed games are off'
       undoMatch = 'Pencereli oyunlar i.in iyile.tirmeler a..k|windowed games are on'
       sub = 'Software\Microsoft\DirectX\UserGpuPreferences'; name = 'DirectXUserGlobalSettings'; expect = { param($v) "$v" -match '(^|[:;])SwapEffectUpgradeEnable=1(;|$)' } }
)
$fixPrefix = '^(D.zelt|Fix): '
$undoPrefix = '^(Geri al|Undo): '
# Metin secilen bulguya ait mi: ozgun bulgu metni (literal) ya da ayni hedefin duzeltme sonrasi metni.
function AboutChosen($s) {
    if (-not $s -or -not $finding -or -not $chosen.match -or -not $chosen.undoMatch) { return $false }
    return ($s.Contains($finding) -or $s -match $chosen.match -or $s -match $chosen.undoMatch)
}

function RegRead($c) {
    $k = $HKCU.OpenSubKey($c.sub)
    if (-not $k) { return '<absent>' }
    try {
        $v = $k.GetValue($c.name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($null -eq $v) { return '<absent>' }
        return "{0}:{1}" -f $k.GetValueKind($c.name), $v
    } finally { $k.Close() }
}
function RegWriteBack($c, $raw) {
    # Son care (yalniz arac geri alamazsa): betigin kendi anlik goruntusunu birebir yaz.
    $k = $HKCU.CreateSubKey($c.sub)
    try {
        if ($raw -eq '<absent>') { $k.DeleteValue($c.name, $false); return }
        $i = $raw.IndexOf(':'); $kind = [Microsoft.Win32.RegistryValueKind]$raw.Substring(0, $i); $val = $raw.Substring($i + 1)
        if ($kind -eq 'DWord') { $val = [int]$val }
        $k.SetValue($c.name, $val, $kind)
    } finally { $k.Close() }
}
function Journal { $o = & $FixTool list; if ($LASTEXITCODE -ne 0) { Fail "fix_journal list exit $LASTEXITCODE : $o" }; return @($o) }
function TxIds($lines) { @($lines | ForEach-Object { if ($_ -match '^tx=([0-9a-f]+) ') { $Matches[1] } }) }
function TxStates($lines) { $m = @{}; foreach ($l in $lines) { if ($l -match '^tx=([0-9a-f]+) state=(\S+) ') { $m[$Matches[1]] = $Matches[2] } }; return $m }
function TxBlock($lines, $id) {
    $out = @(); $in = $false
    foreach ($l in $lines) { if ($l -match '^tx=') { $in = $l -like "tx=$id *" }; if ($in) { $out += $l } }
    return $out
}
function Find($id) { $script:win.FindFirst($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $id))) }
function Rows($pattern) {
    $all = $script:win.FindAll($TS::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    @($all | Where-Object { $_.Current.AutomationId -match $pattern } | ForEach-Object { [pscustomobject]@{ id = $_.Current.AutomationId; name = $_.Current.Name } })
}
function Actions { Rows '^readiness\.action\.\d+$' }
# Urun ici "Geri al" (readiness.undo.N) varsa tercih edilir; yoksa ef_fix_journal yardimcisi kullanilir.
function Undos { Rows '^readiness\.undo\.\d+$' }
function Scroll($n) { for ($i = 0; $i -lt [math]::Abs($n); $i++) { [WFx]::Wheel($script:hwnd, $(if ($n -lt 0) { 120 } else { -120 })) }; Start-Sleep -Milliseconds 500 }
# Sayfayi bastan sona kaydirarak id'yi ara (gorunmeyen kartlar UIA'da yoktur).
function FindScrolled($id) {
    $e = Find $id; if ($e) { return $e }
    Scroll -40
    for ($i = 0; $i -lt 30; $i++) { $e = Find $id; if ($e) { return $e }; Scroll 3 }
    return $null
}
function CollectUndos {
    $u = @{}; Scroll -40
    for ($i = 0; $i -lt 30; $i++) { $n0 = $u.Count; foreach ($a in Undos) { $u[$a.id] = $a.name }; Scroll 3; if ($i -gt 3 -and $u.Count -eq $n0) { break } }
    return $u
}
# Uygulama kapaliyken: tam bir yeni "fixes" islemi, beklenen durum + hedef + on-durum anlik goruntusu.
# Urun geri almasi ayni islemi yerinde "restored" yapar (yeni islem yok); onceden var olan islemlerin
# durumu degismemis olmali (baska bir duzeltme geri alinmadi).
function CheckNewTx($wantState) {
    $j = Journal; $new = @(TxIds $j | Where-Object { $ids0 -notcontains $_ })
    if ($new.Count -ne 1) { Fail "expected 1 new fixes transaction, found $($new.Count) (product undo must roll back in place, not add a tx)" }
    $st = TxStates $j
    foreach ($id in $states0.Keys) {
        if (-not $st.ContainsKey($id)) { Fail "pre-existing fixes transaction $id disappeared" }
        if ($st[$id] -ne $states0[$id]) { Fail "pre-existing fixes transaction $id changed state: $($states0[$id]) -> $($st[$id])" }
    }
    $script:txId = $new[0]
    $blk = TxBlock $j $script:txId; $script:log += $blk
    if ($blk[0] -notmatch " state=$wantState ") { Fail "journal state not $wantState : $($blk[0])" }
    if (($blk -join "`n") -notmatch "target=$([regex]::Escape($chosen.target)) ") { Fail "journal step target is not $($chosen.target)" }
    $existedWant = if ($before[$chosen.key] -eq '<absent>') { 'existed=0' } else { 'existed=1' }
    if ($chosen.key -ne 'windowed' -and ($blk -join "`n") -notmatch $existedWant) { Fail "journal snapshot does not match pre-state ($existedWant)" }
}
# Uygulama ici kip penceresi (spec 2026-11-12 E): ana pencerenin UIA agacinda AutomationId "modal"; yoksa $null.
function ModalWindow { if ($script:win) { return (Find 'modal') }; return $null }
# Kip penceresi dugmesi: 'modal.no' (Hayir / Geri al) ya da 'modal.yes' (Evet / Koru). UIA Invoke; dugme yoksa $false.
function ModalPress($id) {
    $b = Find $id
    if (-not $b) { return $false }
    $b.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    return $true
}
function WaitDialog($sec) { $end = (Get-Date).AddSeconds($sec); while ((Get-Date) -lt $end) { $d = ModalWindow; if ($d) { return $d }; Start-Sleep -Milliseconds 200 }; return $null }
function DialogText($d) { (@($d.FindAll($TS::Descendants, [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object { $_.Current.Name }) -join ' | ') }
function Close-App { if ($script:proc -and -not $script:proc.HasExited) { $script:proc.Kill(); $script:proc.WaitForExit(5000) | Out-Null } }

$log = @("background=$($env:EF_TEST_BACKGROUND -eq '1')")
$before = @{}; foreach ($c in $cands) { $before[$c.key] = RegRead $c; $log += "before $($c.key)=$($before[$c.key])" }
$modesBefore = [WFx]::Modes(); $log += "displays_before=$modesBefore"
$j0 = Journal; $ids0 = TxIds $j0; $states0 = TxStates $j0; $log += "journal_before: $($j0[0])"
$chosen = $null; $display = $false; $txId = $null; $ok = $false; $skipped = $false; $displayYes = $false
$undoPath = 'none'
$script:proc = $null
try { do {
    $script:proc = Start-Process -FilePath $Exe -PassThru
    $end = (Get-Date).AddSeconds(15)
    while ($script:proc.MainWindowHandle -eq 0 -and (Get-Date) -lt $end -and -not $script:proc.HasExited) { Start-Sleep -Milliseconds 150; $script:proc.Refresh() }
    if ($script:proc.HasExited -or $script:proc.MainWindowHandle -eq 0) { Fail 'window not found' }
    $script:hwnd = $script:proc.MainWindowHandle
    $script:win = $AE::FromHandle($script:hwnd)
    $end = (Get-Date).AddSeconds(10); while (-not (Find 'nav.readiness') -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 200 }
    $nav = Find 'nav.readiness'; if (-not $nav) { Fail 'nav.readiness not exposed' }
    $nav.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    # Tarama bitince "Yeniden tara" dugmesi gorunur (tarama sirasinda satir dugmeleri gizlidir).
    $end = (Get-Date).AddSeconds(45); while (-not (Find 'readiness.rescan') -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 300 }
    if (-not (Find 'readiness.rescan')) { Fail 'readiness scan did not finish in 45 s' }
    Start-Sleep -Milliseconds 500
    # Sayfa boyunca kaydirarak tum eylem satirlarini topla (gorunmeyen kartlar UIA'da yoktur).
    $seen = @{}; $undoBefore = @{}
    Scroll -40
    for ($i = 0; $i -lt 30; $i++) {
        $n0 = $seen.Count + $undoBefore.Count
        foreach ($a in Actions) { $seen[$a.id] = $a.name }
        foreach ($a in Undos) { $undoBefore[$a.id] = $a.name }
        Scroll 3
        if ($i -gt 3 -and ($seen.Count + $undoBefore.Count) -eq $n0) { break }
    }
    foreach ($k in ($seen.Keys | Sort-Object)) { $log += "row $k = $($seen[$k])" }
    foreach ($k in ($undoBefore.Keys | Sort-Object)) { $log += "undo_before $k = $($undoBefore[$k])" }
    foreach ($c in $cands) {
        $hit = $seen.Keys | Where-Object { $seen[$_] -match $fixPrefix -and $seen[$_] -match $c.match } | Select-Object -First 1
        if ($hit) { $chosen = $c; $rowId = $hit; break }
    }
    if (-not $chosen -and $AllowDisplay) {
        $hit = $seen.Keys | Where-Object { $seen[$_] -match $fixPrefix -and $seen[$_] -match '\d+ Hz' } | Select-Object -First 1
        if ($hit) { $display = $true; $rowId = $hit; $chosen = @{ key = 'refresh'; target = 'display:'; match = '\d+ Hz' } }
    }
    if (-not $chosen) {
        $skipped = $true
        $log += 'no reversible auto-fix row (gaming toggles' + $(if ($AllowDisplay) { ' or refresh rate' } else { '; refresh rate needs -AllowDisplay' }) + ') - nothing changed'
        break
    }
    $log += "chosen=$($chosen.key) row=$rowId name=$($seen[$rowId])"
    # Onay penceresindeki soru check_text'i icerir (ChkFixQuestion {finding}); satir adi "<eylem>: <metin>".
    $finding = $seen[$rowId] -replace $fixPrefix, ''
    # Satiri gorunur yap ve Duzelt'e bas
    $row = FindScrolled $rowId; if (-not $row) { Fail "row $rowId not reachable" }
    # Liste toplama ile tiklama arasinda yeniden kurulduysa ayni id baska satira (or. Hz, VRR) isaret edebilir.
    $nameNow = $row.Current.Name
    if ($nameNow -ne $seen[$rowId]) { Fail "row $rowId changed since collection: '$nameNow' (chosen '$($seen[$rowId])'); nothing pressed" }
    $row.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    $dlg = WaitDialog 10
    if (-not $dlg) { Fail 'confirmation dialog did not appear (a running game blocks changes; nothing applied)' }
    $text = DialogText $dlg
    $log += "dialog: $text"
    if ($text -notmatch 'Evet|Yes' -or $text -notmatch 'Hay.r|No') { [void](ModalPress 'modal.no'); Fail "Yes/No buttons not exposed: $text" }
    # Evet'e yalniz secilen bulgunun onayi icin basilir; baska bulgu sorulduysa Hayir.
    if (-not $text.Contains($finding) -or $text -notmatch $chosen.match) {
        [void](ModalPress 'modal.no')   # Hayir: hicbir sey degismez
        Fail "dialog does not ask about the chosen finding '$finding' (pressed No): $text"
    }
    # Onay sirasinda hicbir sey degismemis olmali
    if ($display) { if ([WFx]::Modes() -ne $modesBefore) { [void](ModalPress 'modal.no'); Fail 'display mode changed before Yes' } }
    elseif ((RegRead $chosen) -ne $before[$chosen.key]) { [void](ModalPress 'modal.no'); Fail "value changed before Yes: $(RegRead $chosen)" }
    if ($display) { $displayYes = $true }
    if (-not (ModalPress 'modal.yes')) { Fail 'modal.yes not exposed' }   # Evet

    if ($display) {
        # Uygulama Hz'i degistirir, sonra "Koru / Geri al" (15 sn) sorar: Geri al (modal.no).
        Start-Sleep -Milliseconds 800
        $keep = WaitDialog 12
        $during = [WFx]::Modes(); $log += "displays_during=$during"
        if (-not $keep) { Fail 'Keep/Revert dialog did not appear' }
        $kt = DialogText $keep; $log += "keep_dialog: $kt"
        if ($kt -notmatch 'Koru|Keep' -or $kt -notmatch 'Geri al|Revert') { Fail "Keep/Revert buttons not exposed: $kt" }
        if ($during -eq $modesBefore) { Fail 'refresh rate did not change after Yes' }
        if (-not (ModalPress 'modal.no')) { Fail 'modal.no (Revert) not exposed' }  # Geri al
        $end = (Get-Date).AddSeconds(10); while ([WFx]::Modes() -ne $modesBefore -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 300 }
        $after = [WFx]::Modes(); $log += "displays_after_revert=$after"
        if ($after -ne $modesBefore) { Fail "display not restored: $after (before $modesBefore)" }
        Start-Sleep -Seconds 1
        Close-App
        $j1 = Journal; $new = @(TxIds $j1 | Where-Object { $ids0 -notcontains $_ })
        if ($new.Count -ne 1) { Fail "expected 1 new fixes transaction, found $($new.Count)" }
        $blk = TxBlock $j1 $new[0]; $log += $blk
        if ($blk[0] -notmatch ' state=restored ') { Fail "journal state not restored: $($blk[0])" }
        if (($blk -join "`n") -notmatch 'target=display:\d+:refresh') { Fail 'journal step is not a refresh-rate step' }
        $ok = $true
        break
    }

    # Gecerli geri okuma: deger beklenen yeni degere gecmeli
    $end = (Get-Date).AddSeconds(10)
    while (-not (& $chosen.expect (RegRead $chosen)) -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 300 }
    $applied = RegRead $chosen; $log += "after_yes $($chosen.key)=$applied"
    if (-not (& $chosen.expect $applied)) { Fail "read-back did not change to the fixed value: $applied" }
    if ($applied -eq $before[$chosen.key]) { Fail 'value unchanged after Yes' }
    # Uygulama yeniden tarar: satir "Duzelt" eylemini kaybetmeli
    $end = (Get-Date).AddSeconds(30); $still = $true
    while ($still -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 500; $e = Find $rowId; $still = $e -and $e.Current.Name -eq $seen[$rowId] }
    $log += "row_after_fix_gone=$(-not $still)"

    # Geri alma yolu secimi: N bulgu indeksidir (kalici kimlik degil; yeniden taramada kayabilir). Hedef yalniz
    # kume farki: duzeltme sonrasi undo id'leri eksi $undoBefore; tam olarak bir tane olmali. $undoBefore'da olan
    # bir id (or. kullanicinin onceki duzeltmesi) asla basilmaz. Yeni id yoksa ef_fix_journal yardimcisi.
    $end = (Get-Date).AddSeconds(45); while (-not (Find 'readiness.rescan') -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 300 }
    if (-not (Find 'readiness.rescan')) { Fail 'readiness rescan after fix did not finish in 45 s' }
    Start-Sleep -Milliseconds 500
    $undoAfter = CollectUndos
    foreach ($k in ($undoAfter.Keys | Sort-Object)) { $log += "undo_after $k = $($undoAfter[$k])" }
    $newUndo = @($undoAfter.Keys | Where-Object { -not $undoBefore.ContainsKey($_) } | Sort-Object)
    $log += "undo_new=$($newUndo -join ',')"
    if ($newUndo.Count -gt 1) { Fail "expected exactly 1 new readiness.undo.* id after the fix, found $($newUndo.Count): $($newUndo -join ', '); nothing pressed" }
    $undo = $null
    if ($newUndo.Count -eq 1) {
        $undoId = $newUndo[0]
        $undo = FindScrolled $undoId
        if (-not $undo) { Fail "new undo $undoId not reachable; nothing pressed" }
    }
    if ($undo) {
        $undoPath = 'product'
        $un = $undo.Current.Name; $log += "undo_path=product id=$($undo.Current.AutomationId) name=$un"
        # Toplama ile tiklama arasinda liste yeniden kurulduysa ayni id baska satira isaret edebilir.
        if ($undo.Current.AutomationId -ne $undoId -or $un -ne $undoAfter[$undoId]) { Fail "undo $undoId changed since collection: '$un' (collected '$($undoAfter[$undoId])'); nothing pressed" }
        if ($un -notmatch $undoPrefix) { Fail "undo element name is not an undo action: $un" }
        # Invoke ONCESI: ad ("Geri al: <bulgu>") secilen bulguya ait olmali. Onay penceresi Invoke'tan sonra gelir;
        # pencere gelmezse geri alma coktan olmus olabilir, bu yuzden yalniz pencereye guvenilmez.
        if (-not (AboutChosen ($un -replace $undoPrefix, ''))) { Fail "undo element does not refer to the chosen finding '$finding': $un; nothing pressed" }
        $undo.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        # Urun geri almayi Evet/Hayir ile onaylatir: yalniz secilen bulgunun geri almasi soruluyorsa Evet.
        $ud = WaitDialog 5
        if ($ud) {
            $ut = DialogText $ud; $log += "undo_dialog: $ut"
            if ($ut -notmatch 'Evet|Yes' -or $ut -notmatch 'Hay.r|No' -or $ut -notmatch 'Geri al|geri al|Undo|undo|Revert|revert|Restore|restore') {
                [void](ModalPress 'modal.no'); Fail "unexpected dialog after undo (pressed No): $ut"
            }
            if (-not (AboutChosen $ut)) {
                [void](ModalPress 'modal.no')   # Hayir: baska bir duzeltmenin geri almasi olabilir
                Fail "undo dialog does not refer to the chosen finding (pressed No): name '$un' dialog '$ut'"
            }
            if (-not (ModalPress 'modal.yes')) { Fail 'modal.yes not exposed (undo)' }   # Evet = geri almayi onayla
        }
        $end = (Get-Date).AddSeconds(10)
        while ((RegRead $chosen) -ne $before[$chosen.key] -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 300 }
        $restored = RegRead $chosen; $log += "after_product_undo $($chosen.key)=$restored"
        if ($restored -ne $before[$chosen.key]) { Fail "registry not restored by product undo: '$restored' (before '$($before[$chosen.key])')" }
        Start-Sleep -Seconds 1
        Close-App   # journal'i yalniz uygulama kapaliyken oku
        CheckNewTx 'restored'
        $ok = $true
        break
    }

    $undoPath = 'helper'; $log += 'undo_path=helper (no new product readiness.undo.* element after the fix)'
    Close-App   # journal'a tek yazan kalsin
    CheckNewTx 'active'

    # Geri al: ayni uretim motoru, yalniz bu islem
    $rb = & $FixTool rollback $txId; $rc = $LASTEXITCODE; $log += @($rb)
    if ($rc -ne 0) { Fail "rollback tool exit $rc" }
    $restored = RegRead $chosen; $log += "after_rollback $($chosen.key)=$restored"
    if ($restored -ne $before[$chosen.key]) { Fail "registry not restored: '$restored' (before '$($before[$chosen.key])')" }
    $j2 = Journal; $blk2 = TxBlock $j2 $txId; $log += $blk2
    if ($blk2[0] -notmatch ' state=restored ') { Fail "journal not restored after rollback: $($blk2[0])" }
    $ok = $true
} while ($false) } catch {
    $log += "ERROR: $($_.Exception.Message)"
    $ok = $false
} finally {
    # Uygulama hala aciksa ve bir kip penceresi aciksa once modal.no: onay penceresinde "Hayir" (hicbir sey degismez),
    # Koru/Geri al penceresinde "Geri al". Ekran yolunda bu yapilmadan oldurmek 15 sn otomatik geri donusu da keser.
    try {
        if ($script:proc -and -not $script:proc.HasExited) {
            if ((ModalWindow) -and (ModalPress 'modal.no')) { $log += 'SAFETY: pressed modal.no (No / Revert) on open dialog before closing the app' }
            if ($displayYes -and -not $ok) {
                # Uygulamanin kendi geri donusunu bekle (Geri al basildi ya da 15 sn sayaci).
                $end = (Get-Date).AddSeconds(20)
                while ([WFx]::Modes() -ne $modesBefore -and (Get-Date) -lt $end -and -not $script:proc.HasExited) {
                    Start-Sleep -Milliseconds 300
                    if (ModalWindow) { [void](ModalPress 'modal.no') }
                }
            }
        }
    } catch { $log += "SAFETY: dialog handling failed: $($_.Exception.Message)" }
    try { Close-App } catch { $log += "SAFETY: close failed: $($_.Exception.Message)" }
    # Emniyet agi (kayit defteri yolu): islem hala aktifse once arac, sonra (son care) betigin anlik goruntusu.
    # Journal/arac hatasi RegWriteBack'i asla engellemez.
    if ($chosen -and -not $display -and -not $ok) {
        try {
            if (-not $txId) { $txId = @(TxIds (Journal) | Where-Object { $ids0 -notcontains $_ }) | Select-Object -First 1 }
            if ($txId) { $r = & $FixTool rollback $txId; $log += "safety rollback: $r" }
        } catch { $log += "SAFETY: journal/rollback tool failed: $($_.Exception.Message)" }
        try {
            if ((RegRead $chosen) -ne $before[$chosen.key]) { RegWriteBack $chosen $before[$chosen.key]; $log += "SAFETY: wrote snapshot back directly (journal may still say active)" }
        } catch { $log += "SAFETY: direct write-back FAILED: $($_.Exception.Message) - restore $($chosen.sub)\$($chosen.name) to '$($before[$chosen.key])' by hand" }
    }
    foreach ($c in $cands) { $log += "final $($c.key)=$(RegRead $c) (before $($before[$c.key]))" }
    $modesFinal = [WFx]::Modes()
    $log += "displays_final=$modesFinal"
    # Kosulsuz: -AllowDisplay olmadan da ekran modu degismis olmamali (maliyetsiz emniyet).
    if ($modesFinal -ne $modesBefore) {
        $ok = $false
        $log += "!!! DISPLAY MODE STILL DIFFERS: now '$modesFinal', before '$modesBefore'. Restore the refresh rate in Windows Settings > Display; the fixes journal may still list the change as active. !!!"
    }
    $log += "undo_path=$undoPath"
    $log += $(if ($skipped) { 'PASS=skipped' } else { "PASS=$ok" })
    $log -join "`n" | Write-Host
}
if ($skipped) { exit 77 }
if ($ok) { Write-Host 'OK' } else { exit 1 }
