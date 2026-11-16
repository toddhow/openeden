#ifndef OPENEDEN_GAME_SKYRIMSE_PROGRESSION_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_PROGRESSION_SERVICE_H

// The player's progression as a service of the pack (task 0200): it builds the `progression::PlayerProgression` once
// the combat records (AVIF, PERK, GMST) are read, takes the uses of skills the other parts report, keeps the save chunk
// `progression` and declares the script namespace `skyrimse.progression` 1.0.
//
// Namespace (Read unless marked; ids cross as 8 hex digits, a skill is its slot 0 to 17):
//   ready() -> bool, revision() -> int, level() -> int, level_xp() -> int, level_need() -> int,
//   level_up_ready() -> bool, perk_points() -> int, vital_bonus(name) -> int ("health", "magicka", "stamina"),
//   skill_count() -> int, skill_name(slot) -> string, skill_level(slot) -> int, skill_xp(slot) -> int,
//   skill_need(slot) -> int,
//   perks(slot) -> string[]                 the perk nodes of a skill's tree, in the tree's order
//   perk_name(id) -> string, perk_rank(id) -> int, perk_ranks(id) -> int, perk_can_buy(id) -> bool,
//   perk_col(id) -> int, perk_row(id) -> int, perk_reason(id) -> string (why it cannot be bought, "" when it can),
//   last_result() -> string
//   choose_level_up(name)  [Mutate]  level +1, +10 of "health", "magicka" or "stamina", one perk point
//   buy_perk(id)           [Mutate]
//
// The state is saved in the chunk `progression` (version 1): a length and the progression's own versioned blob.
//
// Main thread only (the game phase callback, the script Read handlers and the queued Mutate calls all run there).

#include <game_skyrimse/progression.hpp>

#include <openeden/game/bindings.hpp>
#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace game_skyrimse
{
    namespace progression
    {
        struct Inputs
        {
            const SkillTable* mSkills = nullptr;
            const PerkLibrary* mPerks = nullptr;
            Settings mSettings;
        };

        struct ServiceHooks
        {
            /// Fills the inputs when the records are read (false: ask again next frame).
            std::function<bool(Inputs& out)> mInputs;
            /// A level was reached (the story event LEVL is raised for it).
            std::function<void(std::uint16_t level)> mLevelUp;
            std::function<void(const std::string&)> mLog;
            std::uint16_t mStartLevel = 1;
            std::uint8_t mStartSkill = 15;
        };

        class ProgressionService
        {
        public:
            explicit ProgressionService(ServiceHooks hooks);
            ~ProgressionService();
            ProgressionService(const ProgressionService&) = delete;
            ProgressionService& operator=(const ProgressionService&) = delete;

            /// Asks for the records until they are there, then builds the progression and applies a held restore;
            /// raises the level hook for levels reached. Never blocks. True once usable.
            bool poll();
            [[nodiscard]] bool ready() const { return mProgression != nullptr; }
            /// Null until ready.
            [[nodiscard]] PlayerProgression* progression() { return mProgression.get(); }
            [[nodiscard]] const PlayerProgression* progression() const { return mProgression.get(); }

            /// A use of a skill (a fight, a trade, a craft): the amount is what the use reports. Ignored before ready.
            void skillUsed(std::size_t slot, float amount);

            /// The value of an actor value for the Papyrus host and conditions: a skill at 6 to 23 (its level). False
            /// for
            /// any other index or before ready.
            [[nodiscard]] bool actorValue(std::uint32_t index, float& out) const;

            bool chooseLevelUp(Vital vital);
            bool buyPerk(FormId firstRank);
            [[nodiscard]] const std::string& lastResult() const { return mLastResult; }
            [[nodiscard]] const PerkNode* nodeOf(FormId perk) const
            {
                return mInputs.mSkills != nullptr ? mInputs.mSkills->nodeOfPerk(perk) : nullptr;
            }
            [[nodiscard]] const PerkRecord* perkRecord(FormId perk) const
            {
                return mInputs.mPerks != nullptr ? mInputs.mPerks->perk(perk) : nullptr;
            }

            /// The save chunk. Writes the progression (a held, not yet applied restore is written back unchanged).
            [[nodiscard]] openeden::sdk::SaveChunkHandler& chunk();
            [[nodiscard]] const std::string& refusal() const { return mRefusal; }

            [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

            /// Called by the chunk.
            void writeChunk(openeden::sdk::ChunkWriter& writer);
            bool readChunk(openeden::sdk::ChunkReader& reader, std::uint32_t version);

        private:
            struct Impl;
            std::unique_ptr<Impl> mImpl;
            ServiceHooks mHooks;
            std::unique_ptr<PlayerProgression> mProgression;
            Inputs mInputs;
            std::vector<std::uint8_t> mPendingRestore;
            bool mHavePendingRestore = false;
            std::string mLastResult;
            std::string mRefusal;
            std::uint64_t mLastPerkBuy = 0;
        };

        /// "ArmsmanOneHanded00" to "Armsman One Handed" (an editor id as a name; the perks' own names are in the string
        /// tables, which the pack does not read for perks yet).
        [[nodiscard]] std::string prettyPerkName(const std::string& editorId);
    }
}

#endif
