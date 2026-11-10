# Research note 0051: audit of upstream OpenMW's ESM changes since OpenEden's base

Status: DRAFT, read-only audit (2026-11-10). Nothing merged, no branch or work tree touched. Requested by toddhow (recent commits to the ESM code in upstream OpenMW).

## 1. What was done and what is verified

- `git remote add openmw-upstream https://gitlab.com/OpenMW/openmw.git` (a remote only, never pushed to) and `git fetch openmw-upstream master` into `refs/remotes/openmw-upstream/master`. The fetch worked. Upstream tip: `97722d48f9` (2026-11-09, merge of 'html_fix').
- Local `master` (`4e718b500f`, 2026-10-23) is an ancestor of upstream master, so it can be fast-forwarded later (not done here).
- **Merge base of `main` and upstream master: `a035bcad2a` (2026-03-14)**, not the 442eec6e2f of the guide (that is OpenEden's docs commit of 2026-10-28; the code base is OpenMW as of March 2026). Upstream is **1,291 non-merge commits (1,736 with merges) ahead**.
- Upstream commits touching `components/esm*`, `apps/esmtool`, `apps/essimporter`, `esmstore`, `esmloader`: 98 non-merge commits since the base (section 3). All statements about upstream code were read from `openmw-upstream/master` with `git show` / `git grep`.
- Merge difficulty was measured with `git merge-tree --write-tree` (read-only; it writes only loose objects): section 5.
- Not done: any build, test or run; any check of upstream behaviour beyond reading code.

## 2. Headline

1. The recent ESM churn (26 Oct to 5 Nov 2026, author Aussiemon) is **ESM4 work for Fallout 3/NV, Fallout 4 and Skyrim**: a `GameFormat` enum (TES3, TES4, FO3, FNV, TES5, FO4) assigned per plugin, reader size checks, navmesh logic from xEdit definitions, compressed-record fixes. It is Skyrim-relevant as evidence and as a decision table, but **it cannot be cherry-picked**: 19 of the 36 commits of that series conflict when picked onto `main` in order (section 5).
2. The rest is a long Morrowind-side refactor (ESM::Path model fields by elsid, RefIds for skills/attributes/weather, `openmw.content` additions, custom skills and attributes by Evil Eye, save-format changes). These touch files our `esm` split and `esmbridge` already moved.
3. A full merge is a big task: **385 conflicts** (201 content, 184 modify/delete; the deletes are renderer files our Vulkan move removed), only about 8 of them in ESM files.

## 3. Upstream commits (since `a035bcad2a`) touching the ESM area

Classes: **S** Skyrim/ESM4 format knowledge; **B** bug fix in a shared loader; **R** refactor/move; **F** new feature (Morrowind side); **N** not ESM content. Author is Aussiemon unless named.

### 3.1 ESM4 / game format series (Oct 25 to Nov 5)

| Commit | Date | What it does | Class |
| --- | --- | --- | --- |
| 74b6d284b0 | 10-25 | Compressed record: `dataSize = uncompressedSize`, not minus the 4-byte prefix (upstream reader bug; our `Content::Plugin` strips the prefix itself) | B/S |
| 568b83e4d5 | 10-26 | `ESM::VER_171` handled (Skyrim SE header version 1.71) | S |
| b9e8cce3d1, a944616555, 418d0edf56 | 10-26 | Plugin gets a `GameFormat`; ESM4 conditions use it instead of ESM-version tests | S/R |
| 8d2177fe92 | 10-26 | Fixes a transposed condition in `readerutils.hpp` (`hasMoreRecs` before `getRecordHeader`) | B |
| 37d78bcae7 | 10-26 | Skips 3 unknown FO4 subrecords | S |
| bfebf6dba4, c2f781d776, d249e80b21 | 10-28 | Distant mesh patterns per game, uninitialised versions, gender fallback in newer games | S/B |
| 7360e367c1, 983c1e8ad2, 5dc2e29984 | 10-30 | Game format from previous masters; ambiguous main files; operators for the enum | S |
| 7150de6a73, 48b8fe1f2a, 618263ddd9, 378efd8caa, b118a7879b, 1d1b60125b, ce83a6c573 (11-05) | 10-30 | NAVM/NAVI per game from xEdit definitions (TES4 has no NVMI; TES5/FO4 read correctly; the NVPP hack is wrong for most games; FO3 NVMI flag) | S |
| e272f484f2, 03082e6515, 2d700ed79f, 5973c82453, aade43e2a0 | 10-30 | Unhandled VMAD (script tree), PNAM in IDLM, MODC in ARMO; EYES stored; FO4 activation text | S |
| d35a08c8db, 03a4501197 | 10-30, 11-03 | Fuzzer finding: ESM4 reader size checks (compressed record too small, larger than the file, inflated size over 1032 times the record size) | B/S |
| 2300a70589, fc64000f2b, 74d68401e7, 94be92c9e2, fd5a37aa65, cda8f4bb9a, d5f4fc8dcb, 9889b225b1 | 10-30..11-05 | NPC and other records hold one `mGameFormat`; header `components/esm/gameformat.hpp`; masters as `span`; FONV renamed FNV | R/S |

### 3.2 Shared-loader and save-format changes (Morrowind side, mostly)

- elsid, 06-07..07-11: `ESM::Path` for every record's `mModel`/`mIcon` (about 25 commits; `components/esm/path.hpp` is new), also for ESM4 models (deb2e14f0a, c0511c1aee). Conflicts with every `components/esm3/load*.hpp` we touched. R.
- Evil Eye, 07-15..09-12: RefIds for attributes, skills, weather, weapon types (f3a8561d66, bff4496c07, 0051575225, 668ef5b9ed); custom skills/attributes saved (a4091dd052); `openmw.content` for books, races, factions, attributes, skills (F); `Faction::mFlags` (399be8c40f); `Class::mIsPlayable` (1c7b21d164); NAM9 garbage (0fff67d8b7); custom weathers saved (5105dd8713). Several change save records; nox7's 021234fa35 bumps the save format version (new projectile state).
- Bret Curtis 09-12: fog of war decoded in the consumer (b3da1c7387, 6d0d296e66). Aussiemon 07-05..07-10: ESS import of MWSE scripts (c086431137, e4a565f22e), `Cell.PRDT` skipped (51c5c50fd4), werewolf kill count reverted.
- blagish 11-08/11-09: initialise `CellState` and other fields (85805434f7, 1ec89a5e7c). B.
- Build: AnyOldName3 (7ca3f9959c, 3dfebe8681, 78e45a1d5b) CMake 3.21 and install rules. N.
- Full list: `git log --no-merges a035bcad2a..openmw-upstream/master -- components/esm components/esm3 components/esm4 components/esmloader apps/esmtool apps/essimporter apps/openmw/mwworld/esmstore.cpp apps/openmw/mwworld/esmstore.hpp`.

## 4. Evidence for our Skyrim SE model (`game_packs/skyrimse/src/records.cpp`, `include/game_skyrimse/records.hpp`, `components/contentsource/plugin.cpp`)

**Limit.** OpenMW's ESM4 code is cc9cii's reader, itself written from UESP/TES5Edit. This is corroboration from a second reader, **not independent proof**. It does not retire a marker for a value our own tool already measured on installed data, and where upstream skips a field it says nothing. Source: upstream master `components/esm4/*`.

| Our item | Upstream (file, function) | Verdict |
| --- | --- | --- |
| `NpcFlags` (`records.hpp`): Female 0x1, Essential 0x2, Respawn 0x8, AutoCalc 0x10, Unique 0x20, PlayerLevelMult 0x80, Protected 0x800, Summonable 0x4000, Invulnerable 0x80000000 | `loadnpc.hpp` `ActorBaseFlagsTes5` | **Confirms all nine values and names** |
| ACBS offsets flags@0, level@8, calcMin@10, calcMax@12, size 24 (`records.cpp`, NPC_ `case ACBS`) | `actor.hpp` `ACBS_TES5` (flags u32, magicka u16, stamina u16, levelOrMult u16, calcMin u16, calcMax u16, speed u16, disposition u16, templateFlags u16, health u16, bleedout u16); `loadnpc.cpp` `case ACBS` accepts 24 bytes | **Confirms offsets 0, 8, 10, 12 and size 24**; also gives speed@14, disposition@16, template flags@18, health offset@20, bleedout@22 |
| `RecordFlags` Deleted 0x20, Persistent 0x400, InitiallyDisabled 0x800, Ignored 0x1000, VisibleWhenDistant 0x8000 | `common.hpp` `RecordFlag`: Rec_Deleted, Rec_Persistent, Rec_Disabled, Rec_Ignored, Rec_VisDistant with the same values; also Rec_Compressed 0x40000, Rec_Localized 0x80 (TES4 record), Rec_ESM 0x1 | **Confirms the five** (our header comment says only 0x20, 0x400, 0x800 were checked against data) |
| `XESP` formid + flags, size 8; bit0 opposite of parent, bit1 pop in (`parseRefr`, `EnableParent::mFlags`) | `reference.hpp` `EnableParent {parent; u32 flags; Flag_Inversed 0x1; Flag_PopIn 0x2}`; `loadrefr.cpp` `case XESP` | **Confirms both bits.** Upstream reads the flags as a u32; we read byte 4 (same for these bits) |
| REFR `DATA` 24 bytes position + rotation | `loadrefr.cpp` `case DATA` reads 6 floats | Confirms |
| `XTEL` door teleport (not in our model yet) | `loadrefr.cpp` `case XTEL`: 28, 32 (FO3/FNV/TES5) or 36 (FO4) bytes; destination door formid, 24 bytes pos/rot, flags u32 (0x01 no alarm, TES5 only) | **Layout available** (32 bytes for Skyrim) |
| `XOWN` | `loadrefr.cpp`: 4 bytes owner formid; 12 bytes formid + two u32 (FO4 no-crime flag) | Skyrim form is the 4-byte one |
| `XLOC` | `loadrefr.cpp`: lock level int8, 3 pad bytes, key formid, then flag bytes (upstream comments: "very poor code", "flag?") | Level and key positions agree; the flag bytes are **not** established upstream either |
| NPC_ `DNAM` (18 skills, offsets, health, magicka, stamina, size 52) | `loadnpc.cpp`: DNAM is in the skipped-subrecord block | **No evidence.** Keep RECALLED-VERIFY |
| Light master, 12-bit ids (`FormIdResolver`, records.cpp line 128) | grep of `components/esm`, `components/esm4`: no light-plugin handling; `formid.cpp` only throws for a content file above 0xFE | **No evidence.** Keep RECALLED-VERIFY |
| Compressed records (`plugin.cpp` `sCompressedPrefixSize`, `mMaxUncompressed`; `inflateRecord`) | `reader.cpp` after 74b6d284b0 and 03a4501197: 4-byte prefix is the inflated size; checks: record at least 4 bytes, not larger than the rest of the file, inflated size at most 1032 times the compressed size | Layout confirmed. Suggestion: add the 1032:1 bound to `Content::Plugin` (the BSA path already has it) |
| Header version to game (`pack_core`) | `components/esm/gameformat.cpp` `getGameFormat`: 0.94 is ambiguous FO3/TES5, resolved by master names (Skyrim.esm, Fallout3.esm), earlier masters, else by header form version (15 or lower means FO3); 1.70 and 1.71 mean TES5; 0.95, and 1.00 with a form version, mean FO4; 1.32 to 1.34 mean FNV | Cross-check item: confirm `pack_core` accepts 1.70 and 1.71 and resolves 0.94 the same way |
| CELL water level | `loadcell.cpp`: Skyrim has broken water level records (TES5 only) | Useful if a CELL XCLW reader is added |

Net: ACBS layout and flags and the five record flags are now twice-sourced; XESP flag bits are confirmed; DNAM and light-master stay single-sourced.

## 5. Merge difficulty (measured, read-only)

- Overlap: 534 files changed on both sides since the base; 1,172 changed upstream, 4,583 on `main`.
- `git merge-tree --write-tree --name-only main openmw-upstream/master`: **385 conflicts, 201 content and 184 modify/delete** (renderer and test files our Vulkan move deleted). ESM-area conflicts: `apps/essimporter/converter.cpp`, `apps/openmw/mwworld/esmstore.cpp`, `components/esm3/fogstate.cpp`, `loadcrea.hpp`, `loadnpc.hpp`, `loadrace.cpp`, `components/esm4/loadcell.hpp`, `components/esmbridge/attr.hpp` (git followed our rename from `components/esm/attr.hpp`), and `components/esmterrain/storage.*` (deleted in main).
- Files upstream added under `components/esm/` (`gameformat.*`, `path.hpp`) land in the base library; each needs a decision in `CI/layer_rules.txt`.
- Cherry-picking the ESM4/game-format series alone (36 commits, chained with `merge-tree`): 17 apply, **19 conflict** (`apps/openmw/mwclass/esm4npc.cpp`, `mwworld/esmloader.cpp`, `esmtool`, `loadnavi.cpp`, `loadweap.cpp`, deleted renderer files); they sit on the earlier ESM::Path and RefId refactors. A partial pick is not a stable cut.
- Silent-revert risk (the guide, Upstream merges): high. Our `Content::Plugin`, the record-source dispatch in `ESMStore::load`, `EsmLoader`'s header probe, the `esm` split and the VFS archive factories rewrite code upstream also edited. Resolving toward ours could drop upstream's `GameFormat` assignment in the reader or the NPC `mGameFormat`; the audit step must list those lines.
- A full merge also needs save-format version handling (upstream bumped it) and brings 1,291 commits of non-ESM code that this audit did not review.

## 6. Recommendation

1. **Now: port knowledge, no merge.** (a) Add the 1032:1 inflation bound to `Content::Plugin`; (b) add XTEL (32 bytes), XOWN (4) and XLOC (level, key) layouts to `records.cpp`, marked as confirmed by a second reader with section 4's caveat; (c) cross-check header versions 1.70, 1.71 and ambiguous 0.94 in `pack_core`; (d) update the ACBS and record-flag comments in `records.cpp` and `records.hpp`.
2. **Do not cherry-pick** the ESM4 series.
3. **A full merge is possible but is a project of its own** (385 conflicts; several days with the full loop, the Linux gate and the Lua tests; my estimate). Worth it only if toddhow wants the Morrowind-side features (custom skills and attributes, `openmw.content`, ESM::Path) or wants to stay close before more ESM4 work diverges. The draft `docs/tasks/0101-upstream-esm-merge.md` names target `97722d48f9`; fast-forwarding local `master` to the fetched tip is its first step.
