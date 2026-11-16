#ifndef OPENEDEN_GAME_SKYRIMSE_PROGRESSION_H
#define OPENEDEN_GAME_SKYRIMSE_PROGRESSION_H

// The player's skills, levelling and perks (task 0200). Pure: no engine, renderer, UI or script language type, no
// clock, no random numbers.
//
// SETTLED BY THE DATA (Skyrim.esm, aggregates in the task's stage log):
//  * AVIF of a skill: EDID, FULL, DESC, CNAM (4 bytes: category 1 combat, 2 magic, 3 stealth), AVSK (16 bytes, four
//    floats: use mult, use offset, improve mult, improve offset), then the perk tree: per node PNAM (the first rank's
//    PERK, 0 for the root), FNAM, XNAM and YNAM (grid column and row), HNAM and VNAM (floats: the node's place in the
//    sky), SNAM (the skill's AVIF), CNAM x n (indices of the nodes the node leads to), INAM (the node's own index).
//    The 18 skills are the AVIF forms 0x44C to 0x45D in the order of actor values 6 to 23 (editor ids checked).
//  * GMSTs: fSkillUseCurve 1.95, fXPLevelUpBase 75, fXPLevelUpMult 25, fLevelUpCarryWeightMod 5.
//  * MGEF DATA +12 is the associated skill (an actor value, 18 to 22 for the five schools, -1 for none).
// RECALLED-VERIFY (plausible, tunable): the skill experience formulas (gain = use mult * amount + use offset, the next
// level needs improve mult * level ^ curve + improve offset), the character experience a skill increase gives (the new
// skill level), the level up (needs fXPLevelUpBase + fXPLevelUpMult * level, +10 of the chosen vital, one perk point),
// and the amounts each use of a skill reports.
//
// Not Skyrim: a level up is chosen in the skills window at any time; Skyrim wants a sleep (decision for toddhow).

#include "game_skyrimse/perks.hpp"
#include "game_skyrimse/records.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game_skyrimse
{
    class SettingsTable;
}

namespace game_skyrimse::progression
{
    inline constexpr std::size_t kSkillCount = 18;
    /// Actor value index of skill slot 0 (One-Handed); slot s is value 6 + s.
    inline constexpr std::uint32_t kFirstSkillValue = 6;
    inline constexpr std::uint8_t kMaxSkill = 100;
    inline constexpr std::size_t kMaxNodesPerSkill = 64;
    inline constexpr std::size_t kMaxPerkRanks = 8;
    inline constexpr std::size_t kMaxOwnedPerks = 512;

    /// Skill slots that matter to the hooks (slot = actor value - 6).
    namespace Slot
    {
        inline constexpr std::size_t OneHanded = 0, TwoHanded = 1, Archery = 2, Block = 3, Smithing = 4, HeavyArmor = 5,
                                     LightArmor = 6, Pickpocket = 7, Lockpicking = 8, Sneak = 9, Alchemy = 10,
                                     Speechcraft = 11, Alteration = 12, Conjuration = 13, Destruction = 14,
                                     Illusion = 15, Restoration = 16, Enchanting = 17;
    }

    [[nodiscard]] std::string_view skillEditorId(std::size_t slot); ///< "AVOneHanded" ..., empty out of range
    [[nodiscard]] std::string_view skillName(std::size_t slot); ///< "One-Handed" ...
    /// The slot of an AVIF editor id (case-insensitive), or kSkillCount when it is not one of the 18.
    [[nodiscard]] std::size_t slotOfSkillEditorId(std::string_view editorId);
    /// The skill a magic effect trains: its MGEF associated skill (an actor value 18 to 22) as a slot, else
    /// kSkillCount.
    [[nodiscard]] std::size_t slotOfMagicSkillValue(std::int32_t actorValue);
    /// Task 0206: the slot of a skill by the key a Papyrus script's actor value name is reduced to (lower case, no
    /// spaces or underscores: "onehanded", "marksman" and "archery", "speechcraft" and "speech", "illusion" ...);
    /// kSkillCount when the key is not a skill. The script names are RECALLED-VERIFY.
    [[nodiscard]] std::size_t slotOfActorValueName(std::string_view key);

    struct PerkNode
    {
        std::uint32_t mIndex = 0;
        FormId mPerk = kNullFormId; ///< the first rank; 0 for the root
        std::uint32_t mColumn = 0; ///< XNAM
        std::uint32_t mRow = 0; ///< YNAM
        float mHorizontal = 0.f; ///< HNAM
        float mVertical = 0.f; ///< VNAM
        std::vector<std::uint32_t> mChildren; ///< CNAM: node indices
    };

    struct SkillRecord
    {
        RecordCommon mCommon;
        std::uint32_t mCategory = 0;
        bool mHasSkillData = false;
        float mUseMult = 0.f;
        float mUseOffset = 0.f;
        float mImproveMult = 0.f;
        float mImproveOffset = 0.f;
        std::vector<PerkNode> mNodes;
    };

    [[nodiscard]] bool decodeSkill(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, SkillRecord& out, std::string& why);

    /// The numbers of the rules: GMSTs where the load order has them (the names are VERIFIED, the use RECALLED-VERIFY).
    struct Settings
    {
        float mSkillUseCurve = 1.95f; ///< fSkillUseCurve
        float mXpBase = 75.f; ///< fXPLevelUpBase
        float mXpMult = 25.f; ///< fXPLevelUpMult
        float mCarryWeightMod = 5.f; ///< fLevelUpCarryWeightMod
        float mCharacterXpPerSkillLevel = 1.f; ///< OURS: character experience per level of a skill increase
        float mVitalPerLevel = 10.f; ///< OURS (RECALLED): what a level up adds to the chosen vital
    };
    [[nodiscard]] Settings settingsFrom(const SettingsTable& table);

    /// The 18 skills' records of a load order (a later plugin's replaces a whole record).
    class SkillTable
    {
    public:
        void add(const SkillRecord& record);
        [[nodiscard]] const SkillRecord* skill(std::size_t slot) const;
        [[nodiscard]] bool complete() const; ///< all 18 have skill data
        /// The node and skill slot of a perk as a tree's first rank, or null.
        [[nodiscard]] const PerkNode* nodeOfPerk(FormId perk, std::size_t* slot = nullptr) const;
        [[nodiscard]] std::size_t perkNodeCount() const;

    private:
        std::array<SkillRecord, kSkillCount> mSkills{};
        std::array<bool, kSkillCount> mHave{};
    };

    enum class Vital : std::uint8_t
    {
        Health,
        Magicka,
        Stamina,
    };

    /// One thing the window shows about a perk node.
    struct NodeView
    {
        std::size_t mSkill = 0;
        std::uint32_t mIndex = 0;
        FormId mPerk = kNullFormId; ///< first rank
        std::uint8_t mRank = 0; ///< ranks owned
        std::uint8_t mRanks = 0; ///< ranks the perk has
        bool mCanBuy = false;
    };

    class PlayerProgression
    {
    public:
        /// `perks` may be null (no perk can be bought). All references must outlive this.
        PlayerProgression(const SkillTable& skills, const PerkLibrary* perks, Settings settings = {});

        void setStart(std::uint16_t level, std::uint8_t skill); ///< a new game: before anything was used
        /// A new character: setStart, and no experience, perk points, vital choices, perks or pending level events.
        void reset(std::uint16_t level, std::uint8_t skill);
        [[nodiscard]] std::uint16_t level() const { return mLevel; }
        [[nodiscard]] std::uint8_t skill(std::size_t slot) const;
        [[nodiscard]] float skillXp(std::size_t slot) const; ///< experience towards the next increase
        [[nodiscard]] float skillNeed(std::size_t slot) const; ///< what the next increase needs
        [[nodiscard]] float levelXp() const { return mLevelXp; }
        [[nodiscard]] float levelNeed() const; ///< what the next level up needs
        [[nodiscard]] bool levelUpReady() const;
        [[nodiscard]] std::uint32_t perkPoints() const { return mPerkPoints; }
        [[nodiscard]] float vitalBonus(Vital vital) const;
        [[nodiscard]] float carryWeightBonus() const;
        /// Bumped by every change of anything above.
        [[nodiscard]] std::uint64_t revision() const { return mRevision; }

        /// A use of a skill. `amount` is what the use reports (damage, magicka cost, gold ...). Returns how many
        /// skill increases it caused. A bad slot, a non-finite or negative amount does nothing.
        std::uint32_t useSkill(std::size_t slot, float amount);

        /// Spends the level up on a vital: level +1, +mVitalPerLevel of it, one perk point. False when not ready.
        bool chooseLevelUp(Vital vital);
        /// The levels reached since the last call (the story event LEVL is raised for each).
        [[nodiscard]] std::vector<std::uint16_t> takeLevelUps();

        // ---- perks
        [[nodiscard]] std::uint8_t perkRank(FormId firstRank) const;
        /// The highest rank's PERK of every perk owned, sorted: what combat and the entry points use.
        [[nodiscard]] std::vector<FormId> ownedPerks() const;
        /// Every rank record up to the owned rank, sorted: what HasPerk answers true for.
        [[nodiscard]] std::vector<FormId> hasPerkSet() const;
        /// Can the next rank of the perk whose first rank is `firstRank` be bought? `why` says when not.
        [[nodiscard]] bool canBuyPerk(FormId firstRank, std::string& why) const;
        bool buyPerk(FormId firstRank, std::string& why);
        [[nodiscard]] std::vector<NodeView> nodes(std::size_t slot) const;

        /// Actor values for conditions: skills at 6 to 23.
        [[nodiscard]] std::vector<std::pair<std::uint32_t, float>> actorValues() const;

        // ---- saving
        [[nodiscard]] std::vector<std::uint8_t> serialise() const;
        /// False with `error` for a damaged or newer blob; nothing changes then.
        [[nodiscard]] bool restore(std::span<const std::uint8_t> bytes, std::string& error);

    private:
        [[nodiscard]] std::vector<FormId> ranksOf(FormId firstRank) const;

        const SkillTable& mSkillTable;
        const PerkLibrary* mPerkLibrary;
        Settings mSettings;
        std::uint16_t mLevel = 1;
        std::array<std::uint8_t, kSkillCount> mSkill{};
        std::array<float, kSkillCount> mSkillXp{};
        float mLevelXp = 0.f;
        std::uint32_t mPerkPoints = 0;
        std::array<std::uint16_t, 3> mVitalCount{};
        std::vector<std::pair<FormId, std::uint8_t>> mOwned; ///< first rank, ranks owned; sorted by form
        std::vector<std::uint16_t> mLevelUps;
        std::uint64_t mRevision = 1;
    };
}

#endif
