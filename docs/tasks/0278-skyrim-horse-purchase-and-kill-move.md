# Task 0278: buying a horse, and a first kill move

Design doc: `docs/design/game-packs.md`; follows tasks 0255 (riding), 0267 (paired animations), 0261 (houses).
Goal: the stable sale runs through the game's own scripts and gives a mountable horse; one human-versus-human sword kill move.
Status: built on Windows, local commits on branch `worktree-agent-ae07faac3d9ccfbb6`, not pushed.

## Part A: buying a horse

Where the sale lives (found by scanning Skyrim.esm, not by guessing): quest `stables` (0x68D73), the hostler's
buy topic (INFO links, TCLT), response fragments of the `TIF__` family that call `Actor.AddToFaction(PlayerHorseFaction
0x68D78)`, `PlayerHorse` alias fill and `Game.GetPlayer().RemoveItem(Gold001, price)`.
Why the topic was never offered: every hostler has `JobHostlerFaction` (0x51593) at rank -1, which the dialogue
conditions test as membership. Fixed by adding it to the rank -1 members (`setRankMinusOneMembers`, only with
`riding` on; with `houses` the stewards are in the same list).

What was added (all behind `riding`; no new setting):
- `Riding::setOwnedLookup`: a horse is owned when the test aid names it OR `FollowerState::factionRank(ref,
  PlayerHorseFaction) >= 1` (the faction the game's own fragment sets). The aid `horse test owned` stays as an override.
- Native `ObjectReference.SetFactionOwner` (no-op, logged).
- Hand-over blend: the 7 unit step between the paired mount clip's end pose and the saddle, and the saddle and the
  dismount start, is blended over `kHandoverBlendSeconds` (0.25 s, ours) through `blendOffset()`; the body offset
  (`PlayerBodyState::bodyOffsetX/Y/Z`) is added by `playerbody.cpp`. Mounting stays left-side only.

In game (`tools/skyhorse_check`, Whiterun stables, real clicks): the offer with its price, the purchase (gold 6000 to
5000), the horse mountable with no test aid, three runs with identical traces.

## Part B: first kill move

Setting `[extension.skyrimse] kill moves` (off, declared in `extension.json`, in the test profile) and `kill moves
chance` (0.25, ours). Needs `paired animations`, `combat`.
- Clip: `actors/character/animations/paired_1hmkillmove` (4.5 s). Its frame (`PairedRoot`) is the KILLER's; the victim's
  node is the empty-bone track `2_` at (0, 64.98, 0), yaw pi (provenance F58, `docs/research/0054-provenance.md`).
  `Paired::describeSecond` reads it; `PairedClipInfo` gained trailing `secondStart/secondEnd/hasSecond`.
- Anchor rule: actors cannot be moved by a pack, the player can, so the player is carried to the anchor that puts the
  victim's node where the victim stands (`killmove::anchorFor`); trivial victim skeleton map (the clip's `2_` track
  names equal the humanoid skeleton).
- Trigger: the player's lethal 1H sword blow (WEAP animation type 1) on a living, non-essential, non-creature person,
  no bash, seeded splitmix64 roll per eligible blow (seed fixed in the glue). Both are invulnerable during the clip
  (`applyHit` guard); the victim is held (package off); the death is handed over at the clip's end through the same
  path as a normal death (`changeVital`).
- Clip not readable within 3 s: the victim simply dies.
In game (`n5kill`): the clip plays on both skeletons, the victim dies at the end; three runs, identical trace lines.

## Ranked gaps for the other kill moves
1. Creature victims (bear, wolf, ...): different skeleton, a per-creature `2_` map and their own clip folders
   (`sharedkillmoves/human&bear` etc.); the anchor rule is the same.
2. Other weapon classes (2H, axe, mace, dagger, unarmed): clip names per class are in the animations BSA; the
   sidecar chooses by weapon type; only the table is missing.
3. Victim as the killer (NPC kills the player) and NPC-versus-NPC: needs a way to move actors (placement) or a killer
   anchor that is not the player.
4. First-person camera during the clip, kill cam, blood art.
5. Ragdoll after the clip (the handed-over death plays the death clip, then the ragdoll as usual).
6. Several variants per class (the data has `_short_a` etc.): a seeded pick.

## Tests
New: `testskyrimseriding4.cpp` (owned lookup, handover blend), `testskyrimsekillmove.cpp` (eligibility, roll,
anchor, state machine, hook), `havokpack/testpairedsecond.cpp` (second root; one real-data test gated by
`OPENEDEN_PAIRED_ROOT`). **The tests were written AFTER the code, so there is no red run against stubs**; mutation
checks stand in (see the changelog row). No existing test edited.

## Not run
Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review, a kill move seen in first person, kill moves
with ragdolls on, a horse bought in a hold other than Whiterun, `kill moves` off in game (only argued by the setting
default and the Morrowind frame equality).
