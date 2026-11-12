#ifndef OPENEDEN_GAME_SKYRIMSE_ACTOR_TABLE_H
#define OPENEDEN_GAME_SKYRIMSE_ACTOR_TABLE_H

// The Skyrim SE pack's side of the actor hooks (task 0135 stage S3, sdk/include/openeden/game/actor_rules.hpp): a table
// of the actors the core has told the pack about, keyed by the placement's WorldId, and, for this stage only, one
// trivial rule: actors whose editor id matches the setting `[extension.skyrimse] actor test walk` pace back and forth
// along X. It proves the contract end to end; there are no AI packages yet (stage S5).
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only. update() allocates nothing once the table is built
// (the rows live in a std::map, which allocates only on add).

#include <openeden/game/actor_rules.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace game_skyrimse
{
    /// The pack setting (`[extension.skyrimse]`) that names the test actor; empty (the default) means nobody paces.
    inline constexpr std::string_view kActorTestWalkKey = "actor test walk";
    /// Longest editor id the setting may name (Skyrim's own are far shorter).
    inline constexpr std::size_t kMaxActorTestWalkName = 128;

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
        };

        /// Names the editor id of the test pacer (case-insensitive); empty switches the rule off. A value longer than
        /// kMaxActorTestWalkName is refused (false, the rule stays off).
        bool setTestWalk(std::string_view editorId);
        [[nodiscard]] const std::string& testWalk() const { return mTestWalk; }

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
        };

        std::map<openeden::sdk::game::WorldId, Row> mRows;
        std::string mTestWalk; ///< lower case
        std::size_t mPacers = 0;
        Stats mStats;
    };
}

#endif
