# Task 0365: Skyrim sessions exit with code 173 (shutdown freeze)

Cause: after "Stopping extensions" the main thread runs `SkyrimGamePack::~SkyrimGamePack` ->
`SkyrimWorldView::~SkyrimWorldView`, which joined `mDialogueThread` (`startDialoguePrefetch` ->
`buildDialogueData`) and `mCombatThread`. Those workers walk every plugin of the five masters (`parsePlacedRefs`,
quest, dialogue, package records ...) and need about 6 s in total; `cancelAndJoin` only covered the index worker. The
window could not answer for over 5 s, so the freeze monitor wrote `openmw-freeze.dmp` and ended the run with 173.

Evidence (dump read with a small dbghelp walker, `sf/tool/ds.cpp` outside the repository): main thread stack
`msvcp140!Thrd_join <- std::thread::join <- SkyrimWorldView::~SkyrimWorldView <- SkyrimGamePack::~SkyrimGamePack <-
LifecycleManager::shutdown <- ExtensionHost::stop <- Game::~Game`; another thread in
`PlacedParser::onRecord <- parsePlacedRefs <- buildDialogueData <- startDialoguePrefetch lambda`.

Fix: `SkyrimWorldView::mStopReaders` is set first in the destructor; `buildDialogueData` and the combat/actor worker
check it per plugin, and `Content::Plugin::setCancelFlag` makes a `walk()` end early (no error) inside one big plugin.
The partial results are dropped. Default behaviour with no flag set is unchanged.

Result: before 7 of 7 runs of the Bannered Mare scenario (600 frames) exit 173; after 6 of 6 exit 0, no dump.
Tests: `ContentPluginCancel` (2). Red run of the first test against the walk without the check was built but its
output was not captured (not shown red). Not run: Linux gate, clang-format 14, clang-tidy, Lua tests, a unit test of
the destructor (needs game data).
