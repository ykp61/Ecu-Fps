<#
.SYNOPSIS
  Yerel CI: temiz klonda debug + dist derler, test eder ve sonucu HEAD'e bagli bir gunluge yazar (P9-B10).

.DESCRIPTION
  GitHub Actions faturalandirma kilidi (B-008) acilmadikca yerel kanit gercek kapidir. Bu betik:
    1. Calisma agaci temiz olmali (kanit bir commit'e aittir). Kirliyse durur (-AllowDirty ile "dirty yes" yazilir).
    2. HEAD'i %TEMP%\ef-local-ci-<HEAD12> altina temiz klonlar (kirli dosya, eski build\ ve onbellek sizmaz).
    3. x64-debug ve x64-dist presetlerini derler ve ctest calistirir.
         -Scope Full  (varsayilan): uia_tour ve real_gpu disindaki tum testler; pencere acan testler
                      EF_TEST_BACKGROUND=1 ile odak calmadan kosar. Yayin kapisi yalniz Full kabul eder.
         -Scope Quiet: yalniz pencere acmayan "unit" ve "services" etiketleri. Oyun oynanirken kullanilir;
                      yayin kapisini GECMEZ.
    4. build\local-ci\local-ci-<HEAD12>.log (kanit) ve build\local-ci\EcuFps-<HEAD12>.exe (test edilen dist EXE'si,
       SHA-256 gunlukte) yazar. -EvidenceCopy verilirse gunluk oraya da kopyalanir (ornek: artifacts\evidence\...).
    5. Derleme ve testler varsayilan Normal oncelikle kosar (-Priority BelowNormal: baska is yapilirken makineyi daha az zorlar). ctest -j 4: yalniz RUN_SERIAL olmayan testler paralel (bkz. tests/CMakeLists.txt siniflandirmasi). Bu betik ayar yazmaz, UAC istemez, ag kullanmaz.

  -Verify: yayin kapisi. Gunluk HEAD ile eslesmiyorsa, kirliyse, kapsam Full degilse, adimlardan biri kirmiziysa veya
  -Exe dosyasinin SHA-256 degeri gunlukteki ile ayni degilse cikis kodu 2. Kapi yerel bir tutarlilik denetimidir,
  guvenlik siniri degildir (gunluk elle yazilabilir); amac eski/baska commit'ten EXE yayinlamayi engellemektir.

  -SelfTest: kapi mantigini sentetik gunluklerle dener (derleme yok).
  -DryRun: ne yapacagini yazar, hicbir sey calistirmaz.

.EXAMPLE
  .\scripts\local_ci.ps1                       # tam kosu (oyun oynanmiyorken)
  .\scripts\local_ci.ps1 -Scope Quiet          # pencere acmayan alt kume
  .\scripts\local_ci.ps1 -DryRun
  .\scripts\local_ci.ps1 -Verify -Exe .\build\local-ci\EcuFps-<HEAD12>.exe
  .\scripts\local_ci.ps1 -SelfTest
#>
[CmdletBinding()]
param(
    [ValidateSet('Full', 'Quiet')][string]$Scope = 'Full',
    [switch]$DryRun,
    [switch]$AllowDirty,
    [switch]$Keep,
    [ValidateSet('BelowNormal', 'Normal')][string]$Priority = 'Normal',
    [string]$WorkDir = '',
    [string]$OutDir = '',
    [string]$EvidenceCopy = '',
    [switch]$Verify,
    [string]$Exe = '',
    [string]$LogPath = '',
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $Root 'build\local-ci' }
$LogFormat = 'Ecu Fps local-ci v1'
$RequiredSteps = @('debug-build', 'debug-ctest', 'dist-build', 'dist-ctest')

function Get-Sha256([string]$Path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant() }

# ---- Kanit gunlugu ayristirma ve kapi (Verify ve SelfTest ortak) ----
function Read-CiLog([string]$Path) {
    $kv = @{}
    $steps = @{}
    $lines = [IO.File]::ReadAllLines($Path, [Text.Encoding]::UTF8)
    $first = if ($lines.Count) { $lines[0].TrimStart([char]0xFEFF) } else { '' }
    foreach ($l in $lines) {
        if ($l -eq '---- output ----') { break }
        if ($l -match '^step (\S+) exit (-?\d+)(?: \| (.*))?$') {
            $steps[$Matches[1]] = @{ Exit = [int]$Matches[2]; Summary = "$($Matches[3])" }
        } elseif ($l -match '^(\w+) (.+)$') {
            if (-not $kv.ContainsKey($Matches[1])) { $kv[$Matches[1]] = $Matches[2].Trim() }
        }
    }
    return @{ Format = $first.Trim(); Kv = $kv; Steps = $steps }
}

# Dondurur: sorun listesi (bos = kapi gecti).
function Test-CiEvidence([string]$Log, [string]$Head12, [string]$FullHead, [string]$ExePath) {
    $problems = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path -LiteralPath $Log)) { return , @("Yerel CI gunlugu yok: $Log") }
    $p = Read-CiLog $Log
    if ($p.Format -ne $LogFormat) { $problems.Add("Gunluk bicimi taninmiyor ('$($p.Format)').") }
    $kv = $p.Kv
    if (-not $kv.ContainsKey('HEAD') -or $kv['HEAD'] -ne $Head12) { $problems.Add("Gunluk HEAD '$($kv['HEAD'])' bugunku HEAD '$Head12' ile eslesmiyor.") }
    if (-not $kv.ContainsKey('FULLHEAD') -or $kv['FULLHEAD'] -ne $FullHead) { $problems.Add("Gunluk FULLHEAD '$($kv['FULLHEAD'])' bugunku '$FullHead' ile eslesmiyor.") }
    if ($kv['dirty'] -ne 'no') { $problems.Add("Gunluk calisma agaci kirli ('$($kv['dirty'])'): kanit bir commit'e ait olmali.") }
    if ($kv['scope'] -ne 'Full') { $problems.Add("Kapsam '$($kv['scope'])'; yayin kapisi Full kapsam ister.") }
    foreach ($s in $RequiredSteps) {
        if (-not $p.Steps.ContainsKey($s)) { $problems.Add("Adim yok: $s."); continue }
        if ($p.Steps[$s].Exit -ne 0) { $problems.Add("Adim kirmizi: $s (cikis $($p.Steps[$s].Exit)).") }
        if ($s -like '*-ctest' -and $p.Steps[$s].Summary -notmatch '100% tests passed') { $problems.Add("Adim $s ozetinde '100% tests passed' yok.") }
    }
    if ($kv['result'] -ne 'PASS') { $problems.Add("Gunluk sonucu '$($kv['result'])' (PASS degil).") }
    if (-not $kv.ContainsKey('dist_exe_sha256') -or $kv['dist_exe_sha256'] -notmatch '^[0-9a-f]{64}$') {
        $problems.Add('Gunlukte gecerli dist_exe_sha256 yok.')
    } elseif (-not $ExePath) {
        $problems.Add('-Exe verilmedi; SHA-256 karsilastirilamadi.')
    } elseif (-not (Test-Path -LiteralPath $ExePath)) {
        $problems.Add("EXE yok: $ExePath")
    } elseif ((Get-Sha256 $ExePath) -ne $kv['dist_exe_sha256']) {
        $problems.Add("EXE SHA-256 gunlukle ayni degil (EXE: $((Get-Sha256 $ExePath)); gunluk: $($kv['dist_exe_sha256'])).")
    }
    return , $problems.ToArray()
}

function Get-RepoHead {
    $full = (& git -C $Root rev-parse HEAD).Trim()
    if ($LASTEXITCODE -or $full -notmatch '^[0-9a-f]{40}$') { throw 'git HEAD okunamadi.' }
    $short = (& git -C $Root rev-parse --short=12 HEAD).Trim()  # cmake/gen_build_id.cmake ile ayni bicim
    $dirty = [bool](& git -C $Root status --porcelain)
    return @{ Full = $full; Short = $short; Dirty = $dirty }
}

# ---- SelfTest ----
if ($SelfTest) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("ef-local-ci-selftest-$PID")
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    $fail = 0
    $total = 0
    try {
        $fakeExe = Join-Path $tmp 'EcuFps.exe'
        [IO.File]::WriteAllBytes($fakeExe, [byte[]](77, 90, 1, 2, 3, 4))
        $sha = Get-Sha256 $fakeExe
        $h12 = 'abcdef012345'
        $h40 = 'abcdef0123450000000000000000000000000000'
        $ok = "100% tests passed, 0 tests failed out of 10"
        function New-Log([hashtable]$Over, [string]$Name) {
            $d = @{ HEAD = $h12; FULLHEAD = $h40; dirty = 'no'; scope = 'Full'; result = 'PASS'; dist_exe_sha256 = $sha
                    'debug-build' = 0; 'debug-ctest' = 0; 'dist-build' = 0; 'dist-ctest' = 0 }
            foreach ($k in $Over.Keys) { $d[$k] = $Over[$k] }
            $l = New-Object System.Collections.Generic.List[string]
            $l.Add($LogFormat)
            foreach ($k in 'HEAD', 'FULLHEAD', 'dirty', 'scope', 'result', 'dist_exe_sha256') { if ($null -ne $d[$k]) { $l.Add("$k $($d[$k])") } }
            foreach ($s in $RequiredSteps) {
                if ($null -eq $d[$s]) { continue }
                $sum = if ($s -like '*-ctest') { " | $ok" } else { '' }
                $l.Add("step $s exit $($d[$s])$sum")
            }
            $l.Add('---- output ----')
            $l.Add('step debug-build exit 9 | sahte: govdede gelen satir yok sayilmali')
            $path = Join-Path $tmp $Name
            [IO.File]::WriteAllLines($path, $l, (New-Object Text.UTF8Encoding($true)))
            return $path
        }
        function Expect([string]$Label, [string[]]$Problems, [bool]$ShouldPass, [string]$Needle) {
            $script:total++
            $good = if ($ShouldPass) { $Problems.Count -eq 0 } else { ($Problems.Count -gt 0) -and (($Problems -join "`n") -match [regex]::Escape($Needle)) }
            if ($good) { Write-Host "  ok   $Label" } else { Write-Host "  FAIL $Label -> $($Problems -join ' / ')" -ForegroundColor Red; $script:fail++ }
        }
        Write-Host 'local_ci kapi kendi testi:'
        Expect 'gecerli gunluk kabul' (Test-CiEvidence (New-Log @{} 'ok.log') $h12 $h40 $fakeExe) $true ''
        Expect 'HEAD farkli reddedilir' (Test-CiEvidence (New-Log @{} 'a.log') '111111111111' $h40 $fakeExe) $false 'HEAD'
        Expect 'FULLHEAD farkli reddedilir' (Test-CiEvidence (New-Log @{} 'b.log') $h12 ('f' * 40) $fakeExe) $false 'FULLHEAD'
        Expect 'kirli agac reddedilir' (Test-CiEvidence (New-Log @{ dirty = 'yes' } 'c.log') $h12 $h40 $fakeExe) $false 'kirli'
        Expect 'Quiet kapsam reddedilir' (Test-CiEvidence (New-Log @{ scope = 'Quiet' } 'd.log') $h12 $h40 $fakeExe) $false 'Full'
        Expect 'kirmizi adim reddedilir' (Test-CiEvidence (New-Log @{ 'dist-ctest' = 1 } 'e.log') $h12 $h40 $fakeExe) $false 'dist-ctest'
        Expect 'eksik adim reddedilir' (Test-CiEvidence (New-Log @{ 'debug-ctest' = $null } 'f.log') $h12 $h40 $fakeExe) $false 'debug-ctest'
        Expect 'result FAIL reddedilir' (Test-CiEvidence (New-Log @{ result = 'FAIL' } 'g.log') $h12 $h40 $fakeExe) $false 'PASS'
        Expect 'sha satiri yoksa reddedilir' (Test-CiEvidence (New-Log @{ dist_exe_sha256 = $null } 'h.log') $h12 $h40 $fakeExe) $false 'dist_exe_sha256'
        $other = Join-Path $tmp 'other.exe'
        [IO.File]::WriteAllBytes($other, [byte[]](77, 90, 9, 9, 9))
        Expect 'SHA-256 farkli EXE reddedilir' (Test-CiEvidence (New-Log @{} 'i.log') $h12 $h40 $other) $false 'SHA-256'
        Expect 'EXE verilmezse reddedilir' (Test-CiEvidence (New-Log @{} 'j.log') $h12 $h40 '') $false '-Exe'
        Expect 'gunluk yoksa reddedilir' (Test-CiEvidence (Join-Path $tmp 'yok.log') $h12 $h40 $fakeExe) $false 'yok'
        $bad = Join-Path $tmp 'bad.log'
        [IO.File]::WriteAllText($bad, "baska bicim`nHEAD $h12`n")
        Expect 'taninmayan bicim reddedilir' (Test-CiEvidence $bad $h12 $h40 $fakeExe) $false 'bicim'
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($fail) { Write-Host "SELFTEST FAIL $fail/$total" -ForegroundColor Red; exit 1 }
    Write-Host "SELFTEST OK $total/$total" -ForegroundColor Green
    exit 0
}

# ---- Verify (yayin kapisi) ----
if ($Verify) {
    $h = Get-RepoHead
    if (-not $LogPath) { $LogPath = Join-Path $OutDir "local-ci-$($h.Short).log" }
    # Test-CiEvidence `return ,` ile dizi doner; @() ile sarmak bos listeyi tek (bos) madde yapardi.
    $problems = [string[]](Test-CiEvidence $LogPath $h.Short $h.Full $Exe)
    $problems = @($problems | Where-Object { $_ })
    if ($h.Dirty) { $problems += 'Calisma agaci kirli; kanit yalniz temiz commit icin gecerlidir.' }
    if ($problems.Count) {
        Write-Host "YAYIN KAPISI KAPALI (HEAD $($h.Short)):" -ForegroundColor Red
        $problems | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
        Write-Host '  Cozum: temiz agacta .\scripts\local_ci.ps1 (Full kapsam) calistirin.' -ForegroundColor Yellow
        exit 2
    }
    Write-Host "YAYIN KAPISI ACIK: HEAD $($h.Short), gunluk $LogPath, EXE SHA-256 eslesti." -ForegroundColor Green
    exit 0
}

# ---- Kosu ----
$h = Get-RepoHead
if (-not $WorkDir) { $WorkDir = Join-Path ([IO.Path]::GetTempPath()) "ef-local-ci-$($h.Short)" }
$logOut = Join-Path $OutDir "local-ci-$($h.Short).log"
$exeOut = Join-Path $OutDir "EcuFps-$($h.Short).exe"
$ctestSel = if ($Scope -eq 'Full') { @('-LE', 'uia_tour|real_gpu') } else { @('-L', 'unit|services') }

if ($DryRun) {
    Write-Host "DryRun (hicbir sey calistirilmaz):"
    Write-Host "  HEAD        $($h.Short) ($($h.Full)); calisma agaci $(if ($h.Dirty) { 'KIRLI (gercek kosu durur)' } else { 'temiz' })"
    Write-Host "  kapsam      $Scope; ctest secimi: $($ctestSel -join ' ')"
    Write-Host "  temiz klon  $WorkDir (kosu bitince silinir; -Keep ile kalir)"
    Write-Host "  adimlar     debug-build, debug-ctest, dist-build, dist-ctest (x64-debug, x64-dist)"
    Write-Host "  gunluk      $logOut"
    Write-Host "  dist EXE    $exeOut"
    Write-Host "  Her adim $Priority oncelik; ctest -j 4 (RUN_SERIAL testler tek basina); pencere testleri EF_TEST_BACKGROUND=1; UAC/ayar yazma yok."
    exit 0
}

if ($h.Dirty -and -not $AllowDirty) {
    Write-Host 'HATA: calisma agaci kirli; kanit bir commit icin uretilir. Once commit edin (veya -AllowDirty: gunluk "dirty yes" yazar ve kapiyi gecmez).' -ForegroundColor Red
    exit 2
}

function Enter-VsDevShell {
    # scripts\dev.ps1 ile ayni mantik (dev.ps1 bir komut betigi oldugu icin dot-source edilemez).
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64' -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere bulunamadi. Visual Studio Build Tools 2022 (C++ is yuku) gerekli.' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC x64 arac takimi bulunamadi.' }
    $devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
    $env:PATH = "$(Split-Path -Parent $vswhere);$env:PATH"
    $tmp = Join-Path $env:TEMP "ef-vsdev-ci-$PID.cmd"
    Set-Content -Path $tmp -Encoding ascii -Value "@call `"$devcmd`" -arch=x64 -host_arch=x64 -no_logo >nul 2>nul`r`n@if errorlevel 1 exit /b 1`r`n@set"
    try { $lines = & cmd.exe /d /c $tmp } finally { Remove-Item $tmp -ErrorAction SilentlyContinue }
    if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd.bat basarisiz oldu.' }
    foreach ($l in $lines) {
        $i = $l.IndexOf('=')
        if ($i -gt 0) { [Environment]::SetEnvironmentVariable($l.Substring(0, $i), $l.Substring($i + 1), 'Process') }
    }
    foreach ($tool in 'cl.exe', 'cmake.exe', 'ninja.exe', 'ctest.exe') {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool VS ortaminda bulunamadi." }
    }
}

$Body = New-Object System.Collections.Generic.List[string]
$StepRows = New-Object System.Collections.Generic.List[string]
$StepFail = $false
$started = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'

function Invoke-Step([string]$Name, [string]$Cmd, [string[]]$CmdArgs, [string]$Dir) {
    $line = "==== [$Name] $Cmd $($CmdArgs -join ' ')   (cwd $Dir)"
    Write-Host $line -ForegroundColor Cyan
    $Body.Add($line)
    $summary = ''
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'  # native stderr satirlari 'Stop' altinda kosuyu keserdi
    Push-Location $Dir
    try {
        & $Cmd @CmdArgs 2>&1 | ForEach-Object {
            $l = "$_"
            $Body.Add($l)
            if ($l -match 'tests passed|tests failed') { $summary = $l.Trim() }
            Write-Host $l
        }
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
        $ErrorActionPreference = $prev
    }
    $row = "step $Name exit $code"
    if ($summary) { $row += " | $summary" }
    $StepRows.Add($row)
    $Body.Add($row)
    if ($code -ne 0) { $script:StepFail = $true }
    return $code
}

# Yalniz bu betigin olusturdugu temp klonunu silmeye izin ver.
function Remove-WorkClone([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $leaf = Split-Path -Leaf $Path
    $parent = Split-Path -Parent $Path
    if ($leaf -notlike 'ef-local-ci-*' -or $parent -ne [IO.Path]::GetTempPath().TrimEnd('\')) {
        throw "Guvenlik: '$Path' bu betigin temp klonu gibi gorunmuyor; silinmedi (elle silin)."
    }
    Remove-Item -LiteralPath $Path -Recurse -Force
}

$finished = $false
try {
    [Diagnostics.Process]::GetCurrentProcess().PriorityClass = $Priority
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    foreach ($f in $logOut, $exeOut) { if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force } }

    Remove-WorkClone $WorkDir
    Write-Host "Temiz klon: $WorkDir" -ForegroundColor Cyan
    & git clone --quiet --no-checkout --no-hardlinks $Root $WorkDir
    if ($LASTEXITCODE) { throw 'git clone basarisiz.' }
    & git -C $WorkDir checkout --quiet --detach $h.Full
    if ($LASTEXITCODE) { throw "Klonda $($h.Full) alinamadi (HEAD hicbir dalda degil mi?)." }

    Enter-VsDevShell
    if (-not $env:EF_TEST_BACKGROUND) { $env:EF_TEST_BACKGROUND = '1' }
    $env:EF_UPDATE_CHECK = '0'

    foreach ($cfg in 'debug', 'dist') {
        $preset = "x64-$cfg"
        $c1 = Invoke-Step "$cfg-build" 'cmake' @('--preset', $preset) $WorkDir
        if ($c1 -eq 0) { $null = Invoke-Step "$cfg-build" 'cmake' @('--build', '--preset', $preset) $WorkDir }
        # Ayni adim adiyla iki satir yazilir; ilk kirmizi satir sonucu belirler (asagida birlestirilir).
        $null = Invoke-Step "$cfg-ctest" 'ctest' (@('--test-dir', (Join-Path $WorkDir "build\$preset")) + $ctestSel + @('-j', '4', '--output-on-failure')) $WorkDir
    }

    $distExe = Join-Path $WorkDir 'build\x64-dist\src\app\EcuFps.exe'
    $sha = ''
    $buildId = ''
    if (Test-Path -LiteralPath $distExe) {
        Copy-Item -LiteralPath $distExe -Destination $exeOut -Force
        $sha = Get-Sha256 $exeOut
        $buildId = [Diagnostics.FileVersionInfo]::GetVersionInfo($exeOut).SpecialBuild
        if ($buildId -ne $h.Short) { $StepFail = $true; $Body.Add("HATA: EXE derleme kimligi '$buildId' HEAD '$($h.Short)' ile eslesmiyor.") }
    } else {
        $StepFail = $true
        $Body.Add("HATA: dist EXE yok: $distExe")
    }
    $finished = $true
} catch {
    $StepFail = $true
    $Body.Add("HATA: $($_.Exception.Message)")
    Write-Host "HATA: $($_.Exception.Message)" -ForegroundColor Red
}

# Ayni adim adiyla birden cok satir varsa (build: configure + build) gunlukte her adim icin tek satir: kirmizi olan oncelikli.
$final = [ordered]@{}
foreach ($r in $StepRows) {
    if ($r -match '^step (\S+) exit (-?\d+)(?: \| (.*))?$') {
        $n = $Matches[1]
        if (-not $final.Contains($n) -or ($final[$n].Exit -eq 0 -and [int]$Matches[2] -ne 0)) { $final[$n] = @{ Exit = [int]$Matches[2]; Row = $r } }
    }
}
foreach ($s in $RequiredSteps) { if (-not $final.Contains($s)) { $StepFail = $true } }
$result = if ($StepFail -or -not $finished) { 'FAIL' } else { 'PASS' }

$head = New-Object System.Collections.Generic.List[string]
$head.Add($LogFormat)
$head.Add("HEAD $($h.Short)")
$head.Add("FULLHEAD $($h.Full)")
$head.Add("dirty $(if ($h.Dirty) { 'yes' } else { 'no' })")
$head.Add("scope $Scope")
$head.Add("started $started")
$head.Add("finished $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
$head.Add("clone $WorkDir")
$head.Add("ctest_select $($ctestSel -join ' ')")
$head.Add("env EF_TEST_BACKGROUND=$env:EF_TEST_BACKGROUND EF_UPDATE_CHECK=$env:EF_UPDATE_CHECK")
if ($sha) { $head.Add("dist_exe_sha256 $sha"); $head.Add("dist_exe $exeOut"); $head.Add("dist_exe_build_id $buildId") }
foreach ($s in $RequiredSteps) { if ($final.Contains($s)) { $head.Add($final[$s].Row) } }
$head.Add("result $result")
$head.Add('---- output ----')
[IO.File]::WriteAllLines($logOut, ($head + $Body), (New-Object Text.UTF8Encoding($true)))
if ($EvidenceCopy) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $EvidenceCopy) -Force | Out-Null
    Copy-Item -LiteralPath $logOut -Destination $EvidenceCopy -Force
}

if (-not $Keep) { try { Remove-WorkClone $WorkDir } catch { Write-Host "Uyari: klon silinemedi: $($_.Exception.Message)" -ForegroundColor Yellow } }
$color = if ($result -eq 'PASS') { 'Green' } else { 'Red' }
Write-Host "local-ci $result (HEAD $($h.Short), kapsam $Scope) -> $logOut" -ForegroundColor $color
if ($Scope -ne 'Full') { Write-Host 'Not: Quiet kapsam yayin kapisini gecmez; yayin icin Full gerekir.' -ForegroundColor Yellow }
if ($result -eq 'PASS') { exit 0 } else { exit 1 }
