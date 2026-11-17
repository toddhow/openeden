#ifndef OPENEDEN_GAME_SKYRIMSE_RIDING_H
#define OPENEDEN_GAME_SKYRIMSE_RIDING_H

// Task 0251 (horse riding, first slice): the rules of mounting, riding and dismounting a horse. Plain values and the
// SDK's riding interfaces only (sdk/include/openeden/game/riding_rules.hpp); no engine type.
//
//   * Mount: the player activates a horse he owns (or any horse the test setting names). The pack asks the horse's
//     appearance for its animation project (`horseproject`) through a lookup the cell source gives, so a horse is a
//     horse only when the pack setting `creatures` is on (the profile of tasks 0212 and 0216 gives the horse its own
//     clips and speeds).
//   * Ride: the player's controls drive the horse: forward is walk (and run while the run control is on), the turn
//     controls and the view turn the horse (it faces its direction of travel), back stops it. The player's body is
//     carried to the saddle every frame and plays the rider clip of the gait (`horse_rider/...`).
//   * Dismount: the jump control, or activating the horse again. The player is put down beside the horse.
//
// Every number named kXxx is ours (an estimate) unless the comment says it was measured on the installed data
// (docs/tasks/0251-skyrim-horse-riding.md section 1). No combat while riding in this slice; nothing is saved (a load
// puts the player on his feet).
//
// Threading: main thread only (the core calls update() once a frame before the physics step).

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/bindings.hpp>
#include <openeden/game/paired_animation.hpp>
#include <openeden/game/riding_rules.hpp>
#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace game_skyrimse
{
    /// The pack setting (bool, default false; needs the engine's `[Game] neutral actors` and the pack's `creatures`).
    inline constexpr std::string_view kRidingKey = "riding";
    /// The test setting: the editor id (case-insensitive) of the horse base the player owns, or `*` for every horse.
    /// Empty (the default): no horse is owned.
    inline constexpr std::string_view kHorseTestOwnedKey = "horse test owned";

    /// How fast a rider turns his horse with the turn controls, radians a second. OURS.
    inline constexpr float kRidingTurnRate = 1.7f;
    /// How far to the right of the horse the player is put down when he dismounts, game units. OURS.
    inline constexpr float kDismountSide = 110.f;
    /// Task 0278: the form id of PlayerHorseFaction (the stables' purchase script, TIF__00068CF1, sets the bought horse's
    /// rank in it to 1: VERIFIED on the data) and the seconds over which the 7 unit step between a paired clip's end pose
    /// and the riding loop's pose (or the dismount's start pose and the loop's) is blended away (OURS).
    inline constexpr std::uint32_t kPlayerHorseFactionId = 0x00068D78;
    inline constexpr float kHandoverBlendSeconds = 0.25f;
    /// JobHostlerFaction: every stablemaster holds it at rank -1 and the horse-purchase lines test it (VERIFIED: NPC_ SNAM).
    inline constexpr std::uint32_t kJobHostlerFactionId = 0x00051593;
    /// 1 at the hand-over, falling linearly to 0 after kHandoverBlendSeconds.
    [[nodiscard]] inline float handoverWeight(float seconds)
    {
        if (!(seconds > 0.f))
            return 1.f;
        return seconds >= kHandoverBlendSeconds ? 0.f : 1.f - seconds / kHandoverBlendSeconds;
    }
    /// A forward control above this starts the horse (below it the stick is at rest). OURS.
    inline constexpr float kRidingDeadZone = 0.15f;

    /// Task 0255: the third person camera is pulled back to this many times the distance the player has on foot while
    /// he rides (a horse is about twice as long as a person; the saddle is 135 units up). OURS.
    inline constexpr float kRidingCameraScale = 2.2f;
    /// The script package of the riding camera (task 0255).
    inline constexpr std::string_view kRidingNamespace = "skyrimse.riding";

    /// What a save keeps of a ride (task 0255): that the player rode, and which horse (its placement id). A load puts
    /// the player on his feet where he was (the horse's own place is not saved: decision 0005's rule for actors).
    struct RidingState
    {
        bool mMounted = false;
        std::uint32_t mHorse = 0;
        friend bool operator==(const RidingState&, const RidingState&) = default;
    };

    /// Where the rider's origin goes, relative to the horse's origin, in the horse's frame (x to its right, y forward,
    /// z up), game units. MEASURED: the rest-pose world position of the `SaddleBone` node of the horse skeleton
    /// (`SkyrimRidingSurvey`, docs/tasks/0251-skyrim-horse-riding.md section 1), plus kRiderLift (OURS).
    inline constexpr float kSaddleX = 1.67f;
    inline constexpr float kSaddleY = -0.98f;
    inline constexpr float kSaddleZ = 134.92f;
    inline constexpr float kRiderLift = 0.f;

    /// Task 0267: the pack setting (bool, default false; needs `riding`). On, mounting and dismounting play the
    /// game's own paired clips (the rider's side on the player's body, the horse's side on the horse) instead of
    /// putting the player on the saddle and on the ground at once.
    inline constexpr std::string_view kPairedAnimationsKey = "paired animations";
    /// The clips (sdk/include/openeden/game/paired_animation.hpp names the "~1/" and "~2/" convention). MEASURED:
    /// docs/tasks/0267-skyrim-paired-animations.md section 1.
    inline constexpr std::string_view kPairedMountClip = "~1/actors/sharedkillmoves/human&horse/paired_mount";
    inline constexpr std::string_view kPairedMountHorseClip = "~2/actors/sharedkillmoves/human&horse/paired_mount";
    inline constexpr std::string_view kPairedDismountClip = "~1/actors/sharedkillmoves/human&horse/paired_dismount";
    inline constexpr std::string_view kPairedDismountHorseClip
        = "~2/actors/sharedkillmoves/human&horse/paired_dismount";
    /// How long a mount waits for a clip that is still being read before it mounts without the animation. OURS.
    inline constexpr float kPairedWaitSeconds = 3.f;

    enum class RidePhase : std::uint8_t
    {
        Ride = 0, ///< on the saddle (or on foot)
        Mounting = 1, ///< the paired mount clip is playing
        Dismounting = 2, ///< the paired dismount clip is playing
    };

    enum class Gait : std::uint8_t
    {
        Stand = 0,
        Walk = 1,
        Run = 2,
    };

    /// The gait the controls ask for: back or no forward is Stand; forward is Walk, or Run while the run control is on.
    [[nodiscard]] Gait gaitFor(const openeden::sdk::game::RiderControls& controls);

    /// The rider's yaw after `dt` seconds: the player's own yaw (the view's) turned by the turn control. Finite
    /// inputs only (anything else keeps `heading`); the result is in (-pi, pi].
    [[nodiscard]] float nextRiderHeading(float heading, float turn, float dt);

    /// The horse's velocity in game units a second for a gait, heading and the horse's own walk and run speeds.
    struct Velocity
    {
        float mX = 0.f;
        float mY = 0.f;
    };
    [[nodiscard]] Velocity velocityFor(Gait gait, float heading, float walkSpeed, float runSpeed);

    /// The saddle in the world: the horse's position plus the saddle offset turned by the horse's heading.
    struct Point
    {
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
    };
    [[nodiscard]] Point saddlePoint(double horseX, double horseY, double horseZ, float heading);

    /// Where the player is put down: to the right of the horse (at its heading), at the horse's height.
    [[nodiscard]] Point dismountPoint(double horseX, double horseY, double horseZ, float heading);

    /// Where a point given in the horse's frame (x to its right, y forward, z up, the unit of the clip) is in the
    /// world.
    [[nodiscard]] Point horseFramePoint(
        double horseX, double horseY, double horseZ, float heading, double x, double y, double z);

    /// The heading the horse must have for a rider standing at (riderX, riderY) to be at its left flank (the paired
    /// mount clip starts there). `fallback` when the two are (nearly) at the same place or a value is not finite.
    [[nodiscard]] float headingForLeftFlank(double horseX, double horseY, double riderX, double riderY, float fallback);

    /// The rider clip of a gait: a path under the actor's animations folder (`horse_rider/idle`, ...), with no
    /// extension. The files are in Skyrim - Animations.bsa under actors/character/animations/horse_rider.
    [[nodiscard]] std::string_view riderClipFor(Gait gait);

    /// What the lookup says about an actor base.
    struct HorseInfo
    {
        float mWalkSpeed = 0.f; ///< units a second, the horse's own (creature row)
        float mRunSpeed = 0.f;
    };
    /// True with `out` filled when the base (with this placement as the variation) is a horse.
    using HorseLookup = std::function<bool(openeden::sdk::game::WorldId base, std::uint32_t variation, HorseInfo& out)>;

    class Riding final : public openeden::sdk::game::RidingRules
    {
    public:
        /// Off (the default): nothing is tracked and canMount is false.
        void setEnabled(bool on) { mEnabled = on; }
        [[nodiscard]] bool enabled() const { return mEnabled; }
        /// The test setting (kHorseTestOwnedKey). Applies to actors added from now on.
        void setTestOwned(std::string editorId);
        void setHorseLookup(HorseLookup lookup) { mLookup = std::move(lookup); }
        /// Task 0278: does the player own the horse (placement id)? A purchase made through the game's own scripts
        /// answers true. Asked besides the test setting, which stays an override.
        using OwnedLookup = std::function<bool(openeden::sdk::game::WorldId reference)>;
        void setOwnedLookup(OwnedLookup lookup) { mOwnedLookup = std::move(lookup); }
        /// Called with the placement of the horse when the brain must stand aside (true) or may take its package up
        /// again (false).
        void setBrainControl(std::function<void(openeden::sdk::game::WorldId, bool)> control)
        {
            mBrainControl = std::move(control);
        }
        /// Task 0267 (kPairedAnimationsKey). Off (the default): mounts and dismounts are instant, as in task 0251.
        void setPairedAnimations(bool on) { mPaired = on; }
        [[nodiscard]] bool pairedAnimations() const { return mPaired; }
        [[nodiscard]] RidePhase phase() const { return mPhase; }
        /// The horse's placement id while the player sits on the saddle, 0 during a paired mount or dismount (his
        /// body is drawn at the horse's origin then, not following the saddle) and on foot.
        [[nodiscard]] std::uint32_t bodyMountReference() const
        {
            return mMounted && mPhase == RidePhase::Ride ? static_cast<std::uint32_t>(mHorse.reference) : 0u;
        }
        /// Where the player's body is drawn relative to the player (the world offset to add): zero on foot and on the
        /// saddle, the horse's origin minus the saddle while a paired clip plays (the player himself stays on the
        /// saddle, where his camera is; the clip is in the horse's frame).
        [[nodiscard]] Point bodyOffset() const { return mMounted && mPhase != RidePhase::Ride ? mBodyOffset : Point(); }
        /// Task 0278: what is added to bodyOffset(): the step between a paired clip's end pose and the riding loop (after a
        /// mount) or between the loop and the dismount clip's start pose (during a dismount), faded away over
        /// kHandoverBlendSeconds. Zero otherwise.
        [[nodiscard]] Point blendOffset() const
        {
            float w = 0.f;
            if (mMounted && mPhase == RidePhase::Dismounting)
                w = handoverWeight(mPhaseTime);
            else if (mMounted && mPhase == RidePhase::Ride && mStepActive)
                w = handoverWeight(mStepTime);
            return { mStepDelta.mX * w, mStepDelta.mY * w, mStepDelta.mZ * w };
        }
        /// Called with a line for the log when the rider mounts or dismounts.
        void setLog(std::function<void(const std::string&)> log) { mLog = std::move(log); }

        void onActorAdded(const openeden::sdk::game::ActorInfo& info);
        void onActorRemoved(openeden::sdk::game::ActorHandle handle);

        // RidingRules
        [[nodiscard]] bool canMount(openeden::sdk::game::ActorHandle handle) const override;
        void onMountRequested(openeden::sdk::game::ActorHandle handle) override;
        /// Task 0255: a load door is about to take the player: he gets off now (see RidingRules::dismountNow).
        void dismountNow(openeden::sdk::game::RidingWriter& player) override;

        /// Task 0255: what a save keeps, and the state a load brings back (the player is put on his feet, beside the
        /// saddle's place, at the next update; nothing else of the ride returns).
        [[nodiscard]] RidingState savedState() const;
        void restoreState(const RidingState& state);
        [[nodiscard]] openeden::sdk::game::ActorHandle mountedHorse() const override;
        /// The placement id of the horse he sits on, 0 when on foot.
        [[nodiscard]] std::uint32_t mountReference() const
        {
            return mMounted ? static_cast<std::uint32_t>(mHorse.reference) : 0u;
        }

        /// Once a frame, after the actors' own rules. Does nothing unless a mount is requested or the player rides.
        void update(float seconds, openeden::sdk::game::ActorRequestWriter& writer);

        /// True while the player sits on a horse.
        [[nodiscard]] bool riding() const { return mMounted; }
        /// The rider clip of the current gait (empty when not riding): what the player's body plays.
        [[nodiscard]] std::string_view clip() const;
        [[nodiscard]] Gait gait() const { return mGait; }
        [[nodiscard]] openeden::sdk::game::ActorHandle mount() const { return mHorse; }
        [[nodiscard]] std::uint32_t mounts() const { return mMountCount; }
        [[nodiscard]] std::uint32_t dismounts() const { return mDismountCount; }

    private:
        struct Row
        {
            openeden::sdk::game::ActorHandle mHandle;
            openeden::sdk::game::WorldId mBase = openeden::sdk::game::kNoWorldId;
            bool mOwned = false; ///< the editor id matched the test setting when the row was made
            std::int8_t mHorse = -1; ///< -1 not asked yet, 0 no, 1 yes
            HorseInfo mInfo;
        };

        /// The row of a handle, asking the lookup once; null for an unknown handle.
        Row* rowOf(openeden::sdk::game::ActorHandle handle) const;
        void log(const std::string& line) const
        {
            if (mLog)
                mLog(line);
        }
        void dismount(openeden::sdk::game::ActorRequestWriter& writer, openeden::sdk::game::RidingWriter* player,
            const std::string& why);

        // Task 0267: a paired clip is playing.
        void beginDismount(openeden::sdk::game::ActorRequestWriter& writer, openeden::sdk::game::RidingWriter* player,
            const openeden::sdk::game::PairedClipInfo& info);
        void updatePaired(
            float seconds, openeden::sdk::game::ActorRequestWriter& writer, openeden::sdk::game::RidingWriter& player);
        void clearHorseClip(openeden::sdk::game::RidingWriter* anyWriter);

        bool mEnabled = false;
        bool mPaired = false; ///< task 0267
        RidePhase mPhase = RidePhase::Ride;
        float mPhaseTime = 0.f; ///< seconds into the paired clip that plays
        Point mBodyOffset; ///< task 0267: see bodyOffset()
        Point mStepDelta; ///< task 0278: the step to blend away (see bodyOffset)
        float mStepTime = 0.f;
        bool mStepActive = false; ///< a mount just ended: the riding body is blended from the clip's last pose
        OwnedLookup mOwnedLookup;
        float mPairedWait = 0.f; ///< seconds a mount has waited for its clip
        openeden::sdk::game::PairedClipInfo mPairedInfo; ///< of the clip that plays
        std::string mTestOwned; ///< lower case
        HorseLookup mLookup;
        std::function<void(openeden::sdk::game::WorldId, bool)> mBrainControl;
        std::function<void(const std::string&)> mLog;
        mutable std::unordered_map<openeden::sdk::game::WorldId, Row> mRows; ///< by placement

        // The request the core made (activation) and what it asked for.
        openeden::sdk::game::ActorHandle mRequest;
        bool mHasRequest = false;

        /// task 0267: a load ended a paired clip: the horse's is cleared at the next update
        bool mClearHorseClip = false;
        bool mStopHorse = false; ///< dismountNow left the horse's movement request for the next update to clear
        bool mRestorePending = false; ///< a load said he was riding: put him on his feet at the next update
        bool mMounted = false;
        openeden::sdk::game::ActorHandle mHorse;
        HorseInfo mHorseInfo;
        Gait mGait = Gait::Stand;
        float mHeading = 0.f;
        bool mJumpWasUp = false; ///< the jump control has been released since the mount (one press, one dismount)
        bool mDismountRequested = false; ///< the player activated the horse he sits on
        bool mHorseLost = false; ///< the horse's actor was removed: dismount in the next update
        Point mLastHorse; ///< where the horse was last seen
        std::uint32_t mMountCount = 0;
        std::uint32_t mDismountCount = 0;
    };

    /// The pack's save chunk "riding" (version 1, task 0255): u8 mounted (0 or 1) then u32 horse placement id; exactly
    /// five bytes. Anything else is refused and changes nothing.
    class RidingChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "riding";

        using Getter = std::function<RidingState()>;
        using Setter = std::function<void(const RidingState&)>;

        RidingChunk(Getter get, Setter set)
            : mGet(std::move(get))
            , mSet(std::move(set))
        {
        }

        [[nodiscard]] std::string_view chunkName() const override { return kName; }
        [[nodiscard]] std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        Getter mGet;
        Setter mSet;
    };

    /// The script package `core.skyrimse.riding` 1.0 (task 0255), Read only: `active() -> bool` (the player sits on a
    /// horse) and `camera_scale() -> number` (kRidingCameraScale). The player script of the riding camera reads it.
    class RidingBindings
    {
    public:
        explicit RidingBindings(const Riding& riding);
        ~RidingBindings();
        RidingBindings(const RidingBindings&) = delete;
        RidingBindings& operator=(const RidingBindings&) = delete;

        [[nodiscard]] openeden::sdk::DeclareResult declareTo(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
