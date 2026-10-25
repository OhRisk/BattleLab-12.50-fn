# Loot System (Battle Lab, 12.50)

Decoded from the paks with `Tools/uasset.py` (JSON under `Tools/json/Athena/Playlists/BattleLab/`).

## 1. Selection chain

```
Playlist_BattleLab.LootTierData / LootPackages
   = AthenaCompositeLTD_BattleLab / AthenaCompositeLP_BattleLab      (pre-mutator defaults)
        │
        ▼  replaced at match start by the mutator
BattleLab/Mutators/BattleLabMutator_LootSelection  (option scope of "Loot" + "Experimental")
   LootOverrideDataList[ LootSelection ] →
        Experimental Off → LootTierData / LootPackages
        Experimental On  → ExperimentalLootTierData / ExperimentalLootPackages   (default: On)
```

| `ECustomLootSelection` | UI name | Tables (`BattleLab/LootOverrides/`) |
|---|---|---|
| Default | Default | `AthenaCompositeLTD/LP_Playground` (+`_Exp`) |
| HighExplosives | High Explosives | `…_HE` (+`_Exp`) |
| CloseEncounters | Close Encounters | `…_Shotguns` (+`_Exp`) |
| SolidGold | Solid Gold | `…_SolidGold` (+`_Exp`) |
| WildWest | Wild West | `…_WW` (+`_Exp`) |
| SniperShootout | Sniper Shootout | `…_Snipers` (+`_Exp`) |
| OneShot | *(not offered: blank entry in the option list)* | `…_Low` (+`_Exp`) |

Each loot-package composite is layered as:
`Playground/AthenaLootPackages_Client` + pool override (`OverrideLootPackagesData[_HE]`, except Default)
+ `OverrideLootPackagesDataBackup` **or** `OverrideLootPackagesDataBackup_Exp`.

## 2. What "Experimental" changes

The only difference between each normal/`_Exp` pair is the last layer. `OverrideLootPackagesDataBackup_Exp`
carries exactly six rows, the Bot Grenades:

| Row | Normal | `_Exp` |
|---|---|---|
| `WorldList.AthenaConsumables.50/.51` (floor) | 0.0 | **2.0** |
| `WorldList.AthenaHighConsumables.60/.61` (chests) | 0.0 | **2.0** |
| `WorldList.AthenaSupplyDropConsumables.50/.51` (supply drops) | 0.0 | **2.0** |

Verified in all 7 pools. **With Experimental On (the default), Friendly and Enemy Bot Grenades drop in
every loot pool**, in stacks of 5. Turning Experimental Off removes them.

## 3. Character of each pool (diff against Default)

Derived from weight changes against `AthenaCompositeLP_Playground`. Summaries, not exhaustive: the same item
appears in several rows, so raw diffs list some items as both raised and lowered.

| Pool | Adds / raises | Removes / lowers |
|---|---|---|
| High Explosives | Rocket launchers (C/UC/SR), rocket ammo, higher-rarity ARs/pistols | Frag grenades, some consumables |
| Close Encounters | SR Charge shotgun (BreakBarrel), SR/VR Slug-fire shotguns, Shock grenades | Frag grenades, Explosive bow |
| Solid Gold | Legendary (SR) versions only: tactical/pump shotguns, bolt sniper, SMG, rocket launcher, ARs | Common/uncommon/rare ARs, pistols, shotguns |
| Wild West | Infantry rifles (all rarities), single-action revolvers, TNT | Healing and shields, stone/metal drops, launch pads, fish, standard ARs |
| Sniper Shootout | Bolt-action scoped snipers (all rarities), medkits | Shotguns, most ARs, rocket launchers |
| OneShot (hidden) | Bouncers, knock/shock grenades, small fish, bandages | Shields, medkits, C4, large fish |

## 4. Consequence for the restoration

The authentic way to get Bot Grenades is **the mutator selecting the `_Exp` tables**. That happens by
default whenever the Battle Lab mutators run on the server. The `[AssetHotfix]` in
`LocalServer/hotfixes/DefaultGame.ini` (raising the six rows on `AthenaCompositeLP_BattleLab`) is only a stand-in
for the pre-mutator default table, and becomes redundant once the mutator runs.
