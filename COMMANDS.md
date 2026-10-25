# In-game commands (local Battle Lab)

Open the console with the **`** key (under Esc), type a command and press Enter. Press ` twice for the large console.
The launcher enables the console once you reach the lobby; the batch window prints `developer console: enabled`.

Commands can be typed on their own (`fly`) or with the old prefix (`cheat fly`); both work. `help` lists everything in game.

## Bots

| Command | What it does |
|---|---|
| `friendly [n]` | Gives you Friendly Bot Grenades (default 5) |
| `enemy [n]` | Gives you Enemy Bot Grenades (default 5) |
| `friendlybot [n]` | Throws a Friendly Bot Grenade in front of you: a friendly bot drops in (the grenade's own projectile, so it behaves exactly like a thrown one) |
| `enemybot [n]` | The same with an Enemy Bot Grenade |

## Movement

| Command | What it does |
|---|---|
| `fly` | Fly on/off. Look up or down while moving to change height |
| `noclip` | Fly through walls and terrain on/off |
| `walk` | Back to walking |
| `speed [x]` | Movement speed, x times normal (default 1). Uses Fortnite's own movement speed multiplier and is kept after respawning |
| `flyspeed [x]` | Fly and noclip speed, x times running speed (default 2) |
| `tp` | Teleports you to the spot you're looking at |
| `goto x y z` | Teleports you to coordinates |
| `save <name>` / `wp <name>` | Saves a waypoint / teleports to it |
| `launch x y z` | Launches you with that velocity, e.g. `launch 0 0 3000` |

## Player and items

| Command | What it does |
|---|---|
| `god` | Invincibility on/off |
| `heal` | Full health and shield |
| `health <n>` / `shield <n>` | Sets health or shield |
| `give <WID> [n]` | Puts an item in your inventory |
| `drop <WID> [n]` | Spawns an item on the ground in front of you |
| `clear` | Empties your weapon and consumable slots |
| `skin <CID_...>` | Changes your skin |
| `pickaxe <ID>` | Changes your pickaxe |
| `kill` | Eliminates you (useful for testing respawn) |

## World

| Command | What it does |
|---|---|
| `destroy` | Deletes the actor you're looking at |
| `storm` | Pauses the storm |
| `players` | Lists player names |
| `summon <BlueprintClass> [n]` | Spawns an actor at your position |

Notes:
- Commands run on your local server (Project Reboot 3.0's command set, extended in `commands.cpp`). The client module (`ConsoleCommands.h`) turns a short command into `cheat <command>`; anything else you type is left to the game.
- Item names are the game's internal names (e.g. `WID_Athena_FrenchYedoc_JWFriendly`). A full `/Game/...` path also works.
