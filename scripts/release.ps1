<#
.SYNOPSIS
  GitHub Releases hazirlik ve yayin hatti (varsayilan: yalniz yerel kontrol).

.DESCRIPTION
  Modlar:
    check    - dist ZIP, SHA256SUMS, release-manifest ve git durumunu dogrular; ag yok.
    prepare  - (gerekirse) dev.ps1 package calistirir; dist\SHA256SUMS + dist\RELEASE_BODY.md uretir.
    draft    - GitHub'da DRAFT release olusturur ve asset yukler. Varsayilan -DryRun'dir.
    publish  - Var olan draft'i yayinlar. -PublishPublic olmadan reddeder.

  Token: $env:GITHUB_TOKEN veya -Token (fine-grained PAT, Contents: Read and write).
  Token dosyaya yazilmaz ve ciktiya basilmez. Bu script kullanici acik talimati
  olmadan yayin yapmaz; draft adimi da imzasiz alpha icin -AllowUnsigned ister.

.EXAMPLE
  .\scripts\release.ps1 -Mode check

.EXAMPLE
  .\scripts\release.ps1 -Mode prepare

.EXAMPLE
  .\scripts\release.ps1 -Mode draft -AllowUnsigned -DryRun
#>
[CmdletBinding()]
param(
    [ValidateSet('check', 'prepare', 'draft', 'publish')]
    [string]$Mode = 'check',
    [string]$Version = '',  # bos: CMakeLists.txt project(VERSION)
    [string]$Repo = '',
    [string]$Token = '',
    [int]$ReleaseId = 0,
    [switch]$SkipBuild,
    [switch]$AllowUnsigned,
    [switch]$DryRun,
    [switch]$PublishPublic
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$DistDir = Join-Path $Root 'dist'
$ManifestPath = Join-Path $Root 'artifacts\release-manifest.json'
$NotesPath = Join-Path $Root 'docs\RELEASE_NOTES.md'
function Get-CMakeVersion([string]$RootDir) {
    $text = Get-Content -LiteralPath (Join-Path $RootDir 'CMakeLists.txt') -Raw
    $m = [regex]::Match($text, '(?m)^project\(EcuFps\s*\r?\n\s*VERSION\s+(\d+\.\d+\.\d+)')
    if (-not $m.Success) { throw 'CMakeLists.txt icinde project VERSION bulunamadi.' }
    return $m.Groups[1].Value
}
if (-not $Version) { $Version = Get-CMakeVersion $Root }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { Write-Host "HATA: Surum bicimi major.minor.patch olmali: $Version" -ForegroundColor Red; exit 2 }
$ZipName = "EcuFps-$Version-win64.zip"
$Tag = "v$Version"

function Fail([string]$Message) {
    Write-Host "HATA: $Message" -ForegroundColor Red
    exit 2
}

function Get-FileSha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-RepoSlug {
    if ($Repo) {
        if ($Repo -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { Fail "Repo bicimi owner/repo olmali: $Repo" }
        return $Repo
    }
    $url = (& git -C $Root remote get-url origin 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $url) { Fail 'origin remote bulunamadi; -Repo owner/repo verin.' }
    if ($url -match 'github\.com[:/](?<slug>[^/]+/[^/.]+?)(?:\.git)?$') { return $Matches['slug'] }
    Fail "origin GitHub URL'i degil: $url"
}

function Get-DistZip {
    $zip = Join-Path $DistDir $ZipName
    if (Test-Path -LiteralPath $zip) { return $zip }
    # Baska surumun ZIP'ine sessizce dusulmez: etiket/ad ile icerik ayni surum olmali.
    Fail "ZIP yok: $zip (surum $Version; once: .\scripts\dev.ps1 package -Config dist)"
}

function Write-ReleaseAssets {
    param([string]$ZipPath)
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
    $hash = Get-FileSha256 $ZipPath
    $zipLeaf = Split-Path -Leaf $ZipPath
    $sumLine = "$hash  $zipLeaf"
    $sumPath = Join-Path $DistDir 'SHA256SUMS'
    Set-Content -LiteralPath $sumPath -Value $sumLine -Encoding ASCII

    $notes = [System.IO.File]::ReadAllText($NotesPath, [System.Text.Encoding]::UTF8)
    if (-not $notes) { Fail "Release notlari okunamadi: $NotesPath" }
    $banner = @'
> **Imzasiz alpha.** Authenticode imzasi yoktur (B-003); SmartScreen uyarabilir.
> Indirdikten sonra asagidaki SHA-256 degerini dogrulayin. Bu bir v1.0 surumu degildir.

## Dosyalar

| Dosya | SHA-256 |
|---|---|
'@
    $body = $banner + "`n| ``$zipLeaf`` | ``$hash`` |`n`n---`n`n" + $notes
    $bodyPath = Join-Path $DistDir 'RELEASE_BODY.md'
    Set-Content -LiteralPath $bodyPath -Value $body -Encoding UTF8
    Write-Host "SHA256SUMS: $sumPath" -ForegroundColor Green
    Write-Host "Release govdesi: $bodyPath" -ForegroundColor Green
    Write-Host "ZIP SHA-256: $hash" -ForegroundColor Green
}

function Assert-Sums([string]$ZipPath) {
    $sumPath = Join-Path $DistDir 'SHA256SUMS'
    if (-not (Test-Path -LiteralPath $sumPath)) { Fail "SHA256SUMS yok: $sumPath (once: -Mode prepare)" }
    $lines = Get-Content -LiteralPath $sumPath | Where-Object { $_.Trim() }
    if (-not $lines) { Fail "SHA256SUMS bos: $sumPath" }
    foreach ($line in $lines) {
        if ($line -notmatch '^(?<hash>[0-9a-fA-F]{64})\s+\*?(?<name>.+)$') { Fail "SHA256SUMS bicimi gecersiz: $line" }
        $name = $Matches['name'].Trim()
        $expected = $Matches['hash'].ToLowerInvariant()
        $file = Join-Path $DistDir $name
        if (-not (Test-Path -LiteralPath $file)) { Fail "SHA256SUMS dosyasi yok: $file" }
        $actual = Get-FileSha256 $file
        if ($actual -ne $expected) { Fail "SHA-256 uyusmuyor: $name (beklenen $expected, bulunan $actual)" }
        Write-Host "SHA-256 OK: $name" -ForegroundColor Green
    }
    $zipHash = Get-FileSha256 $ZipPath
    $zipLine = $lines | Where-Object { $_ -match [regex]::Escape((Split-Path -Leaf $ZipPath)) }
    if (-not $zipLine) { Fail "SHA256SUMS ZIP satirini icermiyor: $(Split-Path -Leaf $ZipPath)" }
    if ($zipLine -notmatch [regex]::Escape($zipHash)) { Fail 'SHA256SUMS ZIP hash degeri guncel degil.' }
}

function Assert-Manifest([string]$ZipPath) {
    if (-not (Test-Path -LiteralPath $ManifestPath)) { Fail "Manifest yok: $ManifestPath" }
    $m = [System.IO.File]::ReadAllText($ManifestPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
    if (-not $m.package -or -not $m.package.sha256 -or -not $m.package.bytes) {
        Fail "release-manifest.json paket blogu eksik (package.sha256/package.bytes)."
    }
    $zipHash = Get-FileSha256 $ZipPath
    if ($m.package.sha256.ToLowerInvariant() -ne $zipHash) {
        Fail "release-manifest.json ZIP hash'i guncel degil (manifest $($m.package.sha256), dosya $zipHash). Belgeleri tazeleyin."
    }
    if ([int64]$m.package.bytes -ne (Get-Item -LiteralPath $ZipPath).Length) {
        Fail "release-manifest.json ZIP boyutu guncel degil: $($m.package.bytes)"
    }
    Write-Host "Manifest OK: $($m.package.sha256)" -ForegroundColor Green
}

switch ($Mode) {
    'check' {
        $zip = Get-DistZip
        Write-Host "Proje: $Root"
        $slug = Get-RepoSlug
        Write-Host "Repo: $slug  Tag: $Tag  (yayin yok)"
        Write-Host "ZIP: $zip"
        Assert-Sums $zip
        Assert-Manifest $zip
        $bodyPath = Join-Path $DistDir 'RELEASE_BODY.md'
        if (-not (Test-Path -LiteralPath $bodyPath)) { Write-Host 'UYARI: RELEASE_BODY.md yok; -Mode prepare ile uretin.' -ForegroundColor Yellow }
        $dirty = (& git -C $Root status --porcelain)
        if ($dirty) { Write-Host "UYARI: calisma agaci kirli ($($dirty.Count) dosya); yayin onceki temiz commit'ten yapilmalidir." -ForegroundColor Yellow }
        Write-Host 'Kontrol tamam. Ag islemi yapilmadi.' -ForegroundColor Green
    }
    'prepare' {
        if (-not $SkipBuild) {
            Push-Location $Root
            try {
                & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'dev.ps1') package -Config dist
                if ($LASTEXITCODE) { Fail "Paketleme basarisiz (exit $LASTEXITCODE)" }
            } finally { Pop-Location }
        }
        $zip = Get-DistZip
        Write-ReleaseAssets $zip
        Assert-Sums $zip
        Write-Host 'Hazirlik tamam. Yayin yapilmadi.' -ForegroundColor Green
    }
    'draft' {
        if (-not $AllowUnsigned) { Fail 'Paket imzasiz; bilerek imzasiz alpha yayinlamak icin -AllowUnsigned gerekir.' }
        $zip = Get-DistZip
        Assert-Sums $zip
        Assert-Manifest $zip
        Write-ReleaseAssets $zip
        $slug = Get-RepoSlug
        $bodyPath = Join-Path $DistDir 'RELEASE_BODY.md'
        $body = [System.IO.File]::ReadAllText($bodyPath, [System.Text.Encoding]::UTF8)
        $head = (& git -C $Root rev-parse HEAD).Trim()
        $payload = [ordered]@{
            tag_name         = $Tag
            target_commitish = $head
            name             = "Ecu Fps $Version - imzasiz alpha"
            body             = $body
            draft            = $true
            prerelease       = $true
        } | ConvertTo-Json -Depth 4

        $assetPaths = @($zip, (Join-Path $DistDir 'SHA256SUMS'))
        if ($DryRun) {
            Write-Host "DRY-RUN: POST https://api.github.com/repos/$slug/releases" -ForegroundColor Yellow
            Write-Host $payload
            foreach ($a in $assetPaths) { Write-Host "DRY-RUN: upload asset $(Split-Path -Leaf $a) -> uploads.github.com" -ForegroundColor Yellow }
            Write-Host 'Ag islemi yapilmadi. Gercek draft icin -DryRun kaldirin ve GITHUB_TOKEN tanimlayin.' -ForegroundColor Yellow
            return
        }
        if (-not $Token) { $Token = $env:GITHUB_TOKEN }
        if (-not $Token) { Fail 'GITHUB_TOKEN yok. Fine-grained PAT (Contents: Read and write) gerekir.' }
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $headers = @{
            Authorization          = "Bearer $Token"
            Accept                 = 'application/vnd.github+json'
            'X-GitHub-Api-Version' = '2022-11-28'
        }
        Write-Host "Draft release olusturuluyor: $slug $Tag" -ForegroundColor Yellow
        $release = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$slug/releases" -Headers $headers -Body $payload -ContentType 'application/json'
        Write-Host "Draft: $($release.html_url)" -ForegroundColor Green
        $uploadBase = ($release.upload_url -replace '\{.*$', '')
        foreach ($a in $assetPaths) {
            $leaf = Split-Path -Leaf $a
            $ctype = if ($leaf -like '*.zip') { 'application/zip' } else { 'text/plain' }
            $uri = "$uploadBase?name=$([uri]::EscapeDataString($leaf))"
            Write-Host "Yukleniyor: $leaf" -ForegroundColor Yellow
            Invoke-RestMethod -Method Post -Uri $uri -Headers $headers -InFile $a -ContentType $ctype | Out-Null
        }
        Write-Host 'Draft hazir. Yayinlamak icin -Mode publish -ReleaseId <id> -PublishPublic.' -ForegroundColor Green
        Write-Host "ReleaseId: $($release.id)"
    }
    'publish' {
        if (-not $PublishPublic) { Fail 'Halka acik yayin icin -PublishPublic gerekir. Kullanici acik talimati olmadan yayinlamayin.' }
        if (-not $AllowUnsigned) { Fail 'Paket imzasiz; -AllowUnsigned olmadan yayinlanmaz.' }
        if ($ReleaseId -le 0) { Fail '-ReleaseId gerekli (draft olusturma ciktisindaki id).' }
        $slug = Get-RepoSlug
        if (-not $Token) { $Token = $env:GITHUB_TOKEN }
        if (-not $Token -and -not $DryRun) { Fail 'GITHUB_TOKEN yok.' }
        $payload = @{ draft = $false; prerelease = $true } | ConvertTo-Json
        if ($DryRun) {
            Write-Host "DRY-RUN: PATCH https://api.github.com/repos/$slug/releases/$ReleaseId" -ForegroundColor Yellow
            Write-Host $payload
            return
        }
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $headers = @{
            Authorization          = "Bearer $Token"
            Accept                 = 'application/vnd.github+json'
            'X-GitHub-Api-Version' = '2022-11-28'
        }
        $release = Invoke-RestMethod -Method Patch -Uri "https://api.github.com/repos/$slug/releases/$ReleaseId" -Headers $headers -Body $payload -ContentType 'application/json'
        Write-Host "Yayinlandi: $($release.html_url)" -ForegroundColor Green
        Write-Host 'Not: prerelease=true; releases/latest bu surumu dondurmez (R22 tasarimi).' -ForegroundColor Yellow
    }
}
