param(
    [Parameter(Mandatory)][string]$Probe,
    [Parameter(Mandatory)][string]$App,
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$Config = 'Release'
)
# REL-02/REL-03: dagitim icerigi ve import tablosu denetimi (ADR-033: tasinabilir ZIP teslim edilmez; kurulum
# dosyasi EcuFps-Setup-<v>.exe ayni EXE'nin birebir kopyasidir).
# 1) cmake --install ile paketlenen dosyalar tam olarak EcuFps.exe + LICENSE + NOTICE + README.
# 2) EXE yaninda loose DLL/config/asset yok; alt klasor yok.
# 3) Import tablosu yalniz Windows System32 DLL'leri; CRT/ucrt/uretici DLL'i yok.
$ErrorActionPreference = 'Stop'

function Fail([string]$Message) { throw "PACKAGE CHECK FAILED: $Message" }

$expected = @('LICENSE.txt', 'NOTICE.txt', 'README.txt', 'EcuFps.exe')
$expectedSorted = @($expected | Sort-Object)

$stage = Join-Path ([IO.Path]::GetTempPath()) ('ef-pkg-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    & cmake --install $BuildDir --prefix $stage --config $Config
    if ($LASTEXITCODE -ne 0) { Fail "cmake --install exit $LASTEXITCODE" }

    $items = @(Get-ChildItem -LiteralPath $stage -Recurse -Force)
    $dirs = @($items | Where-Object { $_.PSIsContainer })
    if ($dirs.Count -gt 0) {
        Fail ('unexpected directories: ' + (($dirs | ForEach-Object { $_.FullName }) -join ', '))
    }
    $files = @($items | Where-Object { -not $_.PSIsContainer })
    $names = @($files | ForEach-Object { $_.Name })
    $namesSorted = @($names | Sort-Object)
    if (($namesSorted -join ',') -ne ($expectedSorted -join ',')) {
        Fail ("install contents '" + ($namesSorted -join ', ') + "' != expected '" + ($expectedSorted -join ', ') + "'")
    }
    foreach ($f in $files) {
        if ($f.Length -le 0) { Fail "empty packaged file: $($f.Name)" }
        if ($f.Extension -in @('.dll', '.config', '.json', '.pdb', '.lib', '.obj', '.asset', '.db', '.ini')) {
            Fail "loose application file in package: $($f.Name)"
        }
    }
    Write-Host ('package files: ' + ($namesSorted -join ', '))

    function Get-Imports([string]$Exe) {
        if (-not (Test-Path -LiteralPath $Exe)) { Fail "EXE not found: $Exe" }
        $output = @(& $Probe $Exe)
        if ($LASTEXITCODE -ne 0) { Fail "import probe exit $LASTEXITCODE on $Exe" }
        $found = @()
        $machineSeen = $false
        foreach ($line in $output) {
            if ($line -eq 'machine=x64') { $machineSeen = $true; continue }
            if ($line -match '^import=(.+)$') { $found += $Matches[1]; continue }
            Fail "unexpected probe output: $line"
        }
        if (-not $machineSeen) { Fail "probe did not report x64 machine: $Exe" }
        if ($found.Count -eq 0) { Fail "no import table entries in $Exe" }
        return $found
    }

    $stagedExe = Join-Path $stage 'EcuFps.exe'
    $imports = @(Get-Imports $stagedExe)
    $appImports = @(Get-Imports $App)
    if (($imports -join ',') -ne ($appImports -join ',')) {
        Fail 'staged EXE imports differ from the built application target'
    }

    $system32 = Join-Path $env:WINDIR 'System32'
    foreach ($dll in $imports) {
        $badChars = @($dll.ToCharArray() | Where-Object {
            -not ([char]::IsLetterOrDigit($_) -or $_ -eq '.' -or $_ -eq '_' -or $_ -eq '-')
        })
        if ($badChars.Count -gt 0) { Fail "suspicious import name: '$dll'" }
        if ($dll -match '^(vcruntime|msvcp|ucrtbase|concrt|vccorlib|api-ms-win-crt)') {
            Fail "CRT DLL imported (static CRT expected): $dll"
        }
        if ($dll -match '^(nvapi64|amdadlx64|ControlLib|PresentMonAPI2)') {
            Fail "vendor/third-party DLL imported: $dll"
        }
        if (-not (Test-Path -LiteralPath (Join-Path $system32 $dll))) {
            Fail "import is not a Windows System32 DLL: $dll"
        }
    }
    Write-Host ('imports (System32 only): ' + ($imports -join ', '))

    Write-Host 'PACKAGE OK'
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
