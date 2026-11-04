#ifndef GAME_RENDER_CREATIVEMOTIONSTYLE_H
#define GAME_RENDER_CREATIVEMOTIONSTYLE_H

#include <opas/math/Transform.hpp>
#include <opas/rig/Skeleton.hpp>
#include <opas/style/StyleModifier.hpp>
#include <opas/style/StyleProfile.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// CreativeMotion's style layer and transition rules, as the game uses them (task 0053, docs/tasks/0053-*.md).
// Nothing here touches the scene graph: it works on opas poses, in metres, and on animation group names.

namespace MWRender
{
    /// What an animation group is, for choosing how long a change into or out of it takes.
    enum class MotionKind
    {
        Idle,
        Ready,
        Walk,
        Run,
        Sneak,
        Strafe,
        Turn,
        Swim,
        Jump,
        Attack,
        Hit,
        Death,
        Other
    };

    /// Morrowind's animation group name (any case) to the kind of motion it is.
    MotionKind classifyGroup(std::string_view groupName);

    /// How long the change from one kind to another takes: a factor of the base duration (see the task spec).
    float transitionSeconds(MotionKind from, MotionKind to, float baseSeconds);

    /// What the actor means to do, for the transition rules (task 0067, stage 1).
    struct TransitionIntent
    {
        bool mStarting = false; // the actor wants to move and has not reached its speed yet
        bool mStopping = false; // the actor let go of the movement keys and is still moving
        float mTurnRate = 0.f; // radians per second, signed
    };

    /// Duration factors of the intent rules, as data: the defaults are used unless the [CreativeMotion Transitions]
    /// category of the settings names one (see makeTransitionTimes). The defaults equal what transitionSeconds without
    /// intent gives (task 0067 spec, stage 1, and the stage 1 review): with them the only difference the intent makes
    /// is mFlip, which the spec asks for. The start factor and the fast/slow turn split are not in the spec: they are
    /// data for toddhow to try (open questions in the spec).
    struct TransitionTimes
    {
        float mStopping = 1.5f; // walking, running or sneaking to idle with the stop intent (the rules' sStopping)
        float mFlip = 1.0f; // the same change without the intent (the actor was blocked): the base time (spec)
        float mStarting = 1.0f; // idle to walking, running or sneaking with the start intent (the rules: the base)
        float mFastTurn = 0.6f; // to or from a turn in place at or above mFastTurnRate (the rules' sIdleTurn)
        float mSlowTurn = 0.6f; // to or from a turn in place below it (the rules' sIdleTurn)
        float mFastTurnRate = 1.5f; // radians per second
    };

    /// The largest duration factor accepted from the settings: 4 times the default base of 0.25 s is 1 s, above the
    /// longest blend the spec asks for (a stop that settles within 0.8 s); a larger number is refused, not clamped.
    constexpr float sMaxTransitionFactor = 4.f;
    /// The largest "fast turn rate" accepted, radians per second (Morrowind turns the player at most at about 2 pi per
    /// second with the mouse; 50 is far above it).
    constexpr float sMaxFastTurnRate = 50.f;

    /// Keys "stopping", "flip", "starting", "fast turn", "slow turn" (factors from 0 to sMaxTransitionFactor) and
    /// "fast turn rate" (above 0, at most sMaxFastTurnRate). An unknown key, or a value that is not such a number
    /// (text, "2,5", inf, nan, out of range), keeps the default and adds a located line to problems (when given).
    TransitionTimes makeTransitionTimes(
        const std::vector<std::pair<std::string, std::string>>& entries, std::vector<std::string>* problems = nullptr);

    /// Reads the [CreativeMotion Transitions] category of the settings (defaults, then the user's settings.cfg).
    TransitionTimes loadTransitionTimesFromSettings(std::vector<std::string>* problems = nullptr);

    /// transitionSeconds with the actor's intent: a stop with the intent takes times.mStopping, the same change
    /// without it times.mFlip; a start with the intent takes times.mStarting; a turn in place takes mFastTurn or
    /// mSlowTurn by the turn rate. Every other change is as transitionSeconds says.
    float transitionSeconds(MotionKind from, MotionKind to, float baseSeconds, const TransitionIntent& intent,
        const TransitionTimes& times);

    /// What the style layer needs to know about the actor this frame.
    struct ActorState
    {
        float mSpeedMps = 0.f; // ground speed, metres per second
        bool mRunning = false;
        bool mSneaking = false;
        bool mTurning = false;
        bool mWeaponDrawn = false;
        bool mHit = false;
        // Intent (task 0067, stage 1): computed engine side by IntentTracker.
        float mTargetSpeedMps = 0.f; // the speed the actor is trying to reach, 0 when it wants to stand still
        float mAccelerationMps2 = 0.f; // change of the smoothed speed, signed
        float mTurnRate = 0.f; // radians per second, signed
        bool mStarting = false;
        bool mStopping = false;
        bool mStanceChanging = false; // sneak or run was switched lately (not used by a rule yet: stage 2 and later)
    };

    /// One frame of what the engine knows about the actor's movement.
    struct IntentInput
    {
        float mDt = 0.f; // seconds
        float mMeasuredSpeedMps = 0.f; // ground speed this frame, from the movement actually made
        bool mWantsToMove = false; // the movement keys (or the AI's movement request) are pressed
        float mIntendedSpeedMps = 0.f; // the speed it would reach, used when mWantsToMove
        float mYawDeltaRad = 0.f; // change of the actor's heading since the last frame, wrapped to -pi..pi
        bool mStanceChanged = false; // sneak or run was switched this frame
    };

    /// What IntentTracker works out.
    struct ActorIntent
    {
        float mSmoothedSpeedMps = 0.f;
        float mTargetSpeedMps = 0.f;
        float mAccelerationMps2 = 0.f;
        float mTurnRate = 0.f;
        bool mStarting = false;
        bool mStopping = false;
        bool mStanceChanging = false; // a stance change happened within the last sStanceChangeHoldS seconds
    };

    /// How long a switch of sneak or run counts as a stance change, so that the change of animation group that follows
    /// it a frame or two later still sees it (seconds).
    constexpr float sStanceChangeHoldS = 0.3f;

    /// Smooths the measured speed and says whether the actor is starting or stopping. One per actor; update() makes no
    /// allocation. Not thread safe: one thread (the one that runs the actor's animation) calls it.
    class IntentTracker
    {
    public:
        ActorIntent update(const IntentInput& input);
        void reset() { *this = IntentTracker(); }

    private:
        float mSmoothed = 0.f;
        float mStanceHold = 0.f;
    };

    /// The presets of [Game] creativemotion preset.
    enum class CreativeMotionPreset
    {
        Off,
        Natural
    };

    /// "off" and "natural" (any case); anything else is Off and recognised is false.
    CreativeMotionPreset parseCreativeMotionPreset(std::string_view text, bool* recognised = nullptr);

    /// One boolean setting as the user has it: its value, and whether the user's settings.cfg names it
    /// (Settings::Manager::mUserSettings, which holds the user's lines and the values set at run time).
    struct FeatureSetting
    {
        bool mValue = false;
        bool mExplicit = false;
    };

    /// Which CreativeMotion features are on once the preset and the individual settings are combined.
    struct CreativeMotionFeatures
    {
        bool mAnimation = false;
        bool mTransitionRules = false;
        bool mStyleLayer = false;
        bool mIntentTransitions = false; // the rules above also look at the actor's intent
    };

    /// Combines the preset with the three settings. A setting the user names explicitly wins, true or false; a setting
    /// the user does not name takes the preset's choice (Natural: on; Off: the setting's own value). The intent rules
    /// are on with Natural and the transition rules on.
    CreativeMotionFeatures resolveCreativeMotionFeatures(CreativeMotionPreset preset, FeatureSetting animation,
        FeatureSetting transitionRules, FeatureSetting styleLayer);

    /// The preset and the three [Game] settings as the settings have them now, combined; recognised is false (and the
    /// result is that of Off) when the preset's name is not known.
    CreativeMotionFeatures loadCreativeMotionFeaturesFromSettings(bool* recognised = nullptr);

    /// How much of each part of a style applies this frame, from 0 to 1.
    struct StyleWeights
    {
        float mMoving = 0.f; // 0 standing, 1 walking or faster
        float mIdle = 1.f; // idle life (breathing, sway, fidgets)
        float mPosture = 1.f;
    };

    StyleWeights styleWeights(const ActorState& state);

    /// Fills out with base scaled by the weights; the idle noise seed is mixed with actorSeed. Makes no allocation
    /// when out already holds a profile with the same number of layers.
    void makeEffectiveProfile(const opas::style::StyleProfile& base, const StyleWeights& weights,
        std::uint64_t actorSeed, opas::style::StyleProfile& out);

    /// Factor for the movement animation's speed: 1 + (cadenceScale - 1) * moving.
    float cadenceFactor(const opas::style::StyleProfile& base, const StyleWeights& weights);

    /// Which style an actor gets: its own entry, then its race's, then the default.
    struct StyleRules
    {
        std::string mDefaultStyle;
        std::map<std::string, std::string, std::less<>> mActors; // lower-case actor id
        std::map<std::string, std::string, std::less<>> mRaces; // lower-case race id
    };

    /// entries are (key, style name) pairs; "actor.<id>" and "race.<id>" are used, other keys ignored.
    StyleRules makeStyleRules(
        std::string_view defaultStyle, const std::vector<std::pair<std::string, std::string>>& entries);

    std::string selectStyleName(const StyleRules& rules, std::string_view actorId, std::string_view raceId);

    /// Reads [Game] "creativemotion default style" and the [CreativeMotion Styles] category of the settings.
    StyleRules loadStyleRulesFromSettings();

    /// Style profiles by name, each read once from "styles/<lower-case name>.style.json".
    /// Owned by whoever creates it; profiles are never freed while it lives, so pointers stay valid.
    class StyleLibrary
    {
    public:
        using Reader = std::function<std::optional<std::string>(const std::string& path)>;

        explicit StyleLibrary(Reader reader);

        /// nullptr when the name is empty or the file is missing or invalid (logged once). Thread safe.
        const opas::style::StyleProfile* find(std::string_view name);

    private:
        Reader mReader;
        std::mutex mMutex;
        std::map<std::string, std::unique_ptr<opas::style::StyleProfile>, std::less<>> mProfiles;
    };

    /**
     * @brief Applies a style profile to a pose made of Morrowind bones.
     *
     * opas::style::StyleModifier finds bones by canonical names and rotates about canonical axes (X right, Y back,
     * forward is -Y). This class is the one boundary for both: it maps Morrowind's normalized bone names to the
     * canonical ones and converts each styled bone's local rotation and translation into the canonical frame and back
     * (frame = 180 degrees about Z times the model-space rest rotation of the bone's parent), so that a positive
     * slouch leans the model toward its +Y. Owned by the pose driver; apply() allocates nothing.
     */
    class CreativeMotionStyleLayer
    {
    public:
        explicit CreativeMotionStyleLayer(const opas::rig::Skeleton& skeleton);

        int boundBones() const { return static_cast<int>(mBound.size()); }

        /// pose is indexed like the skeleton given to the constructor; only bones that were found can change.
        void apply(
            std::span<opas::math::Transform> pose, const opas::style::StyleProfile& effective, double timeS) const;

    private:
        struct Bound
        {
            int mDriverIndex;
            opas::math::Quat mFrame; // canonical frame of the bone
        };
        std::vector<Bound> mBound; // in canonical-skeleton order
        opas::rig::Skeleton mCanonical;
        std::unique_ptr<opas::style::StyleModifier> mModifier;
    };

    /// The text of the cmStyle console command.
    std::string describeStyle(const opas::style::StyleProfile& base, bool enabled, const StyleWeights& weights);
}

#endif
