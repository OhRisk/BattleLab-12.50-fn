# Launching the local project

## Current status

The supplied executable is **12.50 CL-13137020**, not 12.41. The local host now loads Apollo with Battle Lab, and the user has confirmed joining and playing. Bot AI and locker-skin replication are under repair; full feature restoration has not been verified.

## Start

1. Close the previous working-copy Fortnite window.
2. Double-click `E:\Fortnite Battle Lab revival\LaunchBattleLab.bat`.
3. The launcher checks the working-copy firewall block, starts the local backend if necessary, and launches the client.
4. Choose **Battle Royale**. Battle Lab should be selected in the lobby.
5. Create a Battle Lab. The backend automatically starts a separate local host and keeps you queued until it is ready. A cold start can take roughly 1–2 minutes.

Use the `.bat`, not the game executable or Epic launcher. Running the `.ps1` directly is blocked by this machine's execution policy. The batch file supplies the required per-process option; no system-wide policy change is needed.

The gameplay host listens on `127.0.0.1:7777`. It uses a separate game process; leave it running while playing. There is no need to launch it manually. Developer-console item commands are in `COMMANDS.md`.

## Logs and failures

- Startup errors appear in the batch window, which stays open on failure.
- Previous logs are archived under `Logs\Launch-<timestamp>`.
- Native diagnostics: `Logs\BattleLabClient.log`.
- Host diagnostics: `Fortnite-12.50-BattleLab\FortniteGame\Binaries\Win64\reboot.log` and `Logs\BattleLabHostClient.log`.
- Backend requests: `LocalServer\logs\requests.log`.
- Engine log: `%LOCALAPPDATA%\FortniteGame\Saved\Logs\FortniteGame.log`.
- If a client is already running, close it before retrying.
- Infinite loading is still an unresolved intermittent failure; preserve logs before restarting.

Only the working copy is used. The source installation remains unchanged. The backend binds to loopback only; retain the existing outbound firewall block.
