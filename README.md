# WeatherBehaviorNG

WeatherBehaviorNG is a lightweight SKSE plugin for Skyrim SE/AE that makes NPCs equip seasonal weather gear dynamically.

NPCs equipped with supported items will wear:
- **rain hoods / rain cloaks** in rainy weather
- **snow hoods / snow cloaks** in snowy weather

They remove the gear again in clear weather or when going indoors.

---

## Features

- Dynamic NPC weather gear behavior
- Separate rain and snow gear support
- Indoor / clear-weather removal
- Runtime headgear blocking for hoods
- Slot 46 conflict check for cloaks
- Rain ↔ snow transition handling
- Distance-aware actor updates
- Per-NPC random delays
- Cooldown system to reduce rapid flicker

---

## Requirements

### Runtime
- Skyrim Special Edition / Anniversary Edition
- SKSE
- Spell Perk Item Distributor (SPID)
- Keyword Item Distributor (KID)
- RMB SPIDified - Core Framework / RMB Keyword Framework
- Cloaks and Capes
- Northborn Fur Hoods
- Simple Vanilla Colored Hoods

### Build
- Visual Studio 2022
- CMake
- CommonLibSSE NG

---

## How it works

WeatherBehaviorNG is split into 3 parts:

### 1. SPID
SPID distributes supported hoods and cloaks to NPCs.

### 2. KID
KID tags supported items with seasonal keywords:

- `WBNG_Hood_Rain`
- `WBNG_Hood_Snow`
- `WBNG_Cloak_Rain`
- `WBNG_Cloak_Snow`

### 3. SKSE plugin
The SKSE plugin checks:
- current weather
- whether the NPC is outside
- whether the NPC is already wearing blocking gear

It then equips or unequips the correct seasonal gear at runtime.

---

## Current behavior

### Rain
NPCs can equip:
- rain hood
- rain cloak

### Snow
NPCs can equip:
- snow hood
- snow cloak

### Clear weather / indoors
NPCs remove managed seasonal gear.

### Runtime restrictions
- NPCs wearing blocking headgear are skipped for hood equip
- NPCs wearing another slot 46 item are skipped for cloak equip

---

## Design goals

- Lightweight runtime behavior
- Low conflict
- No heavy vanilla NPC record edits
- Modular setup using existing frameworks
- Easy expansion through SPID/KID configs

---

## Installation

1. Install all runtime requirements.
2. Install the mod files.
3. Make sure the DLL, plugin, SPID config, and KID config are present.
4. Launch the game.

A new game or a clean test save is recommended when changing distribution setups.

---

## Building from source

This project is built with **CommonLibSSE NG**.

Typical workflow:

```bash
cmake -S . -B build
cmake --build build --config Release