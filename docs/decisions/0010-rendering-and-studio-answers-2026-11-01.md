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
| 23 | D2: baker as a separate process or in the game | The answer was cut off ("LOD"); **still open** |
| 24 to 36 | The rest of the list (container first, archive decisions, test hardware, groundcover mod, pushing policy, branch plan, scratch folders, `CLAUDE.md` open questions) | **Not yet answered** |
