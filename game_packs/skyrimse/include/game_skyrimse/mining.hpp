#ifndef OPENEDEN_GAME_SKYRIMSE_MINING_H
#define OPENEDEN_GAME_SKYRIMSE_MINING_H

// Mining ore veins and chopping wood (task 0224): the pure rules. Standard library, the VMAD model and the SDK save
// chunk types only; no clock, no records.
//
// What is the data's. An ore vein is an ACTI whose script is MineOreScript, a chopping block a FURN whose script
// is ResourceFurnitureScript (both read from the decoded VMAD; the property names and the script's own logic were
// read from the decompiled .pex files of Skyrim - Misc.bsa, see docs/tasks/0224-skyrim-mining-woodcutting.md):
//   MineOreScript: Ore (MISC, what a drop gives), mineOreToolsList (FLST: the player needs one), lItemGems10 (LVLI,
//     one roll a drop), FailureMessage and DepletedMessage (MESG), ResourceCountTotal (3), ResourceCount (1 a drop),
//     StrikesBeforeCollection (1 strike a drop).
//   ResourceFurnitureScript: Resource (MISC), requiredItemList (FLST), FailureMessage (MESG), ResourceCount (1 a use),
//     MaxResourcePerActivation (6 uses a session).
// What is ours (named, RECALLED-VERIFY): the regrowth time of a depleted vein (the game resets a cell; kRegrowSeconds
// is 10 game days), that one activation is one strike, and that one activation of the block is one whole session: the
// script adds ResourceCount per chop and ends the session when the running count reaches MaxResourcePerActivation
// (no chopping animation drives the chops here).
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/vmad.hpp>

#include <openeden/game/save_chunks.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse::mining
{
    /// A depleted vein is full again after this many game seconds (10 days). Ours.
    inline constexpr double kRegrowSeconds = 240.0 * 3600.0;
    /// The most veins a session remembers (a hostile chunk and a very long game).
    inline constexpr std::size_t kMaxVeins = 65536;
    /// The script's own property defaults (MineOreScript.pex / ResourceFurnitureScript.pex).
    inline constexpr int kDefaultTotal = 3;
    inline constexpr int kDefaultPerDrop = 1;
    inline constexpr int kDefaultStrikes = 1;
    inline constexpr int kDefaultMaxPerUse = 6;
    /// The most any count property is taken as (a hostile VMAD).
    inline constexpr int kMaxCount = 1000;
    /// Our lines when the game's message record cannot be read.
    inline constexpr std::string_view kDefaultNeedPickaxe = "You need a pickaxe to use this.";
    inline constexpr std::string_view kDefaultNeedAxe = "You need a woodcutter's axe to use this.";
    inline constexpr std::string_view kNotReady = "The tools cannot be checked yet. Try again in a moment.";
    inline constexpr std::string_view kDefaultDepleted = "This ore vein is depleted.";
    inline constexpr std::string_view kOreVerb = "Mine";
    inline constexpr std::string_view kDepletedSuffix = " (depleted)";
    inline constexpr std::uint16_t kListLevel = 1;

    enum class ResourceKind : std::uint8_t
    {
        None,
        Ore, ///< an ACTI with MineOreScript
        Wood, ///< a FURN with ResourceFurnitureScript
    };

    struct ResourceInfo
    {
        ResourceKind mKind = ResourceKind::None;
        FormId mItem = kNullFormId; ///< Ore / Resource
        FormId mToolList = kNullFormId; ///< mineOreToolsList / requiredItemList; none: no tool needed
        FormId mGems = kNullFormId; ///< lItemGems10 (ore only)
        FormId mFailureMessage = kNullFormId;
        FormId mDepletedMessage = kNullFormId;
        int mTotal = kDefaultTotal; ///< drops before a vein is depleted
        int mPerDrop = kDefaultPerDrop; ///< items a drop (ore) or a use (wood)
        int mStrikes = kDefaultStrikes; ///< strikes a drop
        int mMaxPerUse = kDefaultMaxPerUse; ///< wood: uses a session
    };

    /// Reads the resource data of a base from the scripts of its VMAD; mKind None when neither script is attached.
    /// Property names are matched ignoring case; a property of another type is ignored; counts are clamped to
    /// [1, kMaxCount].
    [[nodiscard]] ResourceInfo resourceFromScripts(const std::vector<vmad::Script>& scripts);

    /// "Mine Iron Ore Vein": the activate text (empty: "Mine") and the display name.
    [[nodiscard]] std::string orePrompt(std::string_view activateText, std::string_view name);
    /// "Iron Ore Vein (depleted)".
    [[nodiscard]] std::string depletedName(std::string_view name);

    /// The seed of the gem roll of one drop.
    [[nodiscard]] std::uint64_t gemSeed(FormId placement, std::uint32_t drop);

    struct VeinState
    {
        std::uint16_t mStrikes = 0; ///< strikes since the last drop
        std::uint16_t mDrops = 0; ///< drops given
        double mDue = -1.0; ///< the game second a depleted vein is full again; negative: not depleted
    };

    struct StrikeOutcome
    {
        bool mDrop = false; ///< this strike gave a drop
        bool mDepleted = false; ///< this strike took the last drop
    };

    /// The veins started or depleted and the strikes made. Main thread.
    class VeinBook
    {
    public:
        /// True while the vein is depleted and has not grown back by `now` (a negative `now`, the clock not known yet,
        /// counts every depleted entry as depleted).
        [[nodiscard]] bool depleted(FormId placement, double now) const;
        [[nodiscard]] VeinState state(FormId placement) const;
        /// One strike of a vein that is not depleted. When the strike takes the last drop the vein is depleted until
        /// `regrowAt`. A null placement, a depleted vein or a full book changes nothing.
        StrikeOutcome strike(FormId placement, const ResourceInfo& info, double regrowAt);
        /// Removes every depleted entry due at `now` (>= 0) and appends its placement to `out`, ascending.
        void collectGrown(double now, std::vector<FormId>& out);
        [[nodiscard]] const std::map<FormId, VeinState>& entries() const { return mEntries; }
        [[nodiscard]] std::uint32_t revision() const { return mRevision; }
        void clear();

        /// The save form: u32 count, then per vein u32 placement (ascending, not 0), u16 strikes, u16 drops and f64 due
        /// (finite). A read that is damaged, over the cap, not ascending, not finite or followed by bytes changes
        /// nothing.
        void write(openeden::sdk::ChunkWriter& writer) const;
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::string& error);

    private:
        std::map<FormId, VeinState> mEntries;
        std::uint32_t mRevision = 0;
    };

    class MiningChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "mining";
        explicit MiningChunk(VeinBook& book)
            : mBook(book)
        {
        }
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
        void setRestoredSink(std::function<void(std::size_t)> sink) { mRestored = std::move(sink); }
        [[nodiscard]] const std::string& refusal() const { return mRefusal; }

        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override { mBook.write(writer); }
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        VeinBook& mBook;
        std::function<void(const std::string&)> mSink;
        std::function<void(std::size_t)> mRestored;
        std::string mRefusal;
    };
}

#endif
