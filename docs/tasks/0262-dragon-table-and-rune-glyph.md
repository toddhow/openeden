# Task 0262: dragon in the creature table, and a rune's glyph goes when the rune does

Design doc section: `docs/tasks/0253-skyrim-dragons.md` (decision 4) and `docs/tasks/0242-skyrim-spell-delivery.md` (limits).
Goal: (A) the dragon joins the shared creature table; (B) a rune's stuck projectile is removed when the rune goes off or ends.
Inherited code touched: none of OpenMW's. `sdk/include/openeden/game/combat_rules.hpp` (one trailing virtual),
`game_packs/skyrimse` combat wrappers and rune code, `apps/openmw/mwphysics/neutralcombatbridge.*`.
Approved by toddhow, 2026-11-16.

## A. Dragons in the creature table: NOT DONE, stopped on a pin

toddhow approved deleting the dragon paths from two tests only
(`SkyrimCreatureTable.creaturesThisSliceDoesNotKnowAndPeopleHaveNoProfile`,
`SkyrimCreatureThirdLookup.creaturesNoRowKnowsAndPeopleStillHaveNoProfile`) and said: any other test that would then
fail, stop and report. Reading the tests showed two more pins that folding the dragon into `creatureProfileFor` and
`creatureProfilesAll()` would break:

- `SkyrimDragonTable.theOlderCreatureLookupStillDoesNotKnowTheDragon` (`testskyrimsedragons.cpp:61`) asserts
  `creatureProfileFor(dragon skeleton) == nullptr` and `creatureProfilesAll().size() == 17`.
- `SkyrimCreatureThirdTable...` (`testskyrimsecreaturesthird.cpp:94`) asserts
  `ASSERT_EQ(creatureProfilesAll().size(), 10u + std::size(kThird))` (17), and the line after it checks every name once.

Nothing was changed for A. What toddhow can decide: (1) approve editing those two lines/tests too (the fold is then
small: move `kDragon` from `dragons.cpp` into `kProfiles` as row 18, drop `dragonProfileFor`'s own table lookup so it
asks `creatureProfileFor` and checks the folder, keep `isDragon` by name; the callers in `appearance.cpp` and
`world_view.cpp` that try `creatureProfileFor` first already work), or (2) leave the dragon where it is.

## B. The rune glyph

`CombatRequestWriter::cancelProjectile(std::uint32_t token)`, the last virtual of the interface, default refuses.
Contract version stays 0.6 (three tests pin minor 6; the change is additive).

- Core (`NeutralCombatBridge::Writer`): finds the token among the stuck projectiles (a `Stuck` entry now keeps its
  token) and removes the drawing at once and forgets its stick timer; failing that, among shots in flight (removes the
  drawing and the shot, never told to the pack); true when one was removed, false for an unknown token, a call outside
  `combatUpdate()`.
- Pack: a `Rune` keeps the token of the projectile that placed it (`result.token` in `deliverAtEnd`). The glyph is
  cancelled when the rune goes off (`tickRunes`), expires, is replaced as the oldest of a full table, or its caster is
  gone. One call per rune; a refusal is ignored (the rune still ends).
- Wrappers: `AiDamageWriter` (combat.hpp), `CrimeWriter` (crime.hpp), `HudWriter` (hud.hpp), `ScriptEventWriter`
  (papyrus_world.hpp) forward it (the comment in `crime.hpp` already says every virtual must be forwarded).
  `cancelCastsOf` (called when a caster is removed) has no writer in hand; its runes' glyphs end by their own stick
  time (600 s) as before.

Tests (new files only): `apps/components_tests/skyrimse/testskyrimseruneglyph.cpp` (`RuneGlyph`, `RuneGlyphWrappers`),
`apps/openmw_tests/mwphysics/testneutralprojectilecancel.cpp` (`CancelFixture`).

## Stage log
(see the end of this file)

### Stage log (2026-11-16)

- Tests first: `RuneGlyph*` 8 of 10 red (2 test absence), `CancelFixture*` 3 of 4 red; then green after the implementation.
- Results: `components-tests` filter `RuneGlyph*:SpellDelivery*:SkyrimCombat*:CombatContract*:CombatMagic*:SkyrimDragon*:SkyrimCreature*:SkyrimCrime*:SkyrimHud*:SkyrimPapyrusNatives*` 328 pass (some gated skips); `openmw-tests` `CancelFixture*:*Projectile*:*Neutral*` 796 pass.
- In game: `OPENEDEN_QUIET_WINDOW=1`, volumes 0, scripts in `I:\Projects\OpenEden-work\bow6\fu` (`run_rune.sh`, `run_dragon.sh`). Fire Rune placed by the real attack key (`summons = true` and `summons test magicka = 1000` give the stand-in the 234 magicka), hostile Hulda released: `rune placed`, `rune set off by actor 108142`, `explosion reached 1`; `keep/screenshot004.png` the glyph on the floor, `keep/screenshot008.png` and the later frames without it. Dragon: `dragons test place = EncDragon01Fire` shows the engage, ground bite, breath, take-off and cruise lines as in task 0253.
- Morrowind: fixed Balmora frame `0d6dd39995b927fa`.
- Not run: Linux gate, clang-format 14, clang-tidy, Lua tests, whole binaries, three-run determinism of the rune scenario, a fresh review.
