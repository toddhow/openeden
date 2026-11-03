# 0010 — toddhow's answers to the open rendering, editor and LOD questions

Date: 2026-11-01 · Status: **accepted** (toddhow, in chat) · Numbered 0010 because 0009 is the Slang decision.

This records what toddhow decided when asked to settle the questions that had piled up in task specs 0028 to 0045 and in `docs/design/world-lod-and-streaming.md`. The item numbers are those of the list in that conversation (kept so the answers can be traced). Where a spec's decision table disagrees with this record, this record wins and the spec is updated by the task that touches it.

## Rendering

| # | Question | Answer |
| --- | --- | --- |
| 1 | The water fix `20f635c3d2` changes the default look | **Keep it** |
| 2 | Default of `gi exterior strength` (spec 0036; 1 may be too dark) | **About 0.6** |
| 3 | Exterior GI formula (occlusion relative to open ground, terrain-following lattice, enclosed probes filled from neighbours) | **Accept** |
| 4 | Atmosphere: the model's sun is about 30 degrees up at 17:30 while Morrowind's colour tables already show sunset | **Follow the colour tables' hour.** Also: **add a procedural sun like Community Shaders provides** (a new spec: 0047) |
| 5 | Subsurface scattering defaults (mild; eyes marked as skin) | **Stronger defaults and exclude eyes** |
| 6 | Terrain gets no ambient occlusion | **Add terrain AO** (a new spec: 0048) |
| 7 | Cloud layer height 15000 units, set by eye | **Tune it in game when he sees the sky** |
| 8 | Wetness shelter comes from the exterior GI bake; cannot resolve eaves under 256 units | **Accept** |
| 9 | Puddles need wetness above 0.5; sleeping or waiting does not dry the ground | **Accept** |
| 10 | Extra texture map naming | **`_rmaos` is the default, `_orm` is still read** |
| 11 | Terrain layers limited to the array's 256 size | **Raise the array size later at a memory cost**, and **build a runtime texture downscaler** (a new spec: 0049) |
| 12 | Terrain always uses full GGX lighting | **Accept** |

## Eden Studio and CreativeMotion

| # | Question | Answer |
| --- | --- | --- |
| 13 | Stage C of task 0031: remove the old runtime editor hooks (`EditorService`, `EditorWorkspace`, `EditorInspector`, `EditorGizmo`), which needs an SDK major bump and deletes their tests | **Approved** (this is the approval CLAUDE.md requires for deleting those tests) |
| 14 | Mesh viewer panels are tabs inside the Mesh view | **Accept** |
| 15 | Mesh viewer: skinned models draw unskinned in non-textured modes; hiding a node edits the shared graph | **Accept for v1** |
| 16 | Edit NIF (NifSkope-style editing) | **Write the spec** (0046) |
| 17 | Ragdoll collision layer | **A new ragdoll layer** |
| 18 | Ragdoll debug trigger | **A console command** |
| 19 | Jolt version gap and scale | **Accept the damping workaround and test the scale in stage b** |
| 20 | Share code with CreativeMotion's own Jolt world | **Keep the copies separate for now.** **CreativeMotion is part of OpenEden: its edits and tasks are done in this repository** (so `components/creativemotion/` is edited here through OpenEden task specs, and the standalone CreativeMotion repository is no longer the source of truth; CLAUDE.md is updated to say so) |
| 21 | Interior GI decisions D1 to D10 (task 0028) | **Accept all** |

## World LOD and streaming

| # | Question | Answer |
| --- | --- | --- |
| 22 | D1: where the distant-land data comes from | **Build our own from full detail** |
| 23 | D2: baker as a separate process or in the game | **Both.** LODs can be baked ahead of time (an offline run whose result can be shipped or kept) or baked in the background while the game runs (applied after a restart). The baker is designed for both modes from the start |
| 24 | D11: container or private cache layout first | **Container first** (the Eden Archive Format comes before the LOD tile cache; the design doc's "private layout first" recommendation is overruled) |
| 25 | D12: the archive draft's eight open decisions | **Accept the design doc's answers** (`docs/design/world-lod-and-streaming.md`, review section) |
| 26 | D13: container reader on the full-loop list | **Accept** (the reader handles untrusted input) |
| 27 | D16: a weaker GPU for testing | **Emulate with settings and a software device** (for example lavapipe, plus settings that cap features and memory) |
| 28 | D17: a groundcover mod for grass tests | **Wait**; to be decided later |
| 29 | The remaining D3 to D10, D14, D15 | **Accept the recommended answers** |
| 30 to 36 | Pushing policy, branch plan, scratch folders and the `CLAUDE.md` open questions | **Not yet answered** |

## Specs 0047 to 0049 (written 2026-11-01)

| Spec | Question | Answer |
| --- | --- | --- |
| 0047 D4 | Does the procedural sun move the drawn sun's orbit too? | **Colours only**, with the sun and the shadows staying where Morrowind puts them |
| 0048 | Default of `ao terrain` | **Off**, until he has seen it |
| 0049 | May single-level compressed textures stay at full size? | **Yes**, since they cannot be cheaply shrunk |
| 0049 | The unmeasured `auto` budget table | **Not yet answered**; it is a starting point until it is measured |
| 0049 | The `auto` budget table | **Auto** (accepted as the starting table; measure and revise once the downscaler exists) |
| 0051 | File extension of the Eden Archive Format | **`.edenpak`** ("edenpak works", toddhow). The collision check in task 0051 stage b still has to confirm it from the extension databases and record the result, and the magic bytes and MIME type are still to be chosen there |

## LOD tile layouts (task 0055, 2026-11-02)

| Spec | Question | Answer |
| --- | --- | --- |
| 0055 | The draft tile key and payload layouts (key: tag 0x10, 8-bit worldspace index, 4-bit level, 44-bit Morton code of the biased coordinates; the 88-byte payload header with heights, normals and albedo each 8-aligned and padding zero; the canonical manifest JSON; `hintMemorySize` is the payload size) | **Approved** ("I approve the draft layout", toddhow). The layout version in the payload header moves from the draft value 0 to 1 |
| 0055 | Bake albedo into tiles, or reuse the live blend maps | **Bake albedo for the far levels (level 2 and up), keep reusing the live blend maps for the nearest tile level** ("Go with your recommendations", toddhow, 2026-11-02). Far tiles then need one texture each; a mod that changes ground textures triggers a re-bake |
| 0055 | Level 1 tiles, or level 2 / a coarser threshold | Not yet answered; the code keeps level 1 and threshold 4 |

## Answers of 2026-11-02 (second list)

toddhow answered the list of open questions. "The rest go with your recommendations" means the recommendation given in the list or in the spec's own decisions table.

| Topic | Answer |
| --- | --- |
| AdvancedBodies, first goal | **NPC variety** (seeded body parameters for resident NPCs; no save data for residents) |
| AdvancedBodies, adult tiers and community formats | **Both are in scope.** Adult tiers: engine-enforced age class on the actor and the package, off by default, a separate removable build target, the age gate always built (design doc `docs/design/advancedbodies-in-openeden.md`; architecture only, neutral fixtures). Community formats (BodySlide and similar): licences are checked before importers are designed (BodySlide is GPL-3.0, **verify**) |
| AdvancedBodies design doc, 21 decisions | **Recommendations accepted** |
| Groundcover mod for grass tests | https://www.nexusmods.com/morrowind/mods/46377 (Nexus returned 403 to the tool, so the name, version and licence are **not yet checked**; the grass work stays generic over the engine's groundcover record path) |
| `[Cells] incremental cell build` default | **Off** |
| Launcher mod organizer (0057), D5 pack priority | **An engine task first**: give a pack a place in the data-folder order (task 0064). Then D3, D4, D7, D10 and the status-file contract as drafted in 0057 and 0058 |
| Object cluster layout (task 0059, draft version 0) | **Approved**, with D2 (no atlas in v0) and the level errors 128, 320 and 960; the layout freezes at version 1 |
| Tile level and threshold (0055) | Recommendation accepted: level 1 tiles and threshold 8 stay |
| Baker (0061) D2, D3, D5 | The spec's recommendations are accepted (bake ahead of time or in the background; mods may ship bakes of their own content only; Morrowind's own bakes are never redistributed) |
| Streaming (0060) D5 and model LOD chains (0062) D3 | The spec's recommendations are accepted |
| Ragdoll adapter | Editing `apps/openmw_tests/mwrender/testbonepose.cpp` to host the allocation counter is **approved**; stage c (the editor's world) starts |
| Style layer cadence | **Kept**: cadence also slows root-motion walking |
| Edit-NIF (0046) | **Go** |
| CreativeMotion's standalone repository | No longer the source of truth (decision 0010 item 20) |
| Pushing | **Standing permission** to push `vulkan` after each green Linux check |
| `master` on GitHub | Stays the upstream copy; `vulkan` is the main line |
| Old scratch folders on I: (`OpenEden-cisim`, `-d2clean`, `-v5base`, `-osgref` and their builds) | **Delete** (`-osgref` last, because the movement-start agent compares against it) |
| Docker volume (50 GB) | Kept until after the next push |
| Still open | `openeden.animation` contract, macOS, executable and data names, extension questions 1 to 6, DirectStorage (plain I/O for now), the container's `metadataSize` cap |
