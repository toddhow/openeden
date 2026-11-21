# Task: Community Shaders feature parity (umbrella)
Design doc section: `docs/research/0150-community-shaders-parity.md` (feature table, OpenEden state, ranked plan, SDK licences, decisions)
Status: **APPROVED by toddhow 2026-11-20** ("yes to all but keep upscaling out of scope for now"): the physical sky replaces `atmosphere`, `sun glow` and the world-LOD haze behind a new switch; order A1 (auto exposure) first, then D, then the rest; **upscaling (stage H: render scale, our own upscaler, FSR, XeSS, DLSS) and frame generation are OUT OF SCOPE for now**; HDR output later (no test display); baked GI stays the default, SSR yes, SSGI later, no ray tracing; scope is every item except the three that do not apply here (tree LOD lighting, sky sync, light limit fix); Community Shaders is credited in the docs as inspiration (ideas only, no code or text used); the weather editor in Eden Studio comes with D5; this file is the umbrella and each stage gets its own spec (0364a ...) when it starts. Stages that touch existing shader families wait for task 0363 (Slang finish).
Goal: reach equal or better visual effect than Community Shaders' feature set in OpenEden's renderer, as opt-in settings, starting with the missing and partial items of note 0150.
Inherited code touched: none yet. Per stage: `files/shaders/*` (Slang only for new shaders, after task 0363), `components/render/*` pure headers, `apps/openmw/mwrender/rendermanager.*`, `apps/openmw/mwworld/weather.*` and `apps/openmw/mwrender/weather.*` (stage D, with 0094), `game_packs/skyrimse` weather and lighting hooks (0127, 0355).
Inputs / outputs: new `[Rendering]` settings, all off by default; pure maths headers with twins; Slang compute and fragment passes; no change to records, scripts or saves.
Constraints:
- Clean-room: ideas only from CS docs; no CS source or shader code is read, copied or adapted.
- Everything off by default; with every new setting at its default Morrowind is byte-identical (`worldobjects_check`, fixed Balmora frame hashes), compared per stage.
- New shaders in Slang only, behind `SlangPermutations` and interface records; the built-ins' move is task 0363, which stages touching existing families depend on.
- Full loop (renderer area): tests first with a red run, a fresh reviewer, frames viewed before commit, GPU cost against a stated per-tier budget.
- No proprietary SDK (DLSS, Streamline) without toddhow's decision; XeSS only after its licence is read; FSR (MIT) acceptable.
- Photosensitivity cap of 0040 for lightning and aurora; determinism by game time and seeded hashes; no per-frame allocation.
- Living test profile: each stage adds its settings.
Stages (sizes, risks, dependencies in note 0150 section 3): A exposure, LUT, HDR output; B post effects; C shadows and materials; D physical sky (D1 LUT atmosphere, D2 physical sun, D3 unified composition, D4 clouds and voxel field, D5 weather generation = 0094, D6 aurora, D7 night and height fog); E real-time reflections and GI; F grass and foliage; G water; H render scale and upscalers (H0 and H1 first-party, H2 FSR, H3 XeSS, H4 DLSS barred); I in-game settings overlay and weather editor.
Acceptance criteria:
- [ ] toddhow answers the decisions of note 0150 section 7 (at least decision 1, order, upscaler policy).
- [ ] Each chosen stage has its own approved spec with acceptance criteria before work starts.
- [ ] With every new setting off, Morrowind frames and `worldobjects_check` equal the parent's.
Out of scope: ray tracing, reading CS source, anything needing CS's D3D11 hooks, own frame generation.
