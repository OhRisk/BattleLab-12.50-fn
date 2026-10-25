# Battle Lab Dependencies (12.50), pass 1

What the original client needs before Battle Lab can start, split by where it comes from.
Everything here is local-only; the backend binds to 127.0.0.1 and never proxies to Epic.

## A. Online services the client calls (from exe strings)

| Service | Evidence | Needed for Battle Lab? |
|---|---|---|
| Account / OAuth | `errors.com.epicgames.common.oauth.*`, `accounts.epicgames.com/login` | Yes: login must succeed before the frontend loads |
| MCP profiles | `QueryProfile`, `ClientQuestLogin`, `SetMtxPlatform`, `GetCreativeProfile`, `ServerBrowser.SyncCreativeProfile` | Yes: `athena`, `common_core`, and the **creative** profile (Battle Lab island saves go through the creative path) |
| Matchmaking | `/api/game/v2/matchmakingservice/ticket/player/`id`, `/ticket/session/`id` | Yes: the server-browser "create" action issues a ticket and expects a session |
| Presence / party / friends (XMPP + REST) | `/presence/api/v1/...` | Party service likely required for "Play" (party leader checks: `ServerBrowserWarning_NotPartyLeader`) |
| Content / lightswitch / cloudstorage / hotfixes | Endpoint host list | Usually required for login flow; exact list confirmed by logging live requests |
| Datarouter (telemetry) | `datarouter/api/v1/public/data` | No: answer 204 |

The exact request order will be captured by running the client against a logging-only local
server (Milestone 2) instead of guessing.

## B. Game server

The client does not host matches for itself. Something must run the server side of
`Playlist_BattleLab` (GameMode, mutator, loot spawning, bot manager, respawn). The standard
approach for this engine version is to run a second instance of the same executable as the
server, with a native DLL loaded at startup that drives the server-side calls the missing
dedicated-server binary would have made. **This requires a C++ toolchain (see KNOWN_ISSUES).**

## C. Asset data (inside encrypted paks)

Playlist, mutator data, loot tables, device definitions, island save defaults, nav data. See
`BATTLELAB_ARCHITECTURE.md` §5. **Requires the pak AES key (see KNOWN_ISSUES).**
