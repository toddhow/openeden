#ifndef OPENEDEN_GAME_SKYRIMSE_ACTOR_TABLE_H
#define OPENEDEN_GAME_SKYRIMSE_ACTOR_TABLE_H

// The Skyrim SE pack's side of the actor hooks (task 0135 stage S3, sdk/include/openeden/game/actor_rules.hpp): a table
// of the actors the core has told the pack about, keyed by the placement's WorldId, and, for this stage only, one
// trivial rule: actors whose editor id matches the setting `[extension.skyrimse] actor test walk` pace back and forth
// along X. It proves the contract end to end; there are no AI packages yet (stage S5).
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only. update() allocates nothing once the table is built
// (the rows live in a std::map, which allocates only on add).

#include <game_skyrimse/package_brain.hpp>

#include <openeden/game/actor_rules.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace game_skyrimse
{
    /// The pack setting (`[extension.skyrimse]`) that names the test actor; empty (the default) means nobody paces.
    inline constexpr std::string_view kActorTestWalkKey = "actor test walk";
    /// Longest editor id the setting may name (Skyrim's own are far shorter).
    inline constexpr std::size_t kMaxActorTestWalkName = 128;

    /// The pack setting (bool, off by default) that makes the NPCs run their AI packages (stage S5).
    inline constexpr std::string_view kActorPackagesKey = "actor packages";

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

    class SkyrimActorTable final : public openeden::sdk::game::ActorRules
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
        void enablePackages(PackageSource& source, std::uint64_t seed);
        [[nodiscard]] const PackageBrain* packageBrain() const { return mBrain.get(); }

        void onActorAdded(const openeden::sdk::game::ActorInfo& info) override;
        void onActorRemoved(openeden::sdk::game::ActorHandle handle) override;
        void update(float seconds, openeden::sdk::game::ActorRequestWriter& writer) override;

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
    };
}

#endif
