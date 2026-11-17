#ifndef OPENEDEN_GAME_SKYRIMSE_KILLMOVE_H
#define OPENEDEN_GAME_SKYRIMSE_KILLMOVE_H

// Kill moves, first slice (task 0278): a one-handed sword blow of the player that would kill a person plays the
// game's own paired clip `actors/character/animations/paired_1hmkillmove` (human against human) with the victim, and the
// victim dies at the clip's end.
//
// What the data says (docs/tasks/0267 section 2, docs/tasks/0278): a kill move is ONE clip for two skeletons, as the
// horse mount is, but its frame (`PairedRoot`) is the KILLER's and the victim's own root node (the track "2_", no bone)
// stands somewhere in it (157 units in front of the killer for a bear). Two ways to meet it: move the victim to the
// clip's place, or keep the victim where the blow left him and put the killer where the clip wants him. Actors cannot
// be moved by a pack, the player can (carryPlayer), so this is the second: `anchorFor`.
//
// The rules (eligibility, the seeded roll, the anchor) are pure; `KillMoves` is the small state machine the pack's cell
// source drives once a frame. No engine type, no clock, no unseeded random number.
#include <openeden/game/actor_rules.hpp>
#include <openeden/game/paired_animation.hpp>
#include <openeden/game/riding_rules.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace game_skyrimse::killmove
{
    /// The pack settings (declared in extension.json): off by default.
    inline constexpr std::string_view kKillMovesKey = "kill moves";
    inline constexpr std::string_view kKillMovesChanceKey = "kill moves chance";

    /// The clip, one name per side (the core's paired clip convention).
    inline constexpr std::string_view kKillerClip = "~1/actors/character/animations/paired_1hmkillmove";
    inline constexpr std::string_view kVictimClip = "~2/actors/character/animations/paired_1hmkillmove";
    /// WEAP DNAM animation type 1: one-handed sword. The clip is a sword kill; the other one-handed types (2 to 4)
    /// have kill moves of their own that this slice does not play.
    inline constexpr std::uint8_t kSwordAnimType = 1;
    /// The share of lethal sword blows that become a kill move (OURS: the game's own rule is in its behaviour graphs).
    inline constexpr float kDefaultChance = 0.25f;
    /// Seconds a kill move waits for its clip to be read before the victim simply dies (OURS).
    inline constexpr float kClipWaitSeconds = 3.f;

    /// What is known when a blow would kill.
    struct Candidate
    {
        bool mAttackerIsPlayer = false;
        bool mVictimIsPlayer = false;
        std::uint8_t mWeaponAnimType = 0;
        bool mVictimIsCreature = false; ///< a creature or a dragon: its skeleton is not the human one
        bool mVictimEssential = false; ///< an essential actor collapses instead (task 0264)
        bool mVictimDead = false;
        bool mBash = false;
        bool mActive = false; ///< a kill move is already playing
    };

    /// Player, a one-handed sword, a living non-essential person, no bash, none already playing.
    [[nodiscard]] bool eligible(const Candidate& candidate);

    /// A number in [0, 1) from (seed, counter): splitmix64, the same on every platform.
    [[nodiscard]] float roll(std::uint64_t seed, std::uint64_t counter);
    /// `roll(seed, counter) < chance`; a chance at or below 0 never, at or above 1 always, NaN never.
    [[nodiscard]] bool chosen(std::uint64_t seed, std::uint64_t counter, float chance);

    /// The killer's heading (0 faces +Y, positive turns to +X) that looks from one point at another; `fallback` when
    /// they are the same place or a value is not finite.
    [[nodiscard]] float headingToward(double fromX, double fromY, double toX, double toY, float fallback);

    /// Where the killer stands and which way he faces: the clip's frame (`PairedRoot`) as the world sees it.
    struct Anchor
    {
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
        float mHeading = 0.f;
    };

    /// The anchor that puts the victim's own root node (`secondRoot`, in the clip's frame: x to the killer's right, y
    /// forward, yaw counter-clockwise) where the victim stands (`victimX`, `victimY`, `victimZ`), with the killer
    /// facing `killerHeading`.
    [[nodiscard]] Anchor anchorFor(double victimX, double victimY, double victimZ, float killerHeading,
        const openeden::sdk::game::PairedPose& secondRoot);

    /// The heading the victim must have to match `secondRoot`'s turn inside a frame facing `killerHeading` (engine
    /// headings are clockwise, yaw is counter-clockwise), wrapped to (-pi, pi].
    [[nodiscard]] float victimHeading(float killerHeading, double secondYaw);

    /// A point of the clip's frame (x right, y forward, z up) in the world.
    struct WorldPoint
    {
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
    };
    [[nodiscard]] WorldPoint toWorld(const Anchor& anchor, double x, double y, double z);

    enum class Phase : std::uint8_t
    {
        Idle,
        Asking, ///< the clip's numbers are being read
        Playing,
    };

    /// One kill move at a time. Main thread; update() runs inside the cell source's update (ActorRules phase).
    class KillMoves
    {
    public:
        void setEnabled(bool on) { mEnabled = on; }
        [[nodiscard]] bool enabled() const { return mEnabled; }
        void setChance(float chance) { mChance = chance; }
        void setSeed(std::uint64_t seed) { mSeed = seed; }
        /// Holds an actor (its package off, its goal cancelled) or lets it go.
        void setHold(std::function<void(openeden::sdk::game::WorldId reference, bool hold)> hold) { mHold = std::move(hold); }
        /// Called once when the clip is over or could not be played: the victim must die now.
        void setFinish(std::function<void(openeden::sdk::game::ActorHandle victim)> finish) { mFinish = std::move(finish); }
        void setLog(std::function<void(const std::string&)> log) { mLog = std::move(log); }

        /// A blow would kill `victim`. True: it is a kill move (the caller leaves the victim alive and invulnerable
        /// until `setFinish`'s callback). Draws one number from the seed per lethal blow that passed `eligible`.
        bool offer(const Candidate& candidate, openeden::sdk::game::ActorHandle victim);

        void update(float seconds, openeden::sdk::game::ActorRequestWriter& writer);

        [[nodiscard]] Phase phase() const { return mPhase; }
        [[nodiscard]] bool active() const { return mPhase != Phase::Idle; }
        /// The clip the player's body plays (empty unless Playing).
        [[nodiscard]] std::string_view playerClip() const { return mPhase == Phase::Playing ? kKillerClip : std::string_view(); }
        [[nodiscard]] openeden::sdk::game::ActorHandle victim() const { return mVictim; }
        [[nodiscard]] std::uint32_t started() const { return mStarted; }
        [[nodiscard]] std::uint32_t finished() const { return mFinished; }
        /// A load or a cell change ended the actors: forget the move without finishing it.
        void reset();

    private:
        void finish(openeden::sdk::game::ActorRequestWriter* writer, const char* why);

        bool mEnabled = false;
        float mChance = kDefaultChance;
        std::uint64_t mSeed = 0;
        std::uint64_t mCounter = 0;
        Phase mPhase = Phase::Idle;
        openeden::sdk::game::ActorHandle mVictim;
        float mWait = 0.f;
        float mTime = 0.f;
        openeden::sdk::game::PairedClipInfo mInfo;
        Anchor mAnchor;
        float mVictimHeading = 0.f;
        std::uint32_t mStarted = 0;
        std::uint32_t mFinished = 0;
        std::function<void(openeden::sdk::game::WorldId, bool)> mHold;
        std::function<void(openeden::sdk::game::ActorHandle)> mFinish;
        std::function<void(const std::string&)> mLog;
    };
}

#endif
