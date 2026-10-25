# Current crash analysis

## Build and scope

**CONFIRMED:** supplied source and working-copy executable are 12.50 CL-13137020, not the requested 12.41. Both SHA-256 values are `34A10AF4DDFF20B75D889E269A66644D570C3000F13548A5799D3A7EF690190C`. No proprietary files were downloaded. The working client has an enabled outbound firewall block; backend binds to loopback.

## Startup sequence observed

**CONFIRMED** from `Logs/Test001/FortniteGame.log`: engine initializes Frontend, creates FortGameStateFrontEnd, enters `UI.State.Startup.Login`, then `UOnlineAccountCommon::StartLogin` advances through LoginWaitingRoom, LoggingIn, CheckPlatformPlayAllowed, CheckServiceAvailability, CheckEntitledToPlay, VerifyAgeGateRequirements, DownloadingClientSettings, QueryProfile, QueryKeychain, SilentClaimMfaReward, RedeemOfflinePurchases, CheckingRejoin, DoQosPingTests, StartVoiceChat and CreatingParty.

OhRisk identity succeeds. Party restore reports no existing party. `UFortniteOnlineAccount::LoginStep_CreatingPartyRestoreComplete` requests a new persistent party. That party task requests XMPP; connection fails because no local XMPP route/service exists. **Test009 confirms the existing disconnected branch:** ten seconds later it logs `Failed to be connected in time, yielding success for create`, followed by `LoginStep_CreatingPartyCreateComplete Result=[Succeeded]`. The original client therefore tolerates unavailable XMPP for this login/party step. Battle Lab gameplay remains untested.

## Exact crash mechanism

**CONFIRMED:** the crash is not a dereference of a missing social UObject. It is corruption of the shared memory allocator pointer at executable RVA `0x8195718`.

1. A timer callback at `0x20dac50` executes `add qword ptr [rip+0x060baab3], 0x077192cc` at `0x20dac5a`.
2. That target is the allocator global. `Logs/Test006/module.log` captured the write before permitting the original instruction to continue.
3. The callback immediately performs cleanup through `0x2ba6570`, the shared Free path.
4. That path loads the corrupted global and its vtable, then faults at `0x2ba659b` (`call [rax+0x30]`). Other allocation/free calls on unrelated threads fault through the same receiver.
5. Exception: `0xc0000005`. Fault addresses vary because the receiver is invalid. Actual x64 unwind frames are in `Logs/Test002/module.log`; the earlier stack scans were only candidate return addresses.

## Why the callback is scheduled

**CONFIRMED by local disassembly:** the registering function at `0x20dad90` decodes the string `fltoken=` (encoded literal at `0x602fe60`), parses that command-line argument, and schedules the callback if it is present and its value is not 24 characters. The old launcher explicitly supplied `-fltoken=none` (four characters). If the argument is absent, the branch at `0x20daea9` skips registration.

Omitting the malformed argument avoids this timer but exposes a separate launcher-validation error after login. Test009 traversed XMPP failure and party fallback without the allocator crash. A 24-character local marker subsequently reached the lobby and locker. However, the delayed launcher-validation error still occurs, including with `-noeac -nobe`. These flags are not a verified fix. No game machine-code patch is retained. Validation is recorded in `PROGRESS.md`.

## Rejected experiment

Test007 replaced only the allocator-add instruction in process memory. It caused an earlier, different crash (unknown module, absolute PC reported by Windows Error Reporting), so it failed validation and was completely removed. No on-disk game executable was changed. The write-watch diagnostics are opt-in only.

## Evidence limits

The only preexisting Saved/Crashes minidump belongs to version 37.50, so it is unrelated and was not used for this diagnosis. The global write watch established the corrupting instruction; a hardware-watch attempt was inconclusive because the executable uses its own debug-register events. No inference that XMPP caused the allocator corruption is supported.

## Dependency matrix

| Subsystem | Needed for lobby | Needed for Battle Lab | Evidence |
|---|---|---|---|
| Local identity | CONFIRMED in tested flow | UNKNOWN below frontend | OhRisk logs in before profile and party steps |
| Profile | CONFIRMED in tested flow | LIKELY | common profiles queried; creative save references in existing research |
| XMPP | Not required for completing the observed login/party step | UNKNOWN | Test009: ten-second timeout yields successful local party creation |
| Friends | Queried; nonempty list not required so far | UNKNOWN | Empty local responses accepted through party restore |
| Party | CONFIRMED login step | UNKNOWN for standalone | CreatingParty in UFortniteOnlineAccount |
| Presence | UNKNOWN | UNKNOWN | No successful local party yet |
| Playlist | UNKNOWN for initial lobby | CONFIRMED content dependency | Original Playlist_BattleLab data and mutators exist |
| Session | LIKELY for normal Play route | UNKNOWN for standalone | Local matchmaker points to an as-yet absent game server |
| Athena world | Not needed for initial Frontend map | CONFIRMED | Original Apollo/Battle Lab level references |

`-nomcp` is present in this binary but Test003 still performed login/XMPP. Its existence alone does not prove an offline shipping-client path.
