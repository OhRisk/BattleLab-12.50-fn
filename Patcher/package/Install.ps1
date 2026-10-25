# Battle Lab 12.50 patcher: sets this Battle Lab folder up for the player's own Fortnite 12.50 (CL-13137020) installation.
# The game's files are only read (to check the build), never changed or copied. What it does:
#   1. finds and checks FortniteClient-Win64-Shipping.exe (the exact 12.50 build this project is made for)
#   2. writes GamePath.txt (where the game is), the player name and a unique local account id (LocalServer\LocalProfile.json)
#   3. adds a Windows Firewall rule that keeps that game executable off the internet; LAN and VPN addresses
#      (Hamachi, Radmin, Tailscale) stay reachable for multiplayer (asks for administrator once)
#   4. checks for Node.js (runs the local backend) and creates a desktop shortcut to Play.bat
param([string]$GamePath, [string]$PlayerName, [switch]$NoShortcut)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BattleLabSetup.ps1')
$expectedHash = '34A10AF4DDFF20B75D889E269A66644D570C3000F13548A5799D3A7EF690190C'  # 12.50 CL-13137020 shipping client

# Accepts the game folder itself, a folder inside it, or the folder above it; returns the folder containing FortniteGame.
function Resolve-GameDir([string]$dir) {
    if (!$dir) { return $null }
    $dir = $dir.Trim().Trim('"').TrimEnd('\')
    if (!(Test-Path -LiteralPath $dir -PathType Container)) { return $null }
    $probe = (Resolve-Path -LiteralPath $dir).Path
    for ($i = 0; $i -lt 4 -and $probe; $i++) {
        if (Test-Path -LiteralPath (Join-Path $probe $BL.ExeRelative)) { return $probe }
        $probe = Split-Path $probe -Parent
    }
    foreach ($child in @(Get-ChildItem -LiteralPath $dir -Directory -ErrorAction SilentlyContinue)) {
        if (Test-Path -LiteralPath (Join-Path $child.FullName $BL.ExeRelative)) { return $child.FullName }
    }
    return $null
}

function Pick-Folder {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select your Fortnite 12.50 folder (the one that contains FortniteGame and Engine)'
    $dialog.ShowNewFolderButton = $false
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { return $dialog.SelectedPath }
    return $null
}

try {
    Say ''
    Say '  Battle Lab for Fortnite 12.50 - setup' 'Cyan'
    Say '  Your game files are not modified. This only links this folder to your 12.50 install.' 'DarkGray'
    Say ''

    # Files from a downloaded zip carry the internet mark; unblock ours so Windows lets the launcher load the modules.
    Get-ChildItem -LiteralPath $BL.Root -Recurse -File | Unblock-File -ErrorAction SilentlyContinue

    # 1. Game folder
    $gameDir = Resolve-GameDir $GamePath
    if (!$gameDir -and (Test-Path -LiteralPath (Join-Path $BL.Root 'GamePath.txt'))) {
        $gameDir = Resolve-GameDir (Get-Content -LiteralPath (Join-Path $BL.Root 'GamePath.txt') -Encoding UTF8 -TotalCount 1)
        if ($gameDir) { Say ('Previously selected game folder: ' + $gameDir) }
    }
    while (!$gameDir) {
        Say 'Select your Fortnite 12.50 folder in the window that opened...' 'Yellow'
        $picked = Pick-Folder
        if (!$picked) { throw 'No folder selected. Run Install.bat again when you have the 12.50 build.' }
        $gameDir = Resolve-GameDir $picked
        if (!$gameDir) { Say ('No ' + $BL.ExeRelative + ' found in ' + $picked + '. Pick the folder that contains FortniteGame.') 'Red' }
    }
    $exe = Join-Path $gameDir $BL.ExeRelative
    Say ('Game folder: ' + $gameDir)

    # 2. Build check: the native modules use this exact build's code layout; any other version would crash.
    Say 'Checking the game version (this takes a few seconds)...'
    $hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    if ($hash -ne $expectedHash) {
        throw ('This is not Fortnite 12.50 CL-13137020 (the executable''s SHA256 is ' + $hash + '). ' +
               'Battle Lab here only works with that exact build.')
    }
    Say 'Fortnite 12.50 CL-13137020 confirmed.' 'Green'

    # 3. Player name (shown in game; change it any time with Rename.bat) and a unique account id for multiplayer
    $localProfile = Read-Json 'LocalServer\LocalProfile.json'
    if ($PlayerName -and !(Test-ValidName $PlayerName)) { throw 'Player names use 3-16 letters, digits, spaces, _ - or .' }
    if (!$PlayerName) { $PlayerName = Read-PlayerName $localProfile.displayName }
    $localProfile.displayName = $PlayerName
    if ($localProfile.accountId -notmatch '^[0-9a-f]{32}$') { $localProfile.accountId = [guid]::NewGuid().ToString('N') }
    Save-Json 'LocalServer\LocalProfile.json' $localProfile
    Write-Utf8 (Join-Path $BL.Root 'GamePath.txt') $gameDir
    Say ('Player name: ' + $PlayerName)

    # 4. Firewall: the 12.50 client must never reach Epic's servers (the local backend answers it on 127.0.0.1).
    if (Test-LanBlockRule $exe) { Say 'Firewall block for this game copy: already present.' 'Green' }
    else {
        Invoke-Elevated @(Get-BlockRuleCommands $exe) 'Adding a firewall rule that keeps this game copy off the internet.'
        Say 'Firewall block added.' 'Green'
    }

    # 5. Node.js
    $node = Get-NodeExe
    if ($node) {
        $version = (& $node --version) -replace '^v', ''
        if ([int]($version.Split('.')[0]) -lt 18) { Say ('Node.js ' + $version + ' is too old. Install the LTS version from https://nodejs.org') 'Red' }
        else { Say ('Node.js ' + $version + ' found.') 'Green' }
    } else {
        Say 'Node.js is not installed. Install the LTS version from https://nodejs.org before playing (it runs the local backend).' 'Red'
    }

    # 6. Desktop shortcut
    if (!$NoShortcut) {
        $shell = New-Object -ComObject WScript.Shell
        $link = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Battle Lab 12.50.lnk'))
        $link.TargetPath = Join-Path $BL.Root 'Play.bat'
        $link.WorkingDirectory = $BL.Root
        $link.IconLocation = $exe + ',0'
        $link.Description = 'Fortnite 12.50 Battle Lab (local)'
        $link.Save()
        Say 'Desktop shortcut "Battle Lab 12.50" created.' 'Green'
    }

    Say ''
    Say 'Setup complete. Start the game with Play.bat (or the desktop shortcut).' 'Cyan'
    Say 'Play with friends: Multiplayer.bat. Change your name: Rename.bat.' 'Cyan'
    Say 'In game: Battle Royale > Play. Open the console with ` and type help for the cheat list.' 'Cyan'
    exit 0
} catch {
    Say ('ERROR: ' + $_.Exception.Message) 'Red'
    exit 1
}
