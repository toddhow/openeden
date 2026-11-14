# Task 0160: placed references, aliases and the last answerable condition functions (milestone M-S4)

Status: built (same hand wrote tests and code, tests AFTER the code and checked by seven mutations, not run red against
stubs; no fresh reviewer; Windows only; local commits on branch `skycond2` from `main`, not pushed).
Design doc section: `docs/design/skyrim-se-implementation-plan.md` M-S4; follows task 0156 (condition functions 3) and
its decisions 1 and 3.

Goal: finish the dialogue conditions the neutral world can answer honestly. Task 0156 left about 2,700 INFO conditions on
OTHER references, the quest aliases that name a reference, and a few functions with no data source. This task adds the
placed-reference table the recommendation named, answers those, and wires the package world.

Inherited code touched: `game_packs/skyrimse` only (`dialogue.*`, `dialogue_runtime.hpp`, `world_view.*`,
`skyrimse_game_pack.cpp`, CMake lists); new `placed_refs.*`. Not touched: `package_brain.*`, `furniture.*`, the mover or
navmesh, anything under `apps/`, `components/`, `sdk/`, `files/`.

## 1. What was built

- **`PlacedRefIndex`** (`placed_refs.*`): for every REFR and ACHR of the load order, `PlacedEntry` (32 bytes): id, base,
  the cell whose children group holds it, enable parent (XESP), position, and flags (initially disabled 0x800, persistent
  0x400, ACHR "starts dead" 0x200, actor, XESP "opposite"). Also CONT inventories (CNTO) and the RELA relationship
  records. Merged over the load order (a later record of an id replaces the earlier one, inherits a missing base or cell,
  a deleted record removes it). Built on the dialogue worker in `buildDialogueData`, next to the other record families.
- **The world** (`SkyrimConditionWorld::setPlacedRefs`, `setPackages`, `setCurrentPackageLookup`): subjects of other
  references are `kPlacedTag | reference id` (a placed reference) and `kBaseTag | base id` (an actor known by its base
  only). Run-on 2 with a reference of the table, with the speaker's own reference (as before), or with 0x14 (now the
  player), and a quest alias that names a reference (one placed subject) or a unique actor (one placed reference: that
  one; several: the base only; the speaker's base: the speaker) resolve to them.
- **Answered on other references (and, where noted, on the speaker):** GetInFaction, GetFactionRank, GetIsSex,
  GetIsRace, HasKeyword (from the base's resolved NPC_), GetIsID, GetIsReference, GetDead (the ACHR flag), GetDisabled
  (the flag, following the enable parent), IsSneaking, GetItemCount (also the speaker's), GetEquipped (also the speaker's),
  GetDistance, GetRelationshipRank, GetIsAliasRef on a placed reference.
- **GetIsCurrentPackage and GetOffersServicesNow** in a conversation: `SkyrimWorldView::setCurrentPackageLookup`; the pack
  gives the package brain's answer (`PackageBrain::view`, nullopt while the actor has not been evaluated), set after
  `enablePackages`. Without the brain they stay unanswered.
- **The package world** (`conditionsPass`, `SkyrimWorldView::setPackageConditionsWired`, default true): it now sees the
  actor's cell (interior flag and location, from the actor's placed reference), the quest scripts' values before any
  script ran, and the placed table. `false` is the world of before.

## 2. How each answer is decided (and what stays unanswered)

| function | answered when | note |
| --- | --- | --- |
| fn_71, 73, 70, 69, 560 | the base is an NPC_ and its template chain reaches no leveled list | a leveled pick is random, so those actors are NOT answered |
| fn_72, 136 | the base is known / the reference is placed | |
| fn_46 | the reference is an ACHR | dead = record flag 0x200 (VERIFIED: the 1,507 ACHR with it are TreasCorpse... bases) |
| fn_35 | the reference is placed | disabled = flag 0x800; an enable parent decides (opposite flag), the player and unknown parents count as enabled (RECALLED-VERIFY); the speaker is never disabled |
| fn_286 | the base is an NPC_ | false: nobody sneaks in a fresh game |
| fn_47 | the inventory is CLOSED: no leveled entry in the CNTO or outfit, and no death item for a corpse | count = CNTO of the resolved NPC_ (or the CONT) plus 1 for each default outfit item; a container's own extra items (none in the five masters' REFR) are not read |
| fn_182 | closed, and the item is in the outfit (true) or not carried at all (false) | an item that is only carried may be auto-equipped: unanswered |
| fn_1 | both references are placed in the SAME cell | the positions are the INITIAL ones (the actor may have walked); another cell, an interior against an exterior and the player (position unknown) are unanswered |
| fn_403 | both are NPC_ bases (or the player) | RELA, either direction; no record means 0. RELA DATA is 16 bytes (VERIFIED 673 of 673); the rank enum (0 lover .. 8 archnemesis, reported 4 .. -4) is RECALLED-VERIFY from xEdit and matches the counts (ally 257, friend 152) |
| fn_161, 255 | the caller's lookup knows the actor | 255 reads PKDT flag bit 0 (RECALLED-VERIFY: the 85 packages that set it are innkeepers, traders and shopkeepers) |
| fn_566 on a placed reference | the alias fill is known | |

NOT answered, with the reason:

- **fn_149 GetIsCurrentWeather**: the engine's weather manager owns the current weather and nothing tells the pack (the
  `DialogueSource` interface has no weather); reachable only with an engine change, which this task may not make.
- **fn_566 on find-matching aliases (3,118)**, location aliases and external aliases: the game decides them at run time.
- **fn_560 HasKeyword 417, fn_72 222**: run-on 1 and 3 (the player's other facts, combat target); **run-on 3 (622)** has no
  subject at all.
- Everything about the player beyond the stand-in (fn_71 on the player: 558 conditions of run-on 1).
- **Leveled actors**: any condition that needs the NPC_ resolved through a leveled template.

Decisions made here (toddhow may overrule): the leveled-template rule above; the "initial positions" rule for GetDistance;
GetEquipped answered only where it is certain; IsSneaking false for everyone; relationship rank 0 when no RELA exists.

## 3. Measurements (five official masters, aggregates; `testskyrimseplacedrefsreal.cpp`, gated)

**The table.** 873,241 reference records read (860,263 REFR + 12,978 ACHR), 871,782 after the merge (12,850 actors, 1,504
start dead, 30,962 initially disabled, 591 containers, 673 relationships), 0 failed, 0 odd subrecords. Parse and merge on
one thread, including reading the five files: **1.9 to 2.6 s standalone; 3.0 s as part of the dialogue worker** (16.6 s in
all for every record family on this machine; a first run while other builds were running took 79 s for the whole worker). **Memory: the
table is 35,029,792 bytes (33 MiB); the working set rose 28 to 62 MiB and 41 to 78 MiB in two runs.** 32 bytes per
reference, plus the by-base list (8 bytes per reference).

**INFO lists (31,675 with conditions, 75,150 conditions).**

| | fully answered | conditions unanswered |
| --- | --- | --- |
| task 0156 as recorded | 22,452 (70.9 percent) | 12,920 |
| 0156 reconstructed in this harness | 22,443 | 12,933 |
| plus the speaker's own state, no table (GetDisabled, IsSneaking, GetItemCount, GetEquipped, the player's reference) | 23,182 | 11,849 |
| **after (table, aliases, relationships)** | **24,882 (78.6 percent)** | **9,716 (12.9 percent)** |

The reconstruction of 0156's world differs from its recorded numbers by 9 lists and 13 conditions (a run-on rule I could
not trace; the harness sets the speaker like 0156's but not identically). By function, unanswered, 0156 to now: fn_46 626
to 98, fn_47 622 to 0, fn_35 287 to 22, fn_182 149 to 0, fn_70 290 to 110, fn_69 150 to 79, fn_403 334 to 196, fn_566 6,061
to 4,928, fn_71 972 to 928 (mostly the player), fn_286 323 to 299, fn_1 240 to 235. fn_161 (227) and fn_255 (176) stay in
this tally because it has no package brain; in a game they are answered for every actor the brain has evaluated. By run-on:
run-on 2 2,791 to 1,063 (about 2,700 in task 0156 decision 1), run-on 5 947 to 736, run-on 0 7,985 to 6,707.

**PACK lists (3,283 with conditions, 5,546 conditions).** The package world as it was in the game (no locations, quest
variables or table; 0156 had left it unwired): 1,810 fully answered, 2,023 conditions unanswered. Wired: **2,480 fully
answered, 1,164 unanswered**. Left: fn_14 229 (no player in that world), fn_362 205 (HasLinkedRef), fn_72 101, fn_1 82,
fn_566 53.

**The Bannered Mare's greeting (8 people).** The same topic and INFO before and after for every one. Infos ahead of the
pick that still hold an unanswered condition: **13 to 0** for Hulda, Saadia, Mikael, Jouane Manette, Orgnar, Ysolda and
Sinmir; Delphine (UniqueVoicesHello) 729 to 371. The greeting was already right; now it is also certain.

## 4. Decision 3 of task 0156: the package world, and what changes for NPCs

Wired as described in section 1 (default on; the whole package brain stays behind `[extension.skyrimse] actor packages`,
off by default). Measured through the real `SkyrimWorldView` (the same `selectPackage` the brain calls), every placed
living actor of the five masters that has packages (10,456), 24 hours of 3 days (a Sunday, a Monday, a Saturday),
wired against not wired: **752,832 samples, 729 changed (0.10 percent), 12 actors affected, none to or from "no package"**.
They change to: VigilantofStendarrSandboxLinkedRef 432 samples, DLC2NelothCastCandlelight 120, DelphineStartInRoom 72,
RiftenAerinSandboxHomeMjollFollower0x0 39, MQEsbernStayInRoomIgnoreCombat 39, DefaultSleepPackageAnyTime 15,
WhiterunHeimskrHomePackage 12. So the wiring is small and the changes are quest-state packages (a quest variable or a
location now holds, as the game's first frame would have it).

## 5. For toddhow

1. Keep the leveled-template rule (conservative: those actors stay unanswered), or pick a deterministic template entry as
   the appearance code does?
2. GetDistance uses initial positions in one cell only. A live position would need the engine to tell the pack where actors
   are (the brain's own estimate exists: `PackageBrain::ActorView::mX`); not wired.
3. The package-world wiring is on by default; `setPackageConditionsWired(false)` restores before.
4. The ACHR dead flag and the PKDT services bit are from the data and names (VERIFIED / RECALLED-VERIFY); xEdit was not
   opened in this session.

## Stage log

2026-11-13, Windows only, branch `skycond2` from `main` at `34609baff1`. Own build tree
`I:\Projects\OpenEden-work\skycond2` (configured from a copy of the skycond configure script, RelWithDebInfo). Python
surveys of the five masters (`I:\Projects\OpenEden-work\skycond2\py`, not committed) settled the flag bits and the RELA
layout before any code.

- **Tests.** `testskyrimseplacedrefs.cpp`, 27 tests: the parse of a generated plugin (flags, positions, nested groups,
  XESP, containers, relations, odd sizes, a cut subrecord), the merged table (override, inheritance, deletion, by-base
  ranges, the enable-parent chain and a loop, relations in both directions and the rank mapping) and the world (every
  function above, the leveled and corpse cases, equipped, distance, relationship, package lookup, the player's reference,
  aliases by reference, by unique actor and not fillable). **Not run red against stubs first** (the code was written
  before its tests); instead seven single-line mutations were built together and six were each caught by a named test:
  the parser's starts-dead flag, the world's dead answer, the enable-opposite rule, the leveled-inventory rule, the
  same-cell rule of GetDistance and the relationship value. The seventh, removing the leveled-list check in
  `actorStable`, failed no test: a leveled template is not an NPC_, so the chain check returns false anyway (an
  equivalent mutant; the check stays as a stated reason). No existing test was edited.
- `components-tests` filtered to `Skyrim*:NifScene*:Conditions*:*Papyrus*:Content*:Compat*`: 1,565 tests, 1,553 pass, 12
  skip (gated on other data), 0 fail, with the gated real-data tests on the installed masters.
- **Not run:** the Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua tests, `openmw-tests`, the engine in
  game (nothing under `apps/` or `components/` changed; the neutral conversation and the package brain were not run in a
  game session, so the pack-side `setCurrentPackageLookup` wiring in `skyrimse_game_pack.cpp` is compiled and
  inspected only), `tools/worldobjects_check` and the Balmora frame comparison (no `openmw` was built; Morrowind is
  untouched by construction), a fresh review.
