// Battle Lab local compatibility backend. Binds to 127.0.0.1 only; never contacts Epic.
// Every request is logged to logs/requests.log so the real client call sequence can be traced.
// Unknown routes are answered with 204 and flagged as [UNHANDLED] in the log.
const http = require("http");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");
const { URL } = require("url");
const ws = require("./lib/ws");
const { Mcp } = require("./lib/mcp");

const HOST = "127.0.0.1";
const cfg = JSON.parse(fs.readFileSync(path.join(__dirname, "LocalProfile.json"), "utf8"));
// mode: "solo" (only this PC), "host" (friends join this PC over LAN/Hamachi/another VPN) or "join" (play on the
// friend at joinAddress). lanPort: the host's small LAN service (status + join registration); cheats: "everyone" | "host".
const serverCfg = Object.assign(
  { gameHost: "127.0.0.1", gamePort: 7777, battleLabAutoStart: true, hostStartTimeoutSec: 300, hostVisible: false,
    mode: "solo", joinAddress: "", lanPort: 3553, cheats: "everyone" },
  (() => {
    try {
      return JSON.parse(fs.readFileSync(path.join(__dirname, "LocalServerConfig.json"), "utf8"));
    } catch {
      return {};
    }
  })()
);
if (!["solo", "host", "join"].includes(serverCfg.mode)) serverCfg.mode = "solo";
if (serverCfg.mode === "join" && !serverCfg.joinAddress) serverCfg.mode = "solo";
cfg.gameServer = { address: serverCfg.mode === "join" ? serverCfg.joinAddress : serverCfg.gameHost, port: serverCfg.gamePort };
const STARTED = new Date().toISOString();

// This PC's IPv4 addresses that friends can reach (LAN, Hamachi 25.x, Radmin 26.x, Tailscale 100.64/10, ...).
function lanAddresses() {
  const out = [];
  for (const [name, list] of Object.entries(require("os").networkInterfaces()))
    for (const a of list || [])
      if (a.family === "IPv4" && !a.internal && !a.address.startsWith("169.254.")) out.push(`${a.address} (${name})`);
  return out;
}
const LOG_DIR = path.join(__dirname, "logs");
fs.mkdirSync(LOG_DIR, { recursive: true });
const logFile = fs.createWriteStream(path.join(LOG_DIR, "requests.log"), { flags: "a" });

function log(line) {
  const s = `${new Date().toISOString()} ${line}`;
  console.log(s);
  logFile.write(s + "\n");
}

const mcp = new Mcp(cfg, log);
const ACCOUNT = cfg.accountId;
const token = () => crypto.randomBytes(16).toString("hex");
const sessions = new Map(); // sessionId -> session info

// ---------------------------------------------------------------- routing
const routes = [];
const route = (method, pattern, fn) => {
  const keys = [];
  const rx = new RegExp(
    "^" + pattern.replace(/:(\w+)/g, (_, k) => (keys.push(k), "([^/]+)")).replace(/\*/g, ".*") + "/?$"
  );
  routes.push({ method, rx, keys, fn });
};

function json(res, status, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(status, { "Content-Type": "application/json" });
  res.end(body);
}
const noContent = (res) => {
  res.writeHead(204);
  res.end();
};

route("GET", "/battlelab/health", (req, res) =>
  json(res, 200, { service: "BattleLabLocal", version: 4, build: "12.50-13137020", account: cfg.displayName, playlist: cfg.playlist, pid: process.pid,
    started: STARTED, mode: serverCfg.mode, joinAddress: serverCfg.joinAddress, lanAddresses: serverCfg.mode === "host" ? lanAddresses() : [] })
);

// ---- account / oauth
route("POST", "/account/api/oauth/token", (req, res) => {
  json(res, 200, {
    access_token: token(),
    expires_in: 28800,
    expires_at: "9999-12-31T23:59:59.999Z",
    token_type: "bearer",
    refresh_token: token(),
    refresh_expires: 86400,
    refresh_expires_at: "9999-12-31T23:59:59.999Z",
    account_id: ACCOUNT,
    client_id: "ec684b8c687f479fadea3cb2ad83f5c6",
    internal_client: true,
    client_service: "fortnite",
    displayName: cfg.displayName,
    app: "fortnite",
    in_app_id: ACCOUNT,
    device_id: "battlelablocaldevice",
  });
});
route("GET", "/account/api/oauth/verify", (req, res) =>
  json(res, 200, {
    token: token(),
    session_id: token(),
    token_type: "bearer",
    client_id: "ec684b8c687f479fadea3cb2ad83f5c6",
    internal_client: true,
    client_service: "fortnite",
    account_id: ACCOUNT,
    expires_in: 28800,
    expires_at: "9999-12-31T23:59:59.999Z",
    auth_method: "exchange_code",
    display_name: cfg.displayName,
    app: "fortnite",
    in_app_id: ACCOUNT,
    device_id: "battlelablocaldevice",
  })
);
route("DELETE", "/account/api/oauth/sessions/kill*", (req, res) => noContent(res));
const accountObj = () => ({
  id: ACCOUNT,
  displayName: cfg.displayName,
  name: cfg.displayName,
  email: "local@battlelab.invalid",
  failedLoginAttempts: 0,
  lastLogin: new Date().toISOString(),
  numberOfDisplayNameChanges: 0,
  ageGroup: "UNKNOWN",
  headless: false,
  country: "US",
  lastName: "Local",
  preferredLanguage: "en",
  canUpdateDisplayName: false,
  tfaEnabled: true,
  emailVerified: true,
  minorVerified: false,
  minorExpected: false,
  minorStatus: "UNKNOWN",
  externalAuths: {},
});
route("GET", "/account/api/public/account/:id/externalAuths", (req, res) => json(res, 200, []));
route("GET", "/account/api/public/account/:id", (req, res) => json(res, 200, accountObj()));
route("GET", "/account/api/public/account", (req, res) =>
  json(res, 200, [{ id: ACCOUNT, displayName: cfg.displayName, externalAuths: {} }])
);
route("GET", "/account/api/epicdomains/ssodomains", (req, res) => json(res, 200, []));

// ---- service status / version / gates
route("GET", "/lightswitch/api/service/bulk/status", (req, res) =>
  json(res, 200, [
    {
      serviceInstanceId: "fortnite",
      status: "UP",
      message: "Battle Lab local",
      maintenanceUri: null,
      overrideCatalogIds: ["a7f138b2e51945ffbfdacc1af0541053"],
      allowedActions: ["PLAY", "DOWNLOAD"],
      banned: false,
      launcherInfoDTO: { appName: "Fortnite", catalogItemId: "4fe75bbc5a674f4f9b356b5c90567da5", namespace: "fn" },
    },
  ])
);
route("GET", "/lightswitch/api/service/:svc/status", (req, res) =>
  json(res, 200, { serviceInstanceId: "fortnite", status: "UP", allowedActions: ["PLAY", "DOWNLOAD"], banned: false })
);
route("GET", "/fortnite/api/version", (req, res) =>
  json(res, 200, { app: "fortnite", serverDate: new Date().toISOString(), overridePropertiesVersion: "unknown", cln: "13137020", build: "12.50", moduleName: "Fortnite-Core", buildDate: "2020-04-14", version: "12.50", branch: "Release-12.50" })
);
route("GET", "/fortnite/api/v2/versioncheck/*", (req, res) => json(res, 200, { type: "NO_UPDATE" }));
route("GET", "/fortnite/api/versioncheck*", (req, res) => json(res, 200, { type: "NO_UPDATE" }));
route("POST", "/fortnite/api/game/v2/tryPlayOnPlatform/account/:id", (req, res) => {
  res.writeHead(200, { "Content-Type": "text/plain" });
  res.end("true");
});
route("GET", "/fortnite/api/game/v2/enabled_features", (req, res) => json(res, 200, []));
route("POST", "/fortnite/api/game/v2/grant_access/:id", (req, res) => noContent(res));
route("GET", "/waitingroom/api/waitingroom", (req, res) => noContent(res));
route("GET", "/eulatracking/api/shared/agreements/:name", (req, res) => noContent(res));
route("GET", "/eulatracking/api/public/agreements/:name/account/:id", (req, res) => noContent(res));
route("POST", "/datarouter/api/v1/public/data*", (req, res) => noContent(res));
route("GET", "/fortnite/api/receipts/v1/account/:id/receipts", (req, res) => json(res, 200, []));
route("GET", "/fortnite/api/game/v2/world/info", (req, res) => json(res, 200, {}));
route("GET", "/fortnite/api/game/v2/privacy/account/:id", (req, res) =>
  json(res, 200, { accountId: ACCOUNT, optOutOfPublicLeaderboards: false })
);
route("GET", "/fortnite/api/statsv2/account/:id", (req, res) =>
  json(res, 200, { startTime: 0, endTime: 0, stats: {}, accountId: ACCOUNT })
);

// ---- cloudstorage (hotfix ini files served from LocalServer/hotfixes/)
const HOTFIX_DIR = path.join(__dirname, "hotfixes");
fs.mkdirSync(HOTFIX_DIR, { recursive: true });
route("GET", "/fortnite/api/cloudstorage/system", (req, res) => {
  const files = fs.readdirSync(HOTFIX_DIR).filter((f) => f.endsWith(".ini"));
  json(
    res,
    200,
    files.map((f) => {
      const data = fs.readFileSync(path.join(HOTFIX_DIR, f));
      return {
        uniqueFilename: f,
        filename: f,
        hash: crypto.createHash("sha1").update(data).digest("hex"),
        hash256: crypto.createHash("sha256").update(data).digest("hex"),
        length: data.length,
        contentType: "application/octet-stream",
        uploaded: new Date().toISOString(),
        storageType: "S3",
        doNotCache: true,
      };
    })
  );
});
route("GET", "/fortnite/api/cloudstorage/system/config", (req, res) => json(res, 200, {}));
route("GET", "/fortnite/api/cloudstorage/system/:file", (req, res, p) => {
  const f = path.join(HOTFIX_DIR, path.basename(p.file));
  if (!fs.existsSync(f)) return noContent(res);
  res.writeHead(200, { "Content-Type": "application/octet-stream" });
  res.end(fs.readFileSync(f));
});
const USER_CLOUD = path.join(__dirname, "data", "cloudstorage");
fs.mkdirSync(USER_CLOUD, { recursive: true });
route("GET", "/fortnite/api/cloudstorage/user/:id", (req, res) => {
  const f = path.join(USER_CLOUD, "ClientSettings.Sav");
  if (!fs.existsSync(f)) return json(res, 200, []);
  const data = fs.readFileSync(f);
  json(res, 200, [
    {
      uniqueFilename: "ClientSettings.Sav",
      filename: "ClientSettings.Sav",
      hash: crypto.createHash("sha1").update(data).digest("hex"),
      hash256: crypto.createHash("sha256").update(data).digest("hex"),
      length: data.length,
      contentType: "application/octet-stream",
      uploaded: fs.statSync(f).mtime.toISOString(),
      storageType: "S3",
      accountId: ACCOUNT,
      doNotCache: false,
    },
  ]);
});
route("GET", "/fortnite/api/cloudstorage/user/:id/:file", (req, res, p) => {
  const f = path.join(USER_CLOUD, path.basename(p.file));
  if (!fs.existsSync(f)) return noContent(res);
  res.writeHead(200, { "Content-Type": "application/octet-stream" });
  res.end(fs.readFileSync(f));
});
route("PUT", "/fortnite/api/cloudstorage/user/:id/:file", (req, res, p) => {
  fs.writeFileSync(path.join(USER_CLOUD, path.basename(p.file)), req.rawBody);
  noContent(res);
});

// ---- content / calendar / store
route("GET", "/content/api/pages/*", (req, res) =>
  json(res, 200, {
    "jcr:isCheckedOut": true,
    _title: "Fortnite Game",
    _activeDate: "2020-01-01T00:00:00.000Z",
    lastModified: new Date().toISOString(),
    _locale: "en-US",
    battleroyalenews: { news: { motds: [], messages: [] }, _title: "battleroyalenews", header: "", style: "None", _noIndex: false, alwaysShow: false, _activeDate: "2020-01-01T00:00:00.000Z", lastModified: new Date().toISOString(), _locale: "en-US" },
    emergencynotice: { news: { platform_messages: [], _type: "Battle Royale News", messages: [] }, _title: "emergencynotice", _activeDate: "2020-01-01T00:00:00.000Z", lastModified: new Date().toISOString(), _locale: "en-US" },
    playlistinformation: { frontend_matchmaking_header_style: "None", _title: "playlistinformation", frontend_matchmaking_header_text: "", playlist_info: { _type: "Playlist Information", playlists: [{ playlist_name: "Playlist_BattleLab", hidden: false, disabled: false }] }, _noIndex: false, _activeDate: "2020-01-01T00:00:00.000Z", lastModified: new Date().toISOString(), _locale: "en-US" },
    dynamicbackgrounds: { backgrounds: { backgrounds: [{ stage: "season12", _type: "DynamicBackground", key: "lobby" }], _type: "DynamicBackgroundList" }, _title: "dynamicbackgrounds", _noIndex: false, _activeDate: "2020-01-01T00:00:00.000Z", lastModified: new Date().toISOString(), _locale: "en-US" },
  })
);
route("GET", "/fortnite/api/calendar/v1/timeline", (req, res) => {
  const t = new Date().toISOString();
  const far = "9999-12-31T23:59:59.999Z";
  json(res, 200, {
    channels: {
      "client-matchmaking": { states: [{ validFrom: "2020-01-01T00:00:00.000Z", activeEvents: [], state: { region: {} } }], cacheExpire: far },
      "client-events": {
        states: [
          {
            validFrom: "2020-01-01T00:00:00.000Z",
            activeEvents: [
              { eventType: `EventFlag.Season${cfg.seasonNumber}`, activeUntil: far, activeSince: "2020-01-01T00:00:00.000Z" },
              { eventType: "EventFlag.LobbySeason12", activeUntil: far, activeSince: "2020-01-01T00:00:00.000Z" },
            ],
            state: {
              activeStorefronts: [],
              eventNamedWeights: {},
              seasonNumber: cfg.seasonNumber,
              seasonTemplateId: `AthenaSeason:athenaseason${cfg.seasonNumber}`,
              matchXpBonusPoints: 0,
              seasonBegin: "2020-02-20T13:00:00Z",
              seasonEnd: far,
              seasonDisplayedEnd: far,
              weeklyStoreEnd: far,
              stwEventStoreEnd: far,
              stwWeeklyStoreEnd: far,
              dailyStoreEnd: far,
            },
          },
        ],
        cacheExpire: far,
      },
    },
    eventsTimeOffsetHrs: 0,
    cacheIntervalMins: 10,
    currentTime: t,
  });
});
route("GET", "/fortnite/api/storefront/v2/catalog", (req, res) =>
  json(res, 200, { refreshIntervalHrs: 24, dailyPurchaseHrs: 24, expiration: "9999-12-31T23:59:59.999Z", storefronts: [] })
);
// The client rejects an empty keychain ("Unable to parse json" -> login aborted at QueryKeychain).
// One well-formed placeholder entry; its GUID matches no pak (12.50 paks all use the main key, GUID 0).
route("GET", "/fortnite/api/storefront/v2/keychain", (req, res) =>
  json(res, 200, ["BA771E1A000000000000000000000001:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=:BattleLabLocalPlaceholder"])
);
route("GET", "/catalog/api/shared/bulk/offers", (req, res) => json(res, 200, {}));

// ---- MCP
route("POST", "/fortnite/api/game/v2/profile/:id/client/:command", (req, res, p, url) => {
  const profileId = url.searchParams.get("profileId") || "athena";
  const rvn = parseInt(url.searchParams.get("rvn") ?? "-1", 10);
  let body = {};
  try {
    body = req.rawBody.length ? JSON.parse(req.rawBody.toString("utf8")) : {};
  } catch {}
  log(`[MCP] ${p.command} profile=${profileId} rvn=${rvn} body=${JSON.stringify(body).slice(0, 500)}`);
  json(res, 200, mcp.handle(p.command, profileId, body, rvn));
});
route("POST", "/fortnite/api/game/v2/profile/:id/dedicated_server/:command", (req, res, p, url) => {
  const profileId = url.searchParams.get("profileId") || "athena";
  json(res, 200, mcp.handle(p.command, profileId, {}, -1));
});

// ---- friends / party / presence (empty but well-formed; single local player)
route("GET", "/friends/api/public/friends/:id", (req, res) => json(res, 200, []));
route("GET", "/friends/api/v1/:id/summary", (req, res) =>
  json(res, 200, { friends: [], incoming: [], outgoing: [], suggested: [], blocklist: [], settings: { acceptInvites: "public" } })
);
route("GET", "/friends/api/public/blocklist/:id", (req, res) => json(res, 200, { blockedUsers: [] }));
route("GET", "/friends/api/v1/:id/blocklist", (req, res) => json(res, 200, { blockedUsers: [] }));
route("GET", "/socialban/api/public/v1/:id", (req, res) => json(res, 200, { bans: [], warnings: [] }));
route("GET", "/friends/api/v1/:id/settings", (req, res) => json(res, 200, { acceptInvites: "public" }));
route("GET", "/friends/api/public/list/fortnite/:id/recentPlayers", (req, res) => json(res, 200, []));
route("GET", "/friends/api/v1/:id/recent/fortnite", (req, res) => json(res, 200, []));
route("GET", "/party/api/v1/Fortnite/user/:id", (req, res) =>
  json(res, 200, { current: [], pending: [], invites: [], pings: [] })
);
route("GET", "/presence/api/v1/*", (req, res) => json(res, 200, []));
route("POST", "/api/v1/user/setting", (req, res) => json(res, 200, []));
route("GET", "/content-controls/:id", (req, res) => noContent(res));

// ---- matchmaking (Battle Lab session creation)
route("GET", "/fortnite/api/game/v2/matchmakingservice/ticket/player/:id", (req, res, p, url) => {
  const bucket = url.searchParams.get("bucketId") || "";
  log(`[BattleLab] Matchmaking ticket requested bucketId=${bucket}`);
  json(res, 200, {
    serviceUrl: `ws://${HOST}:${cfg.matchmakerPort}`,
    ticketType: "mms-player",
    payload: Buffer.from(JSON.stringify({ bucketId: bucket, playlist: cfg.playlist })).toString("base64"),
    signature: "battlelab-local",
  });
});
route("GET", "/fortnite/api/matchmaking/session/findPlayer/:id", (req, res) => json(res, 200, []));
route("GET", "/fortnite/api/matchmaking/session/:sessionId", (req, res, p) => {
  const s = sessions.get(p.sessionId) || newSession(p.sessionId);
  log(`[BattleLab] Session lookup ${p.sessionId} -> ${s.serverAddress}:${s.serverPort}`);
  json(res, 200, s);
});
route("POST", "/fortnite/api/matchmaking/session/:sessionId/join", (req, res) => noContent(res));
route("POST", "/fortnite/api/matchmaking/session/matchMakingRequest", (req, res) => json(res, 200, []));
// Per-session network encryption key. The client queries it after session lookup (without it:
// FailedToQueryEncryptionKey); the host queries the same session, so both sides get the same key.
route("GET", "/fortnite/api/game/v2/matchmaking/account/:id/session/:sessionId", (req, res, p) => {
  const s = sessions.get(p.sessionId) || newSession(p.sessionId);
  log(`[Session] Encryption key requested for ${p.sessionId} by ${p.id}`);
  json(res, 200, { accountId: p.id, sessionId: p.sessionId, key: s.encryptionKey });
});

// ---- local Battle Lab game host (Native/bin/BattleLabLauncher.exe --host)
const { spawn } = require("child_process");
const LAUNCHER = path.join(__dirname, "..", "Native", "bin", "BattleLabLauncher.exe");
const host = { state: "stopped", proc: null, pid: null, playlist: null, waiters: [] };
function settleHostWaiters(err) {
  const w = host.waiters;
  host.waiters = [];
  w.forEach(({ resolve, reject }) => (err ? reject(err) : resolve()));
}
function startHost() {
  if (!fs.existsSync(LAUNCHER)) throw new Error("BattleLabLauncher.exe is missing; run Native\\build.bat");
  host.state = "starting";
  host.pid = null;
  log(`[BattleLabServer] Starting local Battle Lab host (${serverCfg.hostVisible ? "windowed" : "headless"})`);
  const args = ["--host"].concat(serverCfg.hostVisible ? ["--visible"] : []);
  const proc = spawn(LAUNCHER, args, { cwd: path.dirname(LAUNCHER), windowsHide: true });
  host.proc = proc;
  let pending = "";
  proc.stdout.on("data", (d) => {
    pending += d.toString("utf8");
    let nl;
    while ((nl = pending.indexOf("\n")) >= 0) {
      const line = pending.slice(0, nl).trim();
      pending = pending.slice(nl + 1);
      if (!line) continue;
      const m = line.match(/^HOSTPID=(\d+)/);
      if (m) host.pid = Number(m[1]);
      log(line.startsWith("[") ? line : `[BattleLabServer] ${line}`);
    }
  });
  proc.on("exit", (code) => {
    log(`[BattleLabServer] Host launcher exited (code ${code}); host is no longer available`);
    if (host.proc === proc) {
      host.state = "stopped";
      host.proc = null;
      host.pid = null;
      settleHostWaiters(new Error("host exited before becoming ready"));
    }
  });
}
function hostAlive() {
  if (!host.pid) return false;
  try {
    process.kill(host.pid, 0);
    return true;
  } catch {
    return false;
  }
}
// Resolves once a host has reported READY (POST /battlelab/host/ready); starts one if needed.
function ensureHost() {
  if (host.state === "ready" && hostAlive()) return Promise.resolve();
  if (host.state === "ready") host.state = "stopped";
  if (host.state === "stopped") {
    if (!serverCfg.battleLabAutoStart) return Promise.reject(new Error("no host running and battleLabAutoStart is off"));
    startHost();
  }
  return new Promise((resolve, reject) => {
    const waiter = { resolve, reject };
    host.waiters.push(waiter);
    setTimeout(() => {
      const i = host.waiters.indexOf(waiter);
      if (i >= 0) {
        host.waiters.splice(i, 1);
        reject(new Error(`host not ready after ${serverCfg.hostStartTimeoutSec}s`));
      }
    }, serverCfg.hostStartTimeoutSec * 1000);
  });
}
route("POST", "/battlelab/host/ready", (req, res, p, url) => {
  host.state = "ready";
  host.pid = Number(url.searchParams.get("pid")) || host.pid;
  host.playlist = url.searchParams.get("playlist");
  log(`[BattleLabServer] READY pid=${host.pid} port=${url.searchParams.get("port")} playlist=${host.playlist}`);
  settleHostWaiters();
  noContent(res);
});
// Read-only view of the saved Locker for the game host (which runs without MCP profiles).
// Plain text, one "Slot=/Game/Path/Asset.Asset" per line, so the native host needs no JSON parser.
const COSMETIC_PATHS = (() => {
  try {
    const items = JSON.parse(fs.readFileSync(path.join(__dirname, "data", "cosmetics.json"), "utf8")).items;
    return new Map(items.map((i) => [i.templateId.toLowerCase(), i.path]));
  } catch {
    return new Map();
  }
})();
const LOADOUT_SLOTS = ["Character", "Backpack", "Pickaxe", "Glider", "SkyDiveContrail"];
function ownLoadoutLines() {
  const lines = [];
  try {
    const athena = JSON.parse(fs.readFileSync(path.join(__dirname, "data", "profiles", "athena.json"), "utf8"));
    const slots = athena.items?.sandbox_loadout?.attributes?.locker_slots_data?.slots || {};
    for (const slot of LOADOUT_SLOTS) {
      const templateId = (slots[slot]?.items || [])[0];
      const assetPath = templateId && COSMETIC_PATHS.get(templateId.toLowerCase());
      if (assetPath) lines.push(`${slot}=${assetPath}.${assetPath.split("/").pop()}`);
    }
  } catch (e) {
    log(`[Loadout] could not read locker: ${e.message}`);
  }
  return lines;
}
// Friends who joined this host (POST /battlelab/lan/join): lower-case player name -> Locker lines.
const joinedPlayers = new Map();
route("GET", "/battlelab/loadout", (req, res, p, url) => {
  const player = (url.searchParams.get("player") || "").toLowerCase();
  const friend = player && player !== cfg.displayName.toLowerCase() ? joinedPlayers.get(player) : null;
  const lines = friend ? friend.loadout : ownLoadoutLines();
  log(`[Loadout] served ${lines.length} slot(s) to host for ${player || "?"}${friend ? " (friend)" : ""}: ${lines.map((l) => l.split("/").pop()).join(", ")}`);
  res.writeHead(200, { "Content-Type": "text/plain" });
  res.end(lines.join("\n"));
});
// Multiplayer settings for the host module (BattleLabNotify.h GetHostConfig).
route("GET", "/battlelab/hostconfig", (req, res) => {
  res.writeHead(200, { "Content-Type": "text/plain" });
  res.end(`lan=${serverCfg.mode === "host" ? 1 : 0}\ncheats=${serverCfg.cheats === "host" ? "host" : "everyone"}`);
});
route("GET", "/battlelab/host/status", (req, res) =>
  json(res, 200, { state: host.state, pid: host.pid, alive: hostAlive(), playlist: host.playlist, address: serverCfg.gameHost, port: serverCfg.gamePort })
);
route("POST", "/battlelab/host/start", (req, res) => {
  ensureHost().catch((e) => log(`[BattleLabServer] ${e.message}`));
  json(res, 202, { state: host.state });
});

function newSession(id) {
  const s = {
    id,
    ownerId: ACCOUNT,
    ownerName: "[DS]battlelab-local",
    serverName: "[DS]battlelab-local",
    serverAddress: cfg.gameServer.address,
    serverPort: cfg.gameServer.port,
    maxPublicPlayers: 16,
    openPublicPlayers: 15,
    maxPrivatePlayers: 0,
    openPrivatePlayers: 0,
    attributes: {
      REGION_s: "EU",
      GAMEMODE_s: "FORTATHENA",
      ALLOWBROADCASTING_b: true,
      SUBREGION_s: "GB",
      DCID_s: "BATTLELAB-LOCAL",
      tenant_s: "Fortnite",
      MATCHMAKINGPOOL_s: "Any",
      STORMSHIELDDEFENSETYPE_i: 0,
      HOTFIXVERSION_i: 0,
      PLAYLISTNAME_s: cfg.playlist,
      SESSIONKEY_s: crypto.randomUUID().replace(/-/g, ""),
      TENANT_s: "Fortnite",
      BEACONPORT_i: 15009,
      BATTLELABSESSIONOWNERID_s: ACCOUNT,
    },
    publicPlayers: [],
    privatePlayers: [],
    totalPlayers: 1,
    allowJoinInProgress: true,
    shouldAdvertise: false,
    isDedicated: false,
    usesStats: false,
    allowInvites: false,
    usesPresence: false,
    allowJoinViaPresence: true,
    allowJoinViaPresenceFriendsOnly: false,
    // Network-compatible build reported by this client's matchmaking bucket.
    // This differs from the executable's changelist (13137020).
    buildUniqueId: "12895384",
    lastUpdated: new Date().toISOString(),
    started: false,
  };
  // Not part of the Epic session schema; served only by the encryption-key route. Derived from the session id, so a
  // friend's backend (which created the session for its own client) and the host's backend give the same key.
  const encryptionKey = crypto.createHash("sha256").update(`BattleLab-12.50-session:${id}`).digest("base64");
  Object.defineProperty(s, "encryptionKey", { value: encryptionKey, enumerable: false });
  sessions.set(id, s);
  return s;
}

// ---------------------------------------------------------------- HTTP server
const server = http.createServer((req, res) => {
  const chunks = [];
  req.on("data", (c) => chunks.push(c));
  req.on("end", () => {
    req.rawBody = Buffer.concat(chunks);
    const url = new URL(req.url, `http://${HOST}`);
    for (const r of routes) {
      if (r.method !== req.method) continue;
      const m = url.pathname.match(r.rx);
      if (!m) continue;
      const params = Object.fromEntries(r.keys.map((k, i) => [k, decodeURIComponent(m[i + 1])]));
      log(`${req.method} ${req.url}`);
      try {
        return r.fn(req, res, params, url);
      } catch (e) {
        log(`[ERROR] ${req.method} ${req.url}: ${e.stack}`);
        return json(res, 500, { errorCode: "errors.com.battlelab.local.internal", errorMessage: String(e) });
      }
    }
    log(`[UNHANDLED] ${req.method} ${req.url} body=${req.rawBody.toString("utf8").slice(0, 300)}`);
    noContent(res);
  });
});

// ---------------------------------------------------------------- matchmaker (WebSocket)
// Sequence the 12.x client expects: Connecting -> Waiting -> Queued -> SessionAssignment -> Join.
const mm = http.createServer((req, res) => noContent(res));
mm.on("upgrade", (req, socket) => {
  ws.accept(req, socket, (conn) => {
    const ticketId = crypto.randomUUID().replace(/-/g, "");
    const matchId = crypto.randomUUID().replace(/-/g, "");
    const sessionId = crypto.randomUUID().replace(/-/g, "");
    log("[Matchmaking] Battle Lab request received");
    const send = (name, payload) => conn.open && conn.send(JSON.stringify({ payload, name }));
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
    conn.onMessage = (m) => log(`[MM] client message: ${m.slice(0, 300)}`);
    (async () => {
      await sleep(200);
      send("StatusUpdate", { state: "Connecting" });
      await sleep(300);
      send("StatusUpdate", { totalPlayers: 1, connectedPlayers: 1, state: "Waiting" });
      await sleep(300);
      const queued = () =>
        send("StatusUpdate", { ticketId, queuedPlayers: 0, estimatedWaitSec: host.state === "ready" ? 0 : 60, status: {}, state: "Queued" });
      queued();
      // The client stays "in queue" until the host (local, or the friend's in join mode) is really listening.
      const keepAlive = setInterval(queued, 10000);
      try {
        if (serverCfg.mode === "join") await waitForFriendHost(conn);
        else await ensureHost();
      } finally {
        clearInterval(keepAlive);
      }
      if (!conn.open) return log("[Matchmaking] client left the queue before the host was ready");
      log(`[Matchmaking] Assigning server ${cfg.gameServer.address}:${cfg.gameServer.port}`);
      newSession(sessionId);
      log(`[Session] Created local session ${sessionId} (playlist ${cfg.playlist})`);
      send("StatusUpdate", { matchId, state: "SessionAssignment" });
      await sleep(300);
      send("Play", { matchId, sessionId, joinDelaySec: 1 });
    })().catch((e) => {
      log(`[Matchmaking] Battle Lab host unavailable: ${e.message}`);
      conn.close();
    });
  });
});

// ---------------------------------------------------------------- multiplayer (LAN / Hamachi / VPN)
// Join mode: the friend's LAN service says when their Battle Lab is listening; this player's name and Locker are
// registered there first, so their own skin is applied on the friend's host. Polls until the client leaves the queue.
async function lanRequest(method, pathName, body) {
  const r = await fetch(`http://${serverCfg.joinAddress}:${serverCfg.lanPort}${pathName}`, {
    method,
    headers: body ? { "Content-Type": "application/json" } : undefined,
    body: body ? JSON.stringify(body) : undefined,
    signal: AbortSignal.timeout(4000),
  });
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.json();
}
async function waitForFriendHost(conn) {
  let lastProblem = "";
  while (conn.open) {
    try {
      const status = await lanRequest("POST", "/battlelab/lan/join", { name: cfg.displayName, loadout: ownLoadoutLines() });
      if (status.service !== "BattleLabLAN") throw new Error("not a Battle Lab host");
      if (status.ready) {
        cfg.gameServer.port = Number(status.gamePort) || serverCfg.gamePort;
        log(`[Multiplayer] ${serverCfg.joinAddress}: ${status.hostName}'s Battle Lab is ready (${status.players} joined)`);
        return;
      }
      if (lastProblem !== "starting") log(`[Multiplayer] ${serverCfg.joinAddress}: ${status.hostName}'s Battle Lab is starting...`);
      lastProblem = "starting";
    } catch (e) {
      const problem = e.name === "TimeoutError" || e.cause ? "unreachable" : e.message;
      if (problem !== lastProblem)
        log(`[Multiplayer] Cannot reach the host at ${serverCfg.joinAddress}:${serverCfg.lanPort} (${problem}). Is their game running in host mode, and are you on the same LAN/Hamachi network?`);
      lastProblem = problem;
    }
    await new Promise((r) => setTimeout(r, 3000));
  }
}

// Host mode: a separate listener on all interfaces with only these two routes (the main backend stays loopback-only).
// A friend's join registration also starts this PC's Battle Lab host if it is not running yet.
const LOADOUT_LINE = /^(Character|Backpack|Pickaxe|Glider|SkyDiveContrail)=\/Game\/[A-Za-z0-9_\/\.\-]{1,250}$/;
const lan = http.createServer((req, res) => {
  const chunks = [];
  let size = 0;
  req.on("data", (c) => {
    size += c.length;
    if (size <= 16384) chunks.push(c);
  });
  req.on("end", () => {
    const url = new URL(req.url, "http://lan");
    const status = () =>
      json(res, 200, { service: "BattleLabLAN", version: 1, hostName: cfg.displayName, ready: host.state === "ready" && hostAlive(),
        state: host.state, gamePort: serverCfg.gamePort, playlist: cfg.playlist, players: joinedPlayers.size });
    if (req.method === "GET" && url.pathname === "/battlelab/lan/status") return status();
    if (req.method === "POST" && url.pathname === "/battlelab/lan/join" && size <= 16384) {
      let body = {};
      try {
        body = JSON.parse(Buffer.concat(chunks).toString("utf8"));
      } catch {}
      const name = String(body.name || "").trim();
      if (!/^[A-Za-z0-9 _\-\.]{3,16}$/.test(name)) return json(res, 400, { error: "invalid name" });
      const loadout = (Array.isArray(body.loadout) ? body.loadout : []).map(String).filter((l) => LOADOUT_LINE.test(l)).slice(0, LOADOUT_SLOTS.length);
      const key = name.toLowerCase();
      if (!joinedPlayers.has(key)) log(`[Multiplayer] ${name} is joining from ${req.socket.remoteAddress}`);
      joinedPlayers.set(key, { name, loadout, address: req.socket.remoteAddress, seen: Date.now() });
      ensureHost().catch((e) => log(`[BattleLabServer] ${e.message}`));
      return status();
    }
    noContent(res);
  });
});
if (serverCfg.mode === "host") {
  lan.on("error", (e) => log(`[Multiplayer] LAN service failed on port ${serverCfg.lanPort}: ${e.message}`));
  lan.listen(serverCfg.lanPort, "0.0.0.0", () =>
    log(`[Multiplayer] Hosting for friends. LAN service on port ${serverCfg.lanPort}; this PC: ${lanAddresses().join(", ")}`)
  );
}
if (serverCfg.mode === "join") log(`[Multiplayer] Join mode: Battle Lab matches go to ${serverCfg.joinAddress}`);

server.listen(cfg.httpPort, HOST, () => log(`[LocalServer] HTTP listening on http://${HOST}:${cfg.httpPort}`));
mm.listen(cfg.matchmakerPort, HOST, () => log(`[LocalServer] Matchmaker listening on ws://${HOST}:${cfg.matchmakerPort}`));
