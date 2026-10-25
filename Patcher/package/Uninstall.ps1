# Battle Lab 12.50: undoes Install.bat and Multiplayer.bat. Removes this game copy's firewall rules, the desktop shortcut
# and GamePath.txt. The game itself was never changed. Your local profile (LocalServer\data) stays unless you delete this folder.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BattleLabSetup.ps1')
try {
    $exe = Get-GameExe
    if ($exe) {
        $node = Get-NodeExe
        $existing = (Get-ProgramRules $BL.BlockRule $exe).Count + (Get-ProgramRules $BL.GameRule $exe).Count + $(if ($node) { (Get-ProgramRules $BL.LanRule $node).Count } else { 0 })
        if ($existing) { Invoke-Elevated @(Get-RemoveRuleCommands $exe $node) 'Removing the Battle Lab firewall rules.' }
        Remove-Item -LiteralPath (Join-Path $BL.Root 'GamePath.txt')
    }
    $link = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Battle Lab 12.50.lnk'
    if (Test-Path -LiteralPath $link) { Remove-Item -LiteralPath $link }
    Say 'Battle Lab setup removed. Delete this folder to remove everything else.' 'Cyan'
    exit 0
} catch {
    Say ('ERROR: ' + $_.Exception.Message) 'Red'
    exit 1
}
