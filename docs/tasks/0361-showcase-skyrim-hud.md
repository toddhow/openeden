# Task 0361d: the Morrowind HUD and skill messages out of a Skyrim session
Design doc section: docs/research/0149-skyrim-showcase-audit.md, rendering items 6 and 10 (ranked 5 and 14)
Goal: a Skyrim session shows only the Skyrim-style HUD and no Morrowind skill-up message; a clean no-HUD capture is possible.

## Findings
- The HUD part. `HUD::updateReplacedParts` (apps/openmw/mwgui/hud.cpp, policy `replacedHudParts`, tasks 0188 and 0244) already
  hides Morrowind's bars, weapon/spell icons, enemy bar and crosshair in a neutral cell, and with `hud style` + `sneak` + `map`
  also the sneak icon and minimap, but only with `[Game] neutral combat` AND `[GUI] rmlui` (the overlay draws the replacement).
  The showcase harness (tools/showcase_check) ran the testprofile, which has neither `[GUI] rmlui` (a variant: its debug panel) nor
  the pack setting `hud style`/`sneak` (pack settings are not in the profile), so nothing was hidden. Root cause: configuration of
  the harness, not a missing engine rule. Fix: the harness overrides now set `[GUI] rmlui = true`, `rmlui debug panel = false`,
  `hud style = true`, `sneak = true` (`map = true` is in the profile). `SC_NOHUD=1` additionally hides the whole HUD
  (Lua `ui.setHudVisibility(false)` from the harness player script) for clean captures.
- The skill message. Running, jumping, swimming and fighting call `Class::skillUsageSucceeded` of the stub Morrowind NPC player, which
  queues `OnSkillUse` for Lua; the built-in skill scripts raise the skill and show "Your Athletics skill increased to 2" with the
  Morrowind font (`sNotifyMessage39`, task 0212 gave it wording of its own). Fix at the root: `LuaManager::skillUse` returns for
  an actor standing in a neutral cell (the pack has its own progression, `player progression`). Morrowind is untouched
  (a Morrowind cell is never neutral).

## Constraints
Neutral cells only; no setting added; Morrowind bit-identical.

## Evidence
- Before (audit run): `keep/before_market_east.png`, `keep/before_anim_stop_turn.png` (Morrowind bars, hand icon, torch icon, minimap, "Your Athletics skill increased to 2").
- After (same shots, 81 frames of the whiterun group and the Mare group): `keep/after_market_east.png`, `after_run_stop.png`, `after_mare_hud.png`:
  only the pack's bars with the level emblem, the compass strip and the crosshair; no Morrowind icons, no skill message in the walk, run and
  stop shots. `SC_NOHUD=1`: `after_mare_nohud.png` (nothing on screen). All in I:/Projects/OpenEden-work/papyrus/sc61d/keep.
- The "black first-person item" (audit 10): `before_market_east.png` shows two black horn shapes at the bottom; they are NOT a first-person
  item: the tooltip reads "Cow" and they are the horns of a cow behind the fence in shade (actor lighting, audit lighting 4). The
  first-person camera shows no mesh of the stub player. It was not reproduced as a HUD or first-person issue; nothing to remove.
- Not covered: a unit test for the `LuaManager::skillUse` guard (it needs a live Lua manager; shown in game), the Linux gate.
