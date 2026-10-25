# Testing Log

All launches use the working copy and run with the firewall guard
(`BattleLab - block 12.50 client internet`: outbound block for the working-copy
FortniteClient-Win64-Shipping.exe; loopback unaffected).

## Launch 1 (2026-09-25): direct client start, guard on, backend on
Args: `-epicapp=Fortnite -epicenv=Prod -epiclocale=en-us -epicportal -skippatchcheck -nobe -fromfl=eac
-fltoken=none -AUTH_TYPE=exchangecode -AUTH_LOGIN=unused -AUTH_PASSWORD=battlelab -windowed -ResX=1280 -ResY=720 -userdir=…`

Results:
- Client starts without the EAC/BE wrapper and mounts all paks. Engine reports 4.25.
- Tries `https://account-public-service-prod(.ak).epicgames.com/account/api/oauth/token` (env **Prod**).
  All connects fail with "Bad access": **the firewall guard works; nothing reached Epic.**
- Login fails → shows the email/password `SignIn_Credentials` screen (seen by the owner).
- `-userdir=` is **not honored**; logs still go to `%LOCALAPPDATA%\FortniteGame\Saved\Logs`.
  So a config-layer redirect cannot be isolated from that shared folder, and the redirect will be done in-process.
- ~4 s after the login screen: silent crash, `0xC0000005` at module offset `0x2bb4bbc`, no crash reporter.
  Cause unknown (code is encrypted on disk). To investigate with the in-process module / a debugger.
- Local backend received no requests (expected: nothing redirected yet).

Log copy: `Tools/cache/launch1_FortniteGame.log`.

## Launches 2-6 (2026-09-26): BattleLabClient.dll + BattleLabLauncher.exe
- HTTP redirect works: `FCurlHttpRequest::ProcessRequest` located at runtime (anchor string → SetupRequest
  → its single caller → vtable slot) and hooked; every request is rewritten to `http://127.0.0.1:3551`.
- Auto-login as the local account works: `Successfully logged in user … DisplayName=[OhRisk]`.
- Hotfix confirmed applied by the client: all 6 Bot Grenade rows `updated column Weight from 0.00 to 2.00`.
- Login aborted once at `QueryKeychain` (empty keychain rejected). Fixed with a placeholder keychain entry;
  login then continued past keychain, receipts, friends, social ban, party.
- Athena profile now carries all 3,263 local cosmetics (Tools/cosmetics.py), plus locker MCP commands.
- **Blocking:** in every run (with or without the module, logged in or not) the client terminates with an access
  violation ~22-24 s after process start. Suspended FortniteLauncher/_EAC companions made no difference.
  Further investigation of this termination was stopped at the project's permission boundary (see KNOWN_ISSUES).
