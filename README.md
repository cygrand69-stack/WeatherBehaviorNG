# WeatherBehaviorNG

Dynamic seasonal hood and cloak behavior for NPCs.

**WeatherBehaviorNG** is a lightweight **SKSE** plugin for Skyrim SE/AE that gives NPCs dynamic weather gear behavior.

NPCs can:
- wear **rain hoods / rain cloaks** in rain
- wear **snow hoods / snow cloaks** in snow
- remove weather gear indoors or in clear weather

Version **2.0** uses a **dynamic runtime injection system**:
supported items are discovered through **keywords**, then temporarily added, equipped, unequipped, and removed by the plugin only when needed.

---

## Features

- Dynamic rain and snow gear for NPCs
- Runtime gear injection and cleanup
- Stable per-NPC item selection from supported gear pools
- Per-NPC delays for more natural behavior
- Cooldown system to reduce weather flicker
- Distance-aware updates
- Low-conflict design with no direct vanilla NPC edits
- Easy compatibility expansion through per-mod keyword patches

---

## What changed in 2.0

Version 2.0 rewrites the old system.

The previous version relied on distributing seasonal items directly into NPC inventories.

Version 2.0 now:
- scans supported hood and cloak items by keyword
- builds seasonal gear pools at runtime
- temporarily adds and equips the correct gear when needed
- unequips and removes it again when weather clears or the NPC goes indoors

This greatly reduces permanent inventory clutter and makes future compatibility support easier.

---

## Requirements

### Core
- Skyrim Special Edition / Anniversary Edition
- SKSE
- CommonLibSSE NG
- Keyword Item Distributor (**KID**)
- Spell Perk Item Distributor (**SPID**)

### Supported gear mods
Install at least one supported hood/cloak mod.

Examples:
- Cloaks and Capes
- Cloaks of Skyrim
- Winter Is Coming SSE - Cloaks
- Northborn Fur Hoods
- Simple Vanilla Colored Hoods

---

## How it works

### Item side
Supported hood and cloak items are tagged with keywords such as:

- `WBNG_Hood_Rain`
- `WBNG_Hood_Snow`
- `WBNG_Cloak_Rain`
- `WBNG_Cloak_Snow`

These keywords can be added through **KID** patch files.

### Runtime side
At startup, the plugin scans all loaded armor forms and builds keyword-based gear pools:

- rain hoods
- snow hoods
- rain cloaks
- snow cloaks

When an NPC is outside in bad weather, the plugin:
1. selects a suitable item from the correct seasonal pool
2. temporarily adds it to the NPC inventory
3. equips it

When weather clears or the NPC goes indoors, the plugin:
1. unequips the managed item
2. removes it from inventory again

### NPC-side exclusions
**SPID** is used for NPC-side exclusion / control keywords such as:

- `WBNG_ExcludedNPC`
- `WBNG_NoWeatherGear`
- `WBNG_NoRainGear`
- `WBNG_NoSnowGear`
- `WBNG_NoHood`
- `WBNG_NoCloak`

This allows clean handling for:
- children
- beggars
- drunks
- beast race cloak restrictions
- other future exclusion rules

---

## Installation

1. Install all core requirements
2. If you already have **WeatherBehaviorNG 1.x** installed, uninstall the previous version first
3. Install **WeatherBehaviorNG 2.0**
4. Install at least one supported hood/cloak mod
5. Install the optional KID support files for the hood/cloak mods you want to use
6. Optional: install the SPID exclusion file if you want the provided NPC-side exclusions

---

## Optional support files

WeatherBehaviorNG uses **one KID patch per supported mod**.

This lets users install only the support files for the hood/cloak mods they actually use.

Example layout:
- `WBNG_CloaksAndCapes_KID.ini`
- `WBNG_CloaksOfSkyrim_KID.ini`
- `WBNG_WinterIsComing_KID.ini`
- `WBNG_NorthbornFurHoods_KID.ini`
- `WBNG_SimpleVanillaColoredHoods_KID.ini`

---

## Important behavior notes

- Cloaks are skipped if **slot 46** is already occupied
- Khajiit and Argonians can be excluded from cloaks to avoid clipping
- NPCs with helmets equipped will not switch to hoods
- Supported hood and cloak mods must be installed separately
- WeatherBehaviorNG uses supported items from other mods, it does not replace them

---

## Known limitations

- No shelter detection
- No cover-seeking AI
- Cloaks depend on free slot 46
- NPCs with helmets equipped will not switch to hoods
- Wigs are not supported yet
- VR is not tested

---

## Version 2.0.0 changelog

- Rewrote the weather gear system from the ground up
- Weather gear is now dynamically added, equipped, unequipped, and removed at runtime
- Supported hoods and cloaks are now discovered through keyword-based gear pools
- Removed the old permanent seasonal item distribution approach
- Reduced NPC inventory clutter compared to previous versions
- Added stable per-NPC gear selection from available supported items
- Improved support for future compatibility patches through KID item tagging
- Added cleaner NPC exclusion support through SPID keywords
- Improved handling for excluded NPC groups such as children, beggars, and drunks
- Improved beast race cloak restriction handling
- Kept low-conflict design with no direct vanilla NPC edits
- Maintained distance-aware updates, cooldown system, and per-NPC delays

---

## About this mod

I wanted to keep this mod as simple and lightweight as possible.

I am not a coder, so some more advanced features are beyond my current knowledge.

I originally made this mod for myself, then decided to share it with the community.

---

## Inspiration / Acknowledgement

WeatherBehaviorNG is inspired by **Wet and Cold** and its dynamic NPC weather gear concept.

Full credit to **isoku**, the original author of Wet and Cold, for the original idea and groundwork that inspired this project.

This mod is **not** a remake or reupload of Wet and Cold.  
It is a lightweight SKSE-based alternative focused on dynamic hood and cloak behavior for NPCs.

Additional thanks to **TechAngel85** for work associated with **Wet and Cold SE / Wet and Cold - Gear**.

---

## Credits

- **isoku** — Wet and Cold (original inspiration)
- **TechAngel85** — Wet and Cold SE / Wet and Cold - Gear
- **SKSE Team**
- **powerofthree** — Keyword Item Distributor (KID)
- **powerofthree-sasnikol** — Spell Perk Item Distributor (SPID)
- **volvaga0** — Cloaks and Capes
- **Nikinoodles and Nazenn** — Cloaks of Skyrim
- **Nivea** — Winter Is Coming SSE - Cloaks
- **Northborn** — Northborn Fur Hoods
- **JohnTheJohner** — Simple Vanilla Colored Hoods
- **CommonLibSSE NG**

---

## Source

GitHub repository:
https://github.com/cygrand69-stack/WeatherBehaviorNG