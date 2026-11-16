#ifndef OPENEDEN_GAME_SKYRIMSE_FOLLOWER_H
#define OPENEDEN_GAME_SKYRIMSE_FOLLOWER_H

// Followers (task 0196, milestone M-S7): the pack's pure pieces. Who follows lives in the package brain
// (`PackageBrain::setFollowPlayer`) and the actor table; this file holds what does not need an actor: the quest alias
// the real recruit path uses, the limits, and the save chunk.
//
// Recruiting: the game's own path is the quest DialogueFollower (0x750BA, `Follower` alias 0): its dialogue fragments
// call DialogueFollowerScript, which ends with ReferenceAlias.ForceRefTo(actor) on that alias; dismissing clears it.
// The pack reads that alias from the Papyrus host (`PapyrusHost::forcedAlias`) and recruits or dismisses.
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only.

#include <openeden/game/save_chunks.hpp>
#include <openeden/game/world_view.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// The most followers at once. Skyrim allows one human and one animal; the limit is a safety bound, not a rule.
    inline constexpr std::size_t kMaxFollowers = 16;

    /// The quest and alias the game's follower dialogue uses (VERIFIED by the installed data: quest DialogueFollower,
    /// alias 0 named Follower).
    inline constexpr std::uint32_t kFollowerQuestId = 0x000750BA;
    inline constexpr std::uint32_t kFollowerAliasId = 0;

    inline constexpr std::string_view kFollowerTestRecruitKey = "follower test recruit";
    inline constexpr std::string_view kFollowerTestDismissKey = "follower test dismiss after";

    /// One follower as the save chunk keeps it: the placement id and the cell the placement lives in (0 when unknown).
    struct FollowerEntry
    {
        openeden::sdk::game::WorldId mId = openeden::sdk::game::kNoWorldId;
        openeden::sdk::game::WorldId mHome = openeden::sdk::game::kNoWorldId;
        friend bool operator==(const FollowerEntry&, const FollowerEntry&) = default;
    };

    /// The pack's save chunk "followers" (version 1): u32 count (at most kMaxFollowers), then that many pairs of u32
    /// placement id and u32 home cell id, ids strictly ascending and non-zero. Anything else is refused and changes
    /// nothing.
    class FollowerChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "followers";

        using Getter = std::function<std::vector<FollowerEntry>()>;
        using Setter = std::function<bool(const std::vector<FollowerEntry>&)>;

        FollowerChunk(Getter get, Setter set)
            : mGet(std::move(get))
            , mSet(std::move(set))
        {
        }

        [[nodiscard]] std::string_view chunkName() const override { return kName; }
        [[nodiscard]] std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;
        [[nodiscard]] const std::string& refusal() const { return mRefusal; }

    private:
        Getter mGet;
        Setter mSet;
        std::string mRefusal;
    };

    /// What a change of the alias fill asks for: the old fill is dismissed, the new one recruited (form ids, 0 for
    /// none; nothing when they are equal).
    struct FollowerAliasChange
    {
        openeden::sdk::game::WorldId mDismiss = openeden::sdk::game::kNoWorldId;
        openeden::sdk::game::WorldId mRecruit = openeden::sdk::game::kNoWorldId;
    };
    [[nodiscard]] FollowerAliasChange followerAliasChange(std::uint32_t previous, std::uint32_t current);
}

#endif
