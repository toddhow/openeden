#ifndef OPENEDEN_GAME_SKYRIMSE_POISON_H
#define OPENEDEN_GAME_SKYRIMSE_POISON_H

// Poisons on weapons (task 0229, milestone M-S7). A poison is an ALCH (or a potion made at an alchemy lab) whose first
// effect is hostile. The player applies one to the weapon he wields from the inventory window; the weapon then carries
// it for a number of landed hits (`CraftingSettings::mPoisonCharges`, ours: the data has no dose count, only the
// messages sPoisonAlreadyPoisonedMessage, sPoisonNoWeaponMessage and sPoisonBowConfirmMessage), and each landed hit
// applies its effects to the target through the magic runtime of task 0186 (combat_poison.cpp).
//
// This header is the pure part: which weapon carries which poison with how many charges, and the SpellRecord the
// combat rules apply. Nothing here allocates per frame; the table is bounded. Main thread only.

#include <game_skyrimse/crafting.hpp>
#include <game_skyrimse/perks.hpp>

#include <openeden/game/save_chunks.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::crafting
{
    /// The poison a weapon carries.
    struct WeaponPoison
    {
        FormId mPoison = kNullFormId; ///< the item the poison came from
        std::string mName;
        std::vector<EffectEntry> mEffects;
        std::int32_t mCharges = 0; ///< landed hits it still poisons
    };

    enum class CoatStatus : std::uint8_t
    {
        Ok,
        AlreadyPoisoned, ///< the weapon carries a poison already (Skyrim: sPoisonAlreadyPoisonedMessage)
        NoCharges, ///< the poison to apply has no charge
        BadArgument, ///< no weapon, no effects, or the table is full
    };

    /// Which weapon item carries which poison.
    class PoisonCoatings
    {
    public:
        static constexpr std::size_t kMax = 64;
        static constexpr std::size_t kMaxEffects = 8;

        CoatStatus apply(FormId weapon, WeaponPoison poison);
        [[nodiscard]] const WeaponPoison* find(FormId weapon) const;
        /// One charge used. Returns the charges left (the coating ends at 0), or -1 when the weapon carries none.
        int spend(FormId weapon);
        void clear() { mCoats.clear(); }
        [[nodiscard]] std::size_t size() const { return mCoats.size(); }

        /// Task 0233: the saved form. Per coating: weapon item, poison item, name, charges, effects. Canonical (the
        /// order of the table). `read` checks everything first and changes nothing when the bytes are damaged.
        static constexpr std::int32_t kMaxCharges = 1000;
        static constexpr std::size_t kMaxName = 160;
        void write(openeden::sdk::ChunkWriter& writer) const;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::string& error);

    private:
        struct Entry
        {
            FormId mWeapon = kNullFormId;
            WeaponPoison mPoison;
        };
        std::vector<Entry> mCoats;
    };

    /// Task 0233: a weapon's poison is saved in a chunk of its own (`poisons`, version 1), so the `crafting` chunk's
    /// bytes and version are untouched: a save made before this build has no such chunk and loads with clean weapons,
    /// and a save made with it loads in this build only (an older build keeps the chunk unread).
    class PoisonChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "poisons";
        explicit PoisonChunk(PoisonCoatings& coatings)
            : mCoatings(coatings)
        {
        }
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
        /// Called after a good read with the number of coatings (the pack re-syncs the wielded weapon's poison).
        void setRestoredSink(std::function<void(std::size_t)> sink) { mRestored = std::move(sink); }
        [[nodiscard]] const std::string& refusal() const { return mRefusal; }

        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override { mCoatings.write(writer); }
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        PoisonCoatings& mCoatings;
        std::function<void(const std::string&)> mSink;
        std::function<void(std::size_t)> mRestored;
        std::string mRefusal;
    };

    /// The spell the combat rules apply on a hit: a touch, fire-and-forget spell of the poison's effects, named after
    /// the poison, with the poison's item as its id.
    [[nodiscard]] SpellRecord poisonSpell(const WeaponPoison& poison);
}

#endif
