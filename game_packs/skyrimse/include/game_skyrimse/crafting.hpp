#ifndef OPENEDEN_GAME_SKYRIMSE_CRAFTING_H
#define OPENEDEN_GAME_SKYRIMSE_CRAFTING_H

// Skyrim SE crafting: alchemy, smithing, tempering and enchanting (task 0199, milestone M-S7). This header holds the
// records, the pure rules and the saved state; crafting_service.hpp holds the session over the inventory.
//
// Record layouts (VERIFIED against the five masters, see docs/tasks/0199-skyrim-crafting.md section 1):
//   INGR  EFID 4 + EFIT 12 (magnitude f32, area u32, duration u32), up to four pairs
//   COBJ  CNAM 4 (created object), NAM1 2 (count), BNAM 4 (bench keyword), CNTO 8 each (item, count), CTDA 32
//   ENCH  ENIT 36 (cost i32, flags u32, cast type u32, amount i32, delivery u32, type u32, charge time f32, base
//         enchantment u32, worn restrictions u32), EFID/EFIT pairs
//   SLGM  SLCP 1 (capacity), SOUL 1 (the soul held)
//   FURN  KWDA (the bench keywords), WEAP/ARMO EITM 4 (the enchantment)
// Every formula below is RECALLED-VERIFY: it is plausible and tunable (CraftingSettings), not Skyrim's own.
//
// Threads: parseCraftingRecords and CraftingTable::add run on a worker; the state and the chunk run on the main thread.

#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/inventory.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/survey.hpp>

#include <components/contentsource/plugin.hpp>

#include <openeden/game/save_chunks.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game_skyrimse::crafting
{
    /// Items made in game carry this top byte (no plugin of a load order has index 0xFF).
    inline constexpr FormId kSyntheticPrefix = 0xFF000000u;
    [[nodiscard]] constexpr bool isSyntheticId(FormId id)
    {
        return (id >> 24) == 0xFFu && id != kUnresolvedFormId;
    }

    struct EffectEntry
    {
        FormId mEffect = kNullFormId;
        float mMagnitude = 0.f;
        std::uint32_t mArea = 0;
        std::uint32_t mDuration = 0;
        friend bool operator==(const EffectEntry&, const EffectEntry&) = default;
    };

    struct Ingredient
    {
        FormId mId = kNullFormId;
        std::vector<EffectEntry> mEffects; ///< at most 4
    };

    struct Recipe
    {
        FormId mId = kNullFormId;
        FormId mResult = kNullFormId; ///< CNAM: the created object (a tempering recipe: the item improved)
        std::uint16_t mCount = 1; ///< NAM1
        FormId mBench = kNullFormId; ///< BNAM: the bench keyword
        std::vector<InventoryEntry> mItems; ///< CNTO: what is consumed
        std::uint32_t mConditions = 0; ///< CTDA count (not evaluated)
        std::vector<survey::Ctda> mConditionList; ///< task 0206: the decoded CTDA, form parameters resolved
    };

    struct Enchantment
    {
        FormId mId = kNullFormId;
        FormId mBase = kNullFormId; ///< ENIT +28, 0 for a base enchantment itself
        std::uint32_t mCastType = 0; ///< 1 constant effect, 2 fire and forget
        std::uint32_t mDelivery = 0;
        std::uint32_t mEnchantType = 0; ///< 6 enchantment, 12 staff
        std::vector<EffectEntry> mEffects;
        std::string mEditorId;
    };

    struct GearEnchant
    {
        FormId mItem = kNullFormId;
        FormId mEnchantment = kNullFormId;
    };

    struct SoulGem
    {
        FormId mId = kNullFormId;
        std::uint8_t mCapacity = 0;
        std::uint8_t mSoul = 0;
    };

    struct Furniture
    {
        FormId mId = kNullFormId;
        std::vector<FormId> mKeywords; ///< only the crafting keywords (kBenchKeywordNames)
    };

    /// The keywords the pack watches (editor ids) and the bench each stands for.
    enum class BenchKind : std::uint8_t
    {
        None,
        Alchemy,
        Enchanting,
        Forge,
        Skyforge,
        SharpeningWheel,
        ArmorTable,
        Smelter,
        Cookpot,
        TanningRack,
    };
    [[nodiscard]] std::string_view benchKindName(BenchKind kind);
    /// "alchemy", "enchanting", "forge", ... ; None for any other text.
    [[nodiscard]] BenchKind benchKindFromName(std::string_view name);
    /// The keyword editor id of a bench kind ("" for None).
    [[nodiscard]] std::string_view benchKeywordName(BenchKind kind);
    [[nodiscard]] bool isTemperBench(BenchKind kind);

    /// Task 0206: the progression slot (`progression::Slot`) a crafting skill name trains: "alchemy", "smithing",
    /// "enchanting"; the skill count (18) for any other name.
    [[nodiscard]] std::size_t skillSlotOfCraftingName(std::string_view skill);
    /// Task 0206: is every condition of a recipe one the perks module can answer: HasPerk (fn 448), GetBaseActorValue
    /// (277) and the tempering test "the item is enchanted" (659)? An empty list is. The data holds only these on the
    /// forge and tempering recipes; a recipe with any other function (a global, a quest stage) is not decided by them.
    [[nodiscard]] bool recipeConditionsPerkOnly(std::span<const survey::Ctda> list);
    /// Task 0206: evaluates a recipe's conditions for the player: `perks` are the PERK forms HasPerk answers true for,
    /// `values` the base actor values (skills at 6 to 23), `itemEnchanted` is fn 659's answer (the item a tempering
    /// recipe would improve carries an enchantment). Fails closed on a function nothing answers.
    [[nodiscard]] bool recipeConditionsHold(std::span<const survey::Ctda> list, const std::vector<FormId>& perks,
        std::uint16_t level, const std::vector<std::pair<std::uint32_t, float>>& values, bool itemEnchanted);

    /// Task 0229: the tanning rack's recipes (and a few others) carry one condition each, GetItemCount(item) >= 1
    /// (function 47, operators 0 to 5): is every condition of the list that function? An empty list is not.
    [[nodiscard]] bool recipeConditionsItemCountOnly(std::span<const survey::Ctda> list);
    /// Task 0229: do all those conditions hold for the items the player holds (`held` gives the count of an item)? An
    /// empty list holds.
    [[nodiscard]] bool itemCountConditionsHold(
        std::span<const survey::Ctda> list, const std::function<std::int32_t(FormId)>& held);

    struct CraftingLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxItems = 256; ///< CNTO entries of one COBJ
        std::uint32_t mMaxErrors = 256;
    };

    struct CraftingRecords
    {
        std::string mName;
        std::vector<Ingredient> mIngredients;
        std::vector<Recipe> mRecipes;
        std::vector<Enchantment> mEnchantments;
        std::vector<GearEnchant> mGear;
        std::vector<SoulGem> mSoulGems;
        std::vector<Furniture> mFurniture;
        std::vector<std::pair<std::string, FormId>> mKeywords; ///< the crafting keywords: editor id, form
        std::vector<std::pair<FormId, std::string>> mEffectNames; ///< MGEF editor ids
        std::uint64_t mFailed = 0;
        std::uint64_t mOddSubrecords = 0;
        std::vector<Content::Error> mErrors;
        std::optional<Content::Error> mWalkError;
    };

    [[nodiscard]] bool parseCraftingRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const CraftingLimits& limits, CraftingRecords& out);

    /// The merged records of a load order (a later record of an id replaces an earlier one).
    class CraftingTable
    {
    public:
        void add(CraftingRecords&& records);
        void finish(); ///< sorts the recipe list; call once after the last add

        [[nodiscard]] const Ingredient* ingredient(FormId id) const;
        [[nodiscard]] const Recipe* recipe(FormId id) const;
        [[nodiscard]] const Enchantment* enchantment(FormId id) const;
        /// The enchantment an item (WEAP, ARMO) carries from its record; 0 when none.
        [[nodiscard]] FormId enchantmentOf(FormId item) const;
        [[nodiscard]] const SoulGem* soulGem(FormId id) const;
        [[nodiscard]] FormId keyword(std::string_view editorId) const;
        [[nodiscard]] std::string effectName(FormId effect) const; ///< the editor id ("" when unknown)
        /// The bench kind a keyword stands for (None for another keyword).
        [[nodiscard]] BenchKind benchOfKeyword(FormId keyword) const;
        /// The kinds a furniture base offers (a Skyforge forge offers Forge and Skyforge), empty when none.
        [[nodiscard]] std::vector<BenchKind> benchesOf(FormId furniture) const;
        /// The recipes a bench shows, sorted by id. A Forge bench shows the Forge recipes only, a Skyforge bench the
        /// Skyforge ones; `ignoreConditions` false hides recipes that carry conditions.
        [[nodiscard]] std::vector<const Recipe*> recipesFor(BenchKind kind, bool ignoreConditions) const;

        [[nodiscard]] std::size_t ingredientCount() const { return mIngredients.size(); }
        [[nodiscard]] std::size_t recipeCount() const { return mRecipes.size(); }
        [[nodiscard]] std::size_t enchantmentCount() const { return mEnchantments.size(); }
        [[nodiscard]] std::size_t furnitureCount() const { return mFurniture.size(); }

    private:
        std::unordered_map<FormId, Ingredient> mIngredients;
        std::unordered_map<FormId, Recipe> mRecipes;
        std::unordered_map<FormId, Enchantment> mEnchantments;
        std::unordered_map<FormId, FormId> mGear;
        std::unordered_map<FormId, SoulGem> mSoulGems;
        std::unordered_map<FormId, std::vector<FormId>> mFurniture;
        std::map<std::string, FormId> mKeywords;
        std::unordered_map<FormId, std::string> mEffectNames;
        std::vector<FormId> mRecipeOrder;
    };

    /// "EnchWeaponFireDamage03" gives "Fire Damage 03": the prefixes Ench, Weapon, Armor, Alch and a trailing Base are
    /// dropped and a space goes before each capital that follows a lower-case letter. Used when a record has no name.
    [[nodiscard]] std::string prettyEditorId(std::string_view editorId);

    // ---- rules (pure)
    // ------------------------------------------------------------------------------------------------

    struct CraftingSettings
    {
        float mAlchemySkillFactor = 1.5f; ///< GMST fAlchemySkillFactor (VERIFIED value)
        float mEnchantSkillFactor = 1.25f; ///< GMST fEnchantingSkillFactor (VERIFIED value)
        float mTemperPerSkill = 0.004f; ///< weapon damage and armour rating gain per smithing skill point (ours)
        std::array<float, 6> mSoulMultiplier = { 0.f, 0.5f, 0.7f, 1.0f, 1.3f, 1.6f }; ///< by soul size 0 to 5 (ours)
        float mPotionWeight = 0.5f;
        std::int32_t mMaxValue = 100000;
        float mEatMagnitudeScale = 1.f; ///< unused for now (eating applies no effect), kept for tuning
        std::int32_t mPoisonCharges = 1; ///< landed hits a poison on a weapon lasts (ours: the data has no dose count)
    };

    /// 1 + (factor - 1) x skill / 100, the skill clamped to 0 to 100.
    [[nodiscard]] float alchemyStrength(float skill, const CraftingSettings& settings);
    /// The effects shared by at least two of the ingredients (two or three given; others give none), in the order of
    /// first appearance. Magnitude and duration are the best of the ingredients that have the effect.
    [[nodiscard]] std::vector<EffectEntry> sharedEffects(std::span<const Ingredient* const> ingredients);
    /// Magnitudes (not durations) scaled by the strength; an effect with magnitude 0 stays 0.
    [[nodiscard]] std::vector<EffectEntry> scalePotionEffects(const std::vector<EffectEntry>& effects, float strength);
    /// Gold value from the effects' base costs: sum cost x (max(magnitude, 1) x max(duration, 10) / 10) ^ 1.1 (ours),
    /// capped by mMaxValue, at least 1 for a potion with an effect.
    [[nodiscard]] std::int32_t potionValue(const std::vector<EffectEntry>& effects,
        const std::function<float(FormId effect)>& baseCost, const CraftingSettings& settings);

    [[nodiscard]] float temperFraction(float skill, const CraftingSettings& settings);
    /// "" below the first grade, then Fine, Superior, Exquisite, Flawless, Epic, Legendary at 0.04, 0.08, 0.15, 0.25,
    /// 0.35, 0.5.
    [[nodiscard]] std::string_view temperGrade(float fraction);
    /// Per mille of a fraction, rounded to nearest, so the same fraction is the same key.
    [[nodiscard]] std::int32_t toPermille(float fraction);

    /// Base magnitude x soul multiplier x (1 + (factor - 1) x skill / 100). Size outside 1 to 5 gives 0.
    [[nodiscard]] float enchantMagnitude(
        float baseMagnitude, int soulSize, float skill, const CraftingSettings& settings);

    // ---- synthetic items and the saved state
    // -------------------------------------------------------------------------

    enum class SyntheticKind : std::uint8_t
    {
        Potion = 1,
        Gear = 2, ///< a tempered and/or enchanted weapon or armour
    };

    struct Synthetic
    {
        FormId mId = kNullFormId;
        SyntheticKind mKind = SyntheticKind::Potion;
        std::string mName;
        std::int32_t mValue = 0;
        float mWeight = 0.f;
        inventory::Category mCategory = inventory::Category::Potion;
        // potion
        bool mPoison = false;
        std::vector<EffectEntry> mEffects;
        // gear
        FormId mBase = kNullFormId;
        std::int32_t mTemperPermille = 0;
        FormId mEnchantment = kNullFormId; ///< the base enchantment the gear carries
        float mEnchantMagnitude = 0.f; ///< the magnitude of its first effect (scaled)
        friend bool operator==(const Synthetic&, const Synthetic&) = default;
    };

    /// What is saved and what the session knows. Main thread.
    class CraftingState
    {
    public:
        static constexpr std::size_t kMaxSynthetic = 4096;
        static constexpr std::size_t kMaxName = 160;
        static constexpr std::size_t kMaxKnown = 8192;
        static constexpr std::size_t kMaxEffects = 8;

        /// Adds a potion or gear definition and returns its id; an equal definition (everything but the id) returns the
        /// id it already has. 0 when the table is full or the definition is invalid.
        FormId addPotion(
            std::string name, std::int32_t value, float weight, bool poison, std::vector<EffectEntry> effects);
        FormId addGear(FormId base, std::string name, std::int32_t value, float weight, inventory::Category category,
            std::int32_t temperPermille, FormId enchantment, float enchantMagnitude);
        [[nodiscard]] const Synthetic* find(FormId id) const;
        [[nodiscard]] const std::vector<Synthetic>& all() const { return mItems; }

        bool learnEffect(FormId effect); ///< true when new
        bool learnEnchantment(FormId enchantment); ///< true when new
        [[nodiscard]] bool knowsEffect(FormId effect) const { return mEffects.count(effect) != 0; }
        [[nodiscard]] bool knowsEnchantment(FormId e) const { return mEnchantments.count(e) != 0; }
        [[nodiscard]] const std::set<FormId>& knownEffects() const { return mEffects; }
        [[nodiscard]] const std::set<FormId>& knownEnchantments() const { return mEnchantments; }

        /// Moves on every change of the knowledge or the registry (a window re-reads when it differs).
        [[nodiscard]] std::uint32_t revision() const { return mRevision; }
        void clear();

        void write(openeden::sdk::ChunkWriter& writer) const;
        /// A damaged chunk changes nothing.
        [[nodiscard]] bool read(openeden::sdk::ChunkReader& reader, std::string& error);

    private:
        std::vector<Synthetic> mItems;
        std::set<FormId> mEffects;
        std::set<FormId> mEnchantments;
        std::uint32_t mRevision = 0;
    };

    class CraftingChunk final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::string_view kName = "crafting";
        explicit CraftingChunk(CraftingState& state)
            : mState(state)
        {
        }
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }
        void setRestoredSink(std::function<void(const std::string&)> sink) { mRestored = std::move(sink); }
        [[nodiscard]] const std::string& refusal() const { return mRefusal; }

        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }
        void write(openeden::sdk::ChunkWriter& writer) override { mState.write(writer); }
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override;

    private:
        CraftingState& mState;
        std::function<void(const std::string&)> mSink;
        std::function<void(const std::string&)> mRestored;
        std::string mRefusal;
    };
}

#endif
