# Task: The neutral world model, stage N3a-5 (lights and interior lighting)

Design doc section: `docs/design/neutral-world-model.md` sections 10.4 (R2), 10.6 (N3a-5) and 10.15 (as built); decision 0016
Goal: with `[Game] neutral world` on, a Skyrim SE interior's lights (LIGH records) shine through the engine's existing light path, and the interior's own lighting (CELL XCLL) sets its ambient light; Morrowind and the setting off are unchanged.

## Scope

- `world_model.hpp` goes 1.0 to **1.1**, additive only (the version policy in the header): `LightAnimation`, `LightComponent`, `CellLighting`, `WorldObject::light` (last member), `WorldCell::lighting` (last member). `kSdkVersion` is untouched (the header is a type header, not `Host`).
- The Skyrim SE pack decodes LIGH `DATA` (48 bytes) and `FNAM` (4 bytes) and CELL `XCLL` (the 20 byte prefix of the 64 or 92 byte record) and fills the components through the pack-internal `ComponentSource` (the same route the owner, lock and teleport use); the SDK's `WorldView` 1.0 is not touched.
- The Morrowind projection (`apps/openmw/mwworld/neutralprojection.cpp`) fills the same components from `ESM::Light` and from an interior's `ESM::Cell::mAmbi`.
- The engine: `NeutralBase::mLight` (a fact of the base record), `MWWorld::makeNeutralEsmLight` (the component as the `ESM::Light` the engine's light path reads), `MWClass::Neutral::insertObjectRendering` and `MWRender::Scene::insertModel` (a neutral light is created through `MWAnim::createObject`, with or without a model), `makeNeutralCellRecord` (the cell's own ambient, directional and fog colour).

## Inherited code touched (at commit `95c5e2848c`)

- `apps/openmw/mwrender/scene.cpp` `Scene::insertModel`: one more branch next to the Morrowind `ESM::Light` one (a neutral base with a light); nothing runs for any other `Ptr`.
- `apps/openmw/mwworld/neutralstore.*`, `neutralcell.*`, `neutralprojection.*`, `apps/openmw/mwclass/neutral.cpp`: task 0105 to 0110 code, extended.
- `sdk/include/openeden/game/world_model.hpp`: the 1.1 additions. Pinned: `CI/check_extension_sdk.sh` still requires major version 1; `apps/components_tests/worldmodel/testfrozen.cpp` pins the version, edited on purpose (see `docs/toddhow-checks.md` section 3).
- Not touched: `components/mwanimation/light.cpp` (the engine's light and flicker), the light grid and point light shadows (they collect whatever `vsg::PointLight` is in the scene).

## Design decisions

- **A neutral light is the engine's own light.** The component is turned into an `ESM::Light` on the spot (radius, 8 bit colour, the Flicker, FlickerSlow, Pulse or PulseSlow bit, Negative) and handed to `MWAnim::createObject` like a Morrowind light, so flicker, pulse, the clustered light grid and `[Shadows] point light shadows` are the engine's, unchanged. Skyrim's flag bits 0x1 to 0x100 are the same bits as Morrowind's `ESM::Light::Flags`, but the mapping goes through the neutral enum, not through the raw word.
- **One animation.** A light with several animation bits runs the first of Flicker, FlickerSlow, Pulse, PulseSlow (the order `components/mwanimation/light.cpp` tests them in).
- **Off by default.** A light with the Off By Default bit is not created (`allowLight` false; nothing turns it on in this stage).
- **Fade.** Skyrim's FNAM fade value (0.1 to 10 in the masters) multiplies the colour, capped so that the brightest channel stays 255 (hue kept), because `ESM::Light` stores 8 bit colour. A fade above what the colour has room for is therefore not fully applied. Radius is not touched by the fade.
- **No light-only model.** A LIGH with no `MODL` (most of Skyrim's invisible lights) gets a light-only object (`createObject` with an empty mesh); a LIGH with a model gets the model and the light.
- **Position.** The light is at the object's origin (as Morrowind's lights without an `AttachLight` node). Skyrim's models do not name an attach point that this stage reads; a candle flame offset is not applied.
- **Cell lighting.** Ambient, directional and fog colour from XCLL become the interior's `mAmbi` ambient, "sunlight" and fog colour. The directional light keeps the engine's fixed interior direction (XCLL's rotation is not applied), and fog stays off (density 0): Morrowind's fog density has no counterpart in a pair of distances. The ambient cube, specular, fresnel, light fade and inheritance flags are not decoded.
- **Not done:** per-placement light overrides (REFR `XRDS`/`XEMI` and similar), spot lights as spots (they draw as point lights; the spot bits are kept in the raw flag word), shadow-casting flags, a light's duration/burn-out, light templates (`LTMP`), exterior lighting (needs a land and weather model), the light's model-attached sub-nodes.

## Inputs / outputs

`LightComponent` (radius, colour, fade, animation, negative, off by default, raw flags) per object; `CellLighting` (ambient, directional, fog colour, fog distances) per cell.

## Constraints

- Determinism: the components are pure functions of the records. Threading: main thread (the neutral cell is built there). No per-frame allocation added (the `ESM::Light` is made once per object at insertion).
- Off by default: nothing changes without `[Game] neutral world`; with it on and a Morrowind session nothing calls the neutral path (projection only).

## Acceptance criteria

- [x] `world_model.hpp` 1.1, additive, pinned (`worldmodel/testlightmodel.cpp`, `testfrozen.cpp` version line).
- [x] LIGH and XCLL layouts verified on the five official masters by an aggregate scan (stage log).
- [x] The pack fills the components (generated plugins: `skyrimse/testskyrimselights.cpp`; the masters: `skyrimse/testskyrimselightsreal.cpp`).
- [x] The Morrowind projection of every placed light equals the engine's own light data (`mwworld/testneutrallightreal.cpp`, aggregate).
- [x] A neutral light is inserted through the engine's light path and the Bannered Mare is lit (frames viewed).
- [x] With the setting off, Morrowind is unchanged (see the stage log for how it was compared and what was not).

## Out of scope

Spot light shapes, per-reference overrides, exterior lighting, light animation timing data from Skyrim (period and amplitude fields are decoded and not used), the Linux gate.

## Stage log

**Resumed 2026-11-11 after the first agent died mid-task** (it had written the design, the code, the tests and this spec; nothing was committed and nothing had been built or run: the `bld_last.log` of its folder ends in a `MiniDumpWriteDump` "paging file is too small" crash of ninja). I read the whole diff, found it complete and consistent with the spec above, built it (`openmw`, `openmw-tests`, `components-tests`, `skyrimse`, `compat_skyrimse`, `compat_morrowind`, ninja `-j 2`; the machine had other sessions' builds running, so the build took hours of wall time), and ran the checks below. Windows only.

**Tests.** `openmw-tests` 1,962 (1,961 pass, 1 skipped); `components-tests` without `*Slang*`, `*Complement*`, `*Property*`: 8,816 (8,784 pass, 32 skipped). One test failed on the first run and was fixed: `NeutralLightColor.aFadeThatWouldPassTheByteIsCappedAtTheBrightestChannelAndKeepsTheHue` expected 128 from 100 * (255 / 200) = 127.49999999999999 in doubles; the case now uses 240, 120, 60 (scale 17 / 16, exact in binary). **Red runs: not shown.** The previous agent wrote code and tests together and left no record of a red run against stubs; I did not reconstruct one (the code was complete, and stubbing it afterwards would only show that the tests can fail). The tests are therefore "written with the code", not "tests first". Gated real-data tests run on the installed games: `SkyrimLightsRealData`: 759 interiors of the five masters load with 0 failures, all 759 with XCLL lighting (144 with an all-zero ambient, 1 with fog far before near), 13,390 light objects (321 distinct bases, only 88 with a model: most Skyrim lights are light-only), 0 dropped, radius 32 to 1,500, fade 0.1 to 5, flag union 0x3ccb, animations none 3,017 / flicker 9,394 / flicker slow 14 / pulse 965; `NeutralLightReal` (Morrowind): all 2,887 cells project, 1,328 interiors with lighting, 21,758 placed lights, 0 mismatches against the engine's own `ESM::Light` data, 0 lighting mismatches.

**SDK change (justification).** `world_model.hpp` goes 1.0 to 1.1, additive only, as its own version policy permits: new types (`LightAnimation`, `LightComponent`, `CellLighting`) and one new member at the END of each of `WorldObject` and `WorldCell` (`light`, `lighting`, both `std::optional`); no existing member moved or changed; `kSdkVersion` untouched (the header is not `Host`); `CI/check_extension_sdk.sh` (major 1) passes. One existing test line edited: `testfrozen.cpp` pinned minor 0, now 1 (`docs/toddhow-checks.md` section 3). The layout pins of the existing structs in `testfrozen.cpp` pass unedited. A pack that does not know 1.1 is unaffected.

**In game** (`tools/neutral_session_check/check.sh`, five Skyrim masters only, `--start WhiterunBanneredMare`, `look` screenshots, 7000 frames, quiet window; exit 0, 0 dumps, 0 unexpected error lines; "Light 18" among the 694 inserted objects). Frames viewed, before (the N3a-4b build: flat 0x606060 ambient, no lights) and after (`I:\Projects\OpenEden-work\n3a5\run_old\user\screenshots\screenshot001.png` and `run_new\...`): after, the room is warmer and brighter, the timber has its colour, and the counter and pillar show pools of light; the cell's own XCLL ambient is in use. **Not shown:** a measured light count or cost (no per-light trace was added), flicker over time, point light shadows with neutral lights, any other cell, any exterior.

**Morrowind unchanged.** Balmora fixed-time frame (`OPENEDEN_FIXED_FRAME_TIME`, `--random-seed 7`, frame 600): SHA-256 prefix `1c5d47ad0b5dc32d` for the new build, setting off and on, and for the parent executable (the puddles W-c build, `C:\OpenEdenWork\puddlesc\build`), off and on: all four equal (and equal to the hash recorded in the N3a-4a and 4b rows). `tools/worldobjects_check` dump (110 lines) byte-identical to that parent executable with the setting off and on (the "on" run used a temporary copy of the script that wrote `[Game] neutral world = true`; the copy was removed).

**Limits and decisions** (the spec's "Not done" list stands): a light sits at its object's origin (no attach-node offset); spot lights draw as point lights; Off By Default lights are never created (nothing turns them on; 0 in the masters' interiors, so unseen in practice); the fade is capped by the 8 bit colour; one animation per light; XCLL direction, fog distances and the rest of XCLL are not used (fog stays off); exterior lighting is not modelled. **Not run:** the Linux gate (Docker is hung: no GCC `-Werror`, no clang-format 14; `CI/check_clang_format.sh` reports differences because only a newer clang-format exists here and it formats untouched code differently, so it was not applied to the files), clang-tidy, the Lua tests, sanitizers, a fresh review, a second cell, a torch in the player's hand in a neutral session.
