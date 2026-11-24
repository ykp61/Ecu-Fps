param([Parameter(Mandatory)][string]$Probe, [Parameter(Mandatory)][string]$App, [Parameter(Mandatory)][string]$SignedSample,
      [Parameter(Mandatory)][string]$Policy, [ValidateSet('on', 'off')][string]$UnsignedAlpha = 'off')
# SEC-04: imza durumu siniflandirmasi ve release ikilisinde dev muafiyeti olmamasi.
# ADR-034: release + imzasiz yalniz EF_UNSIGNED_ALPHA ile (etiketli) kabul; kurcalanmis imza her derlemede red.
# Uygulama EXE'si calistirilmaz; yalniz probe (imza okuma + bu derlemenin karar fonksiyonu) calisir.
$ErrorActionPreference = 'Stop'
function Status($f) { $p = Start-Process $Probe -ArgumentList "`"$f`"" -PassThru -WindowStyle Hidden -Wait; $p.ExitCode }
function Decide($f) { $p = Start-Process $Probe -ArgumentList "--trust `"$f`"" -PassThru -WindowStyle Hidden -Wait; $p.ExitCode }

$s = Status $App
if ($s -ne 1) { throw "own unsigned build must be 'unsigned' (1), got $s" }
Write-Host "app build: unsigned"

$s = Status $SignedSample
if ($s -ne 0) { throw "signed sample '$SignedSample' must be valid (0), got $s" }
Write-Host "signed sample: valid"
$d = Decide $SignedSample
if ($d -ne 10) { throw "signed sample must be allowed as signed (10), got $d" }

# Imzasiz uygulama ikilisi icin karar: dev -> dev muafiyeti; release -> yalniz alpha bayragiyla.
$d = Decide $App
$want = if ($Policy -eq 'dev') { 12 } elseif ($UnsignedAlpha -eq 'on') { 11 } else { 13 }
if ($d -ne $want) { throw "unsigned app decision policy=$Policy alpha=$UnsignedAlpha must be $want, got $d" }
Write-Host "unsigned app decision: $d (policy=$Policy alpha=$UnsignedAlpha)"

# Kurcalanmis imzali dosya: ortadaki bir bayti degistir -> bozuk imza -> her derlemede red
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("ef-tamper-" + [guid]::NewGuid().ToString('N') + ".exe")
$bytes = [IO.File]::ReadAllBytes($SignedSample)
$i = [int]($bytes.Length / 3)
$bytes[$i] = $bytes[$i] -bxor 0xFF
[IO.File]::WriteAllBytes($tmp, $bytes)
try {
    $s = Status $tmp
    if ($s -ne 2) { throw "tampered file must be 'invalid' (2), got $s" }
    $d = Decide $tmp
    if ($d -ne 13) { throw "tampered file must be rejected (13), got $d" }
    Write-Host "tampered copy: invalid, rejected"
    # Imza blogu sifirlanmis (tablo var, icerik yok): WinVerifyTrust NOSIGNATURE der; yine de 'invalid' ve red
    # (alpha istisnasi yalniz HIC sertifika tablosu yoksa).
    $bytes = [IO.File]::ReadAllBytes($SignedSample)
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    $dd = if ([BitConverter]::ToUInt16($bytes, $pe + 24) -eq 0x20b) { $pe + 24 + 112 } else { $pe + 24 + 96 }
    $cpos = [BitConverter]::ToInt32($bytes, $dd + 32); $csize = [BitConverter]::ToInt32($bytes, $dd + 36)
    if ($csize -le 0) { throw "signed sample has no certificate table" }
    for ($k = $cpos; $k -lt $cpos + $csize; $k++) { $bytes[$k] = 0 }
    [IO.File]::WriteAllBytes($tmp, $bytes)
    $s = Status $tmp
    if ($s -ne 2) { throw "zeroed certificate table must be 'invalid' (2), got $s" }
    $d = Decide $tmp
    if ($d -ne 13) { throw "zeroed certificate table must be rejected (13), got $d" }
    Write-Host "zeroed certificate table: invalid, rejected"
} finally { Remove-Item $tmp -ErrorAction SilentlyContinue }

# Release (dist) ikilisinde dev muafiyet isareti olmamali; dev ikilisinde olmali.
$text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($App))
$has = $text.Contains('EF-DEV-UNSIGNED-BROKER-ALLOWED')
if ($Policy -eq 'release' -and $has) { throw "release build contains the dev bypass marker" }
if ($Policy -eq 'dev' -and -not $has) { throw "dev build should contain the dev marker" }
# Alpha isareti yalniz EF_UNSIGNED_ALPHA derlemede bulunur.
$alpha = $text.Contains('EF-UNSIGNED-ALPHA')
if ($UnsignedAlpha -eq 'on' -and -not $alpha) { throw "unsigned alpha build should contain the alpha marker" }
if ($UnsignedAlpha -eq 'off' -and $alpha) { throw "build without EF_UNSIGNED_ALPHA contains the alpha marker" }
Write-Host "policy=$Policy dev marker present=$has alpha marker present=$alpha"
Write-Host 'TRUST OK'
