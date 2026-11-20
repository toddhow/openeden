# Task 0361: showcase look, night and interior lighting (part a: items 1, 3 and 7 of note 0149)

Date 2026-11-19. Branch `showlight-0361a` from main `753c93599a`. Build `I:\Projects\OpenEden-work\combat3\build`, frames
`I:\Projects\OpenEden-work\combat3\sc61a\keep` (sheets named below). Harness `tools/showcase_check` with the groups `light61`, `hours61`, `int61`, `act61`.

## Item 1: night and dusk exteriors (note 0149 lighting 1)
Re-capture on main `753c93599a` (`keep/sheet_before.png`, `keep/sheet_hours_before.png`): the exterior ambient cube had made night dim blue-grey, but
the streets were still near black from 19:00 on; only torch pools and window glow read.
Cause, by numbers (`OPENEDEN_LIGHT_TRACE`, a temporary trace, removed): at 23:00 `SkyrimClear` gives ambient cube night faces about (0.24, 0.37, 0.43),
sun (moon) light (0.02, 0.51, 0.89) display, tonemap exposure +0.32 stop (the weather's image space), i.e. linear ambient 0.05 to 0.15 and a
blue-only directional. Skyrim lights in gamma space and adds ambient and directional there; the engine lights in linear space, where ambient
alone is exact (display = albedo_g x light_g) but the sum of two dim terms comes out darker, and Skyrim's own HDR image space also adapts up in the
dark. A plain `[Rendering] hdr exposure = 1.5` proved it (`keep/exp15.png`): night reads moonlit blue with readable streets, dusk is a believable
twilight; the same exposure at noon is overexposed, so it has to follow the time of day.
Fix: `[Game] neutral weather night exposure` (default 1.5 stops, 0 off): `MWWorld::neutralNightLiftStops(share, full)` with the share from a
`TimeOfDayInterpolator` (a third at the sunrise and sunset values, 0 by day, 1 at night, the weather's own hours), added to the image-space stops in
`WeatherManager::update`, only for a neutral weather in an exterior (an interior keeps its cell grade; a Morrowind session never reads it).
After: `keep/hours_after.png` (12, 17, 19, 20, 21, 23, 02), `keep/sheet_after.png` (market, plaza, Dragonsreach, aerial at 19:12 and 23:00),
`keep/act_off.png` against `keep/act_after2.png` (same shots with 0 and 1.5).

## Item 3: Bannered Mare yellow wash and heavy fog
Cause: the volumetric fog, not the cell tint or bloom (A/B `keep/mare_variants.png`: volumetrics off gives the right contrast, bloom off does not).
Skyrim's cell states fog 128 to 2,000 in a dim colour (88, 83, 71) as LINEAR fog; the volume uses the Morrowind exponential-matched extinction
(`fogExtinction`, 64 percent fog at 1,000 units against 45 for the linear one) and adds up to 18 lamps' light scattering at gain 0.5, which together
make the room a bright golden mist. Experiments: lamp gain 0.15 and 0.04 (`mare_gain.png`) then density 0.5 and 0.25 (`mare_dens.png`): density 0.5
with lamp share 0.3 (gain 0.15) is clear and warm with depth, light shafts stay.
Fix: `[Game] neutral interior volumetrics density` (0.5) and `... lamps` (0.3), applied in `RenderManager::updateVolume` when the player's cell is a
neutral interior (`RenderManager::setNeutralInterior`, set in `Scene::insertCell`); Morrowind and exteriors untouched. After: `keep/mare_after.png`
(compare `keep/mare_before.png`). Dragonsreach was not re-judged (its probes baked; not shot after the change).

## Item 7: actors in shade are black silhouettes
Not reproduced as a pipeline fault. The skinned and static Skyrim pipelines both add `AMBIENT_CUBE` and per-pixel lighting
(`skyrimmodel.cpp` 401 and 509); Mare actors are lit and textured (`mare_after.png` 000 and 002). The audit's black Hulda (`fight`, `keep/fight_after.png`)
is the harness placing the camera outside the room: the flat grey behind her is the framebuffer clear colour (the wall is single-sided), and she stands
in the unlit void. At night an actor in brown clothes under a blue-only light is physically dark (frames `act_off.png`, `act_after2.png`); the night
exposure of item 1 lifts it from black to a dim readable figure. Open: a real fight shot needs a camera inside the room.

## Tests
`apps/openmw_tests/mwworld/testneutralnightlift.cpp` (6 tests: day 0, full night, a third, setting 0, clamps, NaN and infinity).
Run red by mutation (see changelog row), not by stub: the function was written first.

## Morrowind
No Morrowind path reads the three new settings (`mRegionProvider` null; `mNeutralInterior` false). Checks in the changelog row.
