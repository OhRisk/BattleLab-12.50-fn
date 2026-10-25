# Battle Lab 12.50: choose how you play. Solo (only you), Host (friends join your Battle Lab over LAN, Hamachi,
# Radmin VPN, Tailscale or ZeroTier) or Join (play in a friend's Battle Lab). Saved in LocalServer\LocalServerConfig.json;
# Play.bat picks the change up the next time it starts.
param([ValidateSet('', 'solo', 'host', 'join')][string]$Mode = '', [string]$Address, [ValidateSet('', 'everyone', 'host')][string]$Cheats = '')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BattleLabSetup.ps1')
try {
    $exe = Get-GameExe
    if (!$exe -or !(Test-Path -LiteralPath $exe)) { throw 'Run Install.bat first.' }
    $settings = Read-Json 'LocalServer\LocalServerConfig.json'
    $current = if ($settings.PSObject.Properties['mode']) { $settings.mode } else { 'solo' }
    $lanPort = if ($settings.PSObject.Properties['lanPort']) { [int]$settings.lanPort } else { 3553 }

    if (!$Mode) {
        Say ''
        Say '  Battle Lab multiplayer' 'Cyan'
        Say ('  Currently: ' + $current + $(if ($current -eq 'join') { ' (' + $settings.joinAddress + ')' } else { '' })) 'DarkGray'
        Say ''
        Say '  1  Solo         only you (default)'
        Say '  2  Host         friends join your Battle Lab'
        Say '  3  Join         play in a friend''s Battle Lab'
        Say ''
        Say '  Everyone must be on the same network: the same home network, or the same Hamachi / Radmin VPN /'
        Say '  Tailscale / ZeroTier network for friends elsewhere. Up to 16 players.'
        Say ''
        switch ((Read-Host 'Choose 1, 2 or 3').Trim()) {
            '1' { $Mode = 'solo' }
            '2' { $Mode = 'host' }
            '3' { $Mode = 'join' }
            default { throw 'Nothing changed.' }
        }
    }

    $commands = @()
    if (!(Test-LanBlockRule $exe)) { $commands += Get-BlockRuleCommands $exe }  # an older install's rule also blocked the LAN

    if ($Mode -eq 'host') {
        if (!$Cheats) {
            $answer = (Read-Host 'Can your friends use cheats (fly, god, give...)? [Y/n]').Trim()
            $Cheats = if ($answer -match '^(n|no)$') { 'host' } else { 'everyone' }
        }
        $node = Get-NodeExe
        if (!$node) { throw 'Node.js is not installed. Install the LTS version from https://nodejs.org first.' }
        if (!(Test-FriendRules $exe $node)) { $commands += Get-FriendRuleCommands $exe $node ([int]$settings.gamePort) $lanPort }
        Invoke-Elevated $commands 'Allowing friends on your LAN / VPN to connect (game port UDP 7777, Battle Lab port TCP 3553).'
        Set-JsonProperty $settings 'mode' 'host'
        Set-JsonProperty $settings 'cheats' $Cheats
        Save-Json 'LocalServer\LocalServerConfig.json' $settings
        Say ''
        Say 'You are the host. Give your friends one of these addresses (a Hamachi / VPN address for friends elsewhere):' 'Green'
        foreach ($a in Get-FriendAddresses) { Say ('    ' + $a) 'White' }
        Say ''
        Say 'Start Play.bat and press Play in Battle Lab. Friends can join as soon as your match is up;' 'Cyan'
        Say 'if they press Play first, your PC starts the match for them.' 'Cyan'
    }
    elseif ($Mode -eq 'join') {
        while (!$Address) {
            $Address = (Read-Host 'Your friend''s address (e.g. 25.12.34.56 or 192.168.1.20)').Trim()
            if ($Address -and $Address -notmatch '^[A-Za-z0-9\.\-]{1,253}$') { Say 'That does not look like an address.' 'Red'; $Address = '' }
        }
        Invoke-Elevated $commands 'Letting this game copy reach your friend on the LAN / VPN.'
        Set-JsonProperty $settings 'mode' 'join'
        Set-JsonProperty $settings 'joinAddress' $Address
        Save-Json 'LocalServer\LocalServerConfig.json' $settings
        # Reachability check (the friend's backend answers once their Play.bat is running in host mode).
        try {
            $status = Invoke-RestMethod ('http://' + $Address + ':' + $lanPort + '/battlelab/lan/status') -TimeoutSec 4
            Say ('Found ' + $status.hostName + '''s Battle Lab at ' + $Address + '.') 'Green'
        } catch {
            Say ('Could not reach a Battle Lab host at ' + $Address + ' right now. That is fine if your friend has not started yet;') 'Yellow'
            Say 'otherwise check that you are on the same network and that they chose Host.' 'Yellow'
        }
        Say ''
        Say 'Start Play.bat and press Play in Battle Lab: you join your friend''s match.' 'Cyan'
    }
    else {
        Set-JsonProperty $settings 'mode' 'solo'
        Save-Json 'LocalServer\LocalServerConfig.json' $settings
        Say 'Solo: Battle Lab runs only for you.' 'Green'
    }
    Say 'If the game is open, close it; the change applies the next time you start Play.bat.' 'DarkGray'
    exit 0
} catch {
    Say ('ERROR: ' + $_.Exception.Message) 'Red'
    exit 1
}
