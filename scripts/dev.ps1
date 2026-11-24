<#
.SYNOPSIS
  Ecu Fps geliştirici komutları. Visual Studio Build Tools ortamını kendisi yükler.

.EXAMPLE
  .\scripts\dev.ps1 build            # Release derle + test et
  .\scripts\dev.ps1 build -Config debug
  .\scripts\dev.ps1 configure
  .\scripts\dev.ps1 test
  .\scripts\dev.ps1 run
  .\scripts\dev.ps1 package -Config dist   # Dagitim EXE'si: dist\EcuFps.exe (ADR-033: tasinabilir ZIP yok)
  .\scripts\dev.ps1 clean
#>
param(
    [ValidateSet('configure', 'build', 'test', 'run', 'clean', 'env', 'package')]
    [string]$Command = 'build',
    [ValidateSet('debug', 'release', 'dist')]
    [string]$Config = 'release'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Preset = "x64-$Config"

function Enter-VsDevShell {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64' -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere bulunamadı. Visual Studio Build Tools 2022 (C++ iş yükü) gerekli.' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC x64 araç takımı bulunamadı. Build Tools içinde "C++ ile masaüstü geliştirme" iş yükünü kurun.' }
    $devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
    # VsDevCmd kendi icinde vswhere.exe'yi PATH'te arar.
    $env:PATH = "$(Split-Path -Parent $vswhere);$env:PATH"
    # PowerShell 5.1 tirnak sorunlarindan kacinmak icin gecici .cmd kullan.
    $tmp = Join-Path $env:TEMP "ef-vsdev-$PID.cmd"
    Set-Content -Path $tmp -Encoding ascii -Value "@call `"$devcmd`" -arch=x64 -host_arch=x64 -no_logo >nul 2>nul`r`n@if errorlevel 1 exit /b 1`r`n@set"
    try { $lines = & cmd.exe /d /c $tmp } finally { Remove-Item $tmp -ErrorAction SilentlyContinue }
    if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd.bat başarısız oldu.' }
    foreach ($l in $lines) {
        $i = $l.IndexOf('=')
        if ($i -gt 0) { [Environment]::SetEnvironmentVariable($l.Substring(0, $i), $l.Substring($i + 1), 'Process') }
    }
    foreach ($tool in 'cl.exe', 'cmake.exe', 'ninja.exe') {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool VS ortamında bulunamadı." }
    }
}

function Invoke-Step([string]$Exe, [string[]]$ArgList) {
    Write-Host "> $Exe $($ArgList -join ' ')" -ForegroundColor Cyan
    & $Exe @ArgList
    if ($LASTEXITCODE -ne 0) { throw "$Exe çıkış kodu $LASTEXITCODE" }
}

Push-Location $Root
try {
    Enter-VsDevShell
    switch ($Command) {
        'env' {
            Write-Host "cl:    $((Get-Command cl.exe).Source)"
            Write-Host "cmake: $((Get-Command cmake.exe).Source)"
            Write-Host "ninja: $((Get-Command ninja.exe).Source)"
            Write-Host "SDK:   $env:WindowsSDKVersion"
        }
        'configure' { Invoke-Step cmake @('--preset', $Preset) }
        'build' {
            Invoke-Step cmake @('--preset', $Preset)
            Invoke-Step cmake @('--build', '--preset', $Preset)
            Invoke-Step ctest @('--preset', $Preset)
        }
        'test' { Invoke-Step ctest @('--preset', $Preset) }
        'run' {
            $exe = Join-Path $Root "build\$Preset\src\app\EcuFps.exe"
            if (-not (Test-Path $exe)) { throw "Önce derleyin: $exe yok." }
            Start-Process $exe
        }
        'package' {
            # REL-02/REL-03 + ADR-033: yayin guven ilkesi (EF_RELEASE_TRUST=ON) + icerik/import denetimi. Tasinabilir
            # ZIP artik dagitilmaz; tek teslim dosyasi EXE'dir (publish_release.ps1 onu EcuFps-Setup-<v>.exe olarak
            # yayin klasorune kopyalar; ayni bayt). CPack ZIP hedefi CMake'te kalabilir ama teslim dosyasi degildir.
            if ($Config -ne 'dist') { throw 'Paketleme yalnız -Config dist ile yapılır (x64-dist preset).' }
            Invoke-Step cmake @('--preset', $Preset)
            Invoke-Step cmake @('--build', '--preset', $Preset)
            Invoke-Step ctest @('--preset', $Preset)
            $probe = Join-Path $Root "build\$Preset\tests\ef_package_probe.exe"
            $app = Join-Path $Root "build\$Preset\src\app\EcuFps.exe"
            Invoke-Step powershell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                (Join-Path $Root 'tests\windows\check_package.ps1'),
                '-Probe', $probe, '-App', $app,
                '-BuildDir', (Join-Path $Root "build\$Preset"), '-Config', 'Release')
            $distDir = Join-Path $Root 'dist'
            New-Item -ItemType Directory -Path $distDir -Force | Out-Null
            $exeOut = Join-Path $distDir 'EcuFps.exe'
            Copy-Item -LiteralPath $app -Destination $exeOut -Force
            Write-Host "Paket: $exeOut" -ForegroundColor Green
            $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $exeOut).Hash.ToLowerInvariant()
            $sumFile = Join-Path $distDir 'SHA256SUMS'
            Set-Content -LiteralPath $sumFile -Value ("{0}  {1}" -f $hash, 'EcuFps.exe') -Encoding ASCII
            Write-Host "EXE SHA-256: $hash" -ForegroundColor Green
            Write-Host "SHA256SUMS: $sumFile" -ForegroundColor Green
            Write-Host 'İmzalama yok (B-003): bu paket imzasız alpha olarak etiketlenmelidir.' -ForegroundColor Yellow
        }
        'clean' {
            $dir = Join-Path $Root "build\$Preset"
            if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
        }
    }
}
finally { Pop-Location }
