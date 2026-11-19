#ifndef OPENEDEN_GAME_SKYRIMSE_ACTOR_TABLE_H
#define OPENEDEN_GAME_SKYRIMSE_ACTOR_TABLE_H

// The Skyrim SE pack's side of the actor hooks (task 0135 stage S3, sdk/include/openeden/game/actor_rules.hpp): a table
// of the actors the core has told the pack about, keyed by the placement's WorldId, and, for this stage only, one
// trivial rule: actors whose editor id matches the setting `[extension.skyrimse] actor test walk` pace back and forth
// along X. It proves the contract end to end; there are no AI packages yet (stage S5).
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only. update() allocates nothing once the table is built
// (the rows live in a std::map, which allocates only on add).

#include <game_skyrimse/follower.hpp>
#include <game_skyrimse/package_brain.hpp>

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/follower_rules.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// The pack setting (`[extension.skyrimse]`) that names the test actor; empty (the default) means nobody paces.
    inline constexpr std::string_view kActorTestWalkKey = "actor test walk";
    /// Longest editor id the setting may name (Skyrim's own are far shorter).
    inline constexpr std::size_t kMaxActorTestWalkName = 128;

    /// The pack setting (bool, off by default) that makes the NPCs run their AI packages (stage S5).
    inline constexpr std::string_view kActorPackagesKey = "actor packages";
    /// The pack setting (bool, on by default, read only with `actor packages`) that puts a sleeper on its bed when it
    /// cannot walk there (task 0135 stage S6b).
    inline constexpr std::string_view kActorSleepSnapKey = "actor sleep snap";

    /// The pack setting for the go-to test rule (stage S4): `<editor id>,<x>,<y>,<z>`; empty (the default) means off.
    inline constexpr std::string_view kActorTestGotoKey = "actor test goto";

    /// The parsed go-to setting.
    struct TestGoto
    {
        std::string mEditorId; ///< lower case
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
    };

    /// Parses `<editor id>,<x>,<y>,<z>` (spaces around the parts are ignored; the numbers must be finite). False and
    /// `out` untouched for anything else, including an editor id longer than kMaxActorTestWalkName.
    [[nodiscard]] bool parseTestGoto(std::string_view text, TestGoto& out);

    /// How the test rule paces: this fast (units a second, a named estimate of a slow walk), for this long each way
    /// (so about 120 units each way: inside an inn).
    inline constexpr float kPaceSpeed = 40.f;
    inline constexpr float kPaceHalfPeriodSeconds = 3.f;

    class SkyrimActorTable final : public openeden::sdk::game::ActorRules, public openeden::sdk::game::FollowerRules
    {
    public:
        struct Stats
        {
            std::uint64_t mAdded = 0;
            std::uint64_t mRemoved = 0;
            std::uint64_t mRequestsWritten = 0;
            std::uint64_t mRequestsRefused = 0; ///< the core said the handle was stale: the row was dropped
            std::uint64_t mGoalsIssued = 0; ///< go-to requests the core accepted
            std::uint64_t mGoalsArrived = 0;
            std::uint64_t mGoalsNoPath = 0;
            std::uint64_t mGoalsBlocked = 0;
        };

        /// Names the editor id of the test pacer (case-insensitive); empty switches the rule off. A value longer than
        /// kMaxActorTestWalkName is refused (false, the rule stays off).
        bool setTestWalk(std::string_view editorId);
        [[nodiscard]] const std::string& testWalk() const { return mTestWalk; }

        /// Stage S4: the go-to test rule. Actors with the editor id of the setting walk once to its point at a walk
        /// pace; the result is counted in Stats and kept in the row. An empty `text` switches the rule off. False (the
        /// rule stays off) for text parseTestGoto refuses.
        bool setTestGoto(std::string_view text);
        [[nodiscard]] bool hasTestGoto() const { return mHasGoto; }
        /// The result the core told about the actor's last go-to; nothing before then or for an unknown actor.
        [[nodiscard]] std::optional<openeden::sdk::game::GoalResult> goalResultOf(
            openeden::sdk::game::WorldId reference) const;

        void onGoalFinished(openeden::sdk::game::ActorHandle handle, openeden::sdk::game::GoalResult result) override;

        /// Stage S5: turns the AI package scheduler on over `source` (which must outlive the table). Actors that are
        /// added from now on get their packages; off (no brain) is the default and costs nothing.
        void enablePackages(
            PackageSource& source, std::uint64_t seed, FurnitureSource* furniture = nullptr, bool snapSleepers = false);
        [[nodiscard]] const PackageBrain* packageBrain() const { return mBrain.get(); }
        /// Task 0271: see PackageBrain::setForceGreetHook (no brain: nothing).
        void setForceGreetHook(PackageBrain::ForceGreetHook hook)
        {
            if (mBrain)
                mBrain->setForceGreetHook(std::move(hook));
        }

        /// Task 0310: see PackageBrain::setFollowPlayerPackages (no brain: nothing).
        void setFollowPlayerPackages(bool on)
        {
            if (mBrain)
                mBrain->setFollowPlayerPackages(on);
        }
        /// Task 0306: see PackageBrain::setApproachGreeters (no brain: nothing).
        void setApproachGreeters(bool on)
        {
            if (mBrain)
                mBrain->setApproachGreeters(on);
        }

        /// Stage S6f: the player begins or ends a conversation with the actor of this placement (forwarded to the
        /// brain).
        void onConversation(openeden::sdk::game::WorldId reference, bool begins)
        {
            if (mBrain)
                mBrain->onConversation(reference, begins);
        }

        /// Task 0274: see PackageBrain::setPackageEndHook.
        void setPackageEndHook(PackageBrain::EndHook hook)
        {
            if (mBrain)
                mBrain->setPackageEndHook(std::move(hook));
        }
        /// Task 0348: see PackageBrain::setPackageBeginHook and setUseMagicHook.
        void setPackageBeginHook(PackageBrain::EndHook hook)
        {
            if (mBrain)
                mBrain->setPackageBeginHook(std::move(hook));
        }
        void setUseMagicHook(PackageBrain::UseMagicHook hook)
        {
            if (mBrain)
                mBrain->setUseMagicHook(std::move(hook));
        }
        /// Task 0274: see PackageBrain::setMount.
        void setMount(openeden::sdk::game::WorldId rider, openeden::sdk::game::ActorHandle horse)
        {
            if (mBrain)
                mBrain->setMount(rider, horse);
        }
        /// Task 0274: see PackageBrain::setVehicleRider.
        bool setVehicleRider(openeden::sdk::game::WorldId reference, bool rides)
        {
            return mBrain && mBrain->setVehicleRider(reference, rides);
        }

        /// Task 0264: see PackageBrain::forceStand.
        void forceStand(openeden::sdk::game::WorldId reference)
        {
            if (mBrain)
                mBrain->forceStand(reference);
        }

        /// Task 0194 (scenes): the handle of an actor of the table (invalid when unknown), a package that overrides its
        /// schedule (forwarded to the brain), and whether the brain has the actor standing or done with its package.
        /// The placements of the actors of the table, in ascending order (task 0194: who can take part in an ambient
        /// scene).
        [[nodiscard]] std::vector<openeden::sdk::game::WorldId> actorReferences() const
        {
            std::vector<openeden::sdk::game::WorldId> out;
            out.reserve(mRows.size());
            for (const auto& [reference, row] : mRows)
                out.push_back(reference);
            return out;
        }
        [[nodiscard]] openeden::sdk::game::ActorHandle handleOf(openeden::sdk::game::WorldId reference) const
        {
            const auto it = mRows.find(reference);
            return it == mRows.end() ? openeden::sdk::game::ActorHandle{} : it->second.mHandle;
        }
        /// Task 0277: actors travelling to another cell are carried through doors (needs the package brain).
        void setCarryTravellers(bool on);
        void setSnapMustComplete(bool on); // task 0341
        /// Task 0286: see PackageBrain::setTravelMoveHook; asked before or after the brain is made.
        void setTravelMoveHook(PackageBrain::TravelMoveHook hook);
        /// Task 0277: where the brain thinks the actor stands (arrivals and leg ends); false for an unknown actor.
        [[nodiscard]] bool brainPlace(openeden::sdk::game::WorldId reference, double& x, double& y, double& z) const;
        bool forcePackage(openeden::sdk::game::WorldId reference, const PackageRecord* package)
        {
            return mBrain != nullptr && mBrain->forcePackage(reference, package);
        }
        [[nodiscard]] bool packageDone(openeden::sdk::game::WorldId reference) const
        {
            if (!mBrain)
                return true;
            const auto v = mBrain->view(reference);
            return !v || v->mPhase == PackageBrain::Phase::Done || v->mPhase == PackageBrain::Phase::Idle;
        }

        /// Stage S6g: forwarded to the brain (no brain: ready at once).
        bool prepareTalk(openeden::sdk::game::WorldId reference)
        {
            return mBrain ? mBrain->prepareTalk(reference) : true;
        }
        [[nodiscard]] bool readyToTalk(openeden::sdk::game::WorldId reference) const
        {
            return mBrain ? mBrain->readyToTalk(reference) : true;
        }

        void onActorAdded(const openeden::sdk::game::ActorInfo& info) override;
        void onActorRemoved(openeden::sdk::game::ActorHandle handle) override;
        void update(float seconds, openeden::sdk::game::ActorRequestWriter& writer) override;

        // Task 0196 (followers). They need the package brain (`actor packages`): without it every call is refused.
        /// The test rule `[extension.skyrimse] follower test recruit = <editor id>[,<editor id>...]`: actors with a
        /// matching editor id follow the player as soon as they appear. Empty switches it off. False for a bad list.
        bool setTestFollowers(std::string_view list);
        /// `follower test dismiss after = <seconds>`: the test followers are dismissed this many seconds of frame time
        /// after the first one appeared (0 never).
        void setTestDismissAfter(float seconds) { mTestDismissAfter = seconds > 0.f ? seconds : 0.f; }
        /// Recruit or dismiss the actor of this placement (the real path: the DialogueFollower alias, or a test).
        bool recruit(openeden::sdk::game::WorldId reference);
        bool dismiss(openeden::sdk::game::WorldId reference);
        /// Task 0239: "Wait here" and "Follow me". False when there is no brain or nothing changed.
        bool setFollowerWaiting(openeden::sdk::game::WorldId reference, bool waiting);
        [[nodiscard]] bool followerWaiting(openeden::sdk::game::WorldId reference) const;
        /// Replaces the set of followers (a loaded save). False (nothing changed) when there is no brain.
        bool restoreFollowers(const std::vector<FollowerEntry>& entries);
        [[nodiscard]] std::vector<openeden::sdk::game::WorldId> followerIds() const;
        /// The followers with their home cells (what the save chunk keeps).
        [[nodiscard]] std::vector<FollowerEntry> followerEntries() const;
        /// How the table finds the cell a placement lives in (the pack gives it the world view's placement index). A
        /// home restored from a save wins over the lookup.
        void setHomeLookup(std::function<openeden::sdk::game::WorldId(openeden::sdk::game::WorldId)> lookup)
        {
            mHomeLookup = std::move(lookup);
        }
        [[nodiscard]] openeden::sdk::game::WorldId homeCell(openeden::sdk::game::WorldId reference) const override;

        // FollowerRules
        [[nodiscard]] std::size_t followers(openeden::sdk::game::WorldId* out, std::size_t capacity) const override;
        [[nodiscard]] bool isFollower(openeden::sdk::game::WorldId reference) const override;
        void onFollowerCarried(openeden::sdk::game::WorldId reference) override
        {
            ++mCarried;
            (void)reference;
        }
        [[nodiscard]] std::uint64_t carried() const { return mCarried; }

        [[nodiscard]] std::size_t size() const { return mRows.size(); }
        [[nodiscard]] std::size_t pacers() const { return mPacers; }
        [[nodiscard]] const Stats& stats() const { return mStats; }
        /// True when an actor with this placement id is in the table.
        [[nodiscard]] bool has(openeden::sdk::game::WorldId reference) const { return mRows.count(reference) != 0; }

    private:
        struct Row
        {
            openeden::sdk::game::ActorHandle mHandle;
            openeden::sdk::game::WorldId mBase = openeden::sdk::game::kNoWorldId;
            bool mPaces = false;
            float mElapsed = 0.f; ///< seconds this actor has paced
            int mDirection = 0; ///< last request: +1 east, -1 west, 0 none yet
            bool mGoes = false; ///< matches the go-to test rule
            bool mGoalIssued = false;
            std::optional<openeden::sdk::game::GoalResult> mGoalResult;
        };

        std::map<openeden::sdk::game::WorldId, Row> mRows;
        std::string mTestWalk; ///< lower case
        TestGoto mTestGoto;
        bool mHasGoto = false;
        std::size_t mGoers = 0;
        std::size_t mPacers = 0;
        Stats mStats;
        std::unique_ptr<PackageBrain> mBrain;
        bool mCarryTravellers = false; // task 0277
        bool mSnapMustComplete = false; // task 0341
        PackageBrain::TravelMoveHook mTravelMoveHook; // task 0286
        std::set<std::string> mTestFollowerNames; ///< lower case editor ids (task 0196)
        float mTestDismissAfter = 0.f;
        float mTestFollowClock = -1.f; ///< seconds since the first test follower appeared; negative: none yet
        bool mTestDismissed = false;
        std::set<openeden::sdk::game::WorldId> mTestFollowerRefs;
        std::function<openeden::sdk::game::WorldId(openeden::sdk::game::WorldId)> mHomeLookup;
        mutable std::map<openeden::sdk::game::WorldId, openeden::sdk::game::WorldId> mHomes;
        std::uint64_t mCarried = 0;
    };
}

#endif
