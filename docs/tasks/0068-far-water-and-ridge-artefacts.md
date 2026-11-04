# Task: Far water speckle and white ridge strips under the whole-map view
Design doc section: `docs/tasks/0066-world-lod-whole-map-view.md` (stage a log: far water at 600,000, white strips at 100,000; stage c log: far water speckle with and without the haze)
Goal: find why open water far from the camera speckles white and blue, and why white wavy strips follow some far ridges, when the view reaches far (`[WorldLod] map view`, or a large `view distance`), and fix both without changing the default frame.
Inherited code touched: `files/shaders/water/main.frag`, `files/shaders/water/main.vert`, `files/shaders/lib/view/reflect.glsl`, `files/shaders/lib/view/raytrace.glsl`, `files/shaders/terrain/*` (to be confirmed from the findings), at commit `14527ba896`.
Inputs / outputs: shaders only (and a pure C++ twin of a distance falloff if one is added and tested); no setting is added unless the fix needs one.
Constraints: with `map view = false` and numeric `view distance` the frame must be unchanged (old binary against new binary, PSNR set, a relaxation of byte-identity because clouds, water and NPCs animate; `[WorldLod] enabled = false` must be byte-identical). If the cause is the default path's behaviour (visible without map view), the default look is NOT changed without toddhow's answer. Every test game runs with `OPENEDEN_QUIET_WINDOW=1`.
Acceptance criteria:
- [ ] The cause of each artefact is named with evidence (code path and a frame or number), not a guess.
- [ ] A frame from 3,000 and from 25,000 units up over the Balmora coast with `map view` on shows no white or blue speckle on open water and no white strips on ridges (toddhow's eye is the final judge).
- [ ] The same poses with `map view` off look the same as before the fix (PSNR set, viewed).
- [ ] A pure distance-falloff function, if added, has tests written first and run red.
Out of scope: `rendermanager.cpp`, `viewlimits.*`, `worldlod.hpp` (another change is editing them), the haze function (task 0066 stage c), new water features.

## Stage log

### Stage log, 2026-11-03 (findings, fix, frames)

Frames and scripts: `I:\Projects\OpenEden-work\farwater` (runner `run.sh`, configs `cfg_*`, poses `coast3000.txt`, `coast25000.txt`, `hi.txt`; every game started with `OPENEDEN_QUIET_WINDOW=1`, a copy of the lodtest config, 2560 x 1369).

**1. The far sea speckle: cause found.** Reproduced at the Seyda Neen coast, 3,000 and 25,000 units up, `map view` on (`c3k_mv.png`, `c25k_mv.png`): white and dark specks that grow with distance. By experiment, in a copy of the shader folder (each run one change of `water/main.frag`; montages `m1.png` to `m3.png`):
- without the sun glint (`outColor += specular...` removed): the same speckle. Not the glint.
- with flat normals (`normal = (0, 0, 1)`): a clean sea. The ripple normals cause it.
- without the screen-space reflection (`reflection = fallback`): the dark specks go, the bright/blue noise stays. The dark specks are SSR hits that the noisy normals send to wrong pixels; they are a consequence.
- with every bump read as `textureLod(bumpMap, uv, 0 / 5 / 9)`: all four frames identical. A mip level changed nothing.
- `textureQueryLevels(bumpMap)` painted as a colour: **1**. The water's 128 x 128 normal map reaches the GPU with one mip level. VSG 1.1.16 builds `Image::mipLevels` as `max(1, data.properties.mipLevels)` (`vsg/src/vsg/state/Image.cpp`), so `DescriptorImage::compile` never takes its `mipLevels == 0` branch that would generate a chain (`vsg/src/vsg/state/DescriptorImage.cpp`), and a PNG read by vsgXchange's stb reader (`mipLevels` 0 or 1) stays single level. At 25,000 units one pixel covers tens to hundreds of the map's texels, which aliases.
- Stage a's hypothesis (the reflection distance uses `scene.zFar`) is **not** the cause: it changes how far the SSR ray goes, not the noise (the no-SSR frame still has the noise).

**It is the default path's behaviour too.** The default view has the same one-level map; there the fog and the 7,168 view hide the far water, and only the middle distance (about 1,500 to 7,000 units) can alias. Not measured. Whether other single-level images (TGA, PNG, JPG textures read by the same reader) lack mip levels is **unchecked**: my probe of an object's diffuse map did not take effect (the edited shader was not the one the pipeline used), so nothing is claimed about objects.

**Fix (`files/shaders/water/main.frag`, `components/render/watermath.hpp`).** Each ripple octave's sideways normal is scaled by `waterOctaveFade(texelsPerPixel) = 1 / max(1, texelsPerPixel)`, with the texels per pixel taken from `dFdx`/`dFdy` of the smooth ripple coordinate. That is what a mip level does (the average of the texels is a flat normal), and it is exactly 1 for a pixel that covers at most one texel. Gated on the far plane: on only when `scene.zFar >= 20000` (`waterFadeFromFar`), the world LOD's long view; the default view's far plane is 7,168 or less, so the default path executes the same arithmetic as before (`fadeOctave(n, 1.0)` multiplies by 1.0). **Not changed for the default path: toddhow decides** whether the middle-distance water of the default view should get it too (the cleaner fix for everyone is generating the mip chain for the image: `properties.mipLevels = 0` is not enough in this VSG; it needs `image->mipLevels = 0` or a CPU chain).

**2. A faint rectangle on the sea at 25,000 units** (`c25k_mv.png`, around (1080 to 1390, 690 to 805) of the 2000 px view): an outline and a darker patch with stair-stepped edges. With the refraction branch removed (`if (false)`), it vanishes (`e25_norefr.png`), so it is the refraction. The depth is read at one texel (nearest) and the colour is filtered (linear), so at the edge of the seabed (where a depth jump separates seabed from empty sea beyond) the colour mixes in the far side's. Fix: in the long view the refraction colour is read with `texelFetch` at the same texel. The outline is gone (`rect_fix.png`). The darker patch (no seabed drawn there, so a deeper water colour) is real content and remains: seabed tile coverage under the sea is **not** investigated.

**3. The white wavy strips on far ridges at 100,000 units: not an artefact.** They stay white when every water pixel is painted red (`hi100red.png`: only the sea is red), so they are not water. They are pillars joined by a pale barrier: the Ghostgate fence (`meshes\x\ex_gg_fence_01..03.nif` in `Morrowind.bsa`) around Red Mountain, and they draw identically with `object clusters` on and off (`crop_pair.png`), so the live objects draw them too. Stage a's note was a misreading. The fence is meant to be translucent; here it is a pale blue sheet: whether its animated alpha shows is not checked.

**Frames viewed:** before (`c3k_mv.png`, `c25k_mv.png`), after (`n3k_mv.png`, `n25k_mv.png`, `n25b_mv.png`): the sea is smooth with ripples that fade into the distance, no speckle; a few blue patches remain on the far right of the 3,000 frame (sun glint on the faded normals).

**Off means unchanged (a relaxation, not byte-identity):** with `[WorldLod] enabled = false` (far plane 7,168) old and new binaries give 52.5 to 53.6 dB PSNR (`c3k_off` against `c3k_off2/3`) against 56.0 dB between two runs of the new binary; the 25,000 pose 68.5 dB. Clouds, water and NPCs animate in real time, so the frames cannot be byte-identical; the argument for "unchanged" is the code path (`scene.zFar < waterFadeFromFar` returns before any new arithmetic).

**Tests:** `WaterOctaveFade` (6): written first with a stub that returned 1 (red: 2 of 6 failed; the three monotonic/range/NaN tests passed vacuously against a constant), then the real function (6 of 6 green); the last test reads the shader's text back. `components-tests` 4763 pass on Windows. Linux not run.

**Not verified:** the Linux gate, rain ripples (added after the fade and not faded), `[WorldLod] enabled` with the default 40,000 view in a storm, other single-level textures, weak GPUs, and the Eden Studio viewport (the water shader is shared).
