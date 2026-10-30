# apps/openmw/mwrender

The game's renderer on VulkanSceneGraph (vsgopenmw's, merged in task 0009) plus OpenEden's features rebuilt on it in task 0011. Read the root `CLAUDE.md` first.

## What lives here

- **vsgopenmw's renderer:** `rendermanager.*` builds the scene graph and command graphs; `scene.*`, `animcontext.*`, `npc.*`, `player.*` and `effects.*` hold objects and actors. Its animation system lives in `components/animation` (`Anim::Transform`, `Anim::Bones`, channels and controllers) and `components/mwanimation` (`MWAnim::Play` and `MWAnim::Actor`).
- **OpenEden's features (task 0011):**
  - `bonepose.*`: the one bone interface the post-animation features use.
  - `actorikcontroller.*`: foot IK, look-at, smoother turns, inertial lean and compound hitboxes.
  - `creativemotiondriver.*`: CreativeMotion's animation runtime.
  - `debuggeometry.*`, `joltdebugdraw.*` and `navmesh.*`: debug drawing.
- **Task 0012's parity work:** `rain.*`, `waterripples.*` and `weathereffects.*` (weather and water effects), `groundcover.*` and `groundcoverinstances.*` (instanced groundcover through `Pipeline::Mode::INSTANCE` and `files/shaders/lib/groundcover.glsl`).

## Frame order for an actor

1. `MWMechanics::CharacterController::runAnimation` may run on an animation thread, one actor per call. It runs `MWAnim::Play::runAnimation`, which advances states, text keys and movement and writes bones through the keyframe controllers unless `Play::updateBones` is false. It then runs `CreativeMotionPoseDriver::update` and `apply` when "creativemotion animation" is on; Play's bone writes are then off.
2. `CharacterController::postAnimation` runs on the main thread. It runs `MWAnim::Actor::manualAnimation` (head and spine turns), then `ActorIkController::update` and `applyPostAnimation` (IK, lean and hitboxes).
3. `Scene::update` updates objects, then skins. Anything written to bones before this step is drawn this frame.

## Rules

- **Off means unchanged:** with every OpenEden setting at its default, the controllers never touch a bone. `ActorIkController` binds no bones, `CharacterController` creates no pose driver, and `Play::updateBones` stays true. Keep it that way: create state lazily, only when a setting is on.
- **Bones through the bone interface:**
  - Post-animation features read and write bones only through `BonePose`, which converts to OSG math internally until task 0013.
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

## Pitfalls

- **Instance attributes come before texture coordinates.** The default shader's vertex inputs are numbered in binding order, and the texture coordinate array's length is a specialization constant, so `Mode::INSTANCE`'s two per-instance inputs sit before the texture coordinates (`lib/attributes.glsl`, `Pipeline::Graphics`), and a draw's instance arrays must be inserted before its first `vec2Array` (`MWRender::Groundcover`).
- **vsgXchange 1.1 tags stb_image textures as sRGB** (TGA, PNG, JPG and BMP), and the GPU then darkens them on sampling. `vsgUtil::readOptionalImage` and `readImageFromMemory` strip it (`vsgUtil::removeSrgb`). Read images only through them, or terrain and many statics come out nearly black.
- **Specialization constants in boolean expressions:**
  - The default shader's `colorMode` is a Vulkan specialization constant.
  - Writing `colorMode == 3 || colorMode == 4` in `lib/material/terms.glsl` (a trial fogged unlit mode, since removed) made every frame render nothing (black, or a fully transparent screenshot) on this machine's NVIDIA driver, with no error logged. `colorMode >= 3` worked.
  - Keep conditions on specialization constants to one comparison, and check a screenshot after any shader change.
- **The console command is `togglenavmesh`,** not `tnm`. `tcb` toggles the Jolt draw.
- **Recast's own colours:** the navmesh draw is visibly more opaque than the OSG build's at the same alpha (Recast's `duRGBA` values; task 0010's research note 0004 records the comparison).
