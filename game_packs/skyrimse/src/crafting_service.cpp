#include "game_skyrimse/crafting_service.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>

// The crafting session, its operations and its bindings: see the header (task 0199).

namespace game_skyrimse::crafting
{
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingShape;
    using openeden::sdk::BindingStatus;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    namespace
    {
        class Fn final : public openeden::sdk::BindingHandler
        {
        public:
            using Body = std::function<BindingResult(std::span<const BindingValue>)>;
            explicit Fn(Body body)
                : mBody(std::move(body))
            {
            }
            BindingResult call(std::span<const BindingValue> args) override { return mBody(args); }

        private:
            Body mBody;
        };

        BindingResult ok()
        {
            return BindingResult{};
        }

        BindingResult fail(BindingStatus status, std::string_view message)
        {
            BindingResult r;
            r.status = status;
            r.message = message;
            return r;
        }

        BindingValue integer(std::int32_t v)
        {
            BindingValue b;
            b.type = BindingType::Int32;
            b.integer = v;
            return b;
        }

        BindingValue boolean(bool v)
        {
            BindingValue b;
            b.type = BindingType::Bool;
            b.boolean = v;
            return b;
        }

        BindingValue text(std::string_view v)
        {
            BindingValue b;
            b.type = BindingType::String;
            b.text = v;
            return b;
        }

        const char* stateName(CraftingService::State state)
        {
            switch (state)
            {
                case CraftingService::State::None:
                    return "none";
                case CraftingService::State::Pending:
                    return "pending";
                case CraftingService::State::Ready:
                    return "ready";
                case CraftingService::State::Refused:
                    return "refused";
            }
            return "none";
        }

        std::string join(const std::vector<std::string>& parts, std::string_view separator)
        {
            std::string out;
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                if (i > 0)
                    out += separator;
                out += parts[i];
            }
            return out;
        }
    }

    std::string_view statusName(Status status)
    {
        switch (status)
        {
            case Status::Ok:
                return "ok";
            case Status::NotReady:
                return "not ready";
            case Status::NoBench:
                return "no bench";
            case Status::BadArgument:
                return "bad argument";
            case Status::UnknownItem:
                return "unknown item";
            case Status::NotHeld:
                return "not held";
            case Status::TooFewIngredients:
                return "too few ingredients";
            case Status::NoSharedEffect:
                return "no shared effect";
            case Status::UnknownRecipe:
                return "unknown recipe";
            case Status::MissingItems:
                return "missing items";
            case Status::AlreadyBetter:
                return "already at least that good";
            case Status::NotGear:
                return "not a weapon or armour";
            case Status::NotEnchanted:
                return "not enchanted";
            case Status::AlreadyKnown:
                return "already known";
            case Status::UnknownEnchantment:
                return "unknown enchantment";
            case Status::AlreadyEnchanted:
                return "already enchanted";
            case Status::NoSoulGem:
                return "no filled soul gem";
            case Status::NoEffect:
                return "nothing left to learn";
        }
        return "failed";
    }

    struct CraftingService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mText;
        std::vector<std::string> mStrings;
        std::vector<BindingValue> mElements;
    };

    void linkProgression(CraftingHooks& hooks, progression::ProgressionService& progression, float boost)
    {
        if (!std::isfinite(boost) || boost < 0.f)
            boost = 1.f;
        // what the recipe conditions read of the player, rebuilt only when the progression changed
        struct Cache
        {
            std::uint64_t mRevision = 0;
            bool mValid = false;
            std::vector<FormId> mPerks;
            std::vector<std::pair<std::uint32_t, float>> mValues;
            std::uint16_t mLevel = 1;
        };
        const auto cache = std::make_shared<Cache>();
        const auto earlierSkill = hooks.mSkill;
        hooks.mSkill = [&progression, earlierSkill](std::string_view name) {
            const std::size_t slot = skillSlotOfCraftingName(name);
            float real = 0.f;
            if (slot < progression::kSkillCount
                && progression.actorValue(progression::kFirstSkillValue + static_cast<std::uint32_t>(slot), real))
                return real;
            return earlierSkill ? earlierSkill(name) : 15.f;
        };
        const auto earlierUsed = hooks.mSkillUsed;
        hooks.mSkillUsed = [&progression, boost, earlierUsed](std::string_view name, float amount) {
            const std::size_t slot = skillSlotOfCraftingName(name);
            if (slot < progression::kSkillCount)
                progression.skillUsed(slot, amount * boost);
            if (earlierUsed)
                earlierUsed(name, amount);
        };
        hooks.mRecipeConditions = [&progression, cache](std::span<const survey::Ctda> list, bool enchanted) {
            const progression::PlayerProgression* p = progression.progression();
            if (p == nullptr)
                return false; // the perks are not known yet: nothing a perk gates is offered
            if (!cache->mValid || cache->mRevision != p->revision())
            {
                cache->mValid = true;
                cache->mRevision = p->revision();
                cache->mPerks = p->hasPerkSet();
                cache->mValues = p->actorValues();
                cache->mLevel = p->level();
            }
            return recipeConditionsHold(list, cache->mPerks, cache->mLevel, cache->mValues, enchanted);
        };
    }

    CraftingService::CraftingService(inventory::InventoryService& inventory, CraftingHooks hooks,
        CraftingSettings settings, bool ignoreConditions, bool testBench)
        : mImpl(std::make_unique<Impl>())
        , mInventory(inventory)
        , mHooks(std::move(hooks))
        , mSettings(settings)
        , mIgnoreConditions(ignoreConditions)
        , mTestBench(testBench)
        , mChunk(mState)
    {
    }

    CraftingService::~CraftingService()
    {
        if (mThread.joinable())
            mThread.join();
    }

    void CraftingService::log(const std::string& line) const
    {
        if (mHooks.mLog)
            mHooks.mLog(line);
    }

    // ---- the record table and the saved items
    // -------------------------------------------------------------------------

    void CraftingService::poll()
    {
        if (!mTableReady)
        {
            const int worker = mWorker.load(std::memory_order_acquire);
            if (worker == 0)
            {
                auto inputs = std::make_shared<inventory::Inputs>();
                if (mHooks.mInputs && mHooks.mInputs(*inputs))
                {
                    mWorker.store(1, std::memory_order_release);
                    mThread = std::thread([this, inputs]() {
                        const auto started = std::chrono::steady_clock::now();
                        CraftingTable table;
                        try
                        {
                            const CraftingLimits limits;
                            for (const std::filesystem::path& path : inputs->mPaths)
                            {
                                Content::Error openError;
                                const std::unique_ptr<Content::Plugin> file
                                    = Content::Plugin::openFile(path, openError);
                                if (file == nullptr)
                                    continue;
                                CraftingRecords records;
                                (void)parseCraftingRecords(*file, inputs->mOrder, limits, records);
                                table.add(std::move(records));
                            }
                        }
                        catch (...)
                        {
                            // what was read stays: the table is then partial, never undefined
                        }
                        table.finish();
                        mBuilt = std::move(table);
                        mReadMillis = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - started)
                                                                     .count());
                        mWorker.store(2, std::memory_order_release);
                    });
                }
            }
            else if (worker == 2)
            {
                mThread.join();
                mTable = std::move(mBuilt);
                mTableReady = true;
                log("CRAFT records read on a worker in " + std::to_string(mReadMillis)
                    + " ms: " + std::to_string(mTable.ingredientCount()) + " ingredients, "
                    + std::to_string(mTable.recipeCount()) + " recipes, " + std::to_string(mTable.enchantmentCount())
                    + " enchantments, " + std::to_string(mTable.furnitureCount()) + " furniture with keywords");
            }
        }
        catchUp();
        if (mSession == State::Pending)
        {
            ++mPolls;
            if (!tryOpen() && mPolls > kMaxPendingPolls)
                refuse("the crafting records were not ready");
        }
    }

    void CraftingService::catchUp()
    {
        // the items of a saved game (and the new ones) join the item table once it is built
        if (!mInventory.ready())
            return;
        const std::vector<Synthetic>& all = mState.all();
        if (mMaterialised > all.size())
            mMaterialised = 0;
        for (; mMaterialised < all.size(); ++mMaterialised)
            materialise(all[mMaterialised]);
    }

    void CraftingService::materialise(const Synthetic& s)
    {
        inventory::ItemStat stat;
        stat.mId = s.mId;
        stat.mCategory = s.mCategory;
        stat.mValue = s.mValue;
        stat.mWeight = s.mWeight;
        stat.mDecoded = true;
        stat.mName = s.mName;
        for (const EffectEntry& e : s.mEffects)
            stat.mEffects.push_back({ e.mEffect, e.mMagnitude, e.mArea, e.mDuration });
        if (s.mKind == SyntheticKind::Gear)
            if (const inventory::ItemStat* base = mInventory.table().item(s.mBase))
                stat.mKeywords = base->mKeywords;
        mInventory.addSyntheticItem(std::move(stat));
    }

    // ---- the session
    // --------------------------------------------------------------------------------------------------

    std::string CraftingService::benchAnswer(FormId furniture) const
    {
        if (!mTableReady)
            return "pending";
        return mTable.benchesOf(furniture, mHouseBuilding).empty() ? "no" : "yes";
    }

    void CraftingService::openBench(std::uint64_t placement, FormId furniture, std::string name)
    {
        mSession = State::Pending;
        mReason.clear();
        mKinds.clear();
        mBenchName = std::move(name);
        mPlacement = placement;
        mFurniture = furniture;
        mPolls = 0;
        (void)tryOpen();
    }

    bool CraftingService::tryOpen()
    {
        if (!mTableReady)
            return false;
        std::vector<BenchKind> kinds = mTable.benchesOf(mFurniture, mHouseBuilding);
        if (kinds.empty())
        {
            refuse("that is not a crafting bench");
            return true;
        }
        mKinds = std::move(kinds);
        mSession = State::Ready;
        std::vector<std::string> names;
        for (const BenchKind kind : mKinds)
            names.emplace_back(benchKindName(kind));
        log("CRAFT open " + mBenchName + " (" + join(names, ",") + ") placement " + inventory::formatId(mPlacement));
        if (mHouseBuilding && mHooks.mBenchOpened)
            mHooks.mBenchOpened(mPlacement, mFurniture); // task 0317: the workbench's own OnActivate
        return true;
    }

    void CraftingService::refuse(std::string reason)
    {
        mSession = State::Refused;
        mReason = std::move(reason);
        mKinds.clear();
        log("CRAFT open " + mBenchName + " refused: " + mReason);
    }

    void CraftingService::openKind(BenchKind kind)
    {
        if (kind == BenchKind::None)
            return;
        mSession = State::Ready;
        mReason.clear();
        mKinds = { kind };
        mBenchName = std::string(benchKindName(kind));
        mFurniture = kNullFormId;
        log("CRAFT open test bench " + mBenchName);
    }

    void CraftingService::close()
    {
        if (mSession != State::None)
            log("CRAFT close " + mBenchName + ", " + std::to_string(mResults) + " results");
        mSession = State::None;
        mKinds.clear();
        mReason.clear();
        mBenchName.clear();
    }

    bool CraftingService::hasKind(BenchKind kind) const
    {
        return mSession == State::Ready && std::find(mKinds.begin(), mKinds.end(), kind) != mKinds.end();
    }

    // ---- helpers
    // ------------------------------------------------------------------------------------------------------

    float CraftingService::skill(std::string_view name) const
    {
        return mHooks.mSkill ? mHooks.mSkill(name) : 15.f;
    }

    std::int32_t CraftingService::held(FormId item) const
    {
        return mInventory.model().count(inventory::kPlayerOwner, item);
    }

    EffectInfo CraftingService::effectInfo(FormId effect) const
    {
        return mHooks.mEffectInfo ? mHooks.mEffectInfo(effect) : EffectInfo{};
    }

    std::string CraftingService::itemName(FormId item) const
    {
        if (const inventory::ItemStat* stat = mInventory.table().item(item); stat != nullptr && !stat->mName.empty())
            return stat->mName;
        std::string n = mHooks.mName ? mHooks.mName(item) : std::string();
        return n.empty() ? "<" + inventory::formatId(item) + ">" : n;
    }

    std::string CraftingService::effectName(FormId effect) const
    {
        std::string n = mHooks.mName ? mHooks.mName(effect) : std::string();
        if (n.empty())
            n = prettyEditorId(mTable.effectName(effect));
        return n.empty() ? "<" + inventory::formatId(effect) + ">" : n;
    }

    std::string CraftingService::enchantName(FormId enchantment) const
    {
        std::string n = mHooks.mName ? mHooks.mName(enchantment) : std::string();
        if (n.empty())
            if (const Enchantment* e = mTable.enchantment(enchantment))
                n = prettyEditorId(e->mEditorId);
        return n.empty() ? "<" + inventory::formatId(enchantment) + ">" : n;
    }

    bool CraftingService::isGear(FormId item) const
    {
        const inventory::ItemStat* stat = mInventory.table().item(item);
        return stat != nullptr
            && (stat->mCategory == inventory::Category::Weapon || stat->mCategory == inventory::Category::Armor);
    }

    FormId CraftingService::enchantmentOfGear(FormId item) const
    {
        if (const Synthetic* s = mState.find(item))
            return s->mKind == SyntheticKind::Gear ? s->mEnchantment : kNullFormId;
        return mTable.enchantmentOf(item);
    }

    FormId CraftingService::baseOf(FormId item) const
    {
        const Synthetic* s = mState.find(item);
        return s != nullptr && s->mKind == SyntheticKind::Gear ? s->mBase : item;
    }

    Status CraftingService::done(Status status, std::string_view what)
    {
        ++mResults;
        mLastResult = std::string(statusName(status));
        log("CRAFT " + std::string(what) + ": " + mLastResult);
        return status;
    }

    void CraftingService::swapEquipped(FormId from, FormId to)
    {
        (void)from;
        (void)mInventory.model().equip(to, mInventory.equipInfoOf(to));
    }

    bool CraftingService::materialsHeld(const Recipe& recipe) const
    {
        for (const InventoryEntry& e : recipe.mItems)
            if (held(e.mItem) < e.mCount)
                return false;
        return true;
    }

    std::string CraftingService::gearName(FormId base, std::int32_t permille, FormId enchantment) const
    {
        std::string name = itemName(base);
        const std::string_view grade = temperGrade(static_cast<float>(permille) / 1000.f);
        if (!grade.empty())
            name = std::string(grade) + " " + name;
        if (enchantment != kNullFormId)
            name += " of " + enchantName(enchantment);
        return name;
    }

    bool CraftingService::temperTarget(FormId base, FormId& item, std::int32_t& permille) const
    {
        if (held(base) > 0)
        {
            item = base;
            permille = 0;
            return true;
        }
        bool found = false;
        for (const Synthetic& s : mState.all())
            if (s.mKind == SyntheticKind::Gear && s.mBase == base && held(s.mId) > 0
                && (!found || s.mTemperPermille > permille))
            {
                item = s.mId;
                permille = s.mTemperPermille;
                found = true;
            }
        return found;
    }

    bool CraftingService::conditionsAllow(const Recipe& recipe) const
    {
        if (mIgnoreConditions || recipe.mConditions == 0)
            return true;
        const bool temper = isTemperBench(mTable.benchOfKeyword(recipe.mBench));
        // Task 0206: a list of only perk and skill tests (the forge and tempering recipes of the data) is answered
        // from the player's perks; any other list, and a list that did not decode whole, keeps the earlier rule.
        if (mHooks.mRecipeConditions && recipe.mConditionList.size() == recipe.mConditions
            && recipeConditionsPerkOnly(recipe.mConditionList))
        {
            bool enchanted = false; // fn 659: the item a tempering recipe would improve carries an enchantment
            if (temper)
            {
                FormId item = 0;
                std::int32_t permille = 0;
                if (temperTarget(recipe.mResult, item, permille))
                    enchanted = enchantmentOfGear(item) != kNullFormId;
            }
            return mHooks.mRecipeConditions(recipe.mConditionList, enchanted);
        }
        // Task 0229: the tanning rack's recipes carry GetItemCount(item) >= 1 on the pelt they consume: it holds while
        // the player holds it (the recipe is then offered, as in the game).
        if (recipe.mConditionList.size() == recipe.mConditions && recipeConditionsItemCountOnly(recipe.mConditionList))
            return itemCountConditionsHold(recipe.mConditionList, [this](FormId item) { return held(item); });
        return temper; // every tempering recipe of the game carries conditions: they were never hidden
    }

    bool CraftingService::recipeOk(const Recipe& recipe) const
    {
        if (!materialsHeld(recipe))
            return false;
        if (!isTemperBench(mTable.benchOfKeyword(recipe.mBench)))
            return true;
        FormId item = 0;
        std::int32_t current = 0;
        if (!temperTarget(recipe.mResult, item, current))
            return false;
        return toPermille(temperFraction(skill("smithing"), mSettings)) > current;
    }

    std::vector<const Recipe*> CraftingService::visibleRecipes() const
    {
        std::vector<const Recipe*> out;
        if (mSession != State::Ready)
            return out;
        for (const BenchKind kind : mKinds)
            for (const Recipe* r : mTable.recipesFor(kind, true))
                if (mInventory.table().item(r->mResult) != nullptr
                    && (kind != BenchKind::House || mTable.furnitureOffers(mFurniture, r->mBench))
                    && conditionsAllow(*r))
                    out.push_back(r);
        std::sort(out.begin(), out.end(), [](const Recipe* a, const Recipe* b) { return a->mId < b->mId; });
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // ---- alchemy
    // ------------------------------------------------------------------------------------------------------

    Status CraftingService::brew(std::span<const FormId> ingredients)
    {
        if (!mTableReady || !mInventory.ready())
            return Status::NotReady;
        if (!hasKind(BenchKind::Alchemy))
            return done(Status::NoBench, "brew");
        if (ingredients.size() < 2 || ingredients.size() > 3)
            return done(Status::TooFewIngredients, "brew");
        std::vector<const Ingredient*> chosen;
        for (const FormId id : ingredients)
        {
            const Ingredient* ing = mTable.ingredient(id);
            if (ing == nullptr)
                return done(Status::UnknownItem, "brew");
            for (const Ingredient* other : chosen)
                if (other->mId == id)
                    return done(Status::TooFewIngredients, "brew");
            chosen.push_back(ing);
        }
        for (const FormId id : ingredients)
            if (held(id) < 1)
                return done(Status::NotHeld, "brew");
        const std::vector<EffectEntry> shared = sharedEffects(chosen);
        if (shared.empty())
            return done(Status::NoSharedEffect, "brew");
        const float strength = alchemyStrength(skill("alchemy"), mSettings);
        const std::vector<EffectEntry> effects = scalePotionEffects(shared, strength);
        const bool poison = effectInfo(effects.front().mEffect).mHostile;
        const std::string name
            = std::string(poison ? "Poison of " : "Potion of ") + effectName(effects.front().mEffect);
        const std::int32_t value = potionValue(
            effects, [this](FormId effect) { return effectInfo(effect).mBaseCost; }, mSettings);
        const FormId potion = mState.addPotion(name, value, mSettings.mPotionWeight, poison, effects);
        if (potion == kNullFormId)
            return done(Status::BadArgument, "brew");
        catchUp();
        for (const FormId id : ingredients)
            (void)mInventory.model().consume(inventory::kPlayerOwner, id, 1);
        (void)mInventory.model().grant(inventory::kPlayerOwner, potion, 1);
        for (const EffectEntry& e : shared)
            mState.learnEffect(e.mEffect);
        if (mHooks.mSkillUsed)
            mHooks.mSkillUsed("alchemy", static_cast<float>(value));
        return done(Status::Ok, "brew " + name + " (value " + std::to_string(value) + ")");
    }

    Status CraftingService::eat(FormId ingredient)
    {
        if (!mTableReady || !mInventory.ready())
            return Status::NotReady;
        const Ingredient* ing = mTable.ingredient(ingredient);
        if (ing == nullptr)
            return done(Status::UnknownItem, "eat");
        if (held(ingredient) < 1)
            return done(Status::NotHeld, "eat");
        for (const EffectEntry& e : ing->mEffects)
            if (!mState.knowsEffect(e.mEffect))
            {
                mState.learnEffect(e.mEffect);
                (void)mInventory.model().consume(inventory::kPlayerOwner, ingredient, 1);
                return done(Status::Ok, "eat " + inventory::formatId(ingredient) + " learns " + effectName(e.mEffect));
            }
        return done(Status::NoEffect, "eat");
    }

    // ---- smithing
    // -----------------------------------------------------------------------------------------------------

    Status CraftingService::craft(FormId recipeId)
    {
        if (!mTableReady || !mInventory.ready())
            return Status::NotReady;
        if (mSession != State::Ready)
            return done(Status::NoBench, "craft");
        const Recipe* r = mTable.recipe(recipeId);
        if (r == nullptr)
            return done(Status::UnknownRecipe, "craft");
        const BenchKind kind = mTable.benchOfKeyword(r->mBench);
        if (isTemperBench(kind) || !hasKind(kind))
            return done(Status::NoBench, "craft");
        if (kind == BenchKind::House && !mTable.furnitureOffers(mFurniture, r->mBench))
            return done(Status::NoBench, "craft"); // task 0317: this furniture does not carry the recipe's keyword
        if (!conditionsAllow(*r))
            return done(Status::UnknownRecipe, "craft");
        const inventory::ItemStat* result = mInventory.table().item(r->mResult);
        if (result == nullptr)
            return done(Status::UnknownItem, "craft");
        if (!materialsHeld(*r))
            return done(Status::MissingItems, "craft");
        for (const InventoryEntry& e : r->mItems)
            (void)mInventory.model().consume(inventory::kPlayerOwner, e.mItem, e.mCount);
        if (kind == BenchKind::House)
        {
            // Task 0317: a house part is a MISC whose script hears it come into the player's inventory
            // (OnContainerChanged with no old container): `add` reports the move, `grant` does not
            (void)mInventory.model().add(inventory::kPlayerOwner, r->mResult, std::max<std::int32_t>(1, r->mCount));
            return done(Status::Ok, "craft " + itemName(r->mResult)); // no skill is trained
        }
        (void)mInventory.model().grant(inventory::kPlayerOwner, r->mResult, std::max<std::int32_t>(1, r->mCount));
        // Task 0229: the smelter and the tanning rack train smithing as the forge does; cooking trains nothing
        // (RECALLED-VERIFY: Skyrim gives the cooking pot no experience).
        if (mHooks.mSkillUsed && kind != BenchKind::Cookpot)
            mHooks.mSkillUsed("smithing", static_cast<float>(std::max(1, result->mValue)));
        return done(Status::Ok, "craft " + itemName(r->mResult));
    }

    PoisonUse CraftingService::usePoison(FormId item, FormId weapon, bool melee)
    {
        if (!mTableReady || !mInventory.ready())
            return PoisonUse::NotPoison;
        const inventory::ItemStat* stat = mInventory.table().item(item);
        // A poison is a potion whose first effect is hostile (an ALCH of the game or one made at an alchemy lab).
        if (stat == nullptr || stat->mCategory != inventory::Category::Potion || stat->mEffects.empty()
            || !effectInfo(stat->mEffects.front().mEffect).mHostile)
            return PoisonUse::NotPoison;
        if (weapon == kNullFormId)
            return PoisonUse::NoWeapon;
        if (!melee)
            return PoisonUse::NotMelee;
        WeaponPoison poison;
        poison.mPoison = item;
        poison.mName = itemName(item);
        poison.mCharges = mSettings.mPoisonCharges;
        for (const inventory::ItemEffect& e : stat->mEffects)
        {
            EffectEntry entry;
            entry.mEffect = e.mEffect;
            entry.mMagnitude = e.mMagnitude;
            entry.mArea = e.mArea;
            entry.mDuration = e.mDuration;
            poison.mEffects.push_back(entry);
        }
        const CoatStatus coated = mCoatings.apply(weapon, std::move(poison));
        log("CRAFT poison " + itemName(item) + " on " + itemName(weapon) + ": "
            + (coated == CoatStatus::Ok                     ? "applied"
                    : coated == CoatStatus::AlreadyPoisoned ? "already poisoned"
                                                            : "refused"));
        if (coated == CoatStatus::Ok)
            return PoisonUse::Applied;
        return coated == CoatStatus::AlreadyPoisoned ? PoisonUse::AlreadyPoisoned : PoisonUse::NoWeapon;
    }

    Status CraftingService::temper(FormId recipeId)
    {
        if (!mTableReady || !mInventory.ready())
            return Status::NotReady;
        if (mSession != State::Ready)
            return done(Status::NoBench, "temper");
        const Recipe* r = mTable.recipe(recipeId);
        if (r == nullptr)
            return done(Status::UnknownRecipe, "temper");
        const BenchKind kind = mTable.benchOfKeyword(r->mBench);
        if (!isTemperBench(kind) || !hasKind(kind))
            return done(Status::NoBench, "temper");
        if (!conditionsAllow(*r))
            return done(Status::UnknownRecipe, "temper");
        const inventory::ItemStat* baseStat = mInventory.table().item(r->mResult);
        if (baseStat == nullptr || !isGear(r->mResult))
            return done(Status::NotGear, "temper");
        FormId target = 0;
        std::int32_t current = 0;
        if (!temperTarget(r->mResult, target, current))
            return done(Status::NotHeld, "temper");
        const float fraction = temperFraction(skill("smithing"), mSettings);
        const std::int32_t permille = toPermille(fraction);
        if (permille <= current)
            return done(Status::AlreadyBetter, "temper");
        if (!materialsHeld(*r))
            return done(Status::MissingItems, "temper");
        const Synthetic* old = mState.find(target);
        const FormId enchantment = old != nullptr ? old->mEnchantment : kNullFormId;
        const float enchantMagnitude = old != nullptr ? old->mEnchantMagnitude : 0.f;
        const std::int32_t value = static_cast<std::int32_t>(std::lround(
                                       static_cast<double>(baseStat->mValue) * (1.0 + 2.0 * permille / 1000.0)))
            + (old != nullptr ? old->mValue
                        - static_cast<std::int32_t>(std::lround(
                            static_cast<double>(baseStat->mValue) * (1.0 + 2.0 * old->mTemperPermille / 1000.0)))
                              : 0);
        const std::string name = gearName(r->mResult, permille, enchantment);
        const FormId made = mState.addGear(
            r->mResult, name, value, baseStat->mWeight, baseStat->mCategory, permille, enchantment, enchantMagnitude);
        if (made == kNullFormId)
            return done(Status::BadArgument, "temper");
        catchUp();
        const bool wasEquipped = mInventory.model().isEquipped(target) && held(target) == 1;
        for (const InventoryEntry& e : r->mItems)
            (void)mInventory.model().consume(inventory::kPlayerOwner, e.mItem, e.mCount);
        (void)mInventory.model().consume(inventory::kPlayerOwner, target, 1);
        (void)mInventory.model().grant(inventory::kPlayerOwner, made, 1);
        if (wasEquipped)
            swapEquipped(target, made);
        if (mHooks.mSkillUsed)
            mHooks.mSkillUsed("smithing", static_cast<float>(std::max(1, value)));
        return done(Status::Ok, "temper " + name);
    }

    // ---- enchanting
    // ---------------------------------------------------------------------------------------------------

    Status CraftingService::disenchant(FormId item)
    {
        if (!mTableReady || !mInventory.ready())
            return Status::NotReady;
        if (!hasKind(BenchKind::Enchanting))
            return done(Status::NoBench, "disenchant");
        if (mInventory.table().item(item) == nullptr)
            return done(Status::UnknownItem, "disenchant");
        if (!isGear(item))
            return done(Status::NotGear, "disenchant");
        if (held(item) < 1)
            return done(Status::NotHeld, "disenchant");
        const FormId enchantment = enchantmentOfGear(item);
        if (enchantment == kNullFormId)
            return done(Status::NotEnchanted, "disenchant");
        const Enchantment* e = mTable.enchantment(enchantment);
        if (e == nullptr)
            return done(Status::UnknownEnchantment, "disenchant");
        const FormId learned = e->mBase != kNullFormId ? e->mBase : e->mId;
        if (mState.knowsEnchantment(learned))
            return done(Status::AlreadyKnown, "disenchant");
        mState.learnEnchantment(learned);
        (void)mInventory.model().consume(inventory::kPlayerOwner, item, 1);
        if (mHooks.mSkillUsed)
            mHooks.mSkillUsed("enchanting", static_cast<float>(std::max(1, mInventory.table().item(item)->mValue)));
        return done(Status::Ok, "disenchant " + itemName(item) + " learns " + enchantName(learned));
    }

    Status CraftingService::enchant(FormId item, FormId enchantment, FormId gem)
    {
        if (!mTableReady || !mInventory.ready())
            return Status::NotReady;
        if (!hasKind(BenchKind::Enchanting))
            return done(Status::NoBench, "enchant");
        const inventory::ItemStat* stat = mInventory.table().item(item);
        if (stat == nullptr)
            return done(Status::UnknownItem, "enchant");
        if (!isGear(item))
            return done(Status::NotGear, "enchant");
        if (held(item) < 1)
            return done(Status::NotHeld, "enchant");
        if (enchantmentOfGear(item) != kNullFormId)
            return done(Status::AlreadyEnchanted, "enchant");
        const Enchantment* e = mTable.enchantment(enchantment);
        if (e == nullptr || !mState.knowsEnchantment(enchantment) || e->mEnchantType != 6 || e->mEffects.empty())
            return done(Status::UnknownEnchantment, "enchant");
        const SoulGem* soulGem = mTable.soulGem(gem);
        if (soulGem == nullptr || soulGem->mSoul < 1 || held(gem) < 1)
            return done(Status::NoSoulGem, "enchant");
        const int soul = std::min<int>(soulGem->mSoul, std::max<int>(1, soulGem->mCapacity));
        const float magnitude = enchantMagnitude(e->mEffects.front().mMagnitude, soul, skill("enchanting"), mSettings);
        const Synthetic* old = mState.find(item);
        const FormId base = old != nullptr ? old->mBase : item;
        const std::int32_t permille = old != nullptr ? old->mTemperPermille : 0;
        const std::string name = old != nullptr ? old->mName + " of " + enchantName(enchantment)
                                                : itemName(item) + " of " + enchantName(enchantment);
        const std::int32_t value = stat->mValue + static_cast<std::int32_t>(std::lround(magnitude * 10.f));
        const FormId made
            = mState.addGear(base, name, value, stat->mWeight, stat->mCategory, permille, enchantment, magnitude);
        if (made == kNullFormId)
            return done(Status::BadArgument, "enchant");
        catchUp();
        const bool wasEquipped = mInventory.model().isEquipped(item) && held(item) == 1;
        (void)mInventory.model().consume(inventory::kPlayerOwner, item, 1);
        (void)mInventory.model().consume(inventory::kPlayerOwner, gem, 1);
        (void)mInventory.model().grant(inventory::kPlayerOwner, made, 1);
        if (wasEquipped)
            swapEquipped(item, made);
        if (mHooks.mSkillUsed)
            mHooks.mSkillUsed("enchanting", static_cast<float>(std::max(1, value)));
        return done(Status::Ok, "enchant " + name);
    }

    // ---- the combat read
    // ----------------------------------------------------------------------------------------------

    bool CraftingService::weaponFor(FormId item, const WeaponRecord& base, WeaponRecord& out) const
    {
        const Synthetic* s = mState.find(item);
        if (s == nullptr || s->mKind != SyntheticKind::Gear || s->mCategory != inventory::Category::Weapon)
            return false;
        out = base;
        double damage = static_cast<double>(base.mDamage) * (1.0 + s->mTemperPermille / 1000.0);
        if (s->mEnchantment != kNullFormId)
            if (const Enchantment* e = mTable.enchantment(s->mEnchantment);
                e != nullptr && !e->mEffects.empty() && effectInfo(e->mEffects.front().mEffect).mDamageHealth)
                damage += static_cast<double>(s->mEnchantMagnitude);
        out.mDamage = static_cast<std::uint16_t>(std::clamp<long>(std::lround(damage), 0, 65535));
        log("CRAFT combat reads " + s->mName + ": damage " + std::to_string(base.mDamage) + " -> "
            + std::to_string(out.mDamage));
        return true;
    }

    bool CraftingService::armourFor(FormId item, ArmourPiece& piece) const
    {
        const Synthetic* s = mState.find(item);
        if (s == nullptr || s->mKind != SyntheticKind::Gear || s->mCategory != inventory::Category::Armor)
            return false;
        const float before = piece.mRating;
        piece.mRating *= static_cast<float>(1.0 + s->mTemperPermille / 1000.0);
        log("CRAFT combat reads " + s->mName + ": armour rating " + std::to_string(before) + " -> "
            + std::to_string(piece.mRating));
        return true;
    }

    // ---- bindings
    // -----------------------------------------------------------------------------------------------------

    openeden::sdk::DeclareResult CraftingService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        CraftingService* self = this;
        static const BindingType s1[] = { BindingType::String };
        static const BindingType s3[] = { BindingType::String, BindingType::String, BindingType::String };
        constexpr BindingType kStr = BindingType::String;

        const auto add
            = [&](std::string_view name, std::span<const BindingType> params, bool hasResult, BindingType result,
                  BindingAccess access, Fn::Body body, BindingShape resultShape = BindingShape::Single) {
                  m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
                  openeden::sdk::FunctionDescriptor f;
                  f.name = name;
                  f.params = params;
                  f.hasResult = hasResult;
                  f.result = result;
                  f.access = access;
                  f.handler = m.mHandlers.back().get();
                  f.resultShape = resultShape;
                  m.mFunctions.push_back(f);
              };
        const auto idOf = [](const BindingValue& v, FormId& out) {
            std::uint64_t id = 0;
            if (!inventory::parseId(v.text, id) || id > 0xFFFFFFFFull)
                return false;
            out = static_cast<FormId>(id);
            return true;
        };
        const auto stringFn = [&](std::string_view name, std::function<std::string(const CraftingService&)> get) {
            add(name, {}, true, kStr, BindingAccess::Read,
                [self, &m, get = std::move(get)](std::span<const BindingValue>) {
                    m.mText = get(*self);
                    BindingResult r = ok();
                    r.value = text(m.mText);
                    return r;
                });
        };
        const auto intFn = [&](std::string_view name, std::function<std::int32_t(const CraftingService&)> get) {
            add(name, {}, true, BindingType::Int32, BindingAccess::Read,
                [self, get = std::move(get)](std::span<const BindingValue>) {
                    BindingResult r = ok();
                    r.value = integer(get(*self));
                    return r;
                });
        };
        // one id in, one string out
        const auto itemStringFn
            = [&](std::string_view name, std::function<std::string(const CraftingService&, FormId)> get) {
                  add(name, s1, true, kStr, BindingAccess::Read,
                      [self, &m, idOf, get = std::move(get)](std::span<const BindingValue> a) {
                          FormId id = 0;
                          if (!idOf(a[0], id))
                              return fail(BindingStatus::BadArgument, "not an id");
                          m.mText = get(*self, id);
                          BindingResult r = ok();
                          r.value = text(m.mText);
                          return r;
                      });
              };
        const auto arrayFn
            = [&](std::string_view name, std::function<std::vector<FormId>(const CraftingService&)> get) {
                  add(
                      name, {}, true, kStr, BindingAccess::Read,
                      [self, &m, get = std::move(get)](std::span<const BindingValue>) {
                          std::vector<FormId> ids = get(*self);
                          if (ids.size() > openeden::sdk::kMaxBindingArrayElements)
                              ids.resize(openeden::sdk::kMaxBindingArrayElements);
                          m.mStrings.clear();
                          m.mElements.clear();
                          for (const FormId id : ids)
                              m.mStrings.push_back(inventory::formatId(id));
                          for (const std::string& s : m.mStrings)
                              m.mElements.push_back(text(s));
                          BindingResult r = ok();
                          r.value.type = BindingType::String;
                          r.value.elements = m.mElements.data();
                          r.value.count = m.mElements.size();
                          return r;
                      },
                      BindingShape::Array);
              };
        const auto heldOfKind = [](const CraftingService& s, const std::function<bool(FormId)>& wanted) {
            std::vector<FormId> out;
            for (const inventory::Stack& stack : s.mInventory.model().stacks(inventory::kPlayerOwner))
                if (wanted(stack.mItem))
                    out.push_back(stack.mItem);
            return out;
        };

        stringFn("state", [](const CraftingService& s) { return std::string(stateName(s.session())); });
        stringFn("reason", [](const CraftingService& s) { return s.reason(); });
        stringFn("bench", [](const CraftingService& s) {
            std::vector<std::string> names;
            for (const BenchKind kind : s.mKinds)
                names.emplace_back(benchKindName(kind));
            return join(names, ",");
        });
        stringFn("bench_name", [](const CraftingService& s) { return s.mBenchName; });
        stringFn("last_result", [](const CraftingService& s) { return s.lastResult(); });
        intFn("results", [](const CraftingService& s) { return static_cast<std::int32_t>(s.results() & 0x7FFFFFFF); });
        intFn("revision",
            [](const CraftingService& s) { return static_cast<std::int32_t>(s.mState.revision() & 0x7FFFFFFF); });

        arrayFn("ingredients", [heldOfKind](const CraftingService& s) {
            return heldOfKind(s, [&s](FormId id) { return s.mTable.ingredient(id) != nullptr; });
        });
        itemStringFn("ingredient_effects", [](const CraftingService& s, FormId id) {
            const Ingredient* ing = s.mTable.ingredient(id);
            std::vector<std::string> names;
            if (ing != nullptr)
                for (const EffectEntry& e : ing->mEffects)
                    names.push_back(s.mState.knowsEffect(e.mEffect) ? s.effectName(e.mEffect) : "?");
            return join(names, ", ");
        });
        add("preview", s3, true, kStr, BindingAccess::Read, [self, &m, idOf](std::span<const BindingValue> a) {
            std::vector<const Ingredient*> chosen;
            for (const BindingValue& v : a)
            {
                if (v.text.empty())
                    continue;
                FormId id = 0;
                const Ingredient* ing = idOf(v, id) ? self->mTable.ingredient(id) : nullptr;
                if (ing == nullptr)
                {
                    m.mText.clear();
                    BindingResult r = ok();
                    r.value = text(m.mText);
                    return r;
                }
                chosen.push_back(ing);
            }
            const std::vector<EffectEntry> shared = sharedEffects(chosen);
            std::vector<std::string> names;
            for (const EffectEntry& e : shared)
                names.push_back(self->mState.knowsEffect(e.mEffect) ? self->effectName(e.mEffect) : "?");
            m.mText = chosen.size() < 2 ? std::string()
                : shared.empty()        ? std::string("no shared effects")
                                        : join(names, ", ");
            BindingResult r = ok();
            r.value = text(m.mText);
            return r;
        });

        add(
            "recipes", {}, true, kStr, BindingAccess::Read,
            [self, &m](std::span<const BindingValue>) {
                std::vector<const Recipe*> list = self->visibleRecipes();
                if (list.size() > openeden::sdk::kMaxBindingArrayElements)
                    list.resize(openeden::sdk::kMaxBindingArrayElements);
                m.mStrings.clear();
                m.mElements.clear();
                for (const Recipe* r : list)
                    m.mStrings.push_back(inventory::formatId(r->mId));
                for (const std::string& s : m.mStrings)
                    m.mElements.push_back(text(s));
                BindingResult r = ok();
                r.value.type = kStr;
                r.value.elements = m.mElements.data();
                r.value.count = m.mElements.size();
                return r;
            },
            BindingShape::Array);
        itemStringFn("recipe_name", [](const CraftingService& s, FormId id) {
            const Recipe* r = s.mTable.recipe(id);
            return r != nullptr ? s.itemName(r->mResult) : std::string();
        });
        itemStringFn("recipe_needs", [](const CraftingService& s, FormId id) {
            const Recipe* r = s.mTable.recipe(id);
            std::vector<std::string> parts;
            if (r != nullptr)
                for (const InventoryEntry& e : r->mItems)
                    parts.push_back(s.itemName(e.mItem) + " x" + std::to_string(e.mCount) + " ("
                        + std::to_string(s.held(e.mItem)) + ")");
            return join(parts, ", ");
        });
        add("recipe_ok", s1, true, BindingType::Bool, BindingAccess::Read,
            [self, idOf](std::span<const BindingValue> a) {
                FormId id = 0;
                if (!idOf(a[0], id))
                    return fail(BindingStatus::BadArgument, "not an id");
                const Recipe* r = self->mTable.recipe(id);
                BindingResult out = ok();
                out.value = boolean(r != nullptr && self->recipeOk(*r));
                return out;
            });
        add("recipe_temper", s1, true, BindingType::Bool, BindingAccess::Read,
            [self, idOf](std::span<const BindingValue> a) {
                FormId id = 0;
                if (!idOf(a[0], id))
                    return fail(BindingStatus::BadArgument, "not an id");
                const Recipe* r = self->mTable.recipe(id);
                BindingResult out = ok();
                out.value = boolean(r != nullptr && isTemperBench(self->mTable.benchOfKeyword(r->mBench)));
                return out;
            });

        arrayFn("gear",
            [heldOfKind](const CraftingService& s) { return heldOfKind(s, [&s](FormId id) { return s.isGear(id); }); });
        itemStringFn("gear_enchant", [](const CraftingService& s, FormId id) {
            const FormId e = s.enchantmentOfGear(id);
            if (e == kNullFormId)
                return std::string();
            const Enchantment* record = s.mTable.enchantment(e);
            return s.enchantName(record != nullptr && record->mBase != kNullFormId ? record->mBase : e);
        });
        arrayFn("enchantments", [](const CraftingService& s) {
            return std::vector<FormId>(s.mState.knownEnchantments().begin(), s.mState.knownEnchantments().end());
        });
        itemStringFn("enchant_name", [](const CraftingService& s, FormId id) { return s.enchantName(id); });
        arrayFn("gems", [heldOfKind](const CraftingService& s) {
            return heldOfKind(s, [&s](FormId id) {
                const SoulGem* g = s.mTable.soulGem(id);
                return g != nullptr && g->mSoul >= 1;
            });
        });
        add("gem_size", s1, true, BindingType::Int32, BindingAccess::Read,
            [self, idOf](std::span<const BindingValue> a) {
                FormId id = 0;
                if (!idOf(a[0], id))
                    return fail(BindingStatus::BadArgument, "not an id");
                const SoulGem* g = self->mTable.soulGem(id);
                BindingResult out = ok();
                out.value = integer(g != nullptr ? g->mSoul : 0);
                return out;
            });
        add("enchant_preview", s3, true, kStr, BindingAccess::Read, [self, &m, idOf](std::span<const BindingValue> a) {
            FormId item = 0, e = 0, gem = 0;
            m.mText.clear();
            if (idOf(a[0], item) && idOf(a[1], e) && idOf(a[2], gem))
            {
                const Enchantment* record = self->mTable.enchantment(e);
                const SoulGem* g = self->mTable.soulGem(gem);
                if (record != nullptr && !record->mEffects.empty() && g != nullptr)
                {
                    const float magnitude = enchantMagnitude(record->mEffects.front().mMagnitude,
                        std::min<int>(g->mSoul, std::max<int>(1, g->mCapacity)), self->skill("enchanting"),
                        self->mSettings);
                    char buffer[32];
                    std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(magnitude));
                    m.mText = self->enchantName(e) + " " + buffer;
                }
            }
            BindingResult r = ok();
            r.value = text(m.mText);
            return r;
        });

        // Task 0213: the global script asks before it opens a window (a Mutate call cannot answer in the same handler).
        add("is_bench", s1, true, kStr, BindingAccess::Read, [self, &m, idOf](std::span<const BindingValue> a) {
            FormId furniture = 0;
            m.mText = idOf(a[0], furniture) ? self->benchAnswer(furniture) : std::string("no");
            BindingResult r = ok();
            r.value = text(m.mText);
            return r;
        });

        // ---- Mutate: no result; a refusal is logged and shown through last_result
        add("open", s3, false, BindingType::Bool, BindingAccess::Mutate, [self, idOf](std::span<const BindingValue> a) {
            std::uint64_t placement = 0;
            FormId base = 0;
            if (!inventory::parseId(a[0].text, placement) || !idOf(a[1], base))
            {
                self->log("CRAFT open refused: bad id");
                return ok();
            }
            self->openBench(placement, base, std::string(a[2].text));
            return ok();
        });
        add("open_kind", s1, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue> a) {
            if (!self->mTestBench)
            {
                self->log("CRAFT open_kind refused: the test bench setting is off");
                return ok();
            }
            self->openKind(benchKindFromName(a[0].text));
            return ok();
        });
        add("close", {}, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue>) {
            self->close();
            return ok();
        });
        add("brew", s3, false, BindingType::Bool, BindingAccess::Mutate, [self, idOf](std::span<const BindingValue> a) {
            std::vector<FormId> ids;
            for (const BindingValue& v : a)
            {
                if (v.text.empty())
                    continue;
                FormId id = 0;
                if (!idOf(v, id))
                {
                    (void)self->done(Status::BadArgument, "brew");
                    return ok();
                }
                ids.push_back(id);
            }
            (void)self->brew(ids);
            return ok();
        });
        const auto oneId = [&](std::string_view name, std::function<Status(CraftingService&, FormId)> run) {
            add(name, s1, false, BindingType::Bool, BindingAccess::Mutate,
                [self, idOf, run = std::move(run), label = std::string(name)](std::span<const BindingValue> a) {
                    FormId id = 0;
                    if (!idOf(a[0], id))
                        (void)self->done(Status::BadArgument, label);
                    else
                        (void)run(*self, id);
                    return ok();
                });
        };
        oneId("eat", [](CraftingService& s, FormId id) { return s.eat(id); });
        oneId("craft", [](CraftingService& s, FormId id) { return s.craft(id); });
        oneId("temper", [](CraftingService& s, FormId id) { return s.temper(id); });
        oneId("disenchant", [](CraftingService& s, FormId id) { return s.disenchant(id); });
        add("enchant", s3, false, BindingType::Bool, BindingAccess::Mutate,
            [self, idOf](std::span<const BindingValue> a) {
                FormId item = 0, e = 0, gem = 0;
                if (!idOf(a[0], item) || !idOf(a[1], e) || !idOf(a[2], gem))
                    (void)self->done(Status::BadArgument, "enchant");
                else
                    (void)self->enchant(item, e, gem);
                return ok();
            });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.crafting";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
