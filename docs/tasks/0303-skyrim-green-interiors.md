# Task 0303: Skyrim interiors tinted green (Dawnguard HQ, Temple of Miraak)

Follow-up of `0297-skyrim-world-sweep-2.md`, finding 8. Same hand wrote tests and code; no fresh reviewer. Windows only. Not pushed.

## Cause (found with the records, then confirmed in frames)

A Skyrim interior cell's XCLL (92 bytes) ends with **inherit flags** (offset 88), and the cell names a **lighting template** (`LTMP`, a `LGTM` record). A value whose flag is set is taken from the template; the cell's own stored value is stale. The pack read only the first 20 bytes of XCLL and ignored `LTMP` and `LGTM`, so it drew the stale values.

- `DLC1DawnguardHQ01`: own ambient (98, 238, 143) (bright green), inherit word 0x799 sets the Ambient bit; template `AlikrBaseLightingTemplate` (0x94E50) has ambient (41, 41, 33). Before: solid green walls. After: stone walls.
- `DLC2TempleOfMiraak01`: own ambient (32, 38, 33) and fog (23, 33, 22), inherit word 0x7FF (everything); template `BleakFallsBarrowMedium` (0x3128C): ambient (66, 58, 40), fog (75, 99, 97). Before: dark green. After: teal grey. **The "void" itself is not lighting:** the sweep's spawn point has no floor under it and the player falls out of the cell (z about -44,000); the frame is the fog colour on empty space. Not investigated further (collision or the spawn heuristic).
- `WhiterunBanneredMare`: own ambient (14, 13, 12), template (57, 53, 49), Ambient bit set: it is now brighter (the data says so).

Of 886 interior cells with a 92 byte XCLL and a resolvable LTMP, 758 set the Ambient bit, so many interiors change.

## Change

- `records.*`: `CellLightingData::mInherit` (XCLL offset 88, only when the record is 92 bytes), `CellRecord::mLightingTemplate` (LTMP), `ModelRecord::mLighting` for LGTM records (DATA, same 20 byte prefix), `LightingInherit` bit constants (Ambient 1, Directional 2, FogColor 4, FogNear 8, FogFar 0x10).
- `neutral_world.*`: pure `applyLightingTemplate(own, template*)`.
- `world_view.*`: the cell entry keeps the latest LTMP; `loadCell` applies the template before `makeCellLighting`.
- `apps/openmw/mwworld/neutralcell.cpp`: one `NLIGHT <cell> ambient r,g,b directional ... fog ...` Info line with the colours the cell really uses (a trace for comparing runs).
- No new setting: it is part of reading Skyrim's interior lighting (`[Game] neutral world`). A 64 byte XCLL has no flags and is unchanged. Morrowind never reaches this code.

## Tests (new file `apps/components_tests/skyrimse/testskyrimselightingtemplate.cpp`)

8 tests: the pure rule (none, no flags, each flag alone, unused flags ignored) and generated plugins through the view (all inherited, ambient only, fog only, no flags, 64 byte XCLL, missing template, no LTMP). **Red against stubs (header and stub function, no parsing): 4 of 8 failed**; the other 4 pass by nature (no template or no flags changes nothing), and the 64 byte case of the view test could not build its own record (test fault, fixed). Green after: 8 of 8; `*Skyrim*:*Neutral*:*Papyrus*` 3,468 pass. No existing test edited.

## In game

`I:\Projects\OpenEden-work\sweep2\keep303\` (`hq_`, `miraak_`, `mare_` before and after; before = the sweep's build). Three runs of the HQ, Mare and Miraak sequence: the `NLIGHT` and arrival lines are identical (md5 equal); the Mare frame is byte-identical, the HQ frame differs by torch flicker. Morrowind: `tools/worldobjects_check` OK (110 objects) and the Balmora frame `1c5d47ad0b5dc32d` for the reference (integ2) and this build.

## Not run

The Linux gate, clang-format 14, clang-tidy, the Lua tests, `openmw-tests`, a fresh review, other lighting-template fields (directional rotation, fade, clip distance, fog power and max, ambient cube), weather or location lighting for interiors, the Miraak floor.

## For toddhow

- The inherit flag bit order is RECALLED-VERIFY (xEdit's definition); the data and the frames agree for Ambient (HQ). Directional, fog colour and fog distances follow the same order and are not seen separately in a frame.
- Interiors that inherit ambient get brighter or different (Mare is lighter). This is the data's intent but changes many frames.
