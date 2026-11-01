# 0013 - Rendering stack verification (tasks 0025 to 0029)

Date: 2026-11-01 - Status: checks run on Windows (RTX 4070, `build-jolt`), one run per case, frames looked at. The earlier tasks listed these as "not verified"; this note records what was checked now.

Settings combinations: `c1` hdr; `c2` + gbuffer; `c3` + pbr; `c4` + gi; `c5` pbr + ao (no gi); `c6` everything (hdr, bloom, gbuffer, pbr, gi, ao). Scenes: Seyda Neen at noon, at night, a placed NPC and guar, the Caldera guild with a torch, the player in daedric armour in third person.

## Results

| Check | Result |
| --- | --- |
| Every combination `c1` to `c6`, noon, NPC and guild scenes | Frames viewed; no blank frame, no artefact, no error in the log. |
| Antialiasing 0, 4 and 8 with `c2` and `c6` (noon and NPC) | Frames viewed; fine. (The multisample count is in the groundcover parameters' `w`, as the guide says.) |
| Exterior with `gi` on (`c4`, `c6`) | Falls back to the flat ambient: the frame looks like `c3`. |
| Armoured player (daedric cuirass, helm, pauldrons), `c6` | Draws normally. An NPC wearing armour was not placed (the player is an NPC body with the same models). |
| Night with `c6` | Fine. |
| Window resize (1280 x 720 to 1500 x 900, only the game's own titled window touched) with default, `c6` and `c6` + antialiasing 4 | Full 1484 x 861 frames in all three, no blank frame. |
| Inventory with the character preview, and the local map, with default, `c6` and `c6` + antialiasing 4 | Draw correctly (see the note on the test data folder below). |
| Slang sun with `hdr` (and with `c6`) | Starts and draws without errors; the sun disc was not in view, so the sun itself was not looked at. |
| Main menu with `c6`: runs at about 58 frames a second, closes cleanly on a window close message | The engine's screenshot does not capture the menu, so no frame was looked at. |
| Starting a new game through the character generation | Not driven (needs mouse input). `--skip-menu` starts were used throughout. |
| Quick save then quick load (F5, F9 sent to the game's own window) | **Found a crash, fixed**: see below. After the fix the load completes and the game draws, with default settings and with `c6`. |
| `eden-studio.exe` | Still locked by two running editors (started 01:54, not mine), so it was not relinked; a separate exe was linked by hand from the same objects (`eden-studio-v13.exe`). It draws Seyda Neen and the guild (the cell, the navmesh and the markers), with the new G-buffer, pbr, gi, ao and sss code compiled in. |
| `eden-studio-tests`, `components-tests`, `openmw-tests` | 393, 2740 and 799 pass. |

## The crash found and fixed

Loading a save while a game was running aborted the game (exit 0xC0000409 with no crash dump, in every configuration, rendering settings off too). A dump from a small debug-API runner showed the stack: `StateManager::cleanup` clears the world, then the mechanics; `Actors::clear` destroys each `CharacterController`; `detachAnimation` asked for the actor's container store, which by then had no custom data, so a new one was made, filled itself, and threw (`Class::getServices`) inside a destructor. The fix (commit `1d54da2642`) skips the store when the actor has no custom data. Loading a save at start (`--load-savegame`) never had the problem.

## False alarms, for the next person

- The screenshots have an alpha channel that is not 1 in places; a tool that draws them onto a transparent bitmap shows rainbow streaks in the sky. Look at the PNG itself.
- An earlier test data folder (`scratchpad/pbr/openui`) holds an `openeden/materials/test.json` that makes two house models metal; with it, the roofs in the world and on the local map come out black with `pbr` on. Not a bug.
- The weather depends on the real clock, so two runs an hour apart differ in brightness; compare runs made together.

## Not verified

The menu frame, character generation, an NPC (not the player) in armour, the Slang sun disc, other resolutions than 1280 x 720 and 1500 x 900, the real `eden-studio.exe`.
