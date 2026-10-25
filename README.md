# Battle Lab for Fortnite 12.50

Battle Lab was Fortnite's sandbox mode in Chapter 2: a private copy of the Battle Royale island where you could respawn, spawn bots with Bot Grenades and experiment freely. It was removed years ago. This project brings it back on **Fortnite 12.50 (CL-13137020, Chapter 2 Season 2)**, running entirely on your own PC: alone, with friends on the same network, or over Hamachi or another virtual LAN.

The aim is restoration, not imitation. Wherever possible, the game's own systems do the work: Epic's original `Playlist_BattleLab`, the game's own bot AI and behaviour trees, its team system and its HUD. The project's code supplies only what the old client needs to start: a local stand-in for Epic's backend and a local game host.

> This repository does not contain Fortnite or any of Epic's files. You need your own copy of the 12.50 build. The patcher never modifies the game's files.

## Features

- **Battle Lab as Epic shipped it:** the original playlist with its rules, loot sets, respawning and island.
- **Bot Grenades:** Friendly and Enemy bots, driven by Fortnite's own AI (navigation, combat, looting).
- **Multiplayer:** up to 16 players over LAN, Hamachi, Radmin VPN, Tailscale or ZeroTier. Everyone keeps their own Locker skin.
- **Cheat console:** fly, noclip, speed, god mode, teleports, items, bot spawning. Press `` ` `` in game and type `help`.
- **Your Locker:** every cosmetic in the 12.50 files, unlocked locally.
- **Patcher:** one `Install.bat` sets everything up for an existing 12.50 install. `Multiplayer.bat`, `Rename.bat` and `Uninstall.bat` cover the rest.

## Playing

1. Download the release zip (or build it, see below) and unzip it somewhere with a short path, for example `C:\BattleLab`.
2. Install [Node.js](https://nodejs.org) (LTS version).
3. Run **`Install.bat`**:
   1. Pick your Fortnite 12.50 folder (the one that contains `FortniteGame` and `Engine`). The installer checks the exact build by hash.
   2. Choose a player name.
   3. Allow the one administrator prompt. It adds a firewall rule that keeps this old client off the internet.
4. Run **`Play.bat`**, then go straight to **Battle Royale → Play**. If the client sits in the menus for more than about 70 seconds after starting, it closes itself ("Fortnite was not started correctly"). Just start it again.

### With friends

Everyone needs the release, their own 12.50 copy and a completed `Install.bat`. Everyone must also be on the same network: the same home network, or a virtual LAN for friends elsewhere.

| Who | Steps |
|---|---|
| Host | `Multiplayer.bat` → **2 Host**, then allow the firewall prompt (it opens UDP 7777 and TCP 3553 to LAN and VPN addresses only). It lists your addresses; on Hamachi, give friends your `25.x.x.x` address. Then `Play.bat` → Play. |
| Friends | `Multiplayer.bat` → **3 Join**, enter the host's address. Then `Play.bat` → Play. A friend who queues before the host has a match running starts the host's match automatically. |

`Rename.bat` changes your in-game name at any time.

### Commands

Open the console with `` ` `` (the key under Esc). The most used commands:

| Command | Effect |
|---|---|
| `friendly 5` / `enemy 5` | Gives you Friendly or Enemy Bot Grenades |
| `friendlybot` / `enemybot` | Throws a bot grenade in front of you |
| `fly`, `noclip`, `walk` | Toggles flying, flying through walls, or returns to walking |
| `speed 3`, `flyspeed 4` | Sets movement and flying speed multipliers |
| `god`, `heal`, `tp`, `goto x y z`, `give <WID>` | Invincibility, full health, teleport to where you look, teleport to coordinates, give an item |

The full list is in [`COMMANDS.md`](COMMANDS.md).

## How it works

```
 Fortnite 12.50 client  ── HTTP (redirected in-process) ──▶  LocalServer (Node.js, 127.0.0.1:3551)
   + BattleLabClient.dll                                       accounts, profiles/Locker, hotfixes,
         │                                                     matchmaking, Battle Lab session
         │  UDP 7777                                                       │ starts on demand
         ▼                                                                 ▼
 Local game host: the same 12.50 executable, headless, + BattleLabHost.dll (Project Reboot 3.0 based)
   runs Athena with Playlist_BattleLab on the game's own server code
```

- **`Native/BattleLabLauncher`**: starts the stock client and loads the client module. It also starts the headless host process when the backend asks for one.
- **`Native/BattleLabClient`**:
  - redirects the client's Epic web requests to the local backend;
  - turns short console commands into cheat commands;
  - starts flying and noclip on the client side, so the movement is smooth.
- **`Native/ThirdParty/Project-Reboot-3.0-master`**: the game host, built on [Project Reboot 3.0](https://github.com/Milxnor/Project-Reboot-3.0). Its `BattleLab*.h` files hold the 12.50-specific restoration work:
  - starting Bot Grenade bots through the game's own spawner;
  - navigation relevance for static meshes, so bots path around walls;
  - Locker loadouts;
  - multiplayer binding and permissions.
- **`LocalServer`**: the backend (Epic's account, MCP profile, cloud storage, matchmaking and session APIs). In host mode it also runs a small LAN service, where friends register and wait for the match.
- **`LocalServer/hotfixes/DefaultGame.ini`**: hotfixes in Epic's own format. They expose the playlist and re-enable the Bot Grenade loot rows Epic left in the game files.
- **`Patcher`**: the release builder and the player scripts.

Progress logs and research notes are in [`PROGRESS.md`](PROGRESS.md), [`BOT_AI_PROGRESS.md`](BOT_AI_PROGRESS.md), [`BOT_INITIALIZATION_TRACE.md`](BOT_INITIALIZATION_TRACE.md) and [`Documentation/`](Documentation/).

## Building from source

Requirements: Windows 10 or 11 x64, Visual Studio 2022 Build Tools (the C++ workload) and Node.js.

```bat
Patcher\BuildPatcher.bat
```

This compiles the launcher, the client module and the host into `Native\bin`, then assembles `Patcher\dist\BattleLab-12.50\` and `BattleLab-12.50.zip`. The builder refuses to package game files, logs, local profiles or developer flags. `Native\build.bat` builds only the binaries.

To run from the source tree, put your game folder's path in `GamePath.txt` (one line), then run `LaunchBattleLab.bat`. The script refuses to start the game until the firewall block rule exists, so run `Patcher\package\Install.bat` once or create that rule yourself.

## Known issues

- **Launch timer:** the client closes itself if it stays in the menus for about 70–100 seconds after starting. Head straight into a match.
- **Friendly marker:** a Friendly bot's teammate marker only appears after you respawn once. The cause is in the HUD's indicator refresh; see `BOT_AI_PROGRESS.md`.
- **Steamy Stacks:** bots caught in the cooling-tower updraft hover, because Bot Grenade bots have no skydiving behaviour in Epic's design.
- **Multiplayer is new:** it is tested through matchmaking and host start-up, but real LAN and Hamachi matches still need testing. Reports are welcome.
- **Build:** only 12.50 CL-13137020 is supported. All native offsets are specific to that build. Other builds may follow later.

## Credits and legal

- **Project Reboot 3.0** by Milxnor and contributors, BSD 3-Clause. See [`Native/ThirdParty/Project-Reboot-3.0-master/LICENSE`](Native/ThirdParty/Project-Reboot-3.0-master/LICENSE). Its vendored libraries (MinHook, ImGui, spdlog, fmt, curl, nlohmann/json) keep their own licenses.
- This is a non-commercial preservation project. It is not affiliated with or endorsed by Epic Games. Fortnite is a trademark of Epic Games, Inc. The repository contains no Epic code, assets or game files. Use it only with a copy of the game you own.
