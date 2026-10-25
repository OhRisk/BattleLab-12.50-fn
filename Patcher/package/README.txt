BATTLE LAB FOR FORTNITE 12.50
=============================

Brings Battle Lab back to Fortnite Chapter 2 Season 2 (version 12.50, CL-13137020), running entirely on your own PC:
your own local server, Epic's original Battle Lab playlist, real Fortnite bots from Friendly and Enemy Bot Grenades,
and a set of cheats you type into the console.

This download does NOT include Fortnite. You need your own copy of the 12.50 build. Your game files are not changed:
the patcher only links this folder to your copy, and the launcher loads the Battle Lab modules when it starts the game.


WHAT YOU NEED
  - Windows 10 or 11, 64-bit
  - Fortnite 12.50 CL-13137020 (Install.bat checks the exact build; other versions will not work)
  - Node.js, LTS version: https://nodejs.org


SETUP (once)
  1. Unzip this folder anywhere (a short path without special characters is best, e.g. C:\BattleLab).
  2. Double-click Install.bat.
       - Pick your Fortnite 12.50 folder (the one containing FortniteGame and Engine).
       - Enter a player name.
       - Allow the administrator prompt: it adds a Windows Firewall rule that keeps this old game copy offline.
  3. A "Battle Lab 12.50" shortcut appears on your desktop.


PLAYING
  1. Double-click Play.bat (or the shortcut).
  2. Go straight to Battle Royale, then Play. Don't wait in the menus: if the game is still in the menus about
     70-100 seconds after it starts, it closes itself with "Fortnite was not started correctly". Just start again.
  3. Your local server starts in the background (the first match takes about 1-2 minutes to load).
  4. Press ` (the key under Esc) for the console and type help. Most used:
       friendly 5 / enemy 5      give yourself Friendly / Enemy Bot Grenades
       friendlybot / enemybot    throw a bot grenade in front of you
       fly / noclip / walk       flying, flying through walls, back to normal
       speed 3 / flyspeed 4      move or fly faster
       god, heal, tp, goto x y z, give <item>
     COMMANDS.md has the full list.


PLAYING WITH FRIENDS (up to 16 players)
  Everyone needs this download, their own 12.50 copy, and Install.bat done. You must be on the same network:
  the same home network, or for friends elsewhere a free virtual LAN such as Hamachi, Radmin VPN, Tailscale or ZeroTier.

  The host:   run Multiplayer.bat, choose 2 (Host). Allow the administrator prompt (it opens game port UDP 7777 and
              Battle Lab port TCP 3553, for LAN / VPN addresses only). It shows the addresses friends can use;
              on Hamachi that is your 25.x.x.x address. Then Play.bat and press Play.
  Friends:    run Multiplayer.bat, choose 3 (Join) and enter the host's address. Then Play.bat and press Play.
              If you press Play before the host, the host's PC starts the match for you; you wait in the queue.
  Back to solo: Multiplayer.bat, choose 1.

  Everyone keeps their own skin. The host decides whether friends can use cheats.


CHANGING YOUR NAME
  Run Rename.bat and type a new name (3-16 letters, digits, spaces, _ - or .). It shows the next time you start Play.bat.


TROUBLESHOOTING
  - "not Fortnite 12.50 CL-13137020": the folder holds a different version. Only this exact build is supported.
  - "firewall block ... missing": run Install.bat again and allow the administrator prompt.
  - "Port 3551 is occupied": another program (often an old backend window) uses that port. Close it and retry.
  - Friend stuck in the queue: the host must have chosen Host in Multiplayer.bat and have Play.bat running; check the
    address, and that both PCs are online in the same Hamachi / VPN network. LocalServer\logs\requests.log on the
    friend's PC says "Cannot reach the host" when the connection is the problem.
  - Logs are in the Logs and LocalServer\logs folders. The game host also writes reboot.log into the game's
    FortniteGame\Binaries\Win64 folder.


UNINSTALL
  Run Uninstall.bat (removes the firewall rules and the shortcut), then delete this folder.


This is a non-commercial preservation project and is not affiliated with or endorsed by Epic Games.
Fortnite is a trademark of Epic Games, Inc. The game host is based on Project Reboot 3.0.
