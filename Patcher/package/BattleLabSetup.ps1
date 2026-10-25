# Shared by Install, Multiplayer, Rename and Uninstall (dot-sourced): profile, settings and firewall helpers.
$BL = @{
    Root = $PSScriptRoot
    ExeRelative = 'FortniteGame\Binaries\Win64\FortniteClient-Win64-Shipping.exe'
    BlockRule = 'BattleLab - block 12.50 client internet'
    GameRule = 'BattleLab - friends can join (game)'
    LanRule = 'BattleLab - friends can join (backend)'
    # Everything except LAN and VPN ranges (10/8, 25/8 Hamachi, 26/8 Radmin, 100.64/10 Tailscale, 127/8, 172.16/12, 192.168/16)
    # and all of IPv6. LaunchBattleLab.ps1 recognises this rule by its 11.0.0.0-24.255.255.255 entry.
    BlockRanges = @('0.0.0.0-9.255.255.255', '11.0.0.0-24.255.255.255', '27.0.0.0-100.63.255.255', '100.128.0.0-126.255.255.255',
                    '128.0.0.0-172.15.255.255', '172.32.0.0-192.167.255.255', '192.169.0.0-255.255.255.255',
                    '::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff')
    FriendRanges = @('LocalSubnet', '10.0.0.0/8', '25.0.0.0/8', '26.0.0.0/8', '100.64.0.0/10', '172.16.0.0/12', '192.168.0.0/16')
}

function Say([string]$text, [string]$color = 'Gray') { Write-Host $text -ForegroundColor $color }

function Write-Utf8([string]$path, [string]$text) {
    # No byte-order mark: Node's JSON.parse rejects one.
    [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding $false))
}

function Read-Json([string]$relative) { Get-Content -LiteralPath (Join-Path $BL.Root $relative) -Raw -Encoding UTF8 | ConvertFrom-Json }
function Save-Json([string]$relative, $value) { Write-Utf8 (Join-Path $BL.Root $relative) ($value | ConvertTo-Json -Depth 8) }

function Set-JsonProperty($object, [string]$name, $value) {
    if ($object.PSObject.Properties[$name]) { $object.$name = $value } else { $object | Add-Member -NotePropertyName $name -NotePropertyValue $value }
}

function Get-GameExe {
    $file = Join-Path $BL.Root 'GamePath.txt'
    if (!(Test-Path -LiteralPath $file)) { return $null }
    return Join-Path (Get-Content -LiteralPath $file -Encoding UTF8 -TotalCount 1).Trim().TrimEnd('\') $BL.ExeRelative
}

function Get-NodeExe {
    $node = (Get-Command node.exe -ErrorAction SilentlyContinue).Source
    if (!$node -and (Test-Path -LiteralPath (Join-Path $env:ProgramFiles 'nodejs\node.exe'))) { $node = Join-Path $env:ProgramFiles 'nodejs\node.exe' }
    return $node
}

function Test-ValidName([string]$name) { return $name -match '^[A-Za-z0-9 _\-\.]{3,16}$' }

function Read-PlayerName([string]$current) {
    while ($true) {
        $entered = (Read-Host ('Player name [' + $current + ']')).Trim()
        if (!$entered) { return $current }
        if (Test-ValidName $entered) { return $entered }
        Say 'Use 3-16 letters, digits, spaces, _ - or .' 'Red'
    }
}

# Firewall rules of this game copy (or this node.exe) with the given display name.
function Get-ProgramRules([string]$displayName, [string]$program) {
    @(Get-NetFirewallRule -DisplayName $displayName -ErrorAction SilentlyContinue | Where-Object { ($_ | Get-NetFirewallApplicationFilter).Program -ieq $program })
}

function Test-LanBlockRule([string]$exe) {
    foreach ($rule in Get-ProgramRules $BL.BlockRule $exe) {
        if ($rule.Enabled -eq 'True' -and $rule.Direction -eq 'Outbound' -and $rule.Action -eq 'Block' -and
            @(($rule | Get-NetFirewallAddressFilter).RemoteAddress) -contains '11.0.0.0-24.255.255.255') { return $true }
    }
    return $false
}

function Test-FriendRules([string]$exe, [string]$node) {
    return (Get-ProgramRules $BL.GameRule $exe).Count -gt 0 -and (Get-ProgramRules $BL.LanRule $node).Count -gt 0
}

function Quote([string]$s) { "'" + $s.Replace("'", "''") + "'" }
function List([string[]]$items) { '@(' + (($items | ForEach-Object { Quote $_ }) -join ',') + ')' }

# PowerShell commands that (re)create this copy's outbound block, everything except LAN/VPN ranges.
function Get-BlockRuleCommands([string]$exe) {
    "Get-NetFirewallRule -DisplayName $(Quote $BL.BlockRule) -ErrorAction SilentlyContinue | Where-Object { (`$_ | Get-NetFirewallApplicationFilter).Program -ieq $(Quote $exe) } | Remove-NetFirewallRule"
    "New-NetFirewallRule -DisplayName $(Quote $BL.BlockRule) -Direction Outbound -Action Block -Profile Any -Program $(Quote $exe) -RemoteAddress $(List $BL.BlockRanges) | Out-Null"
}

# Inbound rules for hosting: UDP 7777 (game) and TCP <lanPort> (backend LAN service), LAN/VPN addresses only.
function Get-FriendRuleCommands([string]$exe, [string]$node, [int]$gamePort, [int]$lanPort) {
    "Get-NetFirewallRule -DisplayName $(Quote $BL.GameRule),$(Quote $BL.LanRule) -ErrorAction SilentlyContinue | Remove-NetFirewallRule"
    "New-NetFirewallRule -DisplayName $(Quote $BL.GameRule) -Direction Inbound -Action Allow -Profile Any -Protocol UDP -LocalPort $gamePort -Program $(Quote $exe) -RemoteAddress $(List $BL.FriendRanges) | Out-Null"
    "New-NetFirewallRule -DisplayName $(Quote $BL.LanRule) -Direction Inbound -Action Allow -Profile Any -Protocol TCP -LocalPort $lanPort -Program $(Quote $node) -RemoteAddress $(List $BL.FriendRanges) | Out-Null"
}

function Get-RemoveRuleCommands([string]$exe, [string]$node) {
    foreach ($pair in @(@($BL.BlockRule, $exe), @($BL.GameRule, $exe), @($BL.LanRule, $node))) {
        if ($pair[1]) { "Get-NetFirewallRule -DisplayName $(Quote $pair[0]) -ErrorAction SilentlyContinue | Where-Object { (`$_ | Get-NetFirewallApplicationFilter).Program -ieq $(Quote $pair[1]) } | Remove-NetFirewallRule" }
    }
}

# Runs firewall commands with one administrator prompt. Throws if declined or failed.
function Invoke-Elevated([string[]]$commands, [string]$why) {
    if (!$commands.Count) { return }
    Say ($why + ' Windows will ask for administrator permission...') 'Yellow'
    $script = '$ErrorActionPreference = ''Stop''; try { ' + ($commands -join '; ') + ' } catch { exit 1 }'
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))
    try { $p = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList "-NoProfile -EncodedCommand $encoded" }
    catch { throw 'Administrator permission was declined, so the firewall was not changed. Run this again and choose Yes.' }
    if ($p.ExitCode -ne 0) { throw ('Changing the firewall failed (exit code ' + $p.ExitCode + ').') }
}

# This PC's addresses that friends can use, Hamachi / VPN ones first.
function Get-FriendAddresses {
    $all = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -notmatch '^(127\.|169\.254\.)' })
    $all | Sort-Object { if ($_.IPAddress -match '^(25|26)\.' -or $_.InterfaceAlias -match 'Hamachi|Radmin|ZeroTier|Tailscale') { 0 } else { 1 } } |
        ForEach-Object { '{0}  ({1})' -f $_.IPAddress, $_.InterfaceAlias }
}
