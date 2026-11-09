#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <openeden/version.hpp>

// The crafting rules of a game: alchemy, enchanting, repair and recharge (task 0085 move 2, stage 2c;
// docs/design/game-packs.md section 5.5). Game packs only, main thread only.
//
// The contract takes and returns plain values. An item (a stack in an inventory, or an object in the world) crosses it as
// an opaque ItemHandle that the core made and can map back to its own object; the contract has no record view and no
// engine, renderer or third-party type. Everything that has game-specific meaning (which effect is a skill effect, what
// a number means) is decided by the pack and handed back as a number or a string.

namespace openeden::sdk::game
{
    /// This header's own version; the SDK as a whole is versioned by kSdkVersion (openeden/version.hpp). Additive changes
    /// (a new virtual function at the END of a class, with a default) raise the minor number.
    inline constexpr Version kCraftingRulesVersion{ 1, 0 };

    /**
     * @brief An item as the contract sees it. Made by the core, valid until the scope that made it ends (a session's end,
     * or the end of the call it was made for); a handle never names another item after that. Zero is "no item".
     * The pack never looks inside.
     */
    struct ItemHandle
    {
        std::uint64_t value = 0;

        [[nodiscard]] constexpr bool empty() const noexcept { return value == 0; }
        friend constexpr bool operator==(ItemHandle, ItemHandle) noexcept = default;
    };

    inline constexpr ItemHandle kNoItem{};

    /// One magic effect of a list, the plain form of an effect entry. The ids are the game's identifiers as text (a
    /// game's ids compare without regard to case); an empty `skill` or `attribute` means "not applicable".
    struct EffectLine
    {
        std::string effectId;
        std::string skill;
        std::string attribute;
        std::int32_t range = 0;
        std::int32_t area = 0;
        std::int32_t duration = 0;
        std::int32_t magnitudeMin = 0;
        std::int32_t magnitudeMax = 0;

        friend bool operator==(const EffectLine&, const EffectLine&) = default;
    };

    /// One effect shown by the alchemy window: which effect, what it targets, and whether the alchemist knows it.
    /// The ids are as in EffectLine; an empty `skill` or `attribute` means the effect has no such target.
    struct AlchemyEffectLine
    {
        std::string effectId;
        std::string skill;
        std::string attribute;
        bool known = false;

        friend bool operator==(const AlchemyEffectLine&, const AlchemyEffectLine&) = default;
    };

    /// The outcome of an alchemy attempt. The numbers are the game's.
    enum class AlchemyResult : std::int32_t
    {
        Success = 0,
        NoMortarAndPestle = 1,
        LessThanTwoIngredients = 2,
        NoName = 3,
        NoEffects = 4,
        RandomFailure = 5,
    };

    /// How an enchanted item casts. The numbers are the game's.
    enum class CastStyle : std::int32_t
    {
        CastOnce = 0,
        WhenStrikes = 1,
        WhenUsed = 2,
        ConstantEffect = 3,
    };

    /// The four apparatus slots of alchemy, in the order of the window's slots. The numbers are the game's.
    enum class ApparatusSlot : std::int32_t
    {
        MortarPestle = 0,
        Alembic = 1,
        Calcinator = 2,
        Retort = 3,
    };

    /// Slots of the alchemy window.
    inline constexpr std::size_t kAlchemyIngredientSlots = 4;
    inline constexpr std::size_t kAlchemyApparatusSlots = 4;

    /**
     * @brief One alchemy session: a setup of an alchemist, apparatus and ingredients. Owned by the caller of
     * CraftingRules::beginAlchemy(). The handles it returns are valid until the session is destroyed or clear() is called.
     */
    class AlchemySession
    {
    public:
        virtual ~AlchemySession() = default;

        /// An empty handle: no alchemist (the session ended).
        virtual void setAlchemist(ItemHandle actor) = 0;
        /// Removes the alchemist, the apparatus and the ingredients.
        virtual void clear() = 0;
        virtual void setPotionName(std::string_view name) = 0;
        /// The most potions the selected ingredients can make.
        [[nodiscard]] virtual int countPotionsToBrew() const = 0;
        /// The slot used, or -1 if there was no free slot or the ingredient type is already listed.
        virtual int addIngredient(ItemHandle ingredient) = 0;
        virtual void addApparatus(ItemHandle apparatus) = 0;
        /// A no-op on an empty slot or an index past the slots.
        virtual void removeIngredient(std::size_t index) = 0;
        virtual void removeApparatus(std::size_t index) = 0;
        /// The item in a slot, or kNoItem. Indices past the slots give kNoItem.
        [[nodiscard]] virtual ItemHandle ingredientSlot(std::size_t index) = 0;
        [[nodiscard]] virtual ItemHandle apparatusSlot(std::size_t index) = 0;
        /// The effects shared by at least two ingredients, each with the alchemist's knowledge of it.
        [[nodiscard]] virtual std::vector<AlchemyEffectLine> effects() const = 0;
        [[nodiscard]] virtual std::string suggestPotionName() = 0;
        /// Makes up to `count` potions; `count` is set to how many were made.
        virtual AlchemyResult create(std::string_view name, int& count) = 0;
    };

    /**
     * @brief One enchanting session (an enchanter's table, or a self-enchantment with a soul gem). Owned by the caller of
     * CraftingRules::beginEnchanting(). Handles it returns are valid until the session is destroyed.
     */
    class EnchantingSession
    {
    public:
        virtual ~EnchantingSession() = default;

        virtual void setEnchanter(ItemHandle actor) = 0;
        virtual void setSelfEnchanting(bool selfEnchanting) = 0;
        virtual void setOldItem(ItemHandle item) = 0;
        virtual void setSoulGem(ItemHandle gem) = 0;
        [[nodiscard]] virtual ItemHandle oldItem() = 0;
        [[nodiscard]] virtual ItemHandle soulGem() = 0;
        virtual void setNewItemName(std::string_view name) = 0;
        virtual void setEffects(std::span<const EffectLine> effects) = 0;
        /// True if the item was created, false if the attempt failed.
        virtual bool create() = 0;
        /// Moves to the next style the old item can have.
        virtual void nextCastStyle() = 0;
        [[nodiscard]] virtual CastStyle castStyle() const = 0;
        [[nodiscard]] virtual float enchantPoints(bool precise) const = 0;
        [[nodiscard]] virtual int effectiveCastCost() const = 0;
        [[nodiscard]] virtual int enchantPrice() const = 0;
        [[nodiscard]] virtual int maxEnchantValue() const = 0;
        [[nodiscard]] virtual int gemCharge() const = 0;
        [[nodiscard]] virtual int enchantChance() const = 0;
        [[nodiscard]] virtual bool soulEmpty() const = 0;
        [[nodiscard]] virtual bool itemEmpty() const = 0;
    };

    /**
     * @brief One repair session around a tool. Owned by the caller of CraftingRules::beginRepair(). A tool that is used
     * up and replaced by another of the same kind comes back from tool() as a NEW handle.
     */
    class RepairSession
    {
    public:
        virtual ~RepairSession() = default;

        virtual void setTool(ItemHandle tool) = 0;
        [[nodiscard]] virtual ItemHandle tool() = 0;
        /// Uses left of the tool.
        [[nodiscard]] virtual int toolUses() const = 0;
        [[nodiscard]] virtual float toolQuality() const = 0;
        /// How many of the tool's stack are left (0: used up).
        [[nodiscard]] virtual int toolCount() const = 0;
        virtual void repair(ItemHandle itemToRepair) = 0;
    };

    /**
     * @brief The crafting rules of one game. Implemented by a game pack, installed in the core, which owns the pack's
     * object. Every function is for the main thread.
     */
    class CraftingRules
    {
    public:
        virtual ~CraftingRules() = default;

        [[nodiscard]] virtual std::unique_ptr<AlchemySession> beginAlchemy() = 0;
        [[nodiscard]] virtual std::unique_ptr<EnchantingSession> beginEnchanting() = 0;
        [[nodiscard]] virtual std::unique_ptr<RepairSession> beginRepair() = 0;

        /// Recharges `item` with the soul gem `gem` on behalf of the player. True if the attempt was made (the gem was
        /// there), whether or not it worked.
        virtual bool rechargeWithGem(ItemHandle item, ItemHandle gem) = 0;

        /// The soul value of a charged soul gem, as the recharge window shows it.
        [[nodiscard]] virtual int gemSoul(ItemHandle gem) = 0;

        /// The charge of an enchanted item after `duration` seconds of recharging over time. The core calls this only
        /// when a change is due (the item is not full and has a charge), once per such item per frame, with plain
        /// numbers and no handle.
        [[nodiscard]] virtual float rechargedCharge(float charge, float maxCharge, float duration) = 0;

        /// Does the actor know the potion effect at this index of a potion's effect list?
        [[nodiscard]] virtual bool knownAlchemyEffect(std::size_t potionEffectIndex, ItemHandle actor) = 0;

        /// The names of the effects of an ingredient that a character with this alchemy skill can see.
        [[nodiscard]] virtual std::vector<std::string> ingredientEffectNames(ItemHandle ingredient, float alchemySkill)
            = 0;

        /// Which alchemy slot an apparatus fits, or -1 if the item is no apparatus.
        [[nodiscard]] virtual int apparatusSlotOf(ItemHandle apparatus) = 0;
    };
} // namespace openeden::sdk::game
