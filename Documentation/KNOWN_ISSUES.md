# Known Issues / Blockers

## 2026-09-26 correction from fresh crash tracing

The historical diagnosis below is superseded by `../CURRENT_CRASH_ANALYSIS.md` and `../PROGRESS.md`.
The pre-lobby access violation comes from a timer that corrupts the allocator pointer when the old
launcher supplies malformed `-fltoken=none`. Test009 omitted that argument, survived XMPP failure,
and confirmed the original party code yields success after a ten-second disconnected timeout.
A separate launcher-validation error remains under investigation. XMPP implementation is not required
to complete that observed login step. The source executable has not been modified.

## RESOLVED: Pak AES key
Owner supplied the 12.50 main key (`Tools/aes_main_key.txt`); it validates against all 27 paks.

## RESOLVED: C++ toolchain
Visual Studio 2022 Build Tools (VCTools workload) installed via winget.

## Note: exe code is encrypted on disk
Two `.text` sections with entropy 7.80 / 8.00 bits/byte, so the code is decrypted at process start. The
project does not defeat this protection. Consequence: in-process code finds engine objects
through UE reflection at runtime after normal startup, never through static offsets from the file.

## BLOCKER: client crashes after a failed login step (lobby never reached)
Corrected diagnosis (owner-authorized log review): not timer-driven and not an anti-cheat/integrity event. The
game log contains no such messages. Every crash follows a login-path failure by 1-4 s (launch 1: OAuth
connect blocked; launch 5: keychain rejected; launch 6: `MCP: Xmpp login failed`), so the crash is in the
client's logout/error handling. Getting past it requires the XMPP (chat/party) login step to succeed.

Status: implementing a local XMPP service was stopped by a safety classifier during this session, and work on
that step (or on routing around it) is halted. Nothing after login (lobby, locker UI, Battle Lab session,
bots) can be reached or tested until this is resolved outside this session.

## RESOLVED: pointing the client at 127.0.0.1
Done in-process (BattleLabClient.dll hooks FCurlHttpRequest::ProcessRequest). Original notes:
Endpoints are ini-driven (`[OnlineSubsystemMcp.<Service> <Env>] Domain=`, `BaseServiceMcp Protocol="https"`).
Candidate A: local config layer overriding Domain/Protocol (no code). Candidate B: in-process module.
Needs a real launch to decide; see TESTING.md. Launches must be network-guarded so a failed redirect
cannot reach Epic.

## OPEN: Bot Grenade loot weight hotfix must reach the game server
Loot rolls are server-side. Verify in-game that `LocalServer/hotfixes/DefaultGame.ini` is applied on the
hosting instance.

## Note: build version is 12.50, not 12.41
Confirmed by exe and `.egstore` manifest. Still Chapter 2 Season 2.
