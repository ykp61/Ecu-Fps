param([Parameter(Mandatory)][string]$Tool)
# FOUND-05: destek paketi gercek Windows ZIP araciyla acilabiliyor mu ve kimlik sizmiyor mu.
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ("ef-bundle-" + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory "$root\logs", "$root\crash", "$root\out" | Out-Null
    $line = "2026-11-04T00:00:00.000Z INFO test: user $env:USERNAME on $env:COMPUTERNAME path $env:USERPROFILE\AppData"
    Set-Content -Path "$root\logs\app.log" -Value $line -Encoding UTF8
    & $Tool "$root\logs" "$root\crash" "$root\out\bundle.zip"
    if ($LASTEXITCODE -ne 0) { throw "bundle araci basarisiz: $LASTEXITCODE" }
    Expand-Archive -Path "$root\out\bundle.zip" -DestinationPath "$root\out\x"
    $files = Get-ChildItem -Recurse -File "$root\out\x"
    Write-Host ("icerik: " + (($files | % { $_.FullName.Substring("$root\out\x".Length + 1) }) -join ', '))
    $all = ($files | % { Get-Content $_.FullName -Raw }) -join "`n"
    foreach ($s in @($env:USERNAME, $env:COMPUTERNAME)) {
        if ($all -match [regex]::Escape($s)) { throw "redaksiyon basarisiz: kimlik sizdi" }
    }
    if (-not (Test-Path "$root\out\x\manifest.json")) { throw 'manifest yok' }
    Write-Host 'OK'
}
finally { Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue }
