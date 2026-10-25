# Local Backend (LocalServer/)

Node.js (no npm dependencies). Binds **127.0.0.1 only** and never contacts Epic.

```
cd LocalServer
node server.js
```

| Port | Purpose |
|---|---|
| 3551 | HTTP: account/OAuth, lightswitch, MCP profiles, cloudstorage, content, calendar, friends/party stubs, matchmaking REST |
| 3552 | Matchmaker WebSocket |

Config: `LocalServer/LocalProfile.json` (display name, account id, ports, game-server address,
playlist). Changing `displayName` needs no rebuild; restart the server.

## Files
- `server.js`: routes, matchmaker, logging
- `lib/mcp.js`: profile store (`data/profiles/*.json`, persisted) and MCP command handlers
- `lib/ws.js`: minimal WebSocket server
- `hotfixes/*.ini`: served as cloudstorage "system" files (hotfix mechanism; used later to
  re-enable anything Epic switched off remotely, e.g. Bot Grenades if they were hotfix-disabled)
- `logs/requests.log`: every request. `[UNHANDLED]` lines are the to-do list once the client runs.

## Battle Lab session flow (implemented, verified with a scripted client)
1. `GET /fortnite/api/game/v2/matchmakingservice/ticket/player/:id` → ticket with `serviceUrl=ws://127.0.0.1:3552`
2. WebSocket: `Connecting → Waiting → Queued → SessionAssignment → Play{sessionId}`
3. `GET /fortnite/api/matchmaking/session/:sessionId` → `127.0.0.1:7777`, `PLAYLISTNAME_s=Playlist_BattleLab`, `BATTLELABSESSIONOWNERID_s=<local account>`

## Not yet verified against the real client
Response shapes follow the 12.x API but have not yet been checked against this build's client.
The client's HTTP traffic still has to be pointed at 127.0.0.1 (the client-side DLL,
next milestone). After that, `requests.log` drives corrections. The XMPP presence/party service is
not implemented yet; whether "Play" needs it will show up in the first client run.
