# Task: CreativeMotion transitions and style layer in the game
Design doc section: CreativeMotion `docs/design` (style layer) and `OpenMW Animation System - Implementation Doc.md`; `docs/research/0022-creativemotion-in-game-status.md` (what is wired today)
Goal: make CreativeMotion's contribution to the game visible and controllable: transitions chosen by what the actor is doing, and a data-driven style layer (posture, cadence, additive offsets) that toddhow can A/B per actor.

Status at the time of writing (research note 0022): clip playback and group-to-group inertialization are wired and work (stage S0 below is done); nothing else of CM's animation runtime (graph, pose blend, swing-twist, style) is reached from `apps/openmw`.

Inherited code touched: `apps/openmw/mwrender/creativemotiondriver.*` (`CreativeMotionPoseDriver::update`), `apps/openmw/mwmechanics/character.cpp` (`runPoseDriver`, `createPoseControllers`). `components/creativemotion/` is never patched in place: a needed change to CM becomes a CreativeMotion task spec.
Inputs / outputs: style files (`*.style.json`, `opas.style`, read through the VFS), a settings table for the default style and per-actor overrides, a console command, the trace line.
Constraints: everything behind `creativemotion animation` (off stays exactly vanilla); style layer behind its own setting, off by default; no allocation per frame in `update`; the driver touches only its own actor (it runs on animation threads); deterministic (no clock, no unseeded randomness: the style's noise is seeded from the actor's ref number); no game assets in the repository.

## Decisions (with a recommendation)

1. **Where style data lives.** Recommend: `.style.json` files in the VFS under `styles/`, read at first use through the VFS, cached by name. Alternative: one settings file. VFS keeps mods able to ship styles.
2. **Which style an actor gets.** Recommend a documented rule, first match wins: (1) `[CreativeMotion Styles]` per-actor entry `actor.<refId> = <style>`, (2) per-race `race.<raceId> = <style>`, (3) `default style`. Settings, not code.
3. **What state CM is fed.** Recommend: keep inferring from the group (works today), and add an explicit `ActorState` snapshot (moving, running, sneaking, drawn weapon type, hit, turning, speed) to the driver's `update` for the style layer's cadence and posture only. Do not move the group choice into CM (the engine's `CharacterController` keeps choosing groups).
4. **Graph or no graph.** Recommend: not in this task. The style layer is additive on top of the sampled pose; the graph is a separate, larger task.

## Stages

- **S0 Transitions between groups: done** (before this task). Verified by trace in note 0022.
- **S1 Diagnostics: done in the task that wrote note 0022.** `creativemotion animation debug`, the `CMTRACE` line.
- **S2 Style data in the game.** Load a `.style.json` through the VFS into `opas::style` types; a test generates a strongly different test style (slower cadence, forward-leaning posture) in memory; unit tests parse and round-trip it with located errors. No driver change yet.
- **S3 Evaluate the style on the sampled pose.** The driver evaluates the style's layers after sampling and before the inertializer output is written (so a style change also blends), per actor, seeded noise. Tests on the pure evaluation (a style with zero weights returns the input pose exactly).
- **S4 Selection and A/B.** The documented rule above, `creativemotion style layer = false` (off), a console command `cmStyle` (prints the active style and layer weights of the selected actor; `cmStyle off` and `cmStyle on` toggle the layer for that actor) and the trace line showing the style name and weights.
- **S5 Evidence.** Frame sequences and bone traces: a walk with the default style against the strongly different test style (cadence and lean measured from bone traces), idle to walk with each; the setting off equals today's trace (compare with the `CMTRACE` bone values).

## Interfaces (added before implementation; the stage log below records what actually shipped)

Reading the code before building (2026-11-01) settled five more decisions, logged here:

5. **Bone names and axes are converted at one boundary, with no change to CM.** `opas::style::StyleModifier` finds bones by canonical names ("Spine", "LeftThigh") and pitches about X. Morrowind's bones are "bip01 spine1" and have their own local axes, so the stock modifier would do nothing. `MWRender::CreativeMotionStyleLayer` builds a second small `opas::rig::Skeleton` with the canonical names mapped from Morrowind's, and converts each styled bone's local rotation and translation into a canonical frame and back (frame F = Qc * (rest model rotation of the bone's parent), Qc = 180 degrees about Z so that the canonical "forward" is the model's +Y). CM is not edited.
6. **Transitions chosen by what the actor is doing** are a pure table (`classifyGroup`, `transitionSeconds`) behind a setting, `creativemotion transition rules` (off by default: with it off every change uses `creativemotion animation transition`, exactly as before).
7. **Cadence** is applied where the engine sets the movement animation's speed (`CharacterController::update`, "Layer 3.B"), multiplied by the style's `cadenceScale` weighted by how much the actor moves. With movement taken from the animation this slows the walk itself; with inertial locomotion the feet slow while the ground speed does not (stride is not scaled). Logged, not hidden.
8. **The style is applied to each bone group's sampled pose before the inertializer**, so switching a style, or `cmStyle on` and `off`, blends over the transition time instead of snapping. Time is the driver's own accumulated `duration` (no clock); noise is seeded from the style's seed mixed with the actor's reference number.
9. **Everything new is off by default** (`creativemotion style layer`, `creativemotion transition rules`), as the root guide asks of anything that changes animation feel. The shipped `cm_default` style is only subtle idle life (breathing and sway); the example `cm_heavy_test` is the strong one.

### Files and signatures (namespace `MWRender`, `apps/openmw/mwrender/creativemotionstyle.hpp`; no vsg, no engine singletons except `loadStyleRulesFromSettings`)

```cpp
enum class MotionKind { Idle, Ready, Walk, Run, Sneak, Strafe, Turn, Swim, Jump, Attack, Hit, Death, Other };
MotionKind classifyGroup(std::string_view groupName);                    // case-insensitive
float transitionSeconds(MotionKind from, MotionKind to, float baseSeconds);

struct ActorState { float mSpeedMps = 0; bool mRunning = false, mSneaking = false, mTurning = false, mWeaponDrawn = false, mHit = false; };
struct StyleWeights { float mMoving = 0, mIdle = 1, mPosture = 1; };
StyleWeights styleWeights(const ActorState&);
void makeEffectiveProfile(const opas::style::StyleProfile& base, const StyleWeights&, std::uint64_t actorSeed, opas::style::StyleProfile& out);
float cadenceFactor(const opas::style::StyleProfile& base, const StyleWeights&);   // 1 + (cadenceScale - 1) * mMoving

struct StyleRules { std::string mDefaultStyle; std::map<std::string, std::string, std::less<>> mActors, mRaces; };   // keys lower case
StyleRules makeStyleRules(std::string_view defaultStyle, const std::vector<std::pair<std::string, std::string>>& entries);  // entries "actor.<id>" / "race.<id>" -> style; other keys ignored; keys lower-cased
std::string selectStyleName(const StyleRules&, std::string_view actorId, std::string_view raceId);  // actor, then race, then default; ids compared case-insensitively
StyleRules loadStyleRulesFromSettings();   // Game "creativemotion default style" + category "CreativeMotion Styles" of Settings::Manager (user entries override default entries)

class StyleLibrary {                        // name -> profile, loaded once from "styles/<lower-case name>.style.json"
public:
    using Reader = std::function<std::optional<std::string>(const std::string& path)>;   // nullopt = missing
    explicit StyleLibrary(Reader);
    const opas::style::StyleProfile* find(std::string_view name);   // nullptr when the name is empty, the file is missing or invalid (logged once, with the file and the loader's located error); a returned pointer stays valid for the library's life; thread safe
};

class CreativeMotionStyleLayer {            // owned by the pose driver
public:
    explicit CreativeMotionStyleLayer(const opas::rig::Skeleton& skeleton);   // names are the driver's normalized Morrowind names
    int boundBones() const;                 // how many of the 15 canonical bones were found
    void apply(std::span<opas::math::Transform> pose, const opas::style::StyleProfile& effective, double timeS) const;   // pose is indexed like the skeleton; only found bones change; no allocation
};
std::string describeStyle(const opas::style::StyleProfile& base, bool enabled, const StyleWeights&);  // the cmStyle text
```

Morrowind bones used (normalized names): pelvis `bip01 pelvis`, spine `bip01 spine`, chest `bip01 spine1`, neck `bip01 neck`, head `bip01 head`, clavicles `bip01 l clavicle` / `bip01 r clavicle`, upper arms `bip01 l upperarm` / `bip01 r upperarm`, forearms `bip01 l forearm` / `bip01 r forearm`, thighs `bip01 l thigh` / `bip01 r thigh`, shins `bip01 l calf` / `bip01 r calf`.

`classifyGroup` (first match): names starting `swim` Swim; `idlesneak` Sneak; `idleswim` Swim; `idle` followed by digits only (or nothing) Idle; any other `idle*` (idlehh, idle1h, idle2c, idle2w, idlecrossbow, idlespell, idlestorm) Ready; a name starting `walk` or `run` and ending `left` or `right` Strafe; `walk*` Walk; `run*` Run; `sneak*` Sneak; `turn*` Turn; `jump*` Jump; exactly `weapononehand`, `weapontwohand`, `weapontwowide`, `handtohand`, `bowandarrow`, `crossbow`, `throwweapon` or `spellcast` Attack; `hit*`, `knockdown`, `knockout` Hit; `death*` Death; exactly `shield` or `torch` Ready; anything else Other.

`transitionSeconds` (first match; `baseSeconds <= 0` gives 0): to Hit 0.3 x base; to Death 0.5; to Attack 0.5; from Attack 1.2; from Walk, Run or Sneak to Idle ("stopping") 1.5; Walk to Run 0.8; any change between Idle and Turn (either way) 0.6; to or from Strafe 0.8; otherwise 1.0.

`styleWeights`: mMoving = clamp(speedMps / 1.0, 0, 1); mIdle = (turning or hit) ? 0 : 1 - mMoving; mPosture = hit ? 0 : (weaponDrawn ? 0.6 : 1).

`makeEffectiveProfile`: name, gait, timing and gaze copied; posture values times mPosture; idle: when mIdle < 0.05 breathing and sway amplitudes are 0 and the fidget interval is 0 (the modifier reads that as "no idle"), else the two amplitudes times mIdle, rates and interval unchanged; idle seed = base seed XOR `opas::style::hash64(actorSeed)`; layers keep names and bones, weight times mMoving for `limp` and times 1 for the others, clamped to [0, 1]. Makes no allocation when `out` already holds a profile with the same number of layers.

`describeStyle` text: `style <name> [on|off]  moving=<x.xx> idle=<x.xx> posture=<x.xx> cadence=<x.xx> slouch=<x.xxx> layers: <name>=<x.xx> ...` (layer weights are the base profile's; no layers prints `layers: none`).

### Pose driver additions (`creativemotiondriver.hpp`)

```cpp
void setStyle(const opas::style::StyleProfile* profile, std::uint64_t actorSeed);   // null = none; the profile must outlive the driver (StyleLibrary never frees)
void setStyleEnabled(bool);                 // default false: a driver that is never given an enabled style produces exactly the pose it produced before this task
void setActorState(const ActorState&);      // read by the next update()
void setTransitionRules(bool);              // default false
struct StyleStatus { const opas::style::StyleProfile* mProfile; bool mEnabled; StyleWeights mWeights; float mCadence; };
StyleStatus getStyleStatus() const;         // mCadence = cadenceFactor of the current profile and weights (1 with no profile or disabled)
```
With a profile set and enabled, `update` accumulates `duration` into the style time, builds the effective profile from the actor state, and applies the style to each group's sampled pose before the inertializer. A change of profile, or of enabled, counts as a change of every group (so it blends over the transition time). With rules on, each group's transition duration is `transitionSeconds(classifyGroup(previous group name), classifyGroup(new group name), transitionDuration)`; `TransitionStatus::mDuration` reports it.

### Settings

`[Game] creativemotion style layer` (bool, false), `creativemotion transition rules` (bool, false), `creativemotion default style` (string, `cm_default`); `Settings::game().mCreativeMotionStyleLayer`, `mCreativeMotionTransitionRules`, `mCreativeMotionDefaultStyle`. Overrides: category `[CreativeMotion Styles]`, keys `actor.<refId>` and `race.<raceId>` (lower-case compare). Shipped styles: `files/data/styles/cm_default.style.json` (name `cm_default`: format defaults) and `files/data/styles/cm_heavy_test.style.json` (name `cm_heavy_test`: slouchRad 0.35, headPitchRad 0.2, shoulderElevationRad -0.1, cadenceScale 0.6, strideScale 0.8, breathing amplitude 0.02 m, layers hunch 0.5 and limp 0.3); no game assets.

Console: `cmStyle` prints `describeStyle` for the target actor (the player when none is selected); `cmStyle on` and `cmStyle off` toggle the layer for that actor; `cmStyle <name>` forces a style.

## Stage log (2026-11-01)

**S2 to S4 done** in one change: `apps/openmw/mwrender/creativemotionstyle.{hpp,cpp}` (pure code), the driver additions, `CharacterController::configureStyle` and `styleCommand`, the `cmStyle` console command, settings, and the two shipped styles in `files/data/styles/`. CM itself was not edited (decision 5).

**Tests.** The new tests are written by a separate agent from the spec text only (`apps/components_tests/mwrender/test_creativemotion_style.cpp`, `test_creativemotion_style_driver.cpp`); they passed at the first build with no change to their logic. Windows: components-tests 3868, openmw-tests 830, eden-studio-tests 520, the 32 Lua tests; `CI/check_file_names.sh`, `check_extension_sdk.sh` and `check_no_osg.sh` pass. Not run: the Linux gate (only before pushes).

**S5 evidence.** Scenario: `scripts/cmtest.lua` of note 0022 (idle 2 s, walk 2 s, run 2 s, stop, turn, draw, attack, sheathe), Balmora, third person, Windows. Settings: CM on, style layer on, transition rules on, inertial locomotion high; the player gets `cm_default` (nothing names it) or `cm_heavy_test` (`[CreativeMotion Styles] actor.player = cm_heavy_test`). Traces and frames are in the session scratchpad (`cm/ev`). Frames looked at: side views of idle and of the walk; the heavy style leans the whole torso and head forward and drops the shoulders, the default stands upright.

| measure (player, `CMTRACE`) | `cm_default` | `cm_heavy_test` |
| --- | --- | --- |
| walk cycle period (thigh, s) | 1.10 | 1.70 (1.55 times; a cadence of 0.6 alone gives 1.67) |
| run cycle period (s) | 0.90 | 1.56 |
| walk animation speed multiplier | 0.78 | 0.49 |
| spine1 z component of the rotation, idle | 0.000 | 0.144 (about 16.5 degrees: slouch 0.35 half on spine and chest, plus hunch 0.5) |
| spine1 z, walk / run | 0.004 / 0.207 | 0.153 / 0.351 |

Transition durations with `creativemotion transition rules = true` and `creativemotion animation transition = 0.35` (the same in both styles): idle to walk 0.350, walk to run 0.280, run to walk 0.350, walk to idle 0.525 (stopping), idle to turn 0.210, turn to idle 0.210, idle to attack 0.175, attack to idle-ready 0.420. With rules off every change takes 0.350, as in the build before. Idle to run is not in the scripted scenario.

`cmStyle` (scripted run): `style cm_default [on]  moving=0.00 idle=1.00 posture=1.00 cadence=1.00 slouch=0.000 layers: none`; `cmStyle off` shows `[off]`; `cmStyle cm_heavy_test` shows `slouch=0.350 layers: hunch=0.50 limp=0.30`; `cmStyle nosuchstyle` says `No style named nosuchstyle (see the log)`. The trace line carries `style=<name>(on|off) w=(move idle posture) cadence=` when an actor has a style.

**Open and decisions.**
- A console choice (`cmStyle <name>`, `on`, `off`) belongs to the actor's `CharacterController`; the player's controller is rebuilt when a cell loads, which forgets it. The rule in settings is the lasting way; the console is for trying. I could not check in a scripted run that a console change reaches the running driver (scripted commands run before the player's real controller exists); that path (`mStyleDirty`, applied at the start of `runPoseDriver`) is covered by review only.
- Cadence: slowing the movement animation also slows root-motion movement (NPCs, and the player without inertial locomotion); stride is not scaled, so with inertial locomotion the feet slow while the speed does not. The speed that weights the style is measured from the animation's own movement divided by the cadence applied.
- The style is applied to all four bone-group poses each frame (15 bones, stack arrays); the idle noise is evaluated four times. Not optimised.
- `classifyGroup` allocates a small string; it runs only when a group changes with transition rules on.
- Reviewer's findings fixed: stale cadence after a driver reset and in compare-only mode, the console racing the animation thread, the speed feedback, the trace line only with a style, declaration order. The idle-to-turnleft flicker of the previous task was not reproduced in three scripted runs with all features on, and its old log predates the stall fix; the group choice (`mTurnAnimationThreshold`) was not touched.
- Off-equality of two live runs cannot be exact (frame times differ). The first idle pose is equal in both builds, the transition step table matches within noise, and the driver tests prove bit-identity.


## Acceptance criteria
- [x] With `creativemotion animation = false`, or the style layer off, the pose is unchanged: bit-identical in the driver tests, and the scripted scenario's transition table matches the build before the task within run-to-run noise (stage log; frame times differ between runs, so an exact bone-by-bone equality of two live runs is not possible, see S5).
- [x] A style file in the VFS is loaded, an actor selects it by the documented rule (settings `actor.player = cm_heavy_test`), and the console command prints its name and layer weights (scripted run; see S5 for what the console does not yet reach).
- [x] The trace shows a measurable difference in cadence (walk cycle period) and posture (spine rotation) between the default and the test style (S5 table).
- [x] Unit tests need no GPU, window, network or game data.
- [x] A fresh reviewing agent found no unresolved issue on lifetimes, threading and allocations (its blocking findings were fixed; two should-fix items are left open, see the log).

Out of scope: the animation graph / state machine in the game, IK changes, ragdolls, Eden Studio.
