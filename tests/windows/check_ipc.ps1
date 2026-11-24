param([Parameter(Mandatory)][string]$Probe)
# SEC-02: gercek iki surecli pipe IPC senaryolari.
$ErrorActionPreference = 'Stop'

function New-Nonce { -join ((1..32) | % { '0123456789abcdef'[(Get-Random -Maximum 16)] }) }

function Pair([string]$mode, [int]$expectServer, [int]$expectClient, [switch]$WrongPid) {
    $nonce = New-Nonce
    $dir = Join-Path ([IO.Path]::GetTempPath()) ("ef-ipc-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory $dir | Out-Null
    try {
        $client = Start-Process $Probe -ArgumentList 'client', $nonce, "`"$dir`"", $mode -PassThru -WindowStyle Hidden
        $expected = if ($WrongPid) { $client.Id + 4 } else { $client.Id }
        $server = Start-Process $Probe -ArgumentList 'server', $nonce, $expected, "`"$dir`"" -PassThru -WindowStyle Hidden
        if (-not $server.WaitForExit(30000)) { $server.Kill(); throw "$mode server timeout" }
        if (-not $client.WaitForExit(30000)) { $client.Kill(); throw "$mode client timeout" }
        if ($server.ExitCode -ne $expectServer) { throw "$mode server exit $($server.ExitCode), expected $expectServer" }
        if ($expectClient -ge 0 -and $client.ExitCode -ne $expectClient) { throw "$mode client exit $($client.ExitCode), expected $expectClient" }
        Write-Host ("{0,-10} server={1} client={2}" -f $mode, $server.ExitCode, $client.ExitCode)
    } finally { Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue }
}

Pair good 0 0
Pair replay 30 0
Pair badnonce 30 0
Pair oversize 30 0
Pair malformed 30 0
Pair badop 30 0
Pair good 30 -1 -WrongPid     # pipe adini bilmek yetmez: beklenmeyen PID reddedilir

# Ad gaspi: baska surec ayni pipe'i once acarsa gercek sunucu olusturamaz.
$nonce = New-Nonce
$dir = Join-Path ([IO.Path]::GetTempPath()) ("ef-ipc-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $dir | Out-Null
try {
    $sq = Start-Process $Probe -ArgumentList 'squat', $nonce, "`"$dir`"" -PassThru -WindowStyle Hidden
    for ($i = 0; $i -lt 200 -and -not (Test-Path "$dir\squat.ready"); $i++) { Start-Sleep -Milliseconds 25 }
    $srv = Start-Process $Probe -ArgumentList 'server', $nonce, 1, "`"$dir`"" -PassThru -WindowStyle Hidden -Wait
    if ($srv.ExitCode -notin 21, 22) { throw "squatted pipe: server exit $($srv.ExitCode), expected create failure" }
    Write-Host "squat      server=$($srv.ExitCode) (create refused)"
    $sq.Kill()
} finally { Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue }
Write-Host 'IPC OK'
