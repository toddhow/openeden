#ifndef OPENEDEN_GAME_SKYRIMSE_DRAGONS_H
#define OPENEDEN_GAME_SKYRIMSE_DRAGONS_H

// Task 0253 (dragons, first slice): the pure half. A dragon profile (the dragon's clips, speeds and collision size), the
// flight machine (take off, circle the player at a height, land), the breath cone and the soul rule. No engine, renderer,
// script-language or clock type: every function takes the seconds it is given. The combat half (breath, soul, the
// intents the cell source writes as actor requests) is combat_dragon.cpp.
//
// Where each number comes from (the survey is docs/tasks/0253-skyrim-dragons.md section 1; `SkyrimDragonSurvey`):
//   VERIFIED  clip file names (the listing of Skyrim - Animations.bsa, meshes/actors/dragon/animations), durations
//             (the clips themselves), the sidecar events of the ground attacks (`HitFrame`) and the shout clips
//             (`Voice_SpellFire_Event` at 0.3333 s), the root motion of the walk (512 units in 1.3333 s) and of the glide
//             (2048 units in 1.3333 s), the vertical take-off (rise 2048 in 4 s) and vertical landing (fall 2048 in 3.3333
//             s), the skeleton (84 bones, clips of 83 tracks).
//   OURS      the cruise height, the circle's radius, the seconds on the ground and in the air, the landing distance, the
//             breath cone's half angle and reach, the mouth's place, the soul's delay and range, the capsule's radius.
// Off unless the pack setting `dragons` is on (with `creatures` and the engine's `[Game] neutral actors`).

#include <game_skyrimse/creatures.hpp>

#include <openeden/game/actor_rules.hpp>

#include <cstdint>
#include <string_view>

namespace game_skyrimse::dragons
{
    // ---- settings ---------------------------------------------------------------------------------------------------

    /// The pack setting (bool, default false): dragons walk, fight, breathe, fly and die as dragons. Needs `creatures`.
    inline constexpr std::string_view kDragonsKey = "dragons";
    /// Bool, default true: a dragon that fights takes off, circles and lands (off: it stays on the ground).
    inline constexpr std::string_view kFlightKey = "dragons flight";
    /// A developer aid: the editor id of a dragon NPC_ (for example `EncDragon01Fire`). With `dragons test replace` it
    /// takes the place of every placed actor of that editor id (a wolf in the cell the run starts in).
    inline constexpr std::string_view kTestPlaceKey = "dragons test place";
    inline constexpr std::string_view kTestReplaceKey = "dragons test replace";
    /// Developer aids: seconds on the ground between flights and in the air (0 keeps the defaults below).
    inline constexpr std::string_view kTestGroundKey = "dragons test ground seconds";
    inline constexpr std::string_view kTestAirKey = "dragons test air seconds";
    inline constexpr std::string_view kTestHealthKey = "dragons test health";
    inline constexpr std::string_view kTestDamageKey = "dragons test damage scale";
    inline constexpr std::string_view kTestKillKey = "dragons test kill at";

    // ---- the profile ------------------------------------------------------------------------------------------------

    /// The dragon's creature profile (the same struct as every creature's). `creatureProfileFor` does not know it (two
    /// older tests pin that the dragon skeleton has no profile), so the pack asks `dragonProfileFor`.
    [[nodiscard]] const CreatureProfile& dragonProfile();
    /// The profile when `skeletonPath` is the dragon's skeleton (any case, either slash, the model root optional), else
    /// null. The dragon priest (actors/dragonpriest) and a path that only contains the folder are not dragons.
    [[nodiscard]] const CreatureProfile* dragonProfileFor(std::string_view skeletonPath);
    [[nodiscard]] bool isDragon(const CreatureProfile* profile);

    // ---- the flight numbers -----------------------------------------------------------------------------------------

    // Clip files (names under actors/dragon/animations, no extension) and their VERIFIED durations in seconds.
    inline constexpr std::string_view kClipTakeoff = "mttakeoffvertical";
    inline constexpr std::string_view kClipCruise = "mtforward_glidebankright";
    inline constexpr std::string_view kClipApproach = "mtlandverticalapproach";
    inline constexpr std::string_view kClipTouchdown = "mtlandvertical";
    inline constexpr std::string_view kClipBreathGround = "ground_exhalefirebreath";
    inline constexpr std::string_view kClipBreathAir = "shout_flightexhaleflame";
    inline constexpr float kTakeoffSeconds = 4.f;
    inline constexpr float kApproachSeconds = 3.33333f;
    inline constexpr float kTouchdownSeconds = 1.33333f;
    /// The walk's root motion (mtforwardground): 512 units in 1.33333 s. A dragon on the ground is walked straight at its
    /// goal at this speed (no navmesh: the agent of its size finds no way), see SkyrimCellSource::update.
    inline constexpr float kGroundSpeed = 384.f;
    /// The glide's root motion: 2048 units in 1.33333 s.
    inline constexpr float kGlideSpeed = 1536.f;
    /// The clip's own rise (take-off) and fall (landing approach), for the record; the flight scales to the cruise height.
    inline constexpr float kTakeoffClipRise = 2048.f;

    struct FlightSettings
    {
        bool mEnabled = true;
        float mGroundSeconds = 14.f; ///< fighting on the ground before it takes off (ours)
        float mAirSeconds = 16.f; ///< circling before it lands (ours)
        float mCruiseHeight = 900.f; ///< above the ground it took off from (ours; the clip's rise is 2048)
        float mCircleRadius = 1500.f; ///< round the player (ours; with the height the breath's 2000 reaches)
        float mLandDistance = 900.f; ///< the landing point's distance from the player (ours)
        float mLandHover = 150.f; ///< the approach ends this high above the landing point, the touch-down brings it down
        float mTurnRate = 2.f; ///< radians a second the heading may turn
    };

    enum class Phase : std::uint8_t
    {
        Ground,
        Takeoff,
        Cruise,
        Approach,
        Touchdown,
    };

    struct FlightInput
    {
        float mDt = 0.f;
        double mX = 0.0, mY = 0.0, mZ = 0.0; ///< the dragon now (game units)
        float mHeading = 0.f; ///< radians as the engine stores it (0 faces +Y)
        double mPlayerX = 0.0, mPlayerY = 0.0, mPlayerZ = 0.0;
        bool mEngaged = false; ///< the dragon is fighting
        bool mDead = false;
        bool mBusy = false; ///< it is swinging, breathing or staggered: it does not take off now
        bool mFaceTarget = false; ///< in the air: turn to the target (a breath)
    };

    struct FlightOutput
    {
        bool mPlace = false; ///< the actor is airborne: put it at (x, y, z) facing heading
        bool mStartPose = false; ///< the pose clip changes now: start a new loop of mClip
        bool mLand = false; ///< the flight is over (or the dragon died): take it off the pose at (x, y, z)
        double mX = 0.0, mY = 0.0, mZ = 0.0;
        float mHeading = 0.f;
        std::string_view mClip;
        Phase mPhase = Phase::Ground;
    };

    /// The flight of one dragon. Deterministic: its output depends only on the inputs it was given, in order.
    class Flight
    {
    public:
        explicit Flight(FlightSettings settings = {});
        [[nodiscard]] FlightOutput step(const FlightInput& in);
        [[nodiscard]] Phase phase() const { return mPhase; }
        [[nodiscard]] bool airborne() const { return mPhase != Phase::Ground; }
        /// Seconds since the last landing (or the first engagement) the dragon has fought on the ground.
        [[nodiscard]] float groundTime() const { return static_cast<float>(mGround); }
        [[nodiscard]] std::uint32_t flights() const { return mFlights; }

    private:
        FlightSettings mSettings;
        Phase mPhase = Phase::Ground;
        double mGround = 0.0; ///< seconds fought on the ground (double: a sum of many float steps)
        double mPhaseTime = 0.0;
        float mHeading = 0.f;
        double mOriginX = 0.0, mOriginY = 0.0, mOriginZ = 0.0; ///< where it took off
        double mX = 0.0, mY = 0.0, mZ = 0.0; ///< the last placed position
        double mAngle = 0.0; ///< its bearing from the player on the circle (radians, 0 north, clockwise)
        double mRadius = 0.0;
        double mStartRadius = 0.0;
        double mTopZ = 0.0;
        double mLandX = 0.0, mLandY = 0.0, mLandZ = 0.0;
        double mApproachX = 0.0, mApproachY = 0.0, mApproachZ = 0.0;
        bool mHeadingSet = false;
        std::uint32_t mFlights = 0;
    };

    // ---- the breath -------------------------------------------------------------------------------------------------

    /// A cone from a point along an axis. `mAxis` is a unit vector.
    struct Cone
    {
        double mOx = 0.0, mOy = 0.0, mOz = 0.0;
        double mAx = 0.0, mAy = 1.0, mAz = 0.0;
        float mRange = 0.f;
        float mHalfAngle = 0.f; ///< radians
    };

    /// Half angle of the breath cone, radians (ours: 25 degrees), and the breath's reach when the spell's projectile
    /// names none (ours), game units. The mouth is this far ahead of the dragon's centre and this high above its feet on
    /// the ground, and this far below its centre in the air (ours).
    inline constexpr float kBreathHalfAngle = 0.4363f;
    inline constexpr float kBreathDefaultRange = 1600.f;
    inline constexpr float kBreathMouthForward = 450.f;
    inline constexpr float kBreathMouthHeightGround = 300.f;
    inline constexpr float kBreathMouthHeightAir = 0.f;
    /// A breath does not start closer than reach + this (the bite is the better attack), game units (ours).
    inline constexpr float kBreathMinExtra = 250.f;
    /// The seconds of the clip before the spell goes (`Voice_SpellFire_Event`, VERIFIED) and the breath's length when
    /// the spell gives none (the three breath spells give a cast duration of 2.4 or 2.3 s, VERIFIED).
    inline constexpr float kBreathFireDelay = 0.33333f;
    inline constexpr float kBreathDefaultSeconds = 2.4f;
    /// The least gap between two breaths of one dragon, whatever the shout's recovery says (ours).
    inline constexpr float kBreathMinGap = 6.f;
    /// The breath's clip is this long (ground_exhalefirebreath and shout_flightexhaleflame, VERIFIED): the dragon is busy
    /// until then.
    inline constexpr float kBreathClipSeconds = 3.f;

    /// The cone from `origin` at the target point: axis towards (tx, ty, tz). A zero-length or non-finite aim gives a cone
    /// that contains nothing (range 0).
    [[nodiscard]] Cone breathCone(
        double ox, double oy, double oz, double tx, double ty, double tz, float range, float halfAngle);
    /// True when the point is within the cone's range and half angle. Non-finite values are outside.
    [[nodiscard]] bool coneContains(const Cone& cone, double x, double y, double z);

    // ---- the soul ---------------------------------------------------------------------------------------------------

    /// A dead dragon's soul goes to the player this long after it died, if the player is within this distance (ours:
    /// the real game burns the flesh away first and takes the soul when the player is close).
    inline constexpr float kSoulDelaySeconds = 4.f;
    inline constexpr float kSoulRange = 2000.f;
    [[nodiscard]] bool soulAbsorbs(float secondsSinceDeath, double playerDistance);
    inline constexpr std::string_view kSoulMessage = "Dragon soul absorbed";

    // ---- the combat half's settings and state -----------------------------------------------------------------------

    struct Settings
    {
        bool mOn = false;
        FlightSettings mFlight;
        /// A developer aid: when above 0 a dragon starts with this much health (cut to its maximum), so a run can kill it.
        float mTestHealth = 0.f;
        /// A developer aid: the breath's damage is multiplied by this (so a run lasts longer than three breaths).
        float mTestDamageScale = 1.f;
        /// A developer aid: when above 0 every dragon is killed (health 0, the death reported) when the combat clock reaches
        /// this many seconds, so a run can show the death and the soul without winning the fight.
        float mTestKillAt = 0.f;
    };

    /// Everything the combat keeps for one dragon.
    struct DragonState
    {
        openeden::sdk::game::ActorHandle mHandle;
        Flight mFlight;
        float mBreathReadyAt = 0.f; ///< the combat clock
        bool mBreathing = false;
        bool mBreathFired = false; ///< the spell went (after kBreathFireDelay)
        bool mBreathAir = false;
        std::uint32_t mBreathSpellId = 0; ///< the breath spell of the dragon's own shout (0: none known)
        bool mBreathLooked = false;
        float mNextTrace = 0.f; ///< the combat clock of the next position line of the trace
        float mNextVisual = 0.f; ///< the combat clock of the next drawn-only copy of the breath
        float mBreathStarted = 0.f;
        float mBreathSeconds = 0.f;
        float mBreathRecovery = 0.f;
        std::uint32_t mBreaths = 0;
        bool mDeathSeen = false;
        float mDiedAt = 0.f;
        bool mSoulTaken = false;
        bool mDeathClipPlayed = false;
        bool mFlightOrdered = false; ///< a pose is out at the core (useFurniture was written)
        explicit DragonState(const FlightSettings& flight)
            : mFlight(flight)
        {
        }
    };
}

#endif
