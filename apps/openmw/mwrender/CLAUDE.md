# apps/openmw/mwrender

The game's renderer on VulkanSceneGraph (vsgopenmw's, merged in task 0009) plus OpenEden's features rebuilt on it in task 0011. Read the root `CLAUDE.md` first.

## What lives here

- **vsgopenmw's renderer:** `rendermanager.*` builds the scene graph and command graphs; `scene.*`, `animcontext.*`, `npc.*`, `player.*` and `effects.*` hold objects and actors. Its animation system lives in `components/animation` (`Anim::Transform`, `Anim::Bones`, channels and controllers) and `components/mwanimation` (`MWAnim::Play` and `MWAnim::Actor`).
- **OpenEden's features (task 0011):**
  - `bonepose.*`: the one bone interface the post-animation features use.
  - `actorikcontroller.*`: foot IK, look-at, smoother turns, inertial lean and compound hitboxes.
  - `creativemotiondriver.*`: CreativeMotion's animation runtime.
  - `joltdebugdraw.*` and `navmesh.*`: debug drawing, through `Render::DebugGeometry` (`components/render`).
- **Task 0012's parity work:** `rain.*`, `waterripples.*` and `weathereffects.*` (weather and water effects), `groundcover.*` and `groundcoverinstances.*` (instanced groundcover through `Pipeline::Mode::INSTANCE` and `files/shaders/lib/groundcover.glsl`).

## Frame order for an actor

1. `MWMechanics::CharacterController::runAnimation` may run on an animation thread, one actor per call. It runs `MWAnim::Play::runAnimation`, which advances states, text keys and movement and writes bones through the keyframe controllers unless `Play::updateBones` is false. It then runs `CreativeMotionPoseDriver::update` and `apply` when "creativemotion animation" is on; Play's bone writes are then off.
2. `CharacterController::postAnimation` runs on the main thread. It runs `MWAnim::Actor::manualAnimation` (head and spine turns), then `ActorIkController::update` and `applyPostAnimation` (IK, lean and hitboxes).
3. `Scene::update` updates objects, then skins. Anything written to bones before this step is drawn this frame.

## Debugging CreativeMotion's animation (task 0053 research, note 0022)

- `[Game] creativemotion animation debug = true` logs one `CMTRACE` line per actor per frame (`CharacterController::traceAnimation`): the group each of the four bone groups plays, the previous group and the transition's progress (`trans=elapsed/duration`), position, `loco=(smoothed speed, phase)`, the movement animation's speed multiplier and the rotation of six bones. It also works with the pose driver off, so vanilla and CreativeMotion can be compared. It is a developer tool: it writes about 1500 lines a second per actor.
- **A/B in third person only.** In first person the body's bones are not updated in vanilla mode, so a bone trace of the player shows nothing.
- The driver feeds CreativeMotion group names and an `ActorState`; the animation graph and pose blending are not wired (note 0022 has the table). The style layer is (task 0053, below).
- **With inertial locomotion on, a walking actor's requested movement is the intended velocity, not the animation's root motion** (`InertialMotion::chooseMovementRequestSource`, `CharacterController::postAnimation`). The root motion made the smoothed speed, and with it the animation speed, stall at 15% of walking speed for about 1.7 seconds after each start: the "stutter".

## Rules

- **Off means unchanged:** with every OpenEden setting at its default, the controllers never touch a bone. `ActorIkController` binds no bones, `CharacterController` creates no pose driver, and `Play::updateBones` stays true. Keep it that way: create state lazily, only when a setting is on.
- **Bones through the bone interface:**
  - Post-animation features read and write bones only through `BonePose`, on VSG math.
  - The pose driver writes `Anim::Transform::translation` and `setAttitude` directly; it owns those bones for the actors it drives.
- **Units:**
  - CreativeMotion works in metres. Its adapters convert with `Constants::UnitsPerMeter` at one boundary each: `CreativeMotionPoseDriver` (`toMetres`, and the output loop in `update`).
  - Everything else is in Morrowind units.
- **Debug geometry is data:**
  - Producers add lines and triangles to a `DebugGeometry` between `begin()` and `end()`. The renderer draws it unlit, with no fog, alpha-blended and depth-tested, using colour mode 3 of the default shader.
  - Each producer has its own `DebugGeometry` sized for it: the Jolt draw refills every frame; the navmesh draw refills only when a tile changes. Capacity is fixed, so a full buffer drops primitives and logs once.
- **Threading:**
  - `DebugGeometry::addLine` and `addTriangle` may be called from physics threads.
  - `begin`, `end` and `setEnabled` run on the main thread.
  - The pose driver touches only its own actor.
  - Groundcover chunks are read and assembled on Groundcover's own worker thread (not RenderManager's single operation thread, which cell preloading needs promptly), but compiled, attached and detached only on the main thread, since `CompileContext::compile` updates the viewer. Builds hold references to the store, so anything that drops the store or the `Groundcover` waits for them first (`Groundcover::waitForBuilds`).
  - Nodes leaving the scene graph during play go through the compile context's `detach`, which keeps them alive until the frames already queued have drawn.

## Pitfalls

- **Instance attributes come before texture coordinates.** The default shader's vertex inputs are numbered in binding order, and the texture coordinate array's length is a specialization constant, so `Mode::INSTANCE`'s two per-instance inputs sit before the texture coordinates (`lib/attributes.glsl`, `Pipeline::Graphics`), and a draw's instance arrays must be inserted before its first `vec2Array` (`MWRender::Groundcover`).
- **vsgXchange 1.1 tags stb_image textures as sRGB** (TGA, PNG, JPG and BMP), and the GPU then darkens them on sampling. `vsgUtil::readOptionalImage` and `readImageFromMemory` strip it (`vsgUtil::removeSrgb`). Read images only through them, or terrain and many statics come out nearly black.
- **Specialization constants in boolean expressions:**
  - The default shader's `colorMode` is a Vulkan specialization constant.
  - Writing `colorMode == 3 || colorMode == 4` in `lib/material/terms.glsl` (a trial fogged unlit mode, since removed) made every frame render nothing (black, or a fully transparent screenshot) on this machine's NVIDIA driver, with no error logged. `colorMode >= 3` worked.
  - Keep conditions on specialization constants to one comparison, and check a screenshot after any shader change.
- **New specialization constants in `default/main.frag` can blank every frame.** Declaring two more texture UV-set constants (ids 17 and 18, `START_UVSET_CONSTANTS` + 13 and + 14) made every frame white on this machine's NVIDIA driver, menus and HUD included, even when nothing used them. No error was logged and the cause wasn't found. Normal and specular maps therefore read the diffuse texture's UV set (task 0012, V4h). Bisect shader changes by restoring one file at a time and capturing a frame. This predates the `inout.glsl` fix above, which may be the real cause; not re-tested.
- **Keep specialization-sized arrays last in shader interface blocks.** `lib/inout.glsl` once had `texCoord[numUvSets]` mid-block. Locations are assigned at compile time from the constant's default, so a model with more UV sets overlapped the members after it, `pointLightDiffuse` included. Interiors then lost their point lights or kept them depending on unrelated shader text: one unused `const int` flipped the Caldera Mages Guild frame from a constant 19.7 mean to 36.8 with the same executable (task 0012). A result that changes with text that has no effect means undefined behaviour in the shader; look for it first. Growing `Pipeline::Data::Scene` past 640 bytes brightened interiors in an earlier test, most likely for this reason, but that was not re-tested, so `groundcoverParams.w` still holds the multisample count.
- **Shader includes are not guarded by default.** `lib/light/equations.glsl` is now guarded; including an unguarded file twice redefines its constants and the pipeline compiles to nothing (`glslang` errors in the log, objects missing).
- **NIF record indices are only unique within one NIF.** `ParticleSystem::link` (`components/vsgadapters/nif/particle.cpp`) finds a particle system's emitter and particle node by their index in the model's NIF. A part carried by an actor (a torch) is linked with a path that starts at the skeleton's root bone, and a bone with the emitter's index was taken for the emitter, which ended the search before the torch: its flame came out in the air away from it. The search now starts at the attached node (`attachmentPath.back()`) and only counts the bones above it in the transform paths. Search an attached part's own subtree for anything identified by NIF index (`vsgUtil::ID`).
- **The console command is `togglenavmesh`,** not `tnm`. `tcb` toggles the Jolt draw.
- **Recast's own colours:** the navmesh draw is visibly more opaque than the OSG build's at the same alpha (Recast's `duRGBA` values; task 0010's research note 0004 records the comparison).
- **The HDR path (task 0025):** with `[Rendering] hdr` on, `mRenderGraph` draws into an `HdrTarget` (not the window), `mTonemapGraph` is the graph that has the window, and the GUI graph and `syncExtents` follow it. `Reflect` and `PostProcess` take their single-sample sources from the target (`setSource`, `setDepthSource`, called every frame: a resize replaces the images). Command order: scene, tonemap, post-process, screenshot. Gate for any change here: frames of the default path must not change (compare with `diffmap` under `[Input] camera sensitivity = 0.01`, which makes the camera hold still; the torch lights flicker, so compare interiors without lights such as `coc "ToddTest"`).
- **A new shader file must be listed in `files/shaders/CMakeLists.txt`.** The build copies only the listed files, so an unlisted include is "Failed to load include code" in the glslang text of the log, and the game then hangs at start without another message (task 0027). Reconfigure happens by itself after the list changes; check a new build folder, not an old one.
- **Probe debug geometry (task 0028, stage d):** `mProbeGeometry` is enabled in `updateProbeDebug`, never in `RenderManager`'s constructor: enabling it there hung the game at start (most likely its buffers were compiled before the engine ran; not isolated). `loadReflections` runs before the light probe file is looked at, so a cell may have either file.
- **Baked light probes (task 0028):** `default/main.frag` takes its ambient from `giAmbient` under `GI` (a `Pipeline::Mode` added by `Pipeline::Graphics::giEnabled`, like `PBR`), through `materialTermsAmbient` or `materialTermsScaled(ambientLight, ...)`; without `GI` the caller's `ambientLight` is `scene.ambient.xyz`, the same expression as before. The terrain does not take `GI`. Details in `components/render/CLAUDE.md`.
- **Exterior sky-visibility field (task 0036):** `RenderManager::updateSkyVisibility`, `updateSkyEnvironment` and `updateSkyDebug` run in `onFrame` after `updateSceneData`. `exteriorCellLoaded` and `exteriorCellUnloaded` (from `Scene::loadCell` and `unloadCell`, any thread) only record the cell under `mSkyVisMutex`; files are read and packed in `onFrame`. The pool and header buffers are dirtied only when the cell set or the sky colours change. `gi exterior` makes the pipelines add `GI` and `GI_SKY`, and the main view then binds real buffers (`mSkyVisPool`, `mSkyVisHeader`, and the reflection buffer for the sky image). The baker is `OMW::SkyVisBaker` (`apps/openmw/skyvisbaker.*`, `openmw --bake-gi-exterior "x,y"` or `"x0,y0:x1,y1"`), which moves the player cell by cell with `World::changeToCell`. Details in `components/render/CLAUDE.md`.
- **Rain wetness (task 0037):** `RenderManager::setWetness` (called by `WeatherManager::updateWetness` every frame, 0 indoors) and `updateSceneData`, which writes `1 + wetness * strength` into `sceneData.ambient.w` only when `[Rendering] wetness` took effect. Other views' alpha stays 1 or 0 and so reads as dry. `mWetnessEnabled` is set in the constructor with the other HDR settings and also decides the terrain's and the pipelines' defines. Details in `components/render/CLAUDE.md`.
- **The local map: draw order and height range** (`Map::requestExteriorMap`). The map view has a depth-sorted bin, where blended NIF and DAE shapes draw last, but the map scene's `SimpleWater` is a plain StateGroup, blended and writing no depth: it and the terrain draw in traversal order, so anything opaque added to `sceneChildren` goes before the water (the terrain appended after it covered the sea in every exterior map, task 0073); blended objects below the sea surface still draw after the water, untinted. The camera's height range is `MWRender::mapDepthRange` of the objects, the cell's terrain (`Paging::updateViewForCell`'s height range) and the water: the objects alone cut land higher than them and gave an invalid box for a grid with no objects. A terrain batch holds more than one cell once the reference view distance passes 2 cells (16,384 units, `getBatchSize`), and a cell's batch is found by rounding down (`Terrain::batchIndex`), not by integer division. The map's `mReferenceViewDistance` is taken when the `Map` is built, before world LOD's view limits or a Lua `setViewDistance`, so its batches can differ from the main view's (recorded, not fixed).

## True PBR `_rmaos` material maps, on objects and terrain (task 0044)

- **Naming:** with `[Rendering] pbr material maps`, `<diffuse name>_rmaos` is looked for first, then `<diffuse name>_orm` (task 0027). `_rmaos` is Community Shaders' True PBR layout, a community convention and not a standard: R roughness, G metalness, B occlusion (0 = occluded), A specular. Decoding, the roughness scale and the F0 maths are in `Render::Pbr` (`decodeMapTexel`, `scaledRoughness`, `dielectricF0`, `reflectanceFromF0`); `pbr.glsl` (`pbrSurfaceFromMap`, `pbrApplyMapRmaos`) repeats them: change both together.
- **Scales:** `roughnessScale` and `specularLevel` are two more optional keys of an `openeden.material` entry (a script outside the repository turns Faultier's `PBRNifPatcher/*.json` `roughness_scale` and `specular_level` into such a file). Objects: the roughness scale rides in the descriptor's spare float (`align_dummy2`, kept under that name because `sun.slang` has a copy that must match), the level becomes `pbr.z` (reflectance); a map's alpha multiplies F0. A tier 2 value that is set still wins over the map. `roughnessScale` is applied to `_rmaos` only on objects, to either layout on terrain.
- **Terrain:** with `pbr material maps` (and `pbr`) the terrain builder turns on `TERRAIN_MAPS` and `TERRAIN_PBR`, looks for `_n` and `_rmaos`/`_orm` next to every layer's texture (whatever the `[Shaders]` terrain normal map settings say), and the terrain shader lights with the material model (`pbrPointLights`, `pbrSpecularLobe`). Layer flags: bits 0 to 15 the normal map, 16 parallax, 17 specular, 18 `_rmaos`, 20 to 31 the material map's index (`Terrain::packLayerFlags`; the shader repeats the constants). Binding 8 is the material array, binding 9 a `vec4` per layer (roughness scale, reflectance; below zero: none). A layer with no map has roughness 0.95, no metal, F0 0.04, occlusion 1. Occlusion scales the terrain's ambient light directly (the AO pass cannot: the terrain writes no ambient part). Terrain always uses the GGX lobe at full strength; objects in the `vanilla` look show specular only where reflectance is above 0.5 or the surface is metal, so test with `pbr look = physical`.
- **Array limits:** the terrain's layer arrays (`convertToArrayTexture`) keep the most common size and format, so a layer of another size or compression is grey (or, for colour in an RGBA array, decoded: BC7 is not). Stock land textures are 256 by 256 DXT1; a 2048 by 2048 file must be cut to a 256 level first. The local Faultier setup does that with a block copy.
- **sRGB:** `vsgUtil::removeSrgb` now also strips sRGB from block-compressed formats. A DX10-header DDS tagged `BC1/BC7_..._SRGB` was converted by the sampler and again by the shader (a wall came out nearly black). Normal and material maps are read through the same function, so they are linear.
- **Not built:** `_g` glow maps, subsurface keys, layers of another size than the array's. (Parallax from `_p` came in task 0045, below.)
- **Snow and ash cover (task 0039):** `RenderManager::setCover` (called by `WeatherManager::updateCover` every frame) and `updateSceneData`, which writes `Render::Cover::pack(wetness, kind, amount)` into `sceneData.ambient.w` only when `[Rendering] cover` took effect (`mCoverEnabled`, set in the constructor beside the wetness). It also decides the terrain's define through `TerrainMapSettings::cover`. Details in `components/render/CLAUDE.md`.

## Parallax occlusion mapping from `_p` height files (task 0045)

- **Settings:** `[Rendering] parallax` (off), `parallax quality` (`low`, `medium`, `high`), `parallax scale` (0 to 4), `parallax discard edges` (off), all read at start in `RenderManager`'s constructor, independent of `hdr` and `pbr`. Off, nothing is looked for and no define, binding or buffer exists.
- **The maths** is `Render::Parallax` (`components/render/parallaxmath.hpp`: step counts, fade, depth scale, the packing of two scales into one float, a CPU reference march), and `files/shaders/lib/material/parallax.glsl` (`pomOffset`) repeats it; a test reads the shader's constants back. Change both together. The shader function reads heights through `pomHeight(uv, dx, dy)`, which the including shader defines: that is the only texture-specific part, so a Slang port is a mechanical translation (decision 0009: GLSL stays inside these two existing shaders).
- **Objects:** `findAutoMaps` finds `<diffuse>_p` (`AutoMaps::heightMap`, `findHeightMap`) and also the `_n` next to it, even with object normal maps off, because the march needs the tangent frame. The loader binds the height file at `HEIGHT_UNIT` (17) with `Mode::HEIGHT_MAP`; `Pipeline::Graphics::parallaxQuality` adds `PARALLAX_POM` (and `_LOW` or `_HIGH`) to such draws. `_p` wins over the old alpha parallax (`PARALLAX`, `_nh`), which is untouched when there is no `_p`.
- **The material's displacement scale** has no room in `Pipeline::Data::Material` (96 bytes, pinned by a test): the spare float `align_dummy2` holds two half floats (low: the roughness scale of task 0044, high: the displacement scale times `parallax scale`) for models with a height file only (`createMaterialData`, `unpackHalf2x16` in `default/main.frag`). Models without one still hold the plain roughness scale. A displacement of exactly 0 reads as "not set" (default 0.5).
- **Terrain:** the layers' `_p` files form an array at binding 10 and per layer (binding 11, a `vec4`) the array index and the depth in UV; the shader marches once, on the layer with the largest blend weight, and shifts all four layers by that offset. With the setting on the terrain takes the per-pixel `TERRAIN_MAPS` path, which changes its lighting slightly even with no `_p` file anywhere. A height file the array cannot hold (one byte a texel) is reported and ignored: `convertToArrayTexture` throws for it, and that used to end the game.
- **Not in version 1:** the depth buffer and the G-buffer depth are not displaced (outlines stay flat; a bright line may show where a wall meets the ground); the shadow-map pass skips the march; sun self-shadowing of the height field was not built; `parallax discard edges` applies only to clamped object textures, never to tiling ones or the terrain; the normal, material and specular reads after the march use the compiler's derivatives, so a mip seam can show where the hit jumps.
- **Pitfalls:** the march needs `textureGrad` with derivatives taken outside any branch (the loop's trip count differs per pixel). `HEIGHT_MAP` in `default/main.frag` also changes how the roughness scale is read. Compare a wall with `[Shaders] auto use object normal maps = true` on the off side too, or the picture shows the normal map and not the parallax.
- **Depth, the pivot and the limits (task 0045, follow-ups):** depth is a length in game units (`worldDepthPerDisplacement` 16 times the displacement scale), divided in the shader by the game units one unit of texture coordinates covers at the pixel (`pomWorldPerUv`, from derivatives; at most a tenth of a tile), so a small rock mesh and a terrain tile get the same physical depth: a fixed fraction of a tile made rocks and cobble ground into smeared spikes (32 units per unit of displacement scale since the wall and bridge frame). The offset is measured from the map's mean depth (`pomOffset`'s `pivot`), or the texture slides ("breathes") as the amplitude fades with distance. The ray's slant is limited to 2 (`maxSlant`). The terrain's march fades out with the slope (full at 30 degrees, none at 45: its texture coordinates come from above and are already stretched on a cliff) and mixes the two strongest layers that have a `_p` by blend weight, so the shift does not jump where the strongest layer changes.

- **Weather effects (task 0040):** `RenderManager::setLightning` (called by `WeatherManager::updateLightning` every frame, 0 indoors) and `updateSceneData`, which tilts `mLightPosition` towards the flash for the scene light and the shadow cascades; `Rain::setLit` (the pipeline mode `PRECIPITATION_LIT`, longer streaks) and `WeatherEffects::setSunLight` (snow's emissive), both from `mPrecipitationLit` in `RenderManager::update`; the sun glow is a shader setting (`Resource::ShaderSettings`, `cSunGlow`) with no code here. Details, the photosensitivity limits and what was not built are in `components/render/CLAUDE.md`.
- **Strips at joints and flat faces (task 0045, last follow-up):** the height file is read blurred (`pomBlurLod`: the pixel's level plus 1, floor 1.5) so a joint is a slope; the offset is pulled back where the slope is steep (`steepSoftening`); the slope of the height field at the hit is added to the shading normal and recesses are darkened (`PomResult`, objects only). The includer defines `pomHeight(uv, lod)` and `pomTextureSize()`. A frame check of this shader needs a tree whose other shader work compiles: copy `build-jolt/resources` to a scratch folder and run with `--resources`.

## Volumetric fog (task 0041)

- `RenderManager`'s constructor makes a `View::VolumeFog` only when `[Rendering] volumetrics` is on and `hdr` took effect (`Render::Volume::useVolumetrics`), puts its image and parameter block into the main view's set (bindings 14 and 15; every other creator binds `View::dummyVolume()` and `dummyVolumeParams()`), and builds its commands after the view set exists. Its commands sit in a bin of their own (`Bin_Volume`, a `ComputeBin` whose command graph has submit order -1; shadows -3, the light grid's graph -2; see `components/render/CLAUDE.md`). If a shader file is missing it logs and drops the volume; the parameter block then stays at zero and the readers stay off.
- `updateVolume` (after `updateSceneData` in `onFrame`) sends the fog end (the grid's reach), the settings, the sun's shadow cascade count and fade, and the camera's matrices (for the history). Underwater the volume is told it is off.
- **Pitfall:** do not enable the volume's bin in an editor or preview view: it is only in `mView`.

## Shadows: settings and a pitfall (task 0052)

- **Where:** `MWRender::Shadow` (`shadow.cpp`) owns a `View::Shadow` (`components/view/shadow.*`) only when `[Shadows] enable shadows` is on; `RenderManager`'s constructor adds one command graph per cascade (submit order -3 with the volume on, else -1), each recording a `vsg::View` that draws the casters. The sun term in `lib/view/shadow.glsl` reads the cascades. Settings are read at start.
- **What draws into the cascades:** `enable shadows` alone draws nothing. The casters are chosen by `terrain shadows`, `object shadows`, `actor shadows` and `player shadows` (all off by default); `Shadow::viewMask` turns them into the cascade views' mask (`Mask_Terrain`, `Mask_Object`, `Mask_Actor`, `Mask_Player`), and the compile manager skips the shadow context for anything outside the mask. `enable indoor shadows` (on) lets interiors cast too (`createCastShadowScene`).
- **The cascades:** `number of shadow maps` (cascade count, 1 to 4: `maxCascades` in `cascades.glsl`), `shadow map resolution` (texels per side), `maximum shadow map distance` (0 means the far plane), `shadow fade start` (share of that distance where the shadow fades out), `split point uniform logarithmic ratio` (0 even splits, 1 logarithmic), `use front face culling` (the shadow pass culls front faces; off, it culls nothing), `filter shadow maps` (linear filtering of the compare). `percentage closer soft shadows` (PCSS) with `blocker samples` and `pcf samples` (0 to 16) set the filter in the shader (`cPCSS`, `cBlockerSamples`, `cShadowSamples` in `shadow.settings`); `enable debug hud` defines `DEBUG_SHADOW`.
- **Not read any more** (OSG's LiSPSM tuning, research note 0006): `split point bias`, `enable debug overlay`, `compute scene bounds`, `minimum lispsm near far ratio`, `polygon offset factor`, `polygon offset units`, `normal offset distance`.
- **Pitfall (fixed in task 0052): `object shadows = true` crashed the game** in `vsg::TransferTask::_transferBufferInfos` (and, depending on what the freed memory held, in other places such as `vsgAdapters::mygui::VertexBuffer::lock`). `vsg::View`'s copy constructor gives every copy a default `vsg::ViewDependentState`, and a cascade's view is a copy of the shared base view (whose own state had been set to null, which a copy does not inherit). The objects' lights (torches, lamps) were collected by that state when the object graph was drawn into the cascade, and its `traverse` then worked on light and shadow data that was never set up for a shadow view (the exact write that corrupted the heap was not pinned down; the crash stops once the state is gone). Terrain and actors have no lights, so they never crashed. Every view in this code that must not light things sets `viewDependentState = nullptr` after copying; shadow cascades now get theirs from `View::createShadowView`. Test: `apps/components_tests/view/testshadowview.cpp`. If a heap crash appears with a new caster, check first that no copied `vsg::View` kept its default state.

## Volumetric clouds (task 0042)

- **Settings:** `[Rendering] volumetric clouds` and five more (see `components/render/CLAUDE.md`), read at start. `RenderManager`'s constructor makes a `View::CloudVolume` before the view set (its ground shadow map is binding 16) only when the setting is on and `hdr` and `atmosphere` took effect; the scene graph code then builds its commands, adds its bin (`Bin_Cloud`, a `ComputeBin` whose graph has submit order -4) and gives the sky its picture (`Sky::setVolumeClouds`). If a shader file is missing it logs and resets the object.
- **Each frame:** `updateCloudVolume` (in `onFrame` after `updateVolume`) takes the coverage, wind and share from `Sky::getVolumeClouds`, enables the passes only when the sky is drawn, the share is above 0 and the camera is not under water, and calls `View::CloudVolume::setParams`. `Sky::applyVolumeClouds` (from `setWeather`) works out the coverage and puts the picture on the first cloud dome.
- **Pitfall found by reading the code:** a ground shadow map that is not written (clouds not drawn) would be read as garbage by every lit shader, so `shadowMain` runs every frame outside the enable switch and writes share 0 when nothing is drawn. If `createCommands` fails the object is reset but the view set already holds its descriptor: only possible with a build that has no slangc.

## CreativeMotion style layer and transition rules (task 0053)

- **Settings:** `[Game] creativemotion style layer` and `creativemotion transition rules` (both off), `creativemotion default style` (`cm_default`); overrides in `[CreativeMotion Styles]` as `actor.<refId> = <style>` and `race.<raceId> = <style>` (first match wins: actor, race, default). Styles are `styles/<name>.style.json` (`opas.style`) in the VFS; `cm_default` and `cm_heavy_test` ship in `files/data/styles/`.
- **Code:** `creativemotionstyle.{hpp,cpp}` is opas-only code (no vsg): group classification and transition durations, weights from `ActorState`, the effective profile, the rules, `StyleLibrary`, and `CreativeMotionStyleLayer`, the one boundary that maps Morrowind's bone names and local axes to the style modifier's canonical ones (frame = 180 degrees about Z times the parent's model-space rest rotation; a positive slouch leans toward +Y). `CreativeMotionPoseDriver::update` applies the style to each bone group's sampled pose before the inertializer, so changing a style blends. CM is not edited.
- **Console:** `cmStyle` prints the style, `cmStyle on|off|<name>` changes it for the target (the player when none). The choice lives in the `CharacterController`, is handed to the driver at the start of `runPoseDriver` (never while it runs), and is lost when the actor's controller is rebuilt (a cell load).
- **Cadence:** `CharacterController::update` multiplies the movement animation's speed by the style's cadence factor, which also slows root-motion movement.
- **Pitfall:** `StyleModifier` finds bones by canonical names and pitches about X; on Morrowind's skeleton it would silently do nothing without the layer's mapping.

## The procedural sun (task 0047)

- `Sky`'s constructor decides (`useProceduralSun(setting, hdr)` and `Graphics::hasSlangShader("sky/sunproc")`), puts `sky/sunproc` on `Graphics::slangOnlyPaths` and gives `Sun` a `ProceduralSunParams`. `Sun` then draws its quad with the `sky/sunproc` pipeline and the same descriptor sets; the query and the flash keep `sky/sun`. Details in `components/render/CLAUDE.md`.
- **Frame checks of the sun** need the camera aimed at it: a Lua player script with the static camera at the yaw and pitch of the sun for the game hour (the sun's direction is `(cos t, -0.268, sin t)` with `t = pi (hour - 6) / 14`, `WeatherManager::update`; yaw `atan2(x, y)`, pitch `-asin(z)`). The flash and glare overlay hide the disc's structure at the default settings.

## Texture size cap (task 0049)

- Read images through `vsgUtil::readOptionalImage` or `readImage` with an `ImageUse` to opt in to `[Rendering] texture max size` and friends; reads without one are never capped. With a cap the terrain array's "most common size" rule now sees capped sizes, so a set of mixed 512 to 2048 layers joins one array at the cap. Details and limits are in `components/render/CLAUDE.md`.

## Detaching nodes from a worker thread (LOD S0 crash, 2026-11-02)

- **What happened:** the "hang" in a fast flight with `[Terrain] distant terrain = true` was a crash. The crash catcher's "Fatal Error" box was waiting on the screen, so the process sat idle (the main thread was in `NtUserWaitMessage`, and a crash dump `openmw-crash.dmp` was in the user folder). The main thread crashed in `Render::Engine::loop` at `mDeletionQueue->advanceFrame()` while freeing a node tree (`Group`, `CullNode`, `Commands` destructors).
- **Cause:** `vsgUtil::DeletionQueue::add` was called from the terrain preload thread (`Terrain::Preload::setPositions` runs `Paging::updateView`, which calls `mCompile->detach` on the chunks it replaces) at the same moment the main thread advanced the queue. The queue had no lock, so both threads changed the same vector: an object was kept twice or lost, and a later advance freed a tree with bad memory.
- **Fix:** `DeletionQueue` has a mutex and releases leaving objects after the lock is let go (`components/vsgutil/deletionqueue.hpp`). Test: `DeletionQueueThreads` in `apps/components_tests/vsgutil/testdeletionqueuethreads.cpp` (crashed every time before the fix).
- **Rule:** `CompileContext::detach` may be called from any thread now; anything else a worker thread does to a node that the render threads can reach is still unsynchronised. `Paging::updateView` edits `view.rootNode->children` on the worker, which is safe only while that preload view is not in the scene.
- **Finding a "hang":** check for an `openmw-crash.dmp` (or `openmw-freeze.dmp`) in the log folder first. A Fatal Error window is shown only when `OPENEDEN_QUIET_WINDOW` is not set; with it set there is no box, a crash ends the game with exit code 1 and a freeze with 0xDEAD, so the `.dmp` and the exit code are what tell (root `CLAUDE.md`, "A crashed game shows a Fatal Error box"). `scratchpad`-style runner: start the game under the Win32 debug API (`crashrun`) with `_NO_DEBUG_HEAP=1`, which writes a dump at the first access violation and shows no dialog.

## World LOD object clusters (task 0059: the bake and the runtime)

- **What exists:** `openmw --bake-lod-objects [--bake-gi-out DIR]` (`apps/openmw/lodobjectbaker.*`) writes `<user data>/openeden/lod/objects.edenpak`, a derived Eden Archive of object clusters (layout version 1, frozen on 2026-11-03: the approved draft with the version field changed, levels 1 to 4 and up to 256 cell slots; `components/lod/objectcluster.*`, `clusterpack.*`, `clusterbuild.*`, `nifflatten.*`). `MWRender::LodClusters` (`lodclusters.*`, made by `RenderManager::setStore` only with `[WorldLod] enabled` and `object clusters`) reads it: a worker opens the pack and reads the headers, `Lod::selectTileTree` chooses clusters by pixel error, workers decode a chosen cluster and read its textures, the main thread builds one `StateGroup` per group (the pipeline from `Pipeline::Graphics::getOrCreate` with the new `Mode::LOD_FADE`, a `Pipeline::Material` whose specular alpha is the fade, one descriptor set with the diffuse texture, per-group vertex arrays) and dithers it in; the dither is `dither8` in `default/main.frag` (an edit of the existing GLSL, decision 0009: no new shader file). Cells inside the active grid and cells that list a reference a script disabled or the scene moved (`pagingEnableObject`, `pagingBlacklistObject`) are masked: their ranges are left out of the draws and the live chunk keeps them. `DistantObjects::setClusteredCells` makes the chunks of covered cells skip exactly the statics the cluster lists for the cell (`Lod::clusterDrawsReference`); everything else stays live.
- **One list of references:** `collectDistantReferences` and `distantObjectModel` (end of `distantobjects.cpp`) are the live chunks' own functions, shared so that the bake and `DistantObjects` cannot disagree about what exists. Keep them that way.
- **Pitfalls:** (1) meshoptimizer collapses a zero-thickness double-sided quad pair (swamp scum, thin walls) to nothing at no error; every piece keeps at least max(4 triangles, 1/16). (2) Cutout pieces keep their borders (`LockBorder`). (3) **Do not share `BufferInfo` objects between draws:** the vertices of the later draws came out wrong (long streaks across the sky); each group gets its own arrays. (4) The pack bins an object by the cell it is stored in, as `collectDistantReferences` and the live chunks do. (5) Alpha-blended-only models (10,578 statics: palms, many trees) are baked as cutouts (alpha test at `Lod::blendCutoutThreshold`, `flattenNifModel(..., true)`) unless `Lod::textureLooksTranslucent` (word match on the base name), the material alpha is below 0.99 or there is no texture; 2,219 statics stay out. (8) An interior or a hidden `DistantObjects` makes `staticsReady` true (nothing is drawn there, so nothing to wait for), or a Solid tile would stay on screen in an interior. (7) **The hand-over order** is `Lod::stepTileFade` (`components/lod/clusterfade.*`, 33 GPU-free tests): the cluster dithers in over the live objects, is solid while the live chunks leave out the statics, and when its tile is no longer wanted it stays solid until the live objects are back (`Lod::stepCoverage`: per cell, another Solid cluster covering it counts as back; else `DistantObjects::staticsReady`, i.e. `Lod::liveStaticsReady`), then dithers out; a lost live chunk sends it back up (Reclaiming). A tile that fails to compile covers no cell and is retried after 600 frames. (6) A frame check needs a static camera (`tools/benchmark_data`'s `--script-run` did not move the player in this build); a Lua player script with `camera.MODE.Static` does.

## World LOD terrain tiles (task 0055, stages S1c and S1d; layout version 1, approved 2026-11-02)

- **Where:** `components/lod/` (pure: keys, selection, payload, pack, bake; all layout knowledge is there), `apps/openmw/lodterrainbaker.*` (`--bake-lod-terrain`), `apps/openmw/mwrender/lodterrain.*` (the runtime), `components/terrain/coverage.hpp` and the `coverage` parameter of `Paging::updateView`.
- **Off means unchanged:** `RenderManager` makes `LodTerrain` only with `[WorldLod] enabled`; `updateView` gets `nullptr` otherwise and the preload thread never passes a coverage. Keep it that way.
- **Threads:** the worker of `LodTerrain` only opens and checks the pack and reads the live land through `StorageLandSource` (like the preload thread). The `Terrain::Builder`, the scene graph group, `CompileContext::compile` and `detach` are main thread only. The coverage is read only inside the main thread's `updateView` call.
- **Pitfall found by a reviewer:** `ViewData::add` marks a view changed only when a chunk differs from the old one at the same index. A list that gets shorter or longer was never noticed, which is harmless without a coverage and leaves a hole (or a double draw) with one; `updateView` now also rebuilds when the coverage version or the entry count changed (coverage only).
- **A tile is drawn like a live chunk of the same size:** `Builder::createBatch(bounds, lod)` and `createDraw` with `BakedStorage` supplying heights and normals from the tile (33 vertices, lod 2, 3 or 4 for levels 1, 2 and 3). Level 1 takes colours, blend maps and layers from the live storage. Levels 2 and 3 carry a baked RGBA8 ground picture (256 x 256, no mips) and are drawn through the same terrain shader with `Storage::getDiffuseOverride`: `Builder::create` then binds a 1 x 1 blend map of zeros (so the shader reads layer 0 at texture coordinates that run once over the tile) and a one-layer array holding the picture; the vertex colours are white because the picture already holds them. With the terrain maps on (`TerrainMapSettings::enabled`) this is skipped and every tile uses the live layers.
- **Several levels:** `Lod::selectTileTree` chooses over tile and ancestors (`Covered` for what lies under a drawn tile); a covered tile's node stays until its drawn ancestor is built. A tile's edges stitch to a coarser drawn tile with the same lodFlags as `Paging::updateView` (`LodTerrain::updateStitching`); live chunks next to a tile are split until their vertex LOD is not coarser than the tile's (`Coverage::touchedVertexLod`).
- **Stale packs:** the worker bakes every tile's digest again without blending (`TileBakeSettings::mDigestOnly`; the digest includes `albedoInputsDigest`: blend map, vertex colours and a hash of every used layer's file) and compares with the pack: a changed land texture, blend or colour makes the pack stale, logged as a warning, and the baker says so when it replaces one.
- **Not done:** mip maps of the baked picture (`convertToArrayTexture` keeps one level of an uncompressed image), block compression of the picture (no encoder), a handoff that keeps a parent tile until its drawn children are built when the tiles end at the paging range (without `map view` a parent that turns Live is removed at once and its children appear a frame or two later, as before task 0060; with it, see the planner section below).
- **Checking it:** bake with `openmw --bake-lod-terrain --bake-gi-out <folder>` against a COPY of the config, put the pack in `<user data>/openeden/lod/terrain.edenpak` of the run, set `[WorldLod] enabled = true` and a threshold (64 shows tiles at 30000 view; 4 shows none in the Balmora pose) and compare frames with `enabled = false`. A pack that does not match the loaded land is refused with a warning in the log.

## The residency planner of the LOD clients (task 0060, `components/lod/residency.*`)

- **What:** `Lod::planResidency` is pure (no GPU, no clock): given one `ResidencyItem` per tile or cluster it says what
  to load (nearest first under a budget, index order without one) and what to free. `LodTerrain` calls it every frame;
  `LodClusters` only with a budget (`far memory budget` or the tier's), and without one runs the per-tile loop of the
  code before the planner, in its order. The clients' own rules are pure functions beside it (`clusterReplaceable`,
  `cancelEviction`, `classifyRefinedChild`, `refinedParentMayGo`, `refinementReleases`), so tests can reach them.
- **The governing rule:** an item with **no fallback** is never blocked, freed or evicted, and no eviction cooldown
  holds it back. No fallback means nothing else would draw its square: a terrain tile whose square is not inside the
  live paging range (`squareReplaceable`, with hysteresis inside the range), a cluster with no live objects (object
  paging off: `Camera::mLiveObjectsPage`) or whose square reaches past their range. The budget governs the replaceable
  items only; an overrun the others cause is logged once per change (`mOverBudget`), never fixed with holes. So in map
  view a budget cannot bound memory: that is an open decision for toddhow (task 0060 stage log, third review).
- **Two cooldowns, counted in frames:** the eviction cooldown (`mCooldown`: set when the planner frees a wanted item;
  replaceable items only) and the failure back-off (`mFailureCooldown`: every item; `failureCooldownFrames` doubles the
  base for each further failure, up to 16 times the base). Frames, not seconds: the clusters' 600-frame base capped at 9,600 frames is 160 s at
  60 fps but 32 s at 300 fps. Terrain tiles have no back-off: one that fails to build is never chosen again (`mFailed`).
  A loading item with no fallback whose loads failed before commits no bytes, so it cannot make pressure free others.
- **Refined terrain tiles:** without `map view` a drawn tile that the selection refines (Live) goes at once, as before.
  With it (`camera.mViewDistance > camera.mPagingRange`) it stays until `refinedTileMayGo`: the live terrain draws its
  whole square, or no chosen child is coming (in view, not built yet) or failed past the live range. A child out of view
  or inside the live range lets it go (waiting for one out of view drew the parent over its drawn siblings for as long as
  it stayed unseen); a failed child keeps it for good (overlap rather than a hole), and then its bytes are not credited
  to its children (`RefinementTile::mStays`).
- **Clusters freed while wanted** are marked `mEvicted`, fade out through the hand-over (`Lod::stepCoverage`) and are
  not wanted again until gone; if the square stops being replaceable meanwhile the free is cancelled.
- **Tests:** `apps/components_tests/lod/testresidency.cpp`. Its `ClientSim` and terrain Sim mirror the clients' order
  and rules; they are not the clients, which have no unit test (they need a compile context and a store). Change a
  client's order and its Sim together.

## Cell-load trace and NPC animation preload (task 0060, note 0026)

- **`[Cells] load trace = true`** (off) times the parts of every frame and of every cell load with `Misc::LoadTrace::Scope` (`components/misc/loadtrace.hpp`) and writes `load-trace.jsonl` (user data folder, or `OPENEDEN_LOAD_TRACE_FILE`) at exit. Scope names must be string literals. Add a scope with one line; leave the existing ones in place.
- **Animation names are cache keys.** The animation options cache by the name exactly as written, so the cell preloader must ask for the names `Npc::updateNpcBase` asks for: both go through `Npc::animationSources`. Change one place only and the preload silently stops helping (the first NPC of each kind then reads 20 to 40 ms on the main thread). Body parts are not preloaded yet: each part type has its own options object and cache (`partBoneOptions`).

## CreativeMotion intent and the `natural` preset (task 0067, stage 1)

- **Settings:** `[Game] creativemotion preset` (`off` default, `natural`). `CharacterController` combines it with the three individual settings in one place (`creativeMotionFeatures()` in `character.cpp`: `MWRender::loadCreativeMotionFeaturesFromSettings`): `natural` switches on creativemotion animation (the pose driver), the transition rules, the style layer and the intent rules; a setting the user writes in settings.cfg (`Settings::Manager::mUserSettings`, which also holds values set at run time) wins over the preset, true or false. Inertial locomotion is not part of it. Durations of the intent rules are data: the `[CreativeMotion Transitions]` category of settings.cfg (`stopping`, `flip`, `starting`, `fast turn`, `slow turn`, `fast turn rate`; factors from 0 to `sMaxTransitionFactor`), read ONCE per session (`transitionTimes()` in `character.cpp`) with each bad key or value logged once, located. The defaults equal the rules without intent except a flip (the spec's base time).
- **Intent:** `MWRender::IntentTracker` (`creativemotionstyle.*`, vsg-free; it includes opas) turns the movement request, the speed of the actor's current mode, the measured speed (inertial locomotion's smoothed speed when it drives the actor, else the animation's root motion) and the change of heading into `mStarting`, `mStopping`, `mStanceChanging`, acceleration and turn rate; `runPoseDriver` feeds it and puts the result in `ActorState`. The tracker exists only with the intent rules on.
- **Pitfall: the movement request is gone on the animation thread.** `preAnimation` clears `getMovementSettings(ptr).mPosition` before `runAnimation` runs on the update threads, so anything there that reads it sees 0 (stage 1 shipped with "wants to move" always false because of this; review finding 1). `preAnimation` stores what the intent needs (`mIntentWantsToMove`, `mIntentSpeedMps`, `mIntentMeasuredMps`, `mIntentStanceChanged`) in members on the main thread; read those.
- **Pitfall:** measure a blend where the group really changes, not at the scripted event: with inertial locomotion a stop goes run to walk at the key release and walk to idle about 22 frames later. `creativemotioncheck` reports every group change with the duration chosen there (`trans=…/D`), the offset left by the inertializer (the trace's `tgt=[…]`, the un-offset target of each traced bone, written only with the debug trace on) and the jerk above the target's own.
- **Checking it:** `tools/creativemotion_check/check.sh <build> <base cfg> <empty work dir> vanilla cm cmoff natural` (three runs per variant by default, a summary of the ranges per variant, the build's commit and link time and the extra settings in every report; a natural run whose trace never shows the intent fails); `EXTRA_SETTINGS` adds settings.cfg text (for example a `[CreativeMotion Transitions]` category).

## The merged body (task 0069, stages d and e: `mergedbody.*`, `Npc::attachMergedBody`)

- **Off means unchanged:** with `[Bodies] merged body` false, `attachMergedBody` returns after one setting read and
  `MergedBodies` reads nothing. Keep every new step behind that read.
- **Fail closed, per race, sex and read options, for the whole session:** the template is built once (quick key from
  `OMW::computeStockBodyQuickFacts` against the bake's `quick.txt`: records and file sizes, no part file read or
  hashed, the skeleton parsed once per path and session; payload read; node built inside a try/catch); any failure
  keeps the stock parts and is remembered (decision H15: no live swap). Reasons come from `Body::decideMergedBody`
  (`components/body/mergedruntime.*`). Templates are keyed by the context's read options too: the player and the NPCs
  of one race and sex get separate templates.
- **Lifetime:** the cache, the templates and the reports live in `MergedBodies::Session`, which `OMW::Engine` declares
  after `mRenderEngine` and `mRenderManager`, so it goes before both. An actor holds its template and its copy
  (`Npc::mMergedTemplate`, `mMergedNode`) until its `Npc` goes with RenderManager's `Scene`: after the Session, still
  before the render engine and its device (second review, finding 9). No Vulkan object in a namespace-scope static.
  The bake, check and analysis runs call `disableForThisRun`, whatever `[Bodies] merged body` says.
- **New parts need the actor's `compile()`** (second review): `updateParts` only attaches nodes (the merged copy, a
  draw it gives back to the template, a part); every caller compiles the actor afterwards (`updateEquipment`,
  `rebuild`, `Scene`'s NPC insert, `Preview::compile`). A new caller of `updateParts` must too.
- **The look is the stock part's own:** `findLook` marks the part's model as skin first (`markSubsurfaceModel`, as
  `updateParts` does before reading a part), reads it through `vsgUtil::readNode` with the part's options and a fresh
  `SharedObjects` (or the cache returns the old node and nothing is captured), captures each pipeline's options with
  `Pipeline::Override::pipelineOptions`, adds `MATERIAL` when the group has a material descriptor (the reader adds it
  after the override ran, `nif.cpp` `createStateGroup`), records the stock pipeline's defines and takes the texture
  descriptors without `BONE_BINDING`; their images are shared into the stock options' `SharedObjects`. A part with
  several shapes is matched by `BodyCommon::materialKey` in the order `shapesOf` lists them. An alpha-blended look is
  refused (no depth sorting).
- **Defines must match:** after `getOrCreate`, every define of the merged pipeline must be in the stock one and the
  other way round, except those the adaptation changes on purpose (`adaptedDefine`: vertex inputs, `SKIN`, and
  `GBUFFER_MOTION`, which `Pipeline::Graphics` adds to every skinned draw when the G-buffer is on). A mismatch refuses
  the body with the define in the Verbose log. When a new feature adds a define, run the check tool with it on and see
  that "draws the merged body" still counts 20. With `[Rendering] slang shaders` on a pipeline's stages are Slang
  twins, whose modules have no compile hints: `MergedBodies::pipelineDefines` reads the GLSL defines through
  `vsgUtil::glslDefinesOf` (kept beside the twin by `Pipeline::Graphics::createShaderStage`), and `compareDefines`
  refuses a stage that only one side has defines for (task 0072, review of stage f, finding 1: it used to skip such a
  stage, so with the switch on the check compared nothing).
- **Each draw's skin carries only the bones its vertices use.** All of the body's bones in every draw's skin made 20
  merged NPCs cost a 7.2 ms frame against 3.8 ms for stock; with the subset the two are equal.
- **Attach at any bone:** the vertices are in the skeleton's space and each bone's inverse rest matrix is its
  `invBindMatrix`, so the attachment bone (the groin) only fixes the frame the skin's matrices are relative to.
- **The actor's copy is kept** (stage e review, finding 2): while its template is the same, `attachMergedBody` keeps
  `mMergedNode` and only applies the regions again when the hidden set changed; a torch at dusk or any change that
  touches no merged slot does no node work. `clearControllers` still empties `mStaticControllers` on every
  `updateParts`, so the copy's controllers, recorded in `mMergedControllers` when it was linked, are added again by
  `relinkMergedBody` (valid while `mMergedNode` holds the copy: its bone matrices own the skins). Everything that ends
  the copy goes through `forgetMergedBody`: a rebuild (`updateNpcBase`), a failed `compile()`, the stock parts. The
  skins of draws that stay drawn keep their matrices across a change; that their first frame has the right motion is
  reasoning, not isolated in a frame (the motion scene's change frame shows no motion anywhere on the body, but the
  kept draws are not told apart from the restarted ones there).
- **Skins of hidden draws are left out** (finding 3): `MergedBodies::RegionChange` names each draw's bone matrices and
  whether any of it is drawn; `relinkMergedBody` does not add the skin of a draw nothing of is drawn. When such a draw
  is drawn again, `applyMergedRegions` calls `Anim::Skin::forgetPreviousMatrices` on its skin (found through
  `MergedLink::mSkin`, the non-const clone from `decorated.clonedControllers`): its previous matrices are those of when
  it was hidden, so its first frame would carry that whole motion; it now has none, like a new part (finding 5). Which
  draws and which skins is `Body::drawsDrawnAgain` and `Body::linksOfDraws` (pure and tested; links matched to draws by
  the identity of their bone matrices, never by position); the value scopes `npc.mergedbody.drawnagain` and
  `.restarted` count both per region change and a warning is logged once when they differ (second review, finding 1).
  `tools/mergedbody_check` `SCENE=motion` shows it: without the restart the change frame's motion output has the whole
  arms-down to fists-up swing on every restarted draw, with it none.
- **One draw per material key** (`Body::planMergedDrawsByMaterial`): the player and 20 merged NPCs are about 270
  fewer draws in the scene census than stock (7,116 against 6,848), at the same frame time.
- **Equipment (stage e):** a dressed actor keeps the merged body with the covered regions left out.
  `Body::hiddenMergedSlots` reads `mSlots` (`PartSlots`, the bookkeeping `updateParts` uses) after the equipment pass,
  so it is the stock parts' own rule (a slot held by an inventory group with priority 1 or more);
  `MergedBodies::applyHiddenRegions` then gives the actor's copy, for each draw, one `vsg::VertexIndexDraw` per
  remaining index range (`Body::visibleIndexRanges`) sharing the template's buffer infos, the template's own draw when
  nothing of it is hidden (also after it was hidden: second review, finding 6), or none. It can be applied again to a
  copy it changed; a draw already as wanted is left alone, and the draws it takes out are detached by the caller
  (`CompileContext::detach`). Only the copy changes: its state groups are its own because `MWAnim::decorate` clones
  the bone matrices; a copy that shares one with the template, or has a draw that is not one of the template's
  buffers, is refused. VSG's `VertexIndexDraw` copy constructor does not copy the index type: `assignIndices` sets it,
  then the template's buffer info replaces the one it made (`thirty_two_bit_indices_keep_their_type` fails without
  it). Done in `updateParts` only, never per frame.
- **Sharing the template's buffer infos is safe here, unlike LOD cluster pitfall 3** (different cluster draws, each
  with vertices of its own, given shared `BufferInfo` objects drew streaks): the parts of a merged draw are ranges of
  that one draw's own index list over its own vertices, so they bind exactly the buffers, offsets and index type the
  template's draw binds, compiled once with the template; only `firstIndex` and `indexCount` differ. The frames of
  stage e and of its review (wrist, knee, ankle close-ups, 20 dressed NPCs) show them right.
- **The fallback** (a refused copy): the stock parts entirely, a warning once per race and sex
  (`MergedBodies::reportRegionsRefused`, finding 4), and stage d's rule: at most one swap each way per actor
  (`mMergedDrawn`, `mMergedRetired`). Retiring every actor first built dressed kept the player on the stock parts,
  silently.
- **What an equipment change costs** (trace scopes `npc.updateequipment`, `npc.partnodes`, `npc.relink`,
  `npc.compile`, `npc.mergedbody.*`; stage e reviews): before the copy was kept, the merged body was decorated, linked
  and compiled again on every `updateParts` (decorate and link 2.29 ms of a 26-NPC torch frame; now they run only for
  a new copy). Now the merged body's own work in such a frame (`decide`, `regions`, `keep`, `relink`) is about 0.1
  ms, and `npc.partnodes` and `npc.relink` are the same as stock. `npc.updateequipment` per torch frame (26 NPCs),
  merged against stock, four runs on a shared machine: 9.96 against 6.34 ms, 7.23 against 5.65, 5.67 against 6.18,
  6.17 against 6.10; the rest is VSG's compile of the actor (`npc.compile`, 2.80 to 5.26 ms merged, 2.88 to 3.38
  stock). Worst case seen, 26 NPCs putting on a full iron set in one frame (cold part reads and VSG's compile): merged
  148 ms against stock 68 ms in one run, 107 against 47 in another, then 48 against 77 and 79 against 45: noise of a
  factor of two either way; the merged body's own scopes in that frame are 1.2 to 1.4 ms. The full set on and off
  every second frame: 2.06 ms per change against 1.81 ms for stock.
- **Logs:** every silent fallback is logged once (a race the store lacks, each refusal reason per race and sex, an
  actor on another skeleton at Info); one line per actor that draws the merged body ("Merged bodies: <ref> draws the
  merged body"), which the check tool counts. The default skeleton is compared as a normalised mesh path, prefixed
  with "meshes/" only when missing: `Misc::ResourceHelpers::correctMeshPath` always prefixes, and comparing its output
  for both sides refused every beast.
- **Checking it:** bake into a copied user folder (`openmw --bake-bodies`), then `tools/mergedbody_check/check.sh <build>
  <base cfg> <user data> <work> stock|merged [cell]`; the scene quits by itself and the run fails unless the game exits
  with code 0 and leaves no crash dump. Compare the frames and `stats.txt` of the two variants. `EXTRA_SETTINGS` adds
  settings.cfg text, `START_COMMANDS` console lines: weather by starting in another region, `ChangeWeather` for
  Balmora's region and `coc "Balmora"` (a change of the current region only starts a transition of a minute or more).
  `SCENE=equipment` (with `TIMEOUT=240`) runs the equipment scene of stage e: a row of eight dressed NPCs, the player in
  twelve combinations from four cameras each, then 200 cycles of the full set on and off; add `[Rendering]\nframe
  counters = true` to `EXTRA_SETTINGS` and compare `stats.txt` between "MBPHASE baseline" and "MBPHASE after". Lua's
  `moveInto` never auto-equips (`objectbindings.cpp`), so the NPCs are dressed by a script of their own.
  `SCENE=torches` (stage e review; start it in `Pelagiad`: at Balmora's start the hill pushes the lines together)
  places twenty dressed NPCs of every race and sex with torches behind the player and sets the hour from noon to night
  and back four times (`world.mwscript.getGlobalVariables().gamehour`): `Actors::update` makes every NPC in range take
  out or put away its torch in the same frame, once a second. Then every NPC puts on a full iron set (a 4 s quiet
  window), then the paper doll. With `[Cells]\nload trace = true` the trace is copied next to the frames; each change
  frame has one `npc.updateequipment` per NPC, and `npc.mergedbody.matrices` / `.drawn` give, for each relink, the
  summed sizes of the skins' bone matrix arrays (all draws, drawn draws): what the linked skins write each frame from
  then on, not a count taken per frame, and twice the bones with the G-buffer (the previous frame's half). Pixel
  differences between runs mean little in these scenes: the camera follows where the player settled, a weather change
  is still moving, torches flicker; view the frames side by side. The load trace keeps every scope in memory until
  exit (40 bytes each), so its own list shows in the resident memory (5 to 11 MiB over the equipment scene's 200
  cycles, stock and merged alike): measure memory without it. `SCENE=motion` (second review) puts the full set on the
  player, raises the fists and takes the set off, with screenshots of the frame before, the change frame and the four
  after; run it with `OPENEDEN_FIXED_FRAME_TIME=0.0166667`, `hdr`, `gbuffer` and `gbuffer debug view = velocity` in
  `EXTRA_SETTINGS` (no `[Video] framerate limit` any more: since 2026-11-04 each screenshot holds the frame it was
  asked for, `components/render/CLAUDE.md`). `OPENMW_EXE` starts another executable from the build folder.
- **`OPENEDEN_PARTSLOTS_TRACE=1`** (second review, finding 2; read once) logs for every NPC a `PARTSLOTS <record id>
  <reference number> v<view>` line per part node attached (with its model) and detached (with whether a node was
  there), and the group and priority of all 27 slots after each `updateParts`. Compare two builds by actor, not by
  reference number: objects a script creates get other numbers in two runs of the same executable.
- **The scene census samples every 30 frames,** so equipment toggled every second frame shows as a slow alternation in
  `ctr_scene_draws`, not one change per toggle.

## NPC parts and their controllers (task 0074, `partcontrollers.hpp`)

- **Pitfall: `Npc::updateParts` empties the controller lists** (`clearControllers`: the skins in
  `mStaticControllers`, `mFace`, `mWeaponControllers`; `autoPlay` too until task 0074b) on every equipment change, and
  only what it links again runs. Equipment is removed and inserted again; the parts it keeps (stock body parts, head
  and hair at priority 1) get their controllers back from `mPartControllers` in `relinkSurvivingParts`, at the end of
  every `updateParts` (`relinkKeptControllers`, through `PartControllers::runUpdate`). Without that a kept skin holds
  the matrices of the moment of the change while the skeleton moves (the stock chest broke through pants and
  loincloths) and the head stops blinking and talking. Inherited from vsgopenmw (`2830e7e2b4`); OpenMW 0.51 keeps
  controllers with each part.
- **Rules:** a part's controllers are recorded only in `insertBoundedPart` and forgotten only in
  `removeIndividualPart` (`detachPart`), so a new way to link or remove a part must go through them. Anything linked
  outside the parts into the lists `clearControllers` empties must be attached again on every `updateParts` (the merged
  body is, in `relinkKeptControllers`; the holstered weapon and shield are rebuilt). A part can be inserted and then
  displaced in the same update (a mod record: a left glove listing a right hand, worn with a right gauntlet), so
  `detachPart` takes the part's recorded static, face and weapon controllers out of the live lists
  (`unlinkController`) before it forgets the part; without that they ran on the freed node and the game crashed (task
  0074d). `updateNpcBase` and a failed `compile()` start with `removeAllParts`, which empties the lists first.
- **Auto-play clocks (task 0074b):** an equipment change never empties the object's `autoPlay`
  (`MWAnim::Wielding::clearPartControllers` empties the static and weapon lists only; `Npc::clearControllers` adds the
  face). It holds what nothing would add again: an NPC's inventory lights (`addItemLight`, when the CharacterController
  is made and on each later `itemAdded`) and a creature's own model's controllers and lights. A part's auto-play
  controllers run in the part's own clock (`PartControllers::addAutoPlay`, `update`), which a kept part keeps and a
  part inserted again gets fresh (group 0 at 0, random groups at a random phase, as before: a new torch's flicker
  starts with a step of 0, task 0074c). The holstered weapon and shield have clocks of their own (`mScabbardClock`,
  `mHolsteredShieldClock`), emptied with their paths (`removeHolstered`). Do not put a part's controllers into
  `autoPlay`: nothing removes them from it. The spell cast glow (`addSpellCastGlow`) is never on an actor: it is
  called for non-actor casters, Lock and Open targets that `canLock` (doors, containers) and doors (`spellcasting.cpp`,
  `spelleffects.cpp`, `mwclass/door.cpp`).
- **A controller that joins a group already running (task 0074c):** `Anim::DeltaTime` starts at 0, so a controller
  that steps by it takes its group's whole timer as its first step. `Anim::Roll` relies on that (its random starting
  angle in a random group); `LightController` (`components/mwanimation/light.cpp`) must not: an inventory light added
  later (`UpdateItemLights::itemAdded`) joins the actor's group 0, which runs from its first inventory light, and
  strobed at up to 0.375 times that age times its colour, negative on some frames. Its first step is now 0, as in
  OpenMW 0.51. A new stateful controller that can join a running group must choose one of the two; never change
  `DeltaTime` itself. `Anim::CullParticles` needs neither (assessed in task 0074d): it uses the step only to count
  down a countdown that starts at 0 and as "time passed", so a large first step changes nothing visible.
- **Ownership:** `PartControllers` holds a `vsg::ref_ptr` to every controller and target it recorded until `beginPart`
  or `forget` for the part: a part whose node went another way runs its controllers on a detached node, not on freed
  memory. That covers the entries and the clocks only; the live lists (`Anim::Update`) hold raw pointers (see Rules).
- **Threads (task 0074d):** the clocks (`PartControllers::update`) run on the actor's update thread: a
  `vsgUtil::UpdateThreads` worker for an NPC or creature (`Scene::update`, one operation per cell), the main thread for
  the player and the inventory preview. Everything else in `PartControllers` runs on the main thread in the part update,
  never while that actor's update runs (the main thread waits for the workers). **`Npc::update` compiles nothing and
  edits no graph** (task 0074e, finding 3): `[Game] weapon sheathing`'s quiver change (`mQuiverDirty`, set by `show()`)
  is done by `Npc::updatePendingQuiver` on the main thread: `Scene::update` calls `MWAnim::Object::updateOnMainThread`
  (`Npc` overrides it) for every actor after `mUpdateThreads->run` (which ends in `waitIdle`), `Player::update` calls
  it on itself and `Preview::animate` calls `updatePendingQuiver` for the inventory doll (task 0074f, findings 3 and 4;
  `MainThreadUpdateTest` reaches the first two). `CompileContext::compile` calls `vsg::updateViewer`, which writes the
  command graph, the bins and the database pager without a lock, so it is main thread only.
- **A throw in a part update (task 0074d, corrected and changed in 0074e):** `vsgUtil::readNode` throws when the file cannot be opened:
  `vsgAdapters::vfs::read` opens it through `VFS::Manager::get` outside `nif::read`'s catch, so an Eden Archive entry
  that fails its check or is over `maxOpenSize`, or an I/O error of a BSA or loose file, throws (a missing file, or one
  no reader accepts, gets `Resource::Fallback`'s node instead). **A part's own read is caught, as OpenMW 0.51 does**
  (task 0074e, finding 7: `Npc::insertBoundedPart` logs "Error adding NPC part" and returns false, so `attachPart` makes
  no sound; `WieldingCreature::addItemPart` logs "Can not add creature part"): the slot stays taken and the body part
  under it stays hidden, and the update completes. What still throws (the ammunition's, holstered items' and quiver's
  reads, a value that is not a `std::exception`) reaches the caller, and who catches it differs: Lua's delayed action
  logs it; mwscript logs it and puts the target in `mInactive` (its script is off for the session); the inventory,
  barter and container windows lose the rest of the frame's input (`Engine::frame`); a mechanics-driven change (torch
  auto-equip at dusk, AI combat's ActionEquip, spell effects, race changes, werewolf and vampire rebuilds) has no catch
  before `Game::run`'s, which ends the game session. The kept parts and the merged body are linked again on every way
  out (`PartControllers::runUpdate`), and every equipment change and rebuild compiles the actor also on a throw
  (`MWRender::runEquipmentUpdate`, through `runCompiledUpdate` and `runRebuild`). After such a throw the merged body's
  regions are those of the last update that succeeded (`attachMergedBody` never ran). **A rebuild reads before it
  touches the actor** (finding 1): `Npc::makeBasePlan` names the files, `applyBasePlan` reads the skeleton, the
  custom bones and the animations into locals, and only then removes the parts and swaps the `MWAnim::Play`; the
  `CharacterController`, `Actor::mHead/mSpine/mRoot`, the player's neck and the pose and IK bones point into that Play,
  so a read that throws after the swap left them on freed memory. Keep every read before `removeAllParts`. **The
  type, view mode and part context change after the reads too** (task 0074f, finding 2): the plan carries them
  (`BasePlan::mNpcType`, `mViewMode`, `mPartContext`) and `applyBasePlan` commits them; `updateParts` and
  `Player::setViewMode` no longer set them first. "Exactly as it was" holds for what a read that throws leaves: the
  animation, its bones, and those four. A rebuild that failed is tried again at the next `updateParts` (or the same
  `setViewMode`), so a permanently unreadable file repeats its error on every equipment change of that actor.
  **`Actor::assignBones` is not virtual:** `applyBasePlan` reaches `Actor::assignBones`, never `Player::assignBones`
  (the first-person neck), so a rebuild through `RenderManager::rebuildPtr` (becoming a werewolf or vampire) leaves
  `Player::mNeck` dangling in first person and `Player::manualAnimation` crashes; `Player::setViewMode` calls
  `assignBones()` itself. Known, not fixed (0074f's stage log).
- **Tracing it:** `OPENEDEN_AUTOPLAY_TRACE=<text>` (`autoplaytrace.*`) logs, every frame, an `AUTOPLAYTRACE` line for
  each NPC or weapon-carrying creature whose record id or name contains the text: the static, weapon and face list
  sizes, each auto-play group's phase group, timer, controller count and light colours, the object's own groups first
  (`own=`), then each part's clock (`parts={part:...}`), and an `event=` line at each equipment change. Vanilla body
  parts, hair and heads carry no auto-play controller (a byte scan of `meshes/b` in the three BSAs found controllers
  only in heads, the face's morpher; armour and clothing in `meshes/a` and `meshes/c` have no auto-play node either),
  so a kept part's clock needs a modded or test part (task 0074b's stage log).
- **Inventory and carried lights (task 0074d):** `removeItemLight` removes an inventory light only when none of the
  item is left, and forgets the item (OpenMW 0.51's `ActorAnimation::itemRemoved`); before, removing one of a stack
  took the light away for good and the map entry and node leaked. (A re-add after the count reached 0 is a new stack
  and a new `Ptr`, and worked before: `ContainerStoreIteratorBase` skips count-0 entries.) A weapon-carrying
  creature's carried light is decorated with its light (`WieldingCreature::linkPart`), as an NPC's torch is, and
  `MWAnim::decorate` turns any node given a light by -90 degrees about X (as 0.51's `ActorAnimation::attach`): **a
  creature's torch is therefore drawn rotated now, a visible change of the default game** (task 0074d, recorded in
  0074e). `linkPart` adds the glow's environment data only with a glow colour (`addEnv` dereferences it whenever the
  alpha is not 1).
- **Checking it:** the equipment scene of `tools/mergedbody_check` (`SCENE=equipment`, stock): frames 009, 010, 045 and
  046 show the chest through the pants and loincloth when the controllers are lost. The part code runs in unit tests
  through `Npc`'s test constructor (`TestSetup`), `runPartUpdate`, `removeAllParts`, `attachMergedCopy` and
  `WieldingCreature::linkPart` (`apps/openmw_tests/mwrender/testnpcparts.cpp`); a change that empties `autoPlay`, stops
  running a clock or puts a part's auto-play controllers into `autoPlay` fails it. Task 0074e's seams: `Npc::runRebuild`
  (`testnpcrebuild.cpp`), `insertBoundedPart` and `WieldingCreature::addItemPart` (`testpartreaderrors.cpp`),
  `runPendingQuiver` (`testquiverupdate.cpp`), `runCompiledUpdate` of both classes and the real `updateEquipment` of a
  test actor, whose part update throws on the empty `Ptr` (`testcompiledupdates.cpp`); the shared fakes are in
  `partstestsupport.hpp`. `Npc::rebuild`'s own call into `runRebuild` has no test (it needs a record). The in-game
  throw scene is the 0074d clocks scene with a bad Eden Archive (not in the repository). A rebuild-time throw can be
  provoked in game by a file only the rebuild reads: the werewolf's custom-bone files (with `[Game] weapon sheathing`,
  `animations/wolf/skin/<name>.nif` as a failing Eden Archive entry) and a werewolf player (`Player->BecomeWerewolf` from
  a start script, third person; `SetWerewolf` is no command). The first-person files are read when the game starts and
  the werewolf's skeleton is served from a cache, so neither throws (task 0074f, finding 6).
- **The slot bookkeeping is `PartSlots`** (`partslots.*`, task 0069 stage e review): which inventory group holds each
  part slot and at what priority, the equipment pass (`wear`: slot list, priorities, robe and skirt reservations,
  part-less parts), the race's body parts (`addBodyParts`) and the merged body taking its slots (`releaseBodyParts`),
  moved out of `Npc` unchanged; `Npc` does the node work through `PartSlots::Nodes::detachPart` (forget the part's
  controllers, remove its path) and the attach callbacks (`attachPart`). Change the rules there, not in `Npc`:
  `apps/openmw_tests/mwrender/testpartslots.cpp` runs it over whole update sequences and checks that the merged body
  hides exactly what the stock parts do not draw (every one of the catalogue's 124,416 single outfits, and 3,000
  random sequences): merged equals stock given `PartSlots`. That `PartSlots` equals the code before it rests on the
  tests that pin its constants, the hand-derived expectations, and the second review's trace: the bookkeeping of
  `6151cceb4b^` put back into today's `npc.cpp` gave identical `OPENEDEN_PARTSLOTS_TRACE` lines for every actor of the
  equipment and torch scenes. The skin mark of a worn part is made in `PartSlots::Nodes::beforeWornPart`, just before
  the part asks for its slot, where `Npc::addPartGroup` made it (finding 10).
