#ifndef OPENEDEN_GAME_SKYRIMSE_CRAFTING_SERVICE_H
#define OPENEDEN_GAME_SKYRIMSE_CRAFTING_SERVICE_H

// The crafting session of a Skyrim SE game and its script bindings (task 0199, milestone M-S7).
//
// `CraftingService` sits on top of the `InventoryService` (task 0182). A session is opened for one bench (a FURN base
// with a crafting keyword); the operations are all-or-nothing steps over the inventory model. Ids cross the bindings as
// text exactly as in the inventory namespace (an item, a recipe, an enchantment is 8 hex digits).
//
// Namespace skyrimse.crafting 1.0 (Read unless marked):
//   state() -> string            "none", "pending", "ready" or "refused";  reason() -> string
//   bench() -> string            the kinds the bench offers, comma separated ("forge,skyforge"); bench_name() -> string
//   results() -> int             counts every attempt, so a window can show a changed status line
//   revision() -> int            moves with the knowledge and the registry
//   last_result() -> string      the outcome of the last operation ("" before the first)
//   ingredients() -> array       the ingredients the player holds;  ingredient_effects(item) -> string  ("A, ?, ?, ?")
//   preview(a, b, c) -> string   the known effects the potion would have ("" for a missing third ingredient)
//   recipes() -> array           the recipes the bench shows;  recipe_name(r), recipe_needs(r) -> string
//   recipe_ok(r) -> bool         the player has what it needs;  recipe_temper(r) -> bool  a tempering recipe
//   gear() -> array              the weapons and armour the player holds;  gear_enchant(item) -> string ("" when none)
//   enchantments() -> array      the enchantments learned;  enchant_name(e) -> string
//   gems() -> array              the filled soul gems the player holds;  enchant_preview(item, e, gem) -> string
//   open(placement, base, name)  [Mutate]  a bench session;  open_kind(kind) [Mutate] only with the test setting
//   close(), brew(a, b, c), eat(item), craft(r), temper(r), disenchant(item), enchant(item, e, gem)  [Mutate]
// The Mutate functions are queued by the core and run on the main thread in call order. Nothing here waits.

#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/crafting.hpp>
#include <game_skyrimse/inventory_service.hpp>
#include <game_skyrimse/poison.hpp>
#include <game_skyrimse/progression_service.hpp>

#include <openeden/game/bindings.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace game_skyrimse::crafting
{
    struct EffectInfo
    {
        bool mFound = false;
        float mBaseCost = 0.f;
        bool mHostile = false; ///< MGEF flag Hostile or Detrimental: a potion of it is a poison
        bool mDamageHealth = false; ///< a value modifier on Health that hurts (the combat rules understand it)
    };

    struct CraftingHooks
    {
        std::function<bool(inventory::Inputs&)> mInputs; ///< the plugins to read (the inventory's hook will do)
        std::function<std::string(FormId)> mName; ///< FULL of an item or record; empty when none
        std::function<EffectInfo(FormId effect)> mEffectInfo;
        std::function<float(std::string_view skill)> mSkill; ///< "alchemy", "smithing", "enchanting"; 15 when empty
        std::function<void(std::string_view skill, float amount)> mSkillUsed; ///< told after each successful craft
        /// Task 0206: do a recipe's perk conditions hold for the player? `itemEnchanted` answers the tempering test.
        /// Unset: perk conditions are not evaluated (a forge recipe with conditions is hidden, tempering is shown).
        std::function<bool(std::span<const survey::Ctda> conditions, bool itemEnchanted)> mRecipeConditions;
        std::function<void(const std::string&)> mLog;
    };

    /// Task 0206: connects crafting to the player's progression. A craft trains its skill (`amount * boost`, boost the
    /// pack's test multiplier), the formulas read the real skill once the progression is ready (before: what the hook
    /// said), and a recipe's perk conditions are answered from the bought perks. The earlier hooks still run. The
    /// progression must outlive the crafting service.
    void linkProgression(CraftingHooks& hooks, progression::ProgressionService& progression, float boost = 1.f);

    enum class Status : std::uint8_t
    {
        Ok,
        NotReady,
        NoBench, ///< no session, or the session's bench does not offer this
        BadArgument,
        UnknownItem,
        NotHeld,
        TooFewIngredients, ///< fewer than two, or the same ingredient twice
        NoSharedEffect,
        UnknownRecipe,
        MissingItems,
        AlreadyBetter, ///< the item is tempered at least as well
        NotGear,
        NotEnchanted,
        AlreadyKnown,
        UnknownEnchantment,
        AlreadyEnchanted,
        NoSoulGem,
        NoEffect, ///< eating: nothing left to learn
    };
    [[nodiscard]] std::string_view statusName(Status status);

    /// Task 0229: what happened when the player tried to coat his weapon with an item.
    enum class PoisonUse : std::uint8_t
    {
        NotPoison, ///< the item is not a poison (a potion, food, anything): the caller goes on as for any item
        Applied,
        NoWeapon, ///< no weapon in hand
        NotMelee, ///< a bow: the poison reaches no one through an arrow here
        AlreadyPoisoned, ///< the weapon carries a poison
    };

    class CraftingService
    {
    public:
        enum class State : std::uint8_t
        {
            None,
            Pending,
            Ready,
            Refused,
        };
        static constexpr std::uint32_t kMaxPendingPolls = 3000;

        CraftingService(inventory::InventoryService& inventory, CraftingHooks hooks, CraftingSettings settings = {},
            bool ignoreConditions = false, bool testBench = false);
        ~CraftingService();
        CraftingService(const CraftingService&) = delete;
        CraftingService& operator=(const CraftingService&) = delete;

        /// Once a frame: starts and adopts the record reader (a worker), registers the saved items in the item table
        /// once both tables are built, retries a pending session. Never waits. Main thread.
        void poll();
        [[nodiscard]] bool ready() const { return mTableReady; }
        [[nodiscard]] CraftingState& state() { return mState; }
        [[nodiscard]] const CraftingState& state() const { return mState; }
        [[nodiscard]] const CraftingTable& table() const { return mTable; }
        [[nodiscard]] CraftingChunk& chunk() { return mChunk; }
        /// Task 0233: the weapons' poisons (their own chunk).
        [[nodiscard]] PoisonChunk& poisonChunk() { return mPoisonChunk; }

        /// Task 0213: is this furniture a crafting bench? "yes", "no", or "pending" while the recipe table is being
        /// built.
        [[nodiscard]] std::string benchAnswer(FormId furniture) const;
        void openBench(std::uint64_t placement, FormId furniture, std::string name);
        void openKind(BenchKind kind);
        void close();
        [[nodiscard]] State session() const { return mSession; }
        [[nodiscard]] const std::string& reason() const { return mReason; }
        [[nodiscard]] const std::vector<BenchKind>& kinds() const { return mKinds; }

        Status brew(std::span<const FormId> ingredients);
        Status eat(FormId ingredient);
        Status craft(FormId recipe);
        Status temper(FormId recipe);
        Status disenchant(FormId item);
        Status enchant(FormId item, FormId enchantment, FormId gem);

        /// Task 0229: coats `weapon` (a weapon item the player wields) with `item` when `item` is a poison: its first
        /// effect is hostile. Does not touch the inventory: the caller consumes the item on Applied.
        PoisonUse usePoison(FormId item, FormId weapon, bool melee);
        /// The poison a weapon item carries, or null.
        [[nodiscard]] const WeaponPoison* coating(FormId weapon) const { return mCoatings.find(weapon); }
        /// One landed hit spent: the charges left, or -1 when the weapon carried none.
        int spendPoison(FormId weapon) { return mCoatings.spend(weapon); }

        /// The record item an item stands for: a tempered or enchanted item's base, else the item.
        [[nodiscard]] FormId baseOf(FormId item) const;
        /// Fills `out` with the combat record of a made weapon: the base's damage times (1 + temper) plus the damage of
        /// a damage-health enchantment. False when the item is not a made weapon.
        [[nodiscard]] bool weaponFor(FormId item, const WeaponRecord& base, WeaponRecord& out) const;
        /// Scales the armour rating by the temper of a made armour. False when the item is not made gear.
        [[nodiscard]] bool armourFor(FormId item, ArmourPiece& piece) const;

        /// The recipes the open session's benches show to the player (conditions applied), sorted by id.
        [[nodiscard]] std::vector<const Recipe*> visibleRecipes() const;

        [[nodiscard]] const std::string& lastResult() const { return mLastResult; }
        [[nodiscard]] std::uint32_t results() const { return mResults; }
        [[nodiscard]] float skill(std::string_view name) const;
        [[nodiscard]] std::string itemName(FormId item) const;
        [[nodiscard]] std::string effectName(FormId effect) const;
        [[nodiscard]] std::string enchantName(FormId enchantment) const;

        [[nodiscard]] openeden::sdk::DeclareResult declare(openeden::sdk::BindingsService& service);

    private:
        struct Impl;
        bool tryOpen();
        void refuse(std::string reason);
        void log(const std::string& line) const;
        Status done(Status status, std::string_view what);
        void materialise(const Synthetic& synthetic);
        void catchUp();
        [[nodiscard]] std::int32_t held(FormId item) const;
        [[nodiscard]] bool hasKind(BenchKind kind) const;
        [[nodiscard]] EffectInfo effectInfo(FormId effect) const;
        [[nodiscard]] FormId enchantmentOfGear(FormId item) const; ///< the base enchantment on an item, or 0
        [[nodiscard]] bool isGear(FormId item) const;
        void swapEquipped(FormId from, FormId to);
        /// The held item a tempering recipe improves (the plain base first, else the best made gear of that base) and
        /// its current temper in per mille. False when the player holds none.
        [[nodiscard]] bool temperTarget(FormId base, FormId& item, std::int32_t& permille) const;
        [[nodiscard]] bool materialsHeld(const Recipe& recipe) const;
        [[nodiscard]] std::string gearName(FormId base, std::int32_t permille, FormId enchantment) const;
        [[nodiscard]] bool recipeOk(const Recipe& recipe) const;
        /// Task 0206: do the recipe's conditions let the player see and use it (see
        /// `CraftingHooks::mRecipeConditions`)?
        [[nodiscard]] bool conditionsAllow(const Recipe& recipe) const;

        std::unique_ptr<Impl> mImpl;
        inventory::InventoryService& mInventory;
        CraftingHooks mHooks;
        CraftingSettings mSettings;
        bool mIgnoreConditions;
        bool mTestBench;
        CraftingState mState;
        PoisonCoatings mCoatings;
        PoisonChunk mPoisonChunk{ mCoatings };
        CraftingChunk mChunk;
        CraftingTable mTable;
        CraftingTable mBuilt;
        std::thread mThread;
        std::atomic<int> mWorker{ 0 }; ///< 0 idle, 1 reading, 2 read
        bool mTableReady = false;
        std::uint64_t mReadMillis = 0;
        std::size_t mMaterialised = 0; ///< how many of the state's items are in the item table
        State mSession = State::None;
        std::string mReason;
        std::string mBenchName;
        std::uint64_t mPlacement = 0;
        FormId mFurniture = kNullFormId;
        std::vector<BenchKind> mKinds;
        std::uint32_t mPolls = 0;
        std::uint32_t mResults = 0;
        std::string mLastResult;
    };
}

#endif
