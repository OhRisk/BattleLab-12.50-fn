# Battle Lab 12.50: change the name other players see in game. Play.bat restarts the local backend with it next time.
param([string]$Name)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BattleLabSetup.ps1')
try {
    $localProfile = Read-Json 'LocalServer\LocalProfile.json'
    Say ('Current name: ' + $localProfile.displayName)
    if ($Name -and !(Test-ValidName $Name)) { throw 'Player names use 3-16 letters, digits, spaces, _ - or .' }
    if (!$Name) { $Name = Read-PlayerName $localProfile.displayName }
    if ($Name -ceq $localProfile.displayName) { Say 'Name unchanged.'; exit 0 }
    $localProfile.displayName = $Name
    Save-Json 'LocalServer\LocalProfile.json' $localProfile
    Say ('Your name is now ' + $Name + '. If the game is open, close it; the new name shows the next time you start Play.bat.') 'Green'
    exit 0
} catch {
    Say ('ERROR: ' + $_.Exception.Message) 'Red'
    exit 1
}
