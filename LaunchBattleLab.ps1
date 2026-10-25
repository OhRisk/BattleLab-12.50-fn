param([Parameter(ValueFromRemainingArguments=$true)][string[]]$ClientArguments)
$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
# The 12.50 installation: GamePath.txt (written by Install.bat) or, without it, the development working copy.
$gameDir = Join-Path $project 'Fortnite-12.50-BattleLab'
$gamePathFile = Join-Path $project 'GamePath.txt'
if (Test-Path -LiteralPath $gamePathFile) { $gameDir = (Get-Content -LiteralPath $gamePathFile -Encoding UTF8 -TotalCount 1).Trim().TrimEnd('\') }
$clientPath = Join-Path $gameDir 'FortniteGame\Binaries\Win64\FortniteClient-Win64-Shipping.exe'
$launcher = Join-Path $project 'Native\bin\BattleLabLauncher.exe'
$serverScript = Join-Path $project 'LocalServer\server.js'
$logRoot = Join-Path $project 'Logs'
$backendVersion = 4
try {
    Set-Location -LiteralPath $project
    if (!(Test-Path -LiteralPath $launcher)) { throw 'Launcher is missing. Run Native\build.bat first.' }
    if (!(Test-Path -LiteralPath $clientPath)) { throw ('Fortnite 12.50 was not found at ' + $gameDir + '. Run Install.bat to choose the game folder.') }
    $localProfile = Get-Content -LiteralPath (Join-Path $project 'LocalServer\LocalProfile.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $base = 'http://127.0.0.1:' + $localProfile.httpPort
    # Outbound block for this game copy: everything ('Any'), or everything except LAN/VPN ranges for multiplayer
    # (Multiplayer.bat; that rule's list contains 11.0.0.0-24.255.255.255).
    $rules = @(Get-NetFirewallRule -DisplayName 'BattleLab - block 12.50 client internet' -ErrorAction SilentlyContinue | Where-Object { $_.Enabled -eq 'True' -and $_.Direction -eq 'Outbound' -and $_.Action -eq 'Block' })
    $protected = $false
    foreach ($rule in $rules) {
        $program = ($rule | Get-NetFirewallApplicationFilter).Program
        $remote = @(($rule | Get-NetFirewallAddressFilter).RemoteAddress)
        if ($program -ieq $clientPath -and ($remote -contains 'Any' -or $remote -contains '11.0.0.0-24.255.255.255')) { $protected = $true }
    }
    if (!$protected) { throw 'The outbound firewall block for this game copy is missing or disabled (run Install.bat). Launch cancelled.' }
    # The local Battle Lab host is the same executable; only a player client counts as "already running".
    $hostPid = 0
    try { $hostPid = [int](Invoke-RestMethod ($base + '/battlelab/host/status') -TimeoutSec 2).pid } catch {}
    $running = @(Get-Process -Name FortniteClient-Win64-Shipping -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $clientPath -and $_.Id -ne $hostPid })
    if ($localProfile.httpPort -ne 3551) { $running = @() }  # a second player on this PC: the first one's client is expected
    if ($running.Count) { Write-Host '[BattleLab] The local client is already running. Close it before starting a new session.'; exit 0 }
    New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
    $archive = Join-Path $logRoot ('Launch-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $archive -Force | Out-Null
    foreach ($old in @((Join-Path $logRoot 'BattleLabClient.log'), (Join-Path $env:LOCALAPPDATA 'FortniteGame\Saved\Logs\FortniteGame.log'))) {
        if (Test-Path -LiteralPath $old) { Copy-Item -LiteralPath $old -Destination $archive }
    }
    $health = $null
    try { $health = Invoke-RestMethod ($base + '/battlelab/health') -TimeoutSec 2 } catch {}
    # A backend from an older version, or started before a rename / multiplayer change, is restarted (with its host).
    $settings = (Get-Item -LiteralPath (Join-Path $project 'LocalServer\LocalServerConfig.json')).LastWriteTimeUtc
    if ($health -and $health.service -eq 'BattleLabLocal' -and ($health.version -ne $backendVersion -or $health.account -ne $localProfile.displayName -or
        ($health.started -and [datetime]::Parse($health.started).ToUniversalTime() -lt $settings))) {
        Write-Host '[BattleLab] Restarting the local backend with the current name and settings...'
        foreach ($id in @($hostPid, [int]$health.pid)) {
            if ($id) { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue }
        }
        Start-Sleep -Milliseconds 500
        $health = $null
    }
    if (!$health -or $health.service -ne 'BattleLabLocal') {
        $listener = @(Get-NetTCPConnection -LocalPort $localProfile.httpPort -State Listen -ErrorAction SilentlyContinue)
        if ($listener.Count) { throw ('Port ' + $localProfile.httpPort + ' is occupied by a different service. Close it and retry.') }
        $node = (Get-Command node.exe -ErrorAction SilentlyContinue).Source
        if (!$node) { $node = Join-Path $env:ProgramFiles 'nodejs\node.exe' }
        if (!(Test-Path -LiteralPath $node)) { throw 'Node.js was not found. Install Node.js, then run this launcher again.' }
        $backend = Start-Process -FilePath $node -ArgumentList ('"' + $serverScript + '"') -WorkingDirectory $project -WindowStyle Hidden -RedirectStandardOutput (Join-Path $archive 'backend-stdout.log') -RedirectStandardError (Join-Path $archive 'backend-stderr.log') -PassThru
        for ($attempt=0; $attempt -lt 20; $attempt++) {
            try { $health = Invoke-RestMethod ($base + '/battlelab/health') -TimeoutSec 1 } catch {}
            if ($health -and $health.service -eq 'BattleLabLocal') { break }
            if ($backend.HasExited) { throw ('Backend exited. See ' + (Join-Path $archive 'backend-stderr.log')) }
            Start-Sleep -Milliseconds 250
        }
    }
    if (!$health -or $health.service -ne 'BattleLabLocal') { throw 'Local backend did not become ready.' }
    Write-Host ('[BattleLab] Local backend ready. Player: ' + $health.account + ' (' + $health.mode + ')')
    if ($health.mode -eq 'host' -and $health.lanAddresses) { Write-Host ('[BattleLab] Friends join you at: ' + ($health.lanAddresses -join ', ')) -ForegroundColor Cyan }
    if ($health.mode -eq 'join') { Write-Host ('[BattleLab] Joining ' + $health.joinAddress + ': press Play in Battle Lab once your friend''s game is up.') -ForegroundColor Cyan }
    & $launcher @ClientArguments
    if ($LASTEXITCODE -ne 0) { throw ('Native launcher failed with exit code ' + $LASTEXITCODE) }
    Write-Host '[BattleLab] Client started. Select Battle Royale, then Battle Lab.'
    Write-Host '[BattleLab] Diagnostic logs are in Logs and LocalServer\logs.'
    exit 0
} catch {
    Write-Host ('[BattleLab] ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
