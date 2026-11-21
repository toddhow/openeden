#ifndef OPENEDEN_GAME_SKYRIMSE_FOLLOWER_STATE_H
#define OPENEDEN_GAME_SKYRIMSE_FOLLOWER_STATE_H

// Follower state and commands (task 0239, milestone M-S7): what the game's own follower scripts
// (DialogueFollowerScript, HirelingQuest, the TIF fragments of the DialogueFollower topics) change and its dialogue
// conditions read back:
// factions an actor was added to or removed from, relationship ranks, the teammate flag, the globals scripts set
// (PlayerFollowerCount, CanRehire...), and the WaitingForPlayer actor value that "Wait here" and "Follow me" flip.
//
// The Papyrus host writes it (Actor.AddToFaction, SetRelationshipRank, SetPlayerTeammate, GlobalVariable.SetValue,
// alias fills that carry ALFC factions) and the dialogue runtime's condition world reads it through `FollowerOverlay`
// (GetInFaction, GetFactionRank, GetGlobalValue, GetActorValue of WaitingForPlayer, GetRelationshipRank,
// GetPlayerTeammate), so that the follower topics' own conditions pass for a recruited actor. Its own save chunk
// `followerstate` keeps what is not already in the Papyrus chunk.
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only. Every container is ordered (determinism).

#include <game_skyrimse/records.hpp>

#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace game_skyrimse
{
    /// The actor value that "Wait here" sets to 1 and "Follow me" back to 0 (DialogueFollowerScript.FollowerWait
    /// and FollowerFollow: SetAV("WaitingForPlayer", n)). Its index is the one the Wait and Follow topics'
    /// GetActorValue conditions name (VERIFIED by the data: parameter 0x5F in all 21 INFOs of both topics).
    inline constexpr std::string_view kWaitingForPlayerKey = "waitingforplayer";
    inline constexpr std::uint32_t kWaitingForPlayerIndex = 0x5F;
    /// Task 0302 (draugr ambushes): Variable01, the actor value an ambush draugr's sleep package tests (condition
    /// function GetActorValue, parameter 0x44: VERIFIED in `ambushSleepPackage` and `AmbushSandboxEditorLocation512`);
    /// the script's SetAV("Variable01", 1) lets the actor out. Read like WaitingForPlayer, from the host's actor value
    /// offsets.
    inline constexpr std::string_view kVariable01Key = "variable01";
    inline constexpr std::uint32_t kVariable01Index = 0x44;

    /// The most entries of each kind the state keeps (a script cannot grow it without bound).
    inline constexpr std::size_t kMaxFollowerStateEntries = 256;

    /// What the dialogue conditions read. An answer of nullopt means "nothing was changed here: use the data".
    class FollowerOverlay
    {
    public:
        virtual ~FollowerOverlay() = default;
        /// The rank of the actor in the faction: -1 when it was removed, 0 or more when added; nullopt untouched.
        [[nodiscard]] virtual std::optional<float> factionRank(FormId reference, FormId faction) const = 0;
        /// An actor value by index (only WaitingForPlayer is known); nullopt when nothing set it.
        [[nodiscard]] virtual std::optional<float> actorValue(FormId reference, std::uint32_t index) const = 0;
        /// A global a script set; nullopt when none did.
        [[nodiscard]] virtual std::optional<float> globalValue(FormId global) const = 0;
        /// The relationship rank (-4 to 4) a script set between two actors; nullopt when none did.
        [[nodiscard]] virtual std::optional<float> relationshipRank(FormId reference, FormId target) const = 0;
        [[nodiscard]] virtual bool isPlayerTeammate(FormId reference) const = 0;
    };

    /// The part of the state a save keeps.
    struct FollowerStateData
    {
        std::map<std::pair<FormId, FormId>, std::int32_t> mFactions; ///< (actor, faction) to rank; -1 means removed
        std::map<std::pair<FormId, FormId>, std::int32_t> mRanks; ///< (actor, target) to relationship rank
        std::set<FormId> mTeammates;
        std::map<FormId, float> mGlobals;
        friend bool operator==(const FollowerStateData&, const FollowerStateData&) = default;
    };

    class FollowerState final : public FollowerOverlay
    {
    public:
        /// False when the state is full (a new entry beyond kMaxFollowerStateEntries) or an id is 0.
        bool addToFaction(FormId reference, FormId faction, std::int32_t rank = 0);
        bool removeFromFaction(FormId reference, FormId faction);
        bool setRelationshipRank(FormId reference, FormId target, std::int32_t rank);
        bool setTeammate(FormId reference, bool teammate);
        bool setGlobal(FormId global, float value);

        /// Task 0324: the relation a script gave two factions (`Faction.SetAlly`: 2, `SetEnemy`: 1, the values of
        /// `ActorRelation`); it holds both ways and the last one wins. Not saved (a quest script that set it runs again
        /// only when its stage does): `data()` and the chunk do not hold it. False for a zero id or a full table.
        bool setFactionRelation(FormId a, FormId b, std::uint8_t relation);
        [[nodiscard]] std::optional<std::uint8_t> factionRelation(FormId a, FormId b) const;

        /// Where the WaitingForPlayer value comes from: the Papyrus host's actor value offsets (the same store SetAV
        /// writes). An empty source means no actor ever waits.
        using ActorValueSource = std::function<std::optional<float>(FormId reference, std::string_view key)>;
        void setActorValueSource(ActorValueSource source) { mActorValues = std::move(source); }
        /// Is the actor waiting for the player (WaitingForPlayer above one half)?
        [[nodiscard]] bool waiting(FormId reference) const;

        [[nodiscard]] std::optional<float> factionRank(FormId reference, FormId faction) const override;
        [[nodiscard]] std::optional<float> actorValue(FormId reference, std::uint32_t index) const override;
        [[nodiscard]] std::optional<float> globalValue(FormId global) const override;
        [[nodiscard]] std::optional<float> relationshipRank(FormId reference, FormId target) const override;
        [[nodiscard]] bool isPlayerTeammate(FormId reference) const override;

        [[nodiscard]] const FollowerStateData& data() const { return mData; }
        /// Replaces the state with a restored one. False (nothing changed) when it breaks a cap or holds a zero id.
        bool restore(const FollowerStateData& data);

    private:
        FollowerStateData mData;
        ActorValueSource mActorValues;
        std::map<std::pair<FormId, FormId>, std::uint8_t> mRelations; ///< task 0324: (lower id, higher id) to relation
    };

    /// The pack's save chunk "followerstate" (version 1): u32 count and, per entry, u32 actor, u32 faction, i32 rank
    /// (ascending by actor then faction); the same for relationship ranks (actor, target, rank); u32 count and
    /// ascending u32 teammates; u32 count and, per global, u32 id (ascending) and the 32 bits of its float. Anything
    /// else is refused and changes nothing.
    class FollowerStateChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "followerstate";

        using Getter = std::function<FollowerStateData()>;
        using Setter = std::function<bool(const FollowerStateData&)>;

        FollowerStateChunk(Getter get, Setter set)
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
}

#endif
