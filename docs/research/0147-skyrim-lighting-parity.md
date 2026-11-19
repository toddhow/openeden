# Research note 0147: Skyrim lighting parity, what the data holds and what the engine uses

Date: 2026-11-19. Task 0355 stage a (DRAFT; docs and read-only measurement; no engine, pack or shader code changed).
Branch `lightsurvey-0355` from `7874e00d73`. Author: the survey agent. Estimates are mine; things I recall but did not
read are marked RECALLED-VERIFY; judgements "from memory of the real game" are marked MEMORY.

## 1. How the numbers were made

A throwaway Python walker (`I:\Projects\OpenEden-work\light55\keep\survey.py`, outside the repository, output next to it)
read the five Skyrim SE masters (Skyrim, Update, Dawnguard, HearthFires, Dragonborn) in
`I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data`: every record flat (GRUPs only descended into),
compressed records inflated, CELL, LGTM, WTHR, IMGS, IMAD, LIGH and REFR subrecords tallied. **Counts are per plugin
record, not merged over the load order**: an override in Update.esm counts again (so 929 interior CELL records is more
than the 759 interiors of task 0115). Aggregates only; no record text is quoted. The XCLL layout used (RECALLED-VERIFY,
from xEdit's definition, the data agrees on every offset I looked at: sizes 92 and 64, floats in plausible ranges):

| offset | field |
| --- | --- |
| 0, 4, 8 | ambient, directional, fog (near) colour, RGB + pad |
| 12, 16 | fog near, fog far (units) |
| 20, 24 | directional rotation XY, Z (degrees, i32) |
| 28 | directional fade (f32) |
| 32 | fog clip distance (f32) |
| 36 | fog power (f32) |
| 40..63 | ambient cube: 6 colours, +X -X +Y -Y +Z -Z (RECALLED-VERIFY the order) |
| 64, 68 | specular colour, fresnel power (92 byte form only) |
| 72, 76 | fog far colour, fog max (92 byte form) |
| 80, 84 | light fade begin, end |
| 88 | inherit flags (bit order: ambient, directional, fog colour, fog near, fog far, rotation, directional fade, clip, fog power, fog max, light fade; RECALLED-VERIFY, bits 0 to 4 are task 0303's) |

## 2. What the data holds (five masters)

| Record | Count | Notes |
| --- | --- | --- |
| interior CELL records | 929 | every one has XCLL (928 of 92 bytes, 1 of 64) and an LTMP; 893 have XCIM (image space), 812 XCMO (music), 796 XCAS (acoustic space) |
| exterior CELL records | 75,776 | none carries XCLL; exterior light comes from the weather alone |
| interior flags | show sky 333, water 289, public 201 | "show sky" (DATA bit 0x80, RECALLED-VERIFY): a quarter to a third of interiors are open to the sky |
| LGTM (lighting template) | 131 (126 DATA of 92 bytes + 4 of 72; DALC 32 bytes in 126, 24 in 5) | task 0303 reads the first 20 bytes of DATA only |
| WTHR | 177 | FNAM fog 32 bytes in all 177; DALC (directional ambient) 704 = 4 times of day x 176 (plus 4 short); NAM0 colours 17/14/13 types x 4; IMSP four image spaces in all 177; HNAM volumetric lighting in 93 |
| IMGS / IMAD | 505 / 264 | |
| LIGH | 496 records (435 + 31 + 1 + 29; the comment in `records.hpp` says 1,740 for the five masters: **not reproduced here**, it may count the mod list) | |
| placed lights | 15,175 REFR whose base is a LIGH (494 distinct bases) | |
| REFR light subrecords | XRDS (radius override) 14,072; XLIG (4 floats: 10,102 of 16 bytes, 2,438 of 20) 12,540; XEMI (emittance, form id) 9,326; XPWR 1,402 | |

**Interior XCLL, aggregates (929 interior cells):**

* Ambient cube non-zero in 907 (98 percent): almost every interior is lit by a six direction ambient, not a flat colour.
* Inherit flags set (cells taking the value from their LTMP template): ambient 739 (80 percent), directional 778, fog colour
  757, fog near 828, fog far 745, rotation 443, directional fade 464, clip distance 824, fog power 484, fog max 440, light fade 369
  (flag meanings RECALLED-VERIFY). So **a flagged cell's own stored values are stale for most fields**, which task 0303 already
  handles for five of the eleven.
* Fog far colour differs from the near colour in 222 of 928 cells.
* Fog near: median 0, p90 340, max 4,000; fog far: median 4,500, p90 14,000, max 70,000 (units); fog power 0.4 to 2.5 (median 1);
  fog max (per cell) median 1.0, p10 0.9; clip distance median 0, p90 5,000.
* Directional rotation XY p90 0, Z p90 90 (most cells have no tilted sun); directional fade median 0, p90 1.
* Light fade distances: begin median 0, p90 100; end median 0, p90 5,000, max 60,000.

**LIGH (496):**

* Flags: dynamic 441, flicker 190, pulse 49, flicker-slow 11, carryable 4; no light in the five masters has the negative or the off-by-default bit (neither is in the tally), **spot 39, spot-shadow 12, hemisphere-shadow 110, omni-shadow 216**
  (shadow flag values 0x400/0x800/0x1000/0x2000 are RECALLED-VERIFY, see `LightFlags` in `records.hpp`).
* Radius: p10 144, median 256, p90 512, max 2,000 units. Falloff exponent: 1.0 in 90 percent, up to 10. FNAM fade 0.1 to 10
  (median 1, p90 3). Spot FOV 30 to 90 degrees (median 90) in 51 lights. Near clip: median 6.6, p90 500, one absurd value (1.7e8).
* Model on 16, sound on 4.

**REFR light data:** XRDS (14,072 refs) range -665 to 3.7 million (median 131, p10 -79: a radius, signed offset or something
else; the meaning of the sign is unsettled); XLIG's first four floats vary widely (the field order is not settled by the data:
RECALLED-VERIFY from xEdit: FOV offset, fade offset, end distance cap, shadow depth bias, near clip).

## 3. What OpenEden does with each (at `7874e00d73`)

Legend: **used**, **partly**, **ignored**.

| Data | Status | Where / what lacks |
| --- | --- | --- |
| XCLL ambient colour | used | `SkyrimCellSource` -> `CellLighting::ambient` -> `makeNeutralCellRecord` (`apps/openmw/mwworld/neutralcell.cpp` 197-201) -> `RenderManager::configureAmbient` (`rendermanager.cpp` 2207) with `brighten(minimum ambient luminance)` for interiors |
| XCLL directional colour | partly | becomes the engine's interior "sunlight" colour; its direction is the engine's fixed `(-0.15, 0.15, 1)` (`configureAmbient`), i.e. always from above; rotation XY/Z, directional fade ignored |
| XCLL ambient cube (6 colours) | **ignored** | no hemisphere/cube ambient exists in the shaders; the engine has one flat ambient plus the baked probes of the `gi` feature (Morrowind data only, `openmw --bake-gi`) |
| XCLL fog colour | used | `mAmbi.mFog` |
| XCLL fog near/far/power/max/clip, far colour | **ignored** | `makeNeutralCellRecord` sets fog density 0 ("Morrowind's fog density has no counterpart in a pair of distances", `neutralcell.cpp` 190-194), so interior fog is OFF; dungeons such as Bleak Falls Barrow (fog colour 75,99,97, i.e. blue-green) never get their fog. The fog manager already has a near/far land fog path (`fogmanager.cpp` `configure`) that a pair of distances could feed |
| XCLL specular colour, fresnel | ignored | no consumer (the PBR path has its own specular) |
| XCLL light fade begin/end | ignored | the engine fades lights by radius only |
| XCLL inherit flags + LGTM | partly | task 0303: ambient, directional, fog colour, fog near, fog far taken from the template (`applyLightingTemplate`, `neutral_world.cpp` 184); the other six flags (rotation, fade, clip, fog power, fog max, light fade) not applied because their fields are not used anyway; LGTM DALC (ambient cube) not decoded |
| LGTM DATA/DALC | partly | prefix only (`records.cpp` 1229-1248) |
| CELL XCIM (image space) | **ignored** | decoded in `weather.cpp` 858 (`CellWeatherLinks::mImageSpace`) but not applied; the weather image space path exists (tasks 0127 W-S3/W-S4: IMGS saturation/brightness/contrast/tint through `NeutralImageSpaceDriver`, `Render::ImageSpace::combine`) and would take it with little work (893 of 929 interiors name one) |
| WTHR NAM0 colours | used | sky, fog, ambient, sun by time of day (`weather_source.cpp`) |
| WTHR FNAM fog | partly | day/night near/far/power/max reduced to the engine's single "fog depth" fraction (`fogDepthOf`, `weather_source.cpp` 33, 290-295); the far colour, power curve and max opacity are folded into one number |
| WTHR DALC (directional ambient, 4 times of day) | **ignored** | decoded (`weather.hpp` `DirectionalAmbient`), never given to the engine: exterior ambient is the flat NAM0 ambient colour |
| WTHR IMSP + IMGS | partly | saturation, contrast, brightness, tint used with `[Game] neutral weather image space`; HNAM (eye adaptation, bloom) kept raw: no auto-exposure pass |
| IMAD | partly | only Night Eye (task 0257); 264 records, none else |
| LIGH radius, colour | used | `LightComponent` -> `makeNeutralEsmLight` (`neutrallight.cpp`) -> `ESM::Light` through `Scene::insertModel` (`scene.cpp` 286-296); attenuation by the Morrowind `LightAttenuation_*` fallback keys of the user's config (linear, 3.0) |
| LIGH FNAM fade | used | multiplies the colour up to 255 per channel (`neutralLightColor`): a fade over 1 cannot exceed the 8 bit colour, so fade 3 to 10 lights are clipped |
| LIGH flicker/pulse flags | partly | the four animation kinds map to the engine's fixed flicker/pulse; the record's own period and amplitudes (flicker period, intensity, movement) are ignored |
| LIGH negative, off by default | used | negative flag; off-by-default lights not created |
| LIGH falloff exponent, near clip | ignored | |
| LIGH spot (FOV), hemisphere/spot/omni shadow flags | **ignored** | every light is a point light; point shadows (task 0077, `[Shadows] point light shadows`) pick the nearest lights by radius and distance, not by the shadow flag; 216 + 110 + 12 lights ask for a shadow and 39 are spots |
| REFR XRDS, XLIG, XPWR | **ignored** | 14,072 / 12,540 / 1,402 references |
| REFR XEMI (emittance) | **ignored** | 9,326 references; a static that glows with the light of a form |
| Weather sun/moon/sky, Night Eye, point shadows, HDR/PBR/GI/AO/TAA, clustered light grid | exist | engine settings (all off by default), not data driven by Skyrim records beyond the rows above |

Additional engine facts (read): interior ambient is "brightened" to a minimum luminance (`configureAmbient`); the sun
direction of an exterior comes from the weather (`weather.cpp` 1157), interior's is fixed; the lights of a cell are the
engine's light grid (`CollectLights`, clustered grid, 50 per cluster, task 0072) and point-light shadows are limited to
`point light shadow count` (4 to 8).

## 4. What the frames show

Six frames, `I:\Projects\OpenEden-work\light55\keep` (800 x 600, default settings plus `neutral actors`, `neutral weather`,
`neutral actor idles`; a prebuilt `openmw.exe` of 2026-11-19 12:01 in the `followers` build folder, close to but not
verified equal to the `7874e00d73` tree: I could not rebuild here, the shell has no MSVC environment). **I judge against the
real game from memory (MEMORY), not against a reference capture.**

* `mare_0.png` (Bannered Mare): evenly lit, grey-brown, soft. MEMORY: the real inn has a strong warm orange pool of light
  around the central fire pit and the wall lanterns, deep dark corners and high contrast; here the whole room is similarly
  bright and there is no visible light pool, no firelight on faces or tables, no glow under the hanging lanterns.
* `dragonsreach_1.png`, `dragonsreach_4.png` (Dragonsreach): flat, even, slightly cool grey. MEMORY: hearths and braziers
  give warm pools and the hall falls off into shadow; here the long tables, banners and stone are lit the same everywhere,
  with no light falloff along the walls (no ambient direction, no light pools).
* `barrow_4.png` (Bleak Falls Barrow): dark and murky, which is right in spirit, but without the blue-green fog the cell
  data asks for (fog colour 75,99,97) and without the cool vertical shading of its ambient; the stone reads flat.
* `ext_noon.png`, `ext_night.png` (Whiterun exterior, hour 12 and 23): the noon sky, sun colour and shadows are
  plausible; the night is a blue-teal, dark and readable (MEMORY: the real night is a little more neutral blue with more
  contrast). No lit windows, no torch pools, no lanterns visible at night. (The camera stands against a pillar: poor
  composition; it was the existing `w5` scenario.)

What looks wrong, in order of how obvious it is: (1) **no visible local light**: lanterns, braziers and hearths do not make
pools, so interiors look uniformly lit (cause not yet isolated: either the placed lights are not reaching the shading or the
attenuation of the Morrowind fallback keys makes them negligible next to a brightened flat ambient: the first job of stage b);
(2) the ambient is flat, lacking the six-direction cube; (3) no interior fog; (4) night exteriors have no point light
contribution to see; (5) no per-cell colour grade.

## 5. Conclusions

1. The data are rich and mostly decoded; the engine uses roughly one in four of the interior lighting fields and about the
   same share of the light and weather fields. The biggest visible gap is local lights, then the ambient model (cube),
   then interior fog, then per-cell image space.
2. Nearly every field needed is already in the pack's records (`CellLightingData` and `LightData`); the SDK world model
   (`CellLighting`, `LightComponent`) is additive-extendable (the freeze policy in `world_model.hpp` allows trailing
   fields), so most stages need a trailing struct field, a pack change and one small engine consumer.
3. Open measurement still needed before choosing numbers: how the engine's light grid attenuates a Skyrim light of radius
   256 (Skyrim: intensity falls to zero at the radius, exponent from FNAM/falloff; RECALLED-VERIFY) against the Morrowind
   linear-3.0 default; the ambient scale of the HDR path.
