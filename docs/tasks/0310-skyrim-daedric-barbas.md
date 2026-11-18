# Task 0310: Skyrim Daedric quest "A Daedra's Best Friend" (Barbas), first slice

Setting: `[extension.skyrimse] daedric quests` (off by default, declared in `extension.json`, line in `tools/testprofile/README.md`).
The quests are the game's own: DA03Start (0x0D7939, start-game-enabled) and DA03 (0x01BFC4), their dialogue, packages and Papyrus.

## Reached in game (`tools/skydaedric_check/check.sh`, real clicks)
1. Falkreath gate guard's forced greeting (package DA03StartGuardStartQuestGreet) -> Lod (Falkreath) dialogue, chained through unnamed links -> DA03Start stage 10 starts DA03.
2. DA03 stages 5, 7, 10 (fragments run), Barbas is moved near the player (relocation, 7000 units).
3. Barbas's forced greeting on entering his trigger box; talk "You were looking for me?" ... "Sounds easy enough"; stages 15 and 50; journal entry shown.
4. After stage 50 Barbas follows the player: he moved from (-38484,-91469) to (-37461,-91496), 164 units from the player after the player was teleported 1700 units away (his Follow/Escort package names the player).
Not reached: the walk to Falkreath's shrine of Molag Bal and the objectives after it (escort destination is not walked: hand-written, the follow stands in), stages 70 onward, Barbas's later lines, the dog fight/Clavicus Vile scenes.

## Gaps found and fixed
- Package world lacked the player's level (guard greeting reads GetLevel): `setPlayerLevel`.
- Reference with only a primitive (box trigger) and no script was not materialised: `setPrimitiveTriggers`.
- OnTriggerEnter went only to scripts on the reference; Barbas's trigger script sits on the quest alias holding it: `aliasInstancesHolding`.
- Follow/Escort packages whose target is the player (0x14) were run as Stand: `PackageBrain::mPackageFollow`, `setFollowPlayerPackages`.
- Duplicate continuation text in a forced conversation after task 0304's `followNamelessLinks`: forced (link) mode follows once.
- Box volumes accepted for `daedric`, relocation enabled (`enableDaedric`).

## Unification with task 0304
My own chain continuation (`continueChain`) is removed; ordinary conversations use 0304's `followNamelessLinks` + `mFragmentFlush` (enabled by `setQuestGreetings(true)` / `setFragmentFlush` in the `daedric quests` block). One mechanism.

## Tests (written first, red against stubs)
- `testskyrimsedaedriccontinue.cpp` (3; 2 of 3 red against the runtime without the follow, the third (forced, single continuation) red against the duplicate).
- `testskyrimsedaedricfollow.cpp` (5; 3 of 5 red against the old brain, the others pin refusals).
- `testskyrimsedaedricreal.cpp` (gated, data facts).
- `a_trigger_volume_wakes_the_alias_script_...` in `testskyrimsepapyrushost.cpp` (red first).
No existing test was edited.

## Decisions for toddhow
- Escort to the shrine is replaced by "follow the player" (the destination is 20 km away).
- Barbas is relocated by making a new reference (task 0268's mechanism, within 7000 units).
- Order of conversation fragments: run before the next choices via the flush hook (0304).

## Not run
Linux gate, clang-format 14, clang-tidy, Lua tests, a fresh review, stages after 50.
