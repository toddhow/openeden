# Task: Merge upstream OpenMW master (ESM focus), target 97722d48f9
Status: DRAFT, awaiting toddhow's approval. Not started. Nothing has been merged.
Design doc section: CLAUDE.md "Upstream merges and the Jolt branch"; research note `docs/research/0051-upstream-esm-audit.md`.
Goal: bring OpenMW master at commit `97722d48f9` (2026-11-09) into `main` without losing an upstream fix and without taking our old version of any file wholesale.
Inherited code touched: everything upstream changed since the merge base `a035bcad2a` (2026-03-14): 1,291 non-merge commits, 1,172 files. The ESM area is `components/esm*`, `components/esmbridge`, `components/esm3`, `components/esm4`, `apps/esmtool`, `apps/essimporter`, `apps/openmw/mwworld/esmstore.*`, `esmloader.*`. Our side: `components/contentsource`, `components/esmloader`, record-source dispatch, `game_packs/skyrimse`.
Inputs / outputs: local `master` fast-forwarded to the fetched upstream tip first (fetch and fast-forward only; nothing of ours is committed to it); a merge commit on `main` naming `97722d48f9`; the audit table.
Constraints:
- Merge only through this task, by the named commit. Never push to OpenMW. `master` stays a clean record.
- For each conflicted hunk read the upstream commit (`git log -p a035bcad2a..97722d48f9 -- <file>`) and keep its behaviour, re-expressed for Jolt, VSG and esmbridge where needed. Never take the fork's old version of a file wholesale.
- Expected conflicts (measured 2026-11-10 with `git merge-tree`): 385 (201 content, 184 modify/delete of renderer and test files our Vulkan move deleted). In ESM files: `esmstore.cpp`, `essimporter/converter.cpp`, `esm3/fogstate.cpp`, `loadcrea.hpp`, `loadnpc.hpp`, `loadrace.cpp`, `esm4/loadcell.hpp`, `esmbridge/attr.hpp`. New upstream files in `components/esm/` (`gameformat.*`, `path.hpp`) need layer-rule decisions in `CI/layer_rules.txt`.
- Upstream bumped the save format; check `ESM::FormatVersion` and our save chunks (GPCH) against it.
- Off means unchanged: Morrowind default play is compared before and after (golden tests, Lua tests, a short in-game check).
Acceptance criteria:
- [ ] `master` equals the fetched upstream tip (fast-forward) and the merge names `97722d48f9`.
- [ ] Merge audit: for every changed file, the lines upstream added since the previous merge base that upstream still has and the merged tree lacks are listed and accounted for (kept, ported, or dropped with a reason); the list is committed in `docs/research/`.
- [ ] Specifically accounted for: `GameFormat` assignment in `ESM4::Reader`, `mGameFormat` in the ESM4 NPC, CELL, WRLD and LAND loaders, the compressed-record checks, the ESM::Path model fields, RefIds for skills, attributes and weather, the fog-of-war decode move, and our `Content::Plugin` and registry dispatch (not reverted).
- [ ] Builds on Windows (MSVC) and Linux (GCC `-Werror`); `components-tests`, `openmw-tests`, `eden-studio-tests` and all Lua integration tests pass; `CI/check_layer_rules.sh` (exception count not raised without a reason), `CI/check_no_osg.sh` and `CI/check_tests_registered.sh` pass.
- [ ] Skyrim SE: `tools/skyrimse_records_check` aggregates are unchanged before and after.
- [ ] With every new setting at default, Morrowind behaves as before (comparison stated in the pull request).
Out of scope: taking OpenMW's renderer (Vulkan stays), upstream's OSG changes, any push or merge request to OpenMW, ESM4 gameplay.
Alternative (no merge): port the knowledge items of note 0051 section 6 point 1 as small tasks. A pick of the 36-commit ESM4 series alone conflicts in 19 commits.
