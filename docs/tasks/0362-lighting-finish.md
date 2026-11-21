## 0362a Image-space contrast on the scene's average, night exposure and interior fog re-tuned (branch `lightgrade-0362a`)

**Contrast pivot.** The HDR path has no average luminance (no auto exposure; exposure is one fixed scale, `sceneColor` has one mip and a nearest sampler;
bloom's chain exists only with bloom on). So `gradePivot()` in `files/shaders/post/tonemap.frag` and `.slang` (GRADE only) samples 6 x 6 taps of
the scene colour, averages in linear light, applies the picture's own exposure, curve and encode, takes the luma and holds it between 0.02 and 0.8;
contrast is `(c - pivot) * contrast + pivot` (mid grey stays the pivot for contrast 1, so nothing changes without contrast). CPU twin:
`Render::ImageSpace::Grade::pivot` (default 0.5, so every pinned test value is unchanged). Cost: 36 taps per pixel only in a graded Skyrim session
with a contrast other than 1 (not measured on the GPU). No push constant, binding or `.interface` change; the grade is not a built Slang permutation.
**Existing tests pin `kCellGradeContrastShare == 0` and the default share**, so the constants stay as the pure functions' defaults and the two call sites
(`NeutralCellAtmosphere` cell grade, `WeatherManager` weather grade) pass the new `kContrastShareWithAveragePivot = 1`.
Frames (`I:\Projects\OpenEden-work\combat3\sc62a\keep`): `diag` pose 3 (cliff-shadowed ground) stays textured; Whiterun by day and night are not crushed;
Barrow start view readable. (The other Barrow shots of the group are flat fog colour: the group's relative poses leave the room, not caused here.)
Tests: `ImageSpacePivot` 5 (new file; written after the shader, not run red: they pin default pivot, dark pivot, pivot fixed point, bad pivot, both shaders
use the pivot), `SlangPermutations`, `SlangInterfaceRecords`, `ImageSpace*`, `Tonemap*` pass.

**Night exposure and interior fog.** Mean luma of Whiterun street (hours61, 1600x900, SkyrimClear): with 1.5 stops night read 72 to 79 (day 83, the
same as day: not night); 0.8 gives 54 to 61 at 20 to 02 h (19 h 70), 0.4 gives 46 to 62 with more black (7 to 10 percent below 12). Chose
`neutral weather night exposure = 0.8`. Bannered Mare (`mare` group, hour 20): density/lamps 0.5/0.3 vs 0.3/0.2, mean luma 93.7/125/101 vs 96/114/101;
the thinner fog keeps depth and warm light but less milky; chose 0.3 and 0.2. Run to run noise at fixed settings was up to +-7 mean luma at night (three
runs at 1.5), so these are judged by frames too (`sheet_night.png`, `sheet_mare.png`).

Morrowind: Balmora fixed frame `1c5d47ad0b5dc32d` and hdr `b451a8a6ca771e79` equal for `integ2` and this build. Not run: `worldobjects_check`, Linux gate,
clang-format 14, clang-tidy, Lua tests, GPU cost of the 36 taps, a before frame of the pivot against the old mid-grey (no toggle exists), Night Eye
contrast (still neutral, task 0257).
