# Builds the Battle Lab 12.50 release: compiles the native modules (Native\build.bat), then assembles
# Patcher\dist\BattleLab-12.50\ and BattleLab-12.50.zip from an explicit file list.
# The release contains only this project's own files: no game files, no local profiles, settings, logs or dev flags.
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$package = Join-Path $PSScriptRoot 'package'
$distRoot = Join-Path $PSScriptRoot 'dist'
$name = 'BattleLab-12.50'
$out = Join-Path $distRoot $name
$zip = Join-Path $distRoot ($name + '.zip')

function Copy-Into([string]$source, [string]$relative) {
    $target = Join-Path $out $relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
}

try {
    if (!$SkipBuild) {
        Write-Host '[Patcher] Building native modules...' -ForegroundColor Cyan
        & cmd.exe /c ('"' + (Join-Path $project 'Native\build.bat') + '"')
        if ($LASTEXITCODE -ne 0) { throw 'Native build failed (if it says LNK1104, close the game and the local host first).' }
    }

    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    New-Item -ItemType Directory -Path $out -Force | Out-Null

    # Patcher and player-facing files
    foreach ($f in 'Install.bat', 'Install.ps1', 'Uninstall.bat', 'Uninstall.ps1', 'Multiplayer.bat', 'Multiplayer.ps1', 'Rename.bat', 'Rename.ps1', 'BattleLabSetup.ps1', 'Play.bat', 'README.txt') { Copy-Into (Join-Path $package $f) $f }
    Copy-Into (Join-Path $project 'LaunchBattleLab.ps1') 'LaunchBattleLab.ps1'
    Copy-Into (Join-Path $project 'COMMANDS.md') 'COMMANDS.md'
    Copy-Into (Join-Path $project 'Native\ThirdParty\Project-Reboot-3.0-master\LICENSE') 'Licenses\Project-Reboot-3.0.txt'

    # Native modules (same layout as the project: the launcher and the logs locate everything from Native\bin)
    foreach ($f in 'BattleLabLauncher.exe', 'BattleLabClient.dll', 'BattleLabHost.dll') { Copy-Into (Join-Path $project "Native\bin\$f") "Native\bin\$f" }

    # Local backend: code, hotfixes, cosmetic list. Profiles and cloud settings are created fresh on the player's PC.
    $server = Join-Path $project 'LocalServer'
    Copy-Into (Join-Path $server 'server.js') 'LocalServer\server.js'
    Copy-Into (Join-Path $server 'LocalServerConfig.json') 'LocalServer\LocalServerConfig.json'
    Copy-Into (Join-Path $server 'data\cosmetics.json') 'LocalServer\data\cosmetics.json'
    Get-ChildItem -LiteralPath (Join-Path $server 'lib') -Filter *.js | ForEach-Object { Copy-Into $_.FullName ('LocalServer\lib\' + $_.Name) }
    Get-ChildItem -LiteralPath (Join-Path $server 'hotfixes') -Filter *.ini | ForEach-Object { Copy-Into $_.FullName ('LocalServer\hotfixes\' + $_.Name) }
    $localProfile = Get-Content -LiteralPath (Join-Path $server 'LocalProfile.json') -Raw | ConvertFrom-Json
    $localProfile.displayName = 'Player'
    $localProfile.accountId = 'set-by-install'  # Install.bat generates a unique one per player
    [IO.File]::WriteAllText((Join-Path $out 'LocalServer\LocalProfile.json'), ($localProfile | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding $false))
    $settings = Get-Content -LiteralPath (Join-Path $server 'LocalServerConfig.json') -Raw | ConvertFrom-Json
    foreach ($p in @(@('mode', 'solo'), @('joinAddress', ''), @('lanPort', 3553), @('cheats', 'everyone'))) { $settings | Add-Member -NotePropertyName $p[0] -NotePropertyValue $p[1] -Force }
    [IO.File]::WriteAllText((Join-Path $out 'LocalServer\LocalServerConfig.json'), ($settings | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding $false))

    # Guard: nothing from the game, no dev flags, nothing personal
    $bad = @(Get-ChildItem -LiteralPath $out -Recurse -File | Where-Object { $_.Extension -in '.pak', '.sig', '.ucas', '.utoc', '.flag', '.log', '.sav' -or $_.Name -like 'Fortnite*.exe' })
    if ($bad.Count) { throw ('Unexpected files in the release: ' + ($bad.FullName -join ', ')) }
    $personal = @(Get-ChildItem -LiteralPath $out -Recurse -File -Include *.json, *.ps1, *.bat, *.txt, *.md, *.ini, *.js | Select-String -Pattern 'OhRisk' -SimpleMatch)
    if ($personal.Count) { throw ('Personal name found in: ' + (($personal | ForEach-Object { $_.Path }) -join ', ')) }

    Compress-Archive -Path $out -DestinationPath $zip -CompressionLevel Optimal
    $size = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
    Write-Host ''
    Write-Host ('[Patcher] Release folder: ' + $out) -ForegroundColor Green
    Write-Host ('[Patcher] Release zip:    ' + $zip + " ($size MB)") -ForegroundColor Green
    Write-Host ('[Patcher] SHA256:         ' + (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash)
    exit 0
} catch {
    Write-Host ('[Patcher] ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
