# Task: Neutral world, stage N5-0, Skyrim SE actor and appearance record decoding

Status: **BUILT on Windows** (local commits on branch `n5actors`, not pushed). Authorised by toddhow's standing approval ("go with your recommendations") for stage N5-0 of draft decision 0017.
Design doc section: `docs/decisions/0017-neutral-actors-and-items.md` (N5-0 row); `docs/design/skyrim-se-implementation-plan.md` sections 4.11 and 4.17
Goal: the Skyrim SE pack decodes what an actor's appearance needs (NPC_, RACE, ARMA, ARMO's armature list and slot mask, OTFT, HDPT, CLAS) into its own plain types, with NPC template and leveled list resolution helpers. No engine, SDK or core change.
Inherited code touched: none. `game_packs/skyrimse/CMakeLists.txt` and `apps/components_tests/CMakeLists.txt` list the new files (3 lines each). `records.*` is not edited (another agent is changing it): the new parser walks the plugin itself.
Inputs / outputs: new `game_packs/skyrimse/include/game_skyrimse/actors.hpp`, `src/actors.cpp`; tests `apps/components_tests/skyrimse/testskyrimseactors.cpp` (37 tests, generated plugins) and `testskyrimseactorsreal.cpp` (gated); `tools/skyrimse_actors_check/check.sh`.
Constraints: nothing throws; every list and string is capped (`ActorLimits`); a wrong-sized subrecord is counted in `mOddBy` and decodes to nothing; the same plugin parsed twice gives the same result; no game text is committed.
Acceptance criteria:
- [x] Tests written first on generated records and run red against a stub (30 of 31 then-written tests failed; the one that passed tests absence).
- [x] All NPC_ records of the five masters decode (6,525 of 6,525, 0 failed, 0 odd subrecords, 0 unresolved ids).
- [x] Template chains resolve with no cycles (57,249 resolutions over 3 levels and 3 picks: all Ok).
- [x] Every NPC's race has a skeleton model that exists in the archives (6,361 of 6,361).
- [x] The ARMA models of the default outfits exist in the archives (12,406 of 12,406).
- [x] `CI/check_layer_rules.sh` stays at 104.
Out of scope: engine, SDK and core (N5-1 onward), tint layers, face morph meaning, AI package conditions, string tables, the Linux gate.

## What was decoded

NPC_ (`ActorNpc`): EDID, FULL, RNAM race, CNAM class, VTCK, TPLT, ACBS (flags, level, calc range, template flags at offset 18), DNAM (stats), AIDT, SNAM factions, CNTO inventory, PKID packages, DPLT, KWDA, SPLO, PNAM head parts, WNAM skin, ANAM, INAM, CRIF, ZNAM, FTST, HCLF, NAM6 height, NAM7 weight, QNAM skin tone, NAM9 face morphs, NAMA face parts, TINI layer count, DOFT and SOFT outfits. RACE (`RaceRecord`): skeleton per sex, body (NAM1), behaviour graph (NAM3) and head (NAM0) sections, skin, voice types, decapitate armor, default face textures, available hair colours, face texture sets, BOD2, DATA heights, weights and flags. ARMA (`ArmatureRecord`), ARMO extras (`ArmorExtra`), OTFT, HDPT, CLAS. `ActorIndex` merges plugins (later wins, deleted removes), `resolve` follows TPLT by the template use flags (an LVLN template is picked by level and a caller-supplied number), `pickLeveled`, and `wornModels` (an outfit to the models of the first fitting addon of each armor for a race and sex).

## Settled by the data (five masters; aggregates)

| Fact | Evidence |
| --- | --- |
| NPC_ ACBS 24 bytes; AIDT 20; DNAM 52; SNAM 8 (faction, rank i8, 3 zero bytes; rank 255 on 791 of 14,927); CNTO 8 (item, count i32, never 0 or negative: 11,900 entries); QNAM 12 (three floats in 0..1, 6,362 of 6,362); NAM9 76; NAMA 16; TINI 2 | size distributions, one size each |
| COCT equals the CNTO count, SPCT the SPLO count, KSIZ the KWDA count, on every NPC | 6,362 of 6,362 each |
| Template use flags are 13 bits | OR of all NPC_ is 0x1FFF |
| "Base Data" (0x80) covers FULL | 3,512 of 3,512 templated NPCs with the bit have no FULL of their own |
| TPLT names an NPC_ (3,957) or an LVLN (658); chains are up to 7 deep, no cycle | template-depth histogram at level 30 |
| References resolve to the expected type, every one: RNAM to RACE, CNAM to CLAS, DOFT and SOFT to OTFT, WNAM to ARMO, PNAM to HDPT, RACE WNAM to ARMO, RACE HEAD to HDPT, ARMA RNAM and MODL to RACE, ARMO MODL to ARMA, OTFT INAM to ARMO or LVLI, HDPT TNAM to TXST | `SKYRIMSE-ACTORS ref` lines, total equal to resolves-to-expected |
| The race skeleton is the ANAM after the MNAM / FNAM markers, before NAM1 | 54 distinct paths, all in the archives; 156 of 161 races have both |
| ARMA BOD2 is 8 bytes and BODT 12 bytes (slots u32 first); ARMA DNAM 12; ARMO BOD2 8 (3,665) or BODT 8 or 12 (10) | sizes |
| ARMA armor type is 2 in every record; ARMO armor type is 0, 1 or 2 (1,177, 1,413, 1,075) | values |
| HDPT PNAM 0 to 6, DATA 1 byte; CLAS DATA 36 bytes, skill weights at most 9, trainer skill at most 18 or 255, max training level at most 100 | values |

## Not settled by the data (RECALLED-VERIFY, marked in the headers)

The names of the template flag bits other than Base Data; which fields each flag covers (the resolver's mapping: Traits = race, skin, head parts; Stats = class; Base Data = name; Inventory = items and default outfit; Factions, Spell list, AI data, AI packages, Keywords) - the data shows flag-set NPCs often lack their own subrecords of the category (for example FTST: 2,223 without and 234 with) but not always, so this is corroboration only; the byte meanings of AIDT, NAMA, ARMA DNAM bytes 4 to 7, the RACE DATA flag word (bit 0 set on 10 of 161 races), CLAS DATA bytes 32 to 34, the armor type names, the "highest eligible level" leveled list rule, and the meaning of ANAM on NPC_.

Absent from RACE in the installed data: no biped object names, and no default hair record (hair comes from the HDPT list and `HCLF`/`AHCM`); the spec's "if present" items are therefore not decoded.

## Stage log

Red run: against a stub `actors.cpp` returning empty results, 30 of the 31 tests then written failed (aggregates in the changelog row); after the implementation 37 tests pass (6 were added or corrected afterwards, see below). Two tests were corrected after they failed for test faults (an id in the unresolved-plugin test, and the truncated-file test asserting records kept, which the walk does not guarantee because the group size is checked first). The Morrowind-file refusal was missing from the first implementation and was added after the red test of the same name.

Measured on the five masters (8.6 s for the whole gated test): 12,406 worn models for 5,252 NPCs with a default outfit; 5,014 outfits give at least one model, the other 238 give none (not broken down; likely outfits of leveled lists with no eligible entry at the checked level, armors with no addon for the race, or empty outfits); 6,295 of 6,484 direct ARMO items found an addon for their wearer's race.

## Not run

The Linux gate and GCC `-Werror` (Docker hung); clang-format 14 (clang-format 23 formatted the new files); clang-tidy; the Lua tests; the whole `components-tests` (only `SkyrimActors*` ran); a fresh review (the same hand wrote the tests and the code); the mod list (only the five masters); `openmw` and the pack target were not built here (the new file is compiled into `components-tests`, and the pack's CMake lists it).
