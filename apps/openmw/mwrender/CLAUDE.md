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
- **The console command is `togglenavmesh`,** not `tnm`. `tcb` toggles the Jolt draw.
- **Recast's own colours:** the navmesh draw is visibly more opaque than the OSG build's at the same alpha (Recast's `duRGBA` values; task 0010's research note 0004 records the comparison).
- **The HDR path (task 0025):** with `[Rendering] hdr` on, `mRenderGraph` draws into an `HdrTarget` (not the window), `mTonemapGraph` is the graph that has the window, and the GUI graph and `syncExtents` follow it. `Reflect` and `PostProcess` take their single-sample sources from the target (`setSource`, `setDepthSource`, called every frame: a resize replaces the images). Command order: scene, tonemap, post-process, screenshot. Gate for any change here: frames of the default path must not change (compare with `diffmap` under `[Input] camera sensitivity = 0.01`, which makes the camera hold still; the torch lights flicker, so compare interiors without lights such as `coc "ToddTest"`).
- **A new shader file must be listed in `files/shaders/CMakeLists.txt`.** The build copies only the listed files, so an unlisted include is "Failed to load include code" in the glslang text of the log, and the game then hangs at start without another message (task 0027). Reconfigure happens by itself after the list changes; check a new build folder, not an old one.
