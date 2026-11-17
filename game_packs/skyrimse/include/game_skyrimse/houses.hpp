#ifndef OPENEDEN_GAME_SKYRIMSE_HOUSES_H
#define OPENEDEN_GAME_SKYRIMSE_HOUSES_H

// Task 0261: bought houses. The game's own scripts do the buying (HousePurchaseScript.PurchaseHouse: gold out, the key and
// the decorating guide in, Cell.SetFactionOwner(PlayerFaction)); this file holds what the pack keeps of it: who owns a
// cell now. A cell's owner is the owner of every reference in it that has none of its own (the cell's XOWN in the
// plugins is not inherited by the pack's theft and bed rules, only an owner a script SET is; see effectiveOwner).
//
//   Ledger         cell form id -> the owner a script gave it (a FACT or an NPC_ form id)
//   HousesChunk    the save chunk `houses` v1: the ledger, strictly ascending, capped
//   effectiveOwner the owner of a placement: its own (XOWN) when it has one, else the ledger's owner of its cell
//   applyToAccess  what the trespass rule reads of a cell: the ledger's owner replaces the plugin's

#include <game_skyrimse/crime.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/records.hpp>

#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::houses
{
    inline constexpr std::size_t kMaxHouseEntries = 256;
    inline constexpr std::string_view kChunkName = "houses";
    inline constexpr std::string_view kHousesKey = "houses"; ///< [extension.skyrimse] houses (needs papyrus)
    /// Test aid: the Whiterun jarls like the player (relationship rank 2: Friend), the condition of the steward's offer.
    inline constexpr std::string_view kHousesTestPermissionKey = "houses test permission";
    /// HousePurchase (QUST 0x0A7B33, Skyrim.esm): the quest of the steward's offers; it is not start-game-enabled and no
    /// script starts it, so the pack starts it when `houses` is on.
    inline constexpr FormId kHousePurchaseQuest = 0x000A7B33;
    /// JobStewardFaction (FACT 0x50922): the 13 stewards of the five masters are listed in it at rank -1 and the offer tests
    /// GetInFaction == 1.
    inline constexpr FormId kJobStewardFaction = 0x00050922;
    /// PlayerBedOwnership (FACT 0x0F2073): the owner of eight bed placements in the five masters (Breezehome's among them),
    /// and one of the four factions of the Player NPC_ (0x7: 0x9E0C9, 0xF2073, 0xDB1, 0x8F3E8): the player's own beds.
    inline constexpr FormId kPlayerBedOwnershipFaction = 0x000F2073;
    /// The Whiterun jarl and the Whiterun jarl of the Sons of Skyrim (the HousePurchase aliases 10 and 14).
    inline constexpr FormId kWhiterunJarlBase = 0x00013BBD;
    inline constexpr FormId kWhiterunSonsJarlBase = 0x00013BB5;

    /// Task 0269 (STUB until the tests have run red): the jarls the test aid befriends, the MS11 quest and its marked stage.
    [[nodiscard]] std::vector<FormId> testPermissionJarlBases();
    inline constexpr FormId kMs11Quest = 0x0001F7A3;

    /// Task 0269: the five hold dialogue quests (DialogueWhiterun 0x270A4, DialogueRiften 0x368D8, DialogueWindhelm 0x3502E,
    /// DialogueMarkarth 0x17F05, DialogueSolitude 0x2E1AD): their steward alias puts the steward into JobStewardFaction
    /// while the quest runs (ALFC), and Falk Firebeard (Solitude) has the faction no other way.
    [[nodiscard]] std::vector<FormId> holdDialogueQuests();

    struct AliasFactionGrant
    {
        FormId mReference = kNullFormId; ///< the placed reference the alias is filled with by the data (ALFR)
        FormId mFaction = kNullFormId;
    };
    /// The (reference, faction) pairs of the quests' data-filled specific-reference
    /// aliases whose ALFC list names `faction`.
    [[nodiscard]] std::vector<AliasFactionGrant> aliasFactionGrants(
        const QuestIndex& quests, const std::vector<FormId>& questIds, FormId faction);

    class Ledger
    {
    public:
        /// Records that `cell` is now owned by `owner`. False for a null id, or when the ledger is full.
        bool setCellOwner(FormId cell, FormId owner);
        [[nodiscard]] std::optional<FormId> cellOwner(FormId cell) const;
        /// The owner of a placement standing in `cell`: `own` (its XOWN, null for none) when set, else the ledger's. The
        /// player's own bed faction (kPlayerBedOwnershipFaction) is the player's faction.
        [[nodiscard]] FormId effectiveOwner(FormId own, FormId cell) const;
        /// A cell's access data with the ledger's owner in place of the plugin's.
        void applyToAccess(FormId cell, crime::CellAccess& access) const;

        [[nodiscard]] const std::map<FormId, FormId>& data() const { return mOwners; }
        bool restore(const std::map<FormId, FormId>& data);
        [[nodiscard]] std::uint64_t revision() const { return mRevision; }

    private:
        std::map<FormId, FormId> mOwners;
        std::uint64_t mRevision = 0;
    };

    class HousesChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;

        using Getter = std::function<std::map<FormId, FormId>()>;
        using Setter = std::function<bool(const std::map<FormId, FormId>&)>;

        HousesChunk(Getter get, Setter set)
            : mGet(std::move(get))
            , mSet(std::move(set))
        {
        }

        [[nodiscard]] std::string_view chunkName() const override { return kChunkName; }
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
