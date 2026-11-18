# Task 0290: actors that kept the rest pose (beast races, Dwarven centurions, the boar riekling)

Status: done for the three groups named by task 0288 item 7; the follow-ups in section 5 are ranked. Windows only, local commit, not pushed. Full loop in form: the tests were run red against stubs (below); the same hand wrote tests and code; no fresh reviewer.

## 1. The cause of each group (log lines and the archive listing)

The log line is `Skyrim actors keep their rest pose: Resource '<path>' not found` (`SkyrimIdle::loadClipAsync`). The listing is `Skyrim - Meshes0.bsa`, `Skyrim - Meshes1.bsa` and `Skyrim - Animations.bsa` (a throw-away BSA 105 lister, aggregates only).

- **Khajiit and Argonian (the beast races).** The race names the skeleton MODEL `actors/character/character assets/skeletonbeast.nif` (female: `character assets female/skeletonbeast_female.nif`). `HavokPack::skeletonHkxPath` derives the skeleton FILE from the model's name, `.../skeletonbeast.hkx`, which does not exist: the folders hold only `skeleton.hkx` and `character assets female/skeleton_female.hkx`. The clips are fine (`actors/character/animations/male|female/mt_idle.hkx` are shared with the humans). So the skeleton read failed and every clip of the actor with it. Same defect for the boar riekling model `skeletonboar.nif` (folder holds `skeleton.hkx`).
- **Dwarven steam and sphere centurions.** The race skeleton is `actors/dwarven{steam,sphere}centurion/character assets/skeleton.nif` and its `skeleton.hkx` exists, but the clips are asked as a person's: `animations/male/mt_idle.hkx`. The machines have NO sex folder and their own names: steam `mtidle`, `mtforward`, `mtfastforward`, `h2hattackleftslash`, `h2hattackforwardpower`, `h2hrecoilleft`, `stagger_light`, `death`; sphere `mt_idle`, `mt_forward`, `mt_fastforward`, `h2h_attackslash`, `h2h_attackforwardpowerchop`, `h2h_recoil`, `combat_staggerlight`, `combat_staggerheavy` (no death file). There was no row in the creature table, so the person rules ran.
- **Solstheim `boarriekling`.** Both causes: the skeleton file name (above) and no row (clips are in `dlc02/boarriekling/animations/` with no sex folder: `mt_idle`, `walkforward`, `runforward`, `attack1`, `attackpowerstanding`, `recoil`, `staggerbacksmall`, `death`). The DLC2 archives are searched already (the paths are in `Skyrim - Meshes0/1`); that guess was wrong.

## 2. The fix

1. `HavokPack::skeletonHkxFallbackPath(nif)` and `chooseSkeletonHkx(nif, exists)` (`components/havokpack/idle.*`, pure): the derived file when it exists, else `skeleton.hkx` (`skeleton_female.hkx` in a folder whose name ends " female") in the model's folder, else the derived path so the error names the file the model asks for. `SkyrimIdle` (`skeletonFileFor`) asks the VFS. No per-actor case.
2. `creatureProfilesFourth()` (`game_packs/skyrimse/src/creatures.cpp`): three rows searched by `creatureProfileFor` after the 18 (a second table: two older tests pin the size of `creatureProfilesAll()` and no test is edited).

Numbers (all from the installed data unless marked):

| Row | walk / run stride in units over seconds | height | radius |
| --- | --- | --- | --- |
| steam centurion | 128 / 1.33333, 256 / 1.33333 (sidecar `DefaultForward`, `DefaultFastForward`, the blocks equal the clips' 1.33333 s) | 305 (steamcenturion.nif z extent) | 76.7 (half the 153 mid-body width) |
| sphere centurion | 192 / 1, 384 / 1 (`MT_Forward`, `MT_FastForward`, linked by name and duration) | 143 | 46.5 (mid-body 93 by 106) |
| boar riekling | 75.55 / 1.06667, 240.4 / 0.533333 (ESTIMATE, below) | 138 (boarriekling.nif) | 40.5 |

The boar has NO sidecar in the installed data (`animationdata/boarproject.txt` does not exist; none of DLC2 has one) and its clips carry no root motion. The strides are an estimate: the front hoof's peak to peak travel along the body over the cycle is 45.3 (walkforward) and 96.2 (runforward) units (`SkyrimCreaturesFourthSurvey.theBoarFootExcursion`), divided by a stance share of 0.6 (walk) and 0.4 (run), both named guesses. `boarproject` names a file that does not exist: the clip meta reader logs that once and the boar has no clip events (no HitFrame) so its blows land only by the generic timing.

## 3. Tests

- `HavokSkeletonFallback` (7 tests, `testskeletonfallback.cpp`): **4 of 7 failed red against the stub** (the 3 that passed pin the controls: derived file wins, nothing exists, no fallback for the plain skeleton).
- `SkyrimCreaturesFourth` (5 tests): **3 of 5 failed red against the stub** (the others test absence); each row's numbers, the siblings not taken, names distinct, the older tables keep their size.
- `SkyrimCreaturesFourthSurvey` (gated by `OPENEDEN_SKYRIMSE_DATA`, 4 tests): prints every clip link, root motion, skin bound, foot excursion and placement the numbers came from.
- `components-tests` filter `SkyrimCreature*:Havok*:SkyrimDragon*:SkyrimClipMeta*:SkyrimSe*` 313 pass. Not run as a whole (the Slang groups take hours).
- Existing tests edited: none. (A first version of my test string lost its backslashes through the shell; fixed.)

## 4. In game (Windows, five masters, fixed frame time, sound 0, quiet window)

Scenario `n5wolf` of `tools/neutral_session_check` through `I:\Projects\OpenEden-work\combat5\g\run.sh` (not committed).

- Khajiit Wujeeta (RiftenHaelgasBunkhouse, base 78722, `actor test walk`): idle, walk and run clips decode (`female/mt_idle`, 99 tracks), no rest pose line, the figure stands and paces (frames viewed). **Three runs: the `N5WOLF` and clip lines are byte-identical.** Argonians use the same skeleton model and the same code path (not framed separately).
- Sphere centurion (Mzulft01, base 1109138): `mt_idle` (2 s, 57 tracks), `mt_forward`, `mt_fastforward` decode.
- Steam centurion (Mzinchaleft02, base 1112505): no clip failure; it stands upright on its plinth (a vision statue with no path: `ai lost (no way to the target)`), so no walking frame.
- Boar riekling (DLC2Benkongerike01, base 67264410): `mt_idle` (6.67 s), `walkforward`, `runforward` decode, `COMBAT creature ... is a boarriekling`, the boar moves in the frames (a dark cave). Three runs: runs 2 and 3 identical; run 1 differs in the boar's positions after the 9th second (the first run after a cold navmesh database; the AI path timing, as in tasks 0197 and 0280).
- Morrowind unchanged: `tools/worldobjects_check` 110 objects OK against the `integ2` build of main (a build of main, not of the exact parent). The fixed Balmora frame hash was NOT compared (no Morrowind path touched: only the Skyrim idle loader and the havokpack pure functions).

## 5. Left, ranked

1. The ordinary **riekling** (`actors/dlc02/riekling`, clips `mt_idle`, `mt_walkf`, `mt_runf`, `skeleton.hkx`, no sidecar) still keeps the rest pose in the same logs: one more row, with strides estimated as above.
2. The **Dwarven spider centurion** (`dwarvenspidercenturion`: its sidecar exists, its clips are not in `Skyrim - Animations.bsa` under that folder, so they live elsewhere; not found) and the Dwarven ballista, netch, scrib, hmdaedra and the other DLC2 creatures have no row.
3. `actors/chaurus/animations/mrh_release.hkx` not found (a creature cast clip), seen once in the sphere run.
4. The boar's events (no sidecar), the strides' stance shares.

Decisions for toddhow: the boar and any later DLC row depend on ESTIMATED strides (they only change foot sliding); the fourth table is separate to leave the two size pins untouched (alternatively the pins could be edited and the rows merged into one table).
