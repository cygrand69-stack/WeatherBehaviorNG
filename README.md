# WeatherBehaviorNG

Dynamic seasonal hood, cloak, and scarf behavior for NPCs.

**WeatherBehaviorNG** is a lightweight **SKSE** plugin for Skyrim SE/AE that gives NPCs dynamic weather gear behavior.

NPCs can:
- wear **rain hoods / rain cloaks / rain scarves** in rain
- wear **snow hoods / snow cloaks / snow scarves** in snow
- remove weather gear indoors or in clear weather

Version **2.2.0** uses a **dynamic runtime injection system**:
supported items are discovered through **keywords**, then temporarily added, equipped, unequipped, and removed by the plugin only when needed.

---

## Features

- Dynamic rain and snow gear for NPCs
- Runtime gear injection and cleanup
- Hood, cloak, and scarf support
- Stable per-NPC item selection from supported gear pools
- Stable per-NPC hood / cloak / scarf chance settings
- Stable scarf-vs-cloak selection to reduce visual clipping
- Per-NPC delays for more natural behavior
- Cooldown system to reduce weather flicker
- Distance-aware updates
- Low-conflict design with no direct vanilla NPC edits
- Easy compatibility expansion through per-mod keyword patches

---

## What changed in 2.2.0

Version 2.2.0 expands the 2.0 runtime system with scarf support and more user control.

New in 2.2.0:
- scarf support
- scarf keyword pools
- stable per-NPC hood chance
- stable per-NPC cloak chance
- stable per-NPC scarf chance
- stable scarf-vs-cloak choice to avoid clipping
- improved gear conflict handling using slot mask checks
- improved startup/load safety
- updated FOMOD layout with scarf compatibility support

---

## Requirements

### Core
- Skyrim Special Edition / Anniversary Edition
- SKSE
- CommonLibSSE NG
- Keyword Item Distributor (**KID**)
- Spell Perk Item Distributor (**SPID**)

### Supported gear mods
Install at least one supported hood, cloak, or scarf mod.

Examples:
- Cloaks and Capes
- Cloaks of Skyrim
- Winter Is Coming SSE - Cloaks
- Northborn Fur Hoods
- Simple Vanilla Colored Hoods
- Keeping Warm - Scarves

---

## How it works

### Item side
Supported gear items are tagged with keywords such as:

- `WBNG_Hood_Rain`
- `WBNG_Hood_Snow`
- `WBNG_Cloak_Rain`
- `WBNG_Cloak_Snow`
- `WBNG_Scarf_Rain`
- `WBNG_Scarf_Snow`

These keywords can be added through **KID** patch files.

### Runtime side
At startup, the plugin scans all loaded armor forms and builds keyword-based gear pools:

- rain hoods
- snow hoods
- rain cloaks
- snow cloaks
- rain scarves
- snow scarves

When an NPC is outside in bad weather, the plugin:
1. selects suitable supported gear from the correct seasonal pools
2. temporarily adds it to the NPC inventory
3. equips it

When weather clears or the NPC goes indoors, the plugin:
1. unequips the managed item
2. removes it from inventory again

### NPC-side exclusions and restrictions
**SPID** is used for NPC-side keywords such as:

- `WBNG_ExcludedNPC`
- `WBNG_NoWeatherGear`
- `WBNG_NoRainGear`
- `WBNG_NoSnowGear`
- `WBNG_NoHood`
- `WBNG_NoCloak`
- `WBNG_NoScarf`

This allows clean handling for:
- children
- beggars
- drunks
- beast race cloak restrictions
- other future exclusion rules

---

## Configuration

WeatherBehaviorNG supports INI configuration through:

`Data\SKSE\Plugins\WeatherBehaviorNG.ini`

Current settings include:
- update radius
- weather transition delay
- ticker interval
- indoor unequip timing
- weather equip timing
- clear weather unequip timing
- cloak behavior in combat
- hood chance percent
- cloak chance percent
- scarf chance percent
- scarf-vs-cloak selection chance

### Example
```ini
[General]
fUpdateRadius=6000.0
fWeatherTransitionDelay=3.0
iTickerIntervalMs=500
iHoodChancePercent=100
iCloakChancePercent=100
iScarfChancePercent=35
iScarfInsteadOfCloakChancePercent=25

[Timing]
fInteriorUnequipMin=0.5
fInteriorUnequipMax=1.5
fWeatherEquipMin=2.0
fWeatherEquipMax=4.0
fClearUnequipMin=1.0
fClearUnequipMax=3.0

[Combat]
bDisableCloaksInCombat=true