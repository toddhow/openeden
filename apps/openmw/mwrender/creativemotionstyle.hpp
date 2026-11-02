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

    /// What the style layer needs to know about the actor this frame.
    struct ActorState
    {
        float mSpeedMps = 0.f; // ground speed, metres per second
        bool mRunning = false;
        bool mSneaking = false;
        bool mTurning = false;
        bool mWeaponDrawn = false;
        bool mHit = false;
    };

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
