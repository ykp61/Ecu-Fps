param([Parameter(Mandatory)][string]$Probe)
# UI-06 + spec 2026-11-12 E: oyun onayi uygulama ici kip penceresiyle sorulur (Windows TaskDialog degil). Erisilebilir mi?
#  - ana pencere yokken soru sorulmaz (probun ilk satiri "nowindow=none");
#  - kip penceresi ana pencerenin UIA agacindadir: AutomationId "modal", ControlType Window, WindowPattern.IsModal;
#  - baslik oyun adini icerir, govde okunur, dugmeler adlariyla ("Evet" / "Hayir") bulunur;
#  - kip acikken sayfa ogeleri (nav.overview) UIA agacinda yoktur;
#  - "Evet" Invoke -> yes; "Hayir" Invoke -> no; WindowPattern.Close (Esc ile ayni yol) -> none (karar kaydedilmez).
# Pencere etkinlestirilmez (prob SW_SHOWNOACTIVATE + en alt); basma UIA Invoke ile yapilir, odak gerekmez.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
function Fail($m) { throw "CONSENT: $m" }
$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]
function ById($root, $id) {
    $root.FindFirst($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $id)))
}

function Run($button, $buttonId, $expect) {
    $out = Join-Path $env:TEMP ("ef_consent_{0}.txt" -f $PID)
    $p = Start-Process -FilePath $Probe -PassThru -RedirectStandardOutput $out -WindowStyle Normal
    try {
        $win = $null; $modal = $null
        $end = (Get-Date).AddSeconds(15)
        # T8 kok neden (2026-11-20): Process.MainWindowHandle konsol alt sistemli probda probun KONSOL penceresini
        # (baslik = EXE yolu) dondurebiliyor ve iki okuma arasinda degisebiliyor (bos -> FromHandle hatasi). Konsolu
        # olmayan ust surecten (paralel ctest / Start-Job) baslatilinca probe yeni konsol alir; kip penceresi o agacta
        # aranip 15 sn sonra "not found" dusuyordu. Pencere artik sinif adi + surec kimligiyle UIA kokunden bulunur.
        $winCond = New-Object System.Windows.Automation.AndCondition(
            (New-Object System.Windows.Automation.PropertyCondition($AE::ClassNameProperty, 'EcuFps.ConsentProbe')),
            (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $p.Id)))
        while (-not $modal -and (Get-Date) -lt $end -and -not $p.HasExited) {
            Start-Sleep -Milliseconds 200
            if (-not $win) { $win = $AE::RootElement.FindFirst($TS::Children, $winCond) }
            if ($win) { $modal = ById $win 'modal' }
        }
        if (-not $modal) {
            $what = if ($win) { "window found, modal missing" } else { "probe window (class EcuFps.ConsentProbe, pid $($p.Id)) not found" }
            Fail "in-app modal not found in the main window UIA tree ($what; exited=$($p.HasExited))"
        }
        if ($modal.Current.ControlType -ne [System.Windows.Automation.ControlType]::Window) { Fail "modal control type is $($modal.Current.ControlType.ProgrammaticName)" }
        $wp = $modal.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern)
        if (-not $wp.Current.IsModal) { Fail 'modal does not report IsModal' }
        if ($modal.Current.Name -notmatch 'Test Oyunu') { Fail "modal name does not name the game: $($modal.Current.Name)" }
        $all = $modal.FindAll($TS::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
        $text = ($all | ForEach-Object { $_.Current.Name }) -join ' | '
        if ($text -notmatch 'Test Oyunu') { Fail "question does not name the game: $text" }
        if (-not (ById $modal 'modal.title') -or -not (ById $modal 'modal.body.0')) { Fail "title / body text not exposed: $text" }
        foreach ($b in @('modal.yes', 'modal.no')) { if (-not (ById $modal $b)) { Fail "$b not exposed: $text" } }
        # Kip acikken sayfa ogeleri erisilemez (ne fare ne klavye ne UIA).
        if (ById $win 'nav.overview') { Fail 'page element reachable under the modal' }
        if ($expect -eq 'none') {
            $wp.Close()   # Esc ile ayni yol: Hayir, karar kaydedilmez
        } else {
            $btn = ById $modal $buttonId
            if ($btn.Current.Name -ne $button) { Fail "button $buttonId is named '$($btn.Current.Name)', expected '$button'" }
            $btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        }
        if (-not $p.WaitForExit(8000)) { Fail 'probe did not exit' }
        $lines = @(Get-Content $out | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        if ($lines -notcontains 'nowindow=none') { Fail "expected nowindow=none, got: $($lines -join ' / ')" }
        if ($lines -notcontains "consent=$expect") { Fail "expected consent=$expect, got: $($lines -join ' / ')" }
        Write-Host "$buttonId -> consent=$expect"
    } finally {
        if (-not $p.HasExited) { $p.Kill() }
        Remove-Item $out -ErrorAction SilentlyContinue
    }
}
Run 'Evet' 'modal.yes' 'yes'
Run ('Hay' + [char]0x0131 + 'r') 'modal.no' 'no'  # ASCII disi karakter kodlamadan bagimsiz
Run '' '' 'none'
Write-Host 'OK'
