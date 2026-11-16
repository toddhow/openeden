#include "game_skyrimse/crafting.hpp"

#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/perks.hpp"
#include "game_skyrimse/progression.hpp"
#include "game_skyrimse/survey.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

// Records, rules, synthetic items and the save chunk of crafting: see the header (task 0199).

namespace game_skyrimse::crafting
{
    namespace
    {
        std::uint32_t le32(const std::byte* p)
        {
            std::uint32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }

        std::uint16_t le16(const std::byte* p)
        {
            std::uint16_t v;
            std::memcpy(&v, p, 2);
            return v;
        }

        float lef(const std::byte* p)
        {
            float v;
            std::memcpy(&v, p, 4);
            return v;
        }

        Content::Error makeError(const std::string& file, std::uint64_t offset, std::uint32_t type, std::string message)
        {
            Content::Error error;
            error.mFile = file;
            error.mOffset = offset;
            error.mRecordType = type;
            error.mMessage = std::move(message);
            return error;
        }

        constexpr std::size_t kMaxEffectsPerRecord = 16;
        constexpr std::size_t kMaxFurnitureKeywords = 64;

        struct BenchName
        {
            BenchKind mKind;
            std::string_view mName;
            std::string_view mKeyword;
        };
        constexpr BenchName kBenches[] = {
            { BenchKind::Alchemy, "alchemy", "WICraftingAlchemy" },
            { BenchKind::Enchanting, "enchanting", "WICraftingEnchanting" },
            { BenchKind::Forge, "forge", "CraftingSmithingForge" },
            { BenchKind::Skyforge, "skyforge", "CraftingSmithingSkyforge" },
            { BenchKind::SharpeningWheel, "sharpening", "CraftingSmithingSharpeningWheel" },
            { BenchKind::ArmorTable, "armor", "CraftingSmithingArmorTable" },
            { BenchKind::Smelter, "smelter", "CraftingSmelter" },
            { BenchKind::Cookpot, "cook", "CraftingCookpot" },
            { BenchKind::TanningRack, "tanning", "CraftingTanningRack" },
        };

        class Parser final : public Content::Visitor
        {
        public:
            Parser(const Content::Plugin& plugin, const FormIdResolver& resolver, const CraftingLimits& limits,
                CraftingRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                const std::uint32_t type = record.mType;
                const bool wanted = type == Content::fourCC("INGR") || type == Content::fourCC("COBJ")
                    || type == Content::fourCC("ENCH") || type == Content::fourCC("SLGM")
                    || type == Content::fourCC("FURN") || type == Content::fourCC("KYWD")
                    || type == Content::fourCC("WEAP") || type == Content::fourCC("ARMO")
                    || type == Content::fourCC("MGEF");
                if (!wanted || (record.mFlags & RecordFlags::Deleted) != 0)
                    return true;
                std::span<const std::byte> bytes = record.mData;
                if (record.isCompressed())
                {
                    if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record, "the record inflates past the limit");
                        return true;
                    }
                    Content::Error error;
                    if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                    {
                        addError(std::move(error));
                        return true;
                    }
                    bytes = mInflated;
                }
                else if (record.mDataSize > mLimits.mMaxRecordBytes)
                {
                    fail(record, "the record is over the size limit");
                    return true;
                }
                const FormId id = resolved(record.mFormId);
                if (id == kNullFormId || id == kUnresolvedFormId)
                    return true;
                bool ok = true;
                if (type == Content::fourCC("INGR"))
                    ok = ingredient(id, bytes);
                else if (type == Content::fourCC("COBJ"))
                    ok = recipe(id, bytes);
                else if (type == Content::fourCC("ENCH"))
                    ok = enchantment(id, bytes);
                else if (type == Content::fourCC("SLGM"))
                    ok = soulGem(id, bytes);
                else if (type == Content::fourCC("FURN"))
                    ok = furniture(id, bytes);
                else if (type == Content::fourCC("KYWD"))
                    ok = keyword(id, bytes);
                else if (type == Content::fourCC("MGEF"))
                    ok = effectName(id, bytes);
                else
                    ok = gear(id, bytes);
                if (!ok)
                    fail(record, "a subrecord is cut short or claims more bytes than the record holds");
                return true;
            }

        private:
            bool ingredient(FormId id, std::span<const std::byte> bytes)
            {
                Ingredient ing;
                ing.mId = id;
                bool filled = true;
                const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (sub.mType == Content::fourCC("EFID"))
                    {
                        if (sub.mData.size() != 4)
                            ++mOut.mOddSubrecords;
                        else if (ing.mEffects.size() < kMaxEffectsPerRecord)
                        {
                            EffectEntry e;
                            e.mEffect = resolved(le32(sub.mData.data()));
                            ing.mEffects.push_back(e);
                            filled = false;
                        }
                    }
                    else if (sub.mType == Content::fourCC("EFIT"))
                    {
                        if (sub.mData.size() != 12 || ing.mEffects.empty() || filled)
                            ++mOut.mOddSubrecords;
                        else
                        {
                            EffectEntry& e = ing.mEffects.back();
                            e.mMagnitude = lef(sub.mData.data());
                            e.mArea = le32(sub.mData.data() + 4);
                            e.mDuration = le32(sub.mData.data() + 8);
                            filled = true;
                        }
                    }
                });
                if (ok)
                    mOut.mIngredients.push_back(std::move(ing));
                return ok;
            }

            bool recipe(FormId id, std::span<const std::byte> bytes)
            {
                Recipe r;
                r.mId = id;
                bool haveResult = false, haveBench = false, tooMany = false, unresolved = false;
                const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    const std::uint32_t t = sub.mType;
                    if (t == Content::fourCC("CNAM"))
                    {
                        if (sub.mData.size() != 4)
                            ++mOut.mOddSubrecords;
                        else
                        {
                            r.mResult = resolved(le32(sub.mData.data()));
                            haveResult = r.mResult != kNullFormId && r.mResult != kUnresolvedFormId;
                        }
                    }
                    else if (t == Content::fourCC("BNAM"))
                    {
                        if (sub.mData.size() != 4)
                            ++mOut.mOddSubrecords;
                        else
                        {
                            r.mBench = resolved(le32(sub.mData.data()));
                            haveBench = r.mBench != kNullFormId && r.mBench != kUnresolvedFormId;
                        }
                    }
                    else if (t == Content::fourCC("NAM1"))
                    {
                        if (sub.mData.size() != 2)
                            ++mOut.mOddSubrecords;
                        else
                            r.mCount = std::max<std::uint16_t>(1, le16(sub.mData.data()));
                    }
                    else if (t == Content::fourCC("CNTO"))
                    {
                        if (sub.mData.size() != 8)
                            ++mOut.mOddSubrecords;
                        else if (r.mItems.size() >= mLimits.mMaxItems)
                            tooMany = true;
                        else
                        {
                            InventoryEntry e;
                            e.mItem = resolved(le32(sub.mData.data()));
                            e.mCount = static_cast<std::int32_t>(le32(sub.mData.data() + 4));
                            if (e.mItem == kNullFormId || e.mItem == kUnresolvedFormId || e.mCount < 1)
                                unresolved = true;
                            r.mItems.push_back(e);
                        }
                    }
                    else if (t == Content::fourCC("CTDA"))
                    {
                        ++r.mConditions;
                        // task 0206: kept decoded, the form parameters of the functions that have one resolved
                        survey::Ctda c;
                        if (r.mConditionList.size() < 32 && survey::decodeCtda(sub.mData, c))
                        {
                            if ((c.mFunction == 448 || c.mFunction == 74 || c.mFunction == 58
                                    || c.mFunction == 59)
                                && c.mParam1 != 0)
                                c.mParam1 = resolved(c.mParam1);
                            r.mConditionList.push_back(std::move(c));
                        }
                    }
                });
                if (!ok || tooMany)
                    return false;
                if (haveResult && haveBench && !unresolved)
                    mOut.mRecipes.push_back(std::move(r));
                return true;
            }

            bool enchantment(FormId id, std::span<const std::byte> bytes)
            {
                Enchantment e;
                e.mId = id;
                bool filled = true;
                const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    const std::uint32_t t = sub.mType;
                    if (t == Content::fourCC("EDID"))
                    {
                        std::size_t n = sub.mData.size();
                        while (n > 0 && sub.mData[n - 1] == std::byte{ 0 })
                            --n;
                        e.mEditorId.assign(reinterpret_cast<const char*>(sub.mData.data()), n);
                    }
                    else if (t == Content::fourCC("ENIT"))
                    {
                        if (sub.mData.size() != 36 && sub.mData.size() != 32)
                            ++mOut.mOddSubrecords;
                        else
                        {
                            e.mCastType = le32(sub.mData.data() + 8);
                            e.mDelivery = le32(sub.mData.data() + 16);
                            e.mEnchantType = le32(sub.mData.data() + 20);
                            e.mBase = resolved(le32(sub.mData.data() + 28));
                        }
                    }
                    else if (t == Content::fourCC("EFID"))
                    {
                        if (sub.mData.size() != 4)
                            ++mOut.mOddSubrecords;
                        else if (e.mEffects.size() < kMaxEffectsPerRecord)
                        {
                            EffectEntry f;
                            f.mEffect = resolved(le32(sub.mData.data()));
                            e.mEffects.push_back(f);
                            filled = false;
                        }
                    }
                    else if (t == Content::fourCC("EFIT"))
                    {
                        if (sub.mData.size() != 12 || e.mEffects.empty() || filled)
                            ++mOut.mOddSubrecords;
                        else
                        {
                            EffectEntry& f = e.mEffects.back();
                            f.mMagnitude = lef(sub.mData.data());
                            f.mArea = le32(sub.mData.data() + 4);
                            f.mDuration = le32(sub.mData.data() + 8);
                            filled = true;
                        }
                    }
                });
                if (e.mBase == kUnresolvedFormId)
                    e.mBase = kNullFormId;
                if (ok)
                    mOut.mEnchantments.push_back(std::move(e));
                return ok;
            }

            bool soulGem(FormId id, std::span<const std::byte> bytes)
            {
                SoulGem g;
                g.mId = id;
                const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (sub.mType == Content::fourCC("SLCP") || sub.mType == Content::fourCC("SOUL"))
                    {
                        if (sub.mData.size() != 1)
                            ++mOut.mOddSubrecords;
                        else if (sub.mType == Content::fourCC("SLCP"))
                            g.mCapacity = static_cast<std::uint8_t>(sub.mData[0]);
                        else
                            g.mSoul = static_cast<std::uint8_t>(sub.mData[0]);
                    }
                });
                if (ok)
                    mOut.mSoulGems.push_back(g);
                return ok;
            }

            bool furniture(FormId id, std::span<const std::byte> bytes)
            {
                Furniture f;
                f.mId = id;
                const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (sub.mType != Content::fourCC("KWDA"))
                        return;
                    if (sub.mData.size() % 4 != 0)
                        ++mOut.mOddSubrecords;
                    else
                        for (std::size_t i = 0; i + 4 <= sub.mData.size() && f.mKeywords.size() < kMaxFurnitureKeywords;
                            i += 4)
                            f.mKeywords.push_back(resolved(le32(sub.mData.data() + i)));
                });
                if (ok && !f.mKeywords.empty())
                    mOut.mFurniture.push_back(std::move(f));
                return ok;
            }

            bool keyword(FormId id, std::span<const std::byte> bytes)
            {
                std::string name;
                const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (sub.mType != Content::fourCC("EDID"))
                        return;
                    std::size_t n = sub.mData.size();
                    while (n > 0 && sub.mData[n - 1] == std::byte{ 0 })
                        --n;
                    name.assign(reinterpret_cast<const char*>(sub.mData.data()), n);
                });
                if (ok)
                    for (const BenchName& bench : kBenches)
                        if (bench.mKeyword == name)
                            mOut.mKeywords.emplace_back(name, id);
                return ok;
            }

            bool effectName(FormId id, std::span<const std::byte> bytes)
            {
                std::string name;
                const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (sub.mType != Content::fourCC("EDID"))
                        return;
                    std::size_t n = sub.mData.size();
                    while (n > 0 && sub.mData[n - 1] == std::byte{ 0 })
                        --n;
                    name.assign(reinterpret_cast<const char*>(sub.mData.data()), n);
                });
                if (ok && !name.empty())
                    mOut.mEffectNames.emplace_back(id, std::move(name));
                return ok;
            }

            bool gear(FormId id, std::span<const std::byte> bytes)
            {
                FormId enchantment = kNullFormId;
                const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                    if (sub.mType != Content::fourCC("EITM"))
                        return;
                    if (sub.mData.size() != 4)
                        ++mOut.mOddSubrecords;
                    else
                        enchantment = resolved(le32(sub.mData.data()));
                });
                if (ok && enchantment != kNullFormId && enchantment != kUnresolvedFormId)
                    mOut.mGear.push_back({ id, enchantment });
                return ok;
            }

            FormId resolved(std::uint32_t stored)
            {
                if (stored == 0)
                    return kNullFormId;
                const std::optional<FormId> id = mResolver.resolve(stored);
                return id ? *id : kUnresolvedFormId;
            }

            void addError(Content::Error error)
            {
                ++mOut.mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                addError(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)));
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const CraftingLimits& mLimits;
            CraftingRecords& mOut;
            std::vector<std::byte> mInflated;
        };
    }

    std::string_view benchKindName(BenchKind kind)
    {
        for (const BenchName& b : kBenches)
            if (b.mKind == kind)
                return b.mName;
        return {};
    }

    BenchKind benchKindFromName(std::string_view name)
    {
        for (const BenchName& b : kBenches)
            if (b.mName == name)
                return b.mKind;
        return BenchKind::None;
    }

    std::string_view benchKeywordName(BenchKind kind)
    {
        for (const BenchName& b : kBenches)
            if (b.mKind == kind)
                return b.mKeyword;
        return {};
    }

    bool isTemperBench(BenchKind kind)
    {
        return kind == BenchKind::SharpeningWheel || kind == BenchKind::ArmorTable;
    }

    bool parseCraftingRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const CraftingLimits& limits, CraftingRecords& out)
    {
        out = CraftingRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        Parser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    // ---- the table
    // ---------------------------------------------------------------------------------------------------

    void CraftingTable::add(CraftingRecords&& records)
    {
        for (Ingredient& i : records.mIngredients)
            mIngredients[i.mId] = std::move(i);
        for (Recipe& r : records.mRecipes)
            mRecipes[r.mId] = std::move(r);
        for (Enchantment& e : records.mEnchantments)
            mEnchantments[e.mId] = std::move(e);
        for (const GearEnchant& g : records.mGear)
            mGear[g.mItem] = g.mEnchantment;
        for (const SoulGem& g : records.mSoulGems)
            mSoulGems[g.mId] = g;
        for (Furniture& f : records.mFurniture)
            mFurniture[f.mId] = std::move(f.mKeywords);
        for (auto& [name, id] : records.mKeywords)
            mKeywords[name] = id;
        for (auto& [id, name] : records.mEffectNames)
            mEffectNames[id] = std::move(name);
    }

    void CraftingTable::finish()
    {
        mRecipeOrder.clear();
        mRecipeOrder.reserve(mRecipes.size());
        for (const auto& [id, recipe] : mRecipes)
            mRecipeOrder.push_back(id);
        std::sort(mRecipeOrder.begin(), mRecipeOrder.end());
    }

    const Ingredient* CraftingTable::ingredient(FormId id) const
    {
        const auto it = mIngredients.find(id);
        return it == mIngredients.end() ? nullptr : &it->second;
    }

    const Recipe* CraftingTable::recipe(FormId id) const
    {
        const auto it = mRecipes.find(id);
        return it == mRecipes.end() ? nullptr : &it->second;
    }

    const Enchantment* CraftingTable::enchantment(FormId id) const
    {
        const auto it = mEnchantments.find(id);
        return it == mEnchantments.end() ? nullptr : &it->second;
    }

    FormId CraftingTable::enchantmentOf(FormId item) const
    {
        const auto it = mGear.find(item);
        return it == mGear.end() ? kNullFormId : it->second;
    }

    const SoulGem* CraftingTable::soulGem(FormId id) const
    {
        const auto it = mSoulGems.find(id);
        return it == mSoulGems.end() ? nullptr : &it->second;
    }

    FormId CraftingTable::keyword(std::string_view editorId) const
    {
        const auto it = mKeywords.find(std::string(editorId));
        return it == mKeywords.end() ? kNullFormId : it->second;
    }

    std::string CraftingTable::effectName(FormId effect) const
    {
        const auto it = mEffectNames.find(effect);
        return it == mEffectNames.end() ? std::string() : it->second;
    }

    BenchKind CraftingTable::benchOfKeyword(FormId keywordId) const
    {
        if (keywordId == kNullFormId)
            return BenchKind::None;
        for (const BenchName& b : kBenches)
        {
            const auto it = mKeywords.find(std::string(b.mKeyword));
            if (it != mKeywords.end() && it->second == keywordId)
                return b.mKind;
        }
        return BenchKind::None;
    }

    std::vector<BenchKind> CraftingTable::benchesOf(FormId furniture) const
    {
        std::vector<BenchKind> out;
        const auto it = mFurniture.find(furniture);
        if (it == mFurniture.end())
            return out;
        for (const FormId k : it->second)
        {
            const BenchKind kind = benchOfKeyword(k);
            if (kind != BenchKind::None && std::find(out.begin(), out.end(), kind) == out.end())
                out.push_back(kind);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::size_t skillSlotOfCraftingName(std::string_view skill)
    {
        if (skill == "alchemy")
            return progression::Slot::Alchemy;
        if (skill == "smithing")
            return progression::Slot::Smithing;
        if (skill == "enchanting")
            return progression::Slot::Enchanting;
        return progression::kSkillCount;
    }

    namespace
    {
        constexpr std::uint16_t kFnGetBaseActorValue = 277;
        constexpr std::uint16_t kFnHasPerk = 448;
        constexpr std::uint16_t kFnTemperingItemIsEnchanted = 659; // RECALLED-VERIFY name; the data's use is certain

        // The answer of fn 659 for the list being evaluated. Set around one evaluation, on the calling thread (the
        // evaluator's function table is plain function pointers: no object to carry it).
        thread_local bool tItemEnchanted = false;

        float temperingItemEnchanted(const survey::Ctda&, conditions::Subject, const conditions::ConditionWorld&)
        {
            return tItemEnchanted ? 1.f : 0.f;
        }
    }

    bool recipeConditionsPerkOnly(std::span<const survey::Ctda> list)
    {
        for (const survey::Ctda& c : list)
            if (c.mFunction != kFnHasPerk && c.mFunction != kFnTemperingItemIsEnchanted
                && c.mFunction != kFnGetBaseActorValue)
                return false;
        return true;
    }

    bool recipeConditionsHold(std::span<const survey::Ctda> list, const std::vector<FormId>& perks, std::uint16_t level,
        const std::vector<std::pair<std::uint32_t, float>>& values, bool itemEnchanted)
    {
        if (list.empty())
            return true;
        PerkParticipant player;
        player.mKind = PerkParticipant::Kind::Actor;
        player.mLevel = level;
        player.mPerks = &perks;
        player.mValues = &values;
        PerkQuery query;
        query.set(PerkRole::Owner, &player);
        PerkConditionWorld world;
        world.setQuery(&query);
        world.setCurrentRole(PerkRole::Owner);
        static constexpr std::array<conditions::ExtraFunction, 1> kExtra = { { { kFnTemperingItemIsEnchanted,
            &temperingItemEnchanted } } };
        conditions::Evaluator evaluator;
        evaluator.setExtraFunctions(kExtra);
        tItemEnchanted = itemEnchanted;
        const bool holds = evaluator.evaluate(list, world);
        tItemEnchanted = false;
        return holds;
    }

    std::vector<const Recipe*> CraftingTable::recipesFor(BenchKind kind, bool ignoreConditions) const
    {
        std::vector<const Recipe*> out;
        if (kind == BenchKind::None)
            return out;
        for (const FormId id : mRecipeOrder)
        {
            const Recipe& r = mRecipes.at(id);
            // every tempering recipe of the game carries two conditions (a perk and a tempering test), so conditions
            // hide only the recipes of the other benches
            if (benchOfKeyword(r.mBench) != kind || (!ignoreConditions && r.mConditions > 0 && !isTemperBench(kind)))
                continue;
            out.push_back(&r);
        }
        return out;
    }

    // ---- rules
    // -------------------------------------------------------------------------------------------------------

    namespace
    {
        float clampSkill(float skill)
        {
            if (!std::isfinite(skill))
                return 0.f;
            return std::clamp(skill, 0.f, 100.f);
        }
    }

    float alchemyStrength(float skill, const CraftingSettings& settings)
    {
        return 1.f + (settings.mAlchemySkillFactor - 1.f) * clampSkill(skill) / 100.f;
    }

    std::vector<EffectEntry> sharedEffects(std::span<const Ingredient* const> ingredients)
    {
        std::vector<EffectEntry> out;
        if (ingredients.size() < 2 || ingredients.size() > 3)
            return out;
        for (std::size_t i = 0; i < ingredients.size(); ++i)
            for (std::size_t j = i + 1; j < ingredients.size(); ++j)
                if (ingredients[i] == nullptr || ingredients[j] == nullptr
                    || ingredients[i]->mId == ingredients[j]->mId)
                    return out;
        std::vector<FormId> seen;
        for (const Ingredient* ingredient : ingredients)
            for (const EffectEntry& e : ingredient->mEffects)
            {
                if (std::find(seen.begin(), seen.end(), e.mEffect) != seen.end())
                    continue;
                seen.push_back(e.mEffect);
                int count = 0;
                EffectEntry best = e;
                for (const Ingredient* other : ingredients)
                    for (const EffectEntry& f : other->mEffects)
                        if (f.mEffect == e.mEffect)
                        {
                            ++count;
                            best.mMagnitude = std::max(best.mMagnitude, f.mMagnitude);
                            best.mDuration = std::max(best.mDuration, f.mDuration);
                            break; // an ingredient counts once
                        }
                if (count >= 2)
                    out.push_back(best);
            }
        return out;
    }

    std::vector<EffectEntry> scalePotionEffects(const std::vector<EffectEntry>& effects, float strength)
    {
        std::vector<EffectEntry> out = effects;
        for (EffectEntry& e : out)
            e.mMagnitude *= strength;
        return out;
    }

    std::int32_t potionValue(const std::vector<EffectEntry>& effects,
        const std::function<float(FormId effect)>& baseCost, const CraftingSettings& settings)
    {
        if (effects.empty())
            return 0;
        double total = 0.0;
        for (const EffectEntry& e : effects)
        {
            const double cost = baseCost ? std::max(0.f, baseCost(e.mEffect)) : 0.f;
            const double magnitude = std::max(1.0, static_cast<double>(e.mMagnitude));
            const double duration = std::max(10.0, static_cast<double>(e.mDuration));
            total += cost * std::pow(magnitude * duration / 10.0, 1.1);
        }
        const double clipped = std::isfinite(total) ? std::min(total, static_cast<double>(settings.mMaxValue)) : 0.0;
        return std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(clipped)));
    }

    float temperFraction(float skill, const CraftingSettings& settings)
    {
        return std::max(0.f, settings.mTemperPerSkill * clampSkill(skill));
    }

    std::string_view temperGrade(float fraction)
    {
        if (fraction >= 0.5f)
            return "Legendary";
        if (fraction >= 0.35f)
            return "Epic";
        if (fraction >= 0.25f)
            return "Flawless";
        if (fraction >= 0.15f)
            return "Exquisite";
        if (fraction >= 0.08f)
            return "Superior";
        if (fraction >= 0.04f)
            return "Fine";
        return "";
    }

    std::int32_t toPermille(float fraction)
    {
        if (!std::isfinite(fraction))
            return 0;
        return static_cast<std::int32_t>(std::lround(std::clamp(fraction, 0.f, 10.f) * 1000.f));
    }

    float enchantMagnitude(float baseMagnitude, int soulSize, float skill, const CraftingSettings& settings)
    {
        if (soulSize < 1 || soulSize > 5)
            return 0.f;
        return baseMagnitude * settings.mSoulMultiplier[static_cast<std::size_t>(soulSize)]
            * (1.f + (settings.mEnchantSkillFactor - 1.f) * clampSkill(skill) / 100.f);
    }

    std::string prettyEditorId(std::string_view editorId)
    {
        std::string_view rest = editorId;
        const auto strip = [&](std::string_view prefix) {
            if (rest.size() > prefix.size() && rest.substr(0, prefix.size()) == prefix)
            {
                rest.remove_prefix(prefix.size());
                return true;
            }
            return false;
        };
        if (strip("Ench"))
        {
            if (!strip("Weapon"))
                strip("Armor");
        }
        else
            strip("Alch");
        if (rest.size() > 4 && rest.substr(rest.size() - 4) == "Base")
            rest.remove_suffix(4);
        std::string out;
        for (std::size_t i = 0; i < rest.size(); ++i)
        {
            const char c = rest[i];
            if (i > 0)
            {
                const char p = rest[i - 1];
                const bool upper = c >= 'A' && c <= 'Z';
                const bool digit = c >= '0' && c <= '9';
                const bool prevLower = (p >= 'a' && p <= 'z');
                const bool prevDigit = p >= '0' && p <= '9';
                if ((upper && (prevLower || prevDigit)) || (digit && !prevDigit))
                    out.push_back(' ');
            }
            out.push_back(c);
        }
        return out;
    }

    // ---- synthetic items and the state
    // ---------------------------------------------------------------------------------

    namespace
    {
        bool sameDefinition(const Synthetic& a, const Synthetic& b)
        {
            Synthetic x = a;
            x.mId = b.mId;
            return x == b;
        }
    }

    FormId CraftingState::addPotion(
        std::string name, std::int32_t value, float weight, bool poison, std::vector<EffectEntry> effects)
    {
        if (name.empty() || name.size() > kMaxName || effects.empty() || effects.size() > kMaxEffects)
            return kNullFormId;
        Synthetic s;
        s.mKind = SyntheticKind::Potion;
        s.mName = std::move(name);
        s.mValue = value;
        s.mWeight = weight;
        s.mCategory = inventory::Category::Potion;
        s.mPoison = poison;
        s.mEffects = std::move(effects);
        for (const Synthetic& other : mItems)
            if (sameDefinition(s, other))
                return other.mId;
        if (mItems.size() >= kMaxSynthetic)
            return kNullFormId;
        s.mId = kSyntheticPrefix | static_cast<FormId>(mItems.size() + 1);
        mItems.push_back(std::move(s));
        ++mRevision;
        return mItems.back().mId;
    }

    FormId CraftingState::addGear(FormId base, std::string name, std::int32_t value, float weight,
        inventory::Category category, std::int32_t temperPermille, FormId enchantment, float enchantMagnitude)
    {
        if (base == kNullFormId || name.empty() || name.size() > kMaxName
            || (category != inventory::Category::Weapon && category != inventory::Category::Armor) || temperPermille < 0
            || temperPermille > 10000 || !std::isfinite(enchantMagnitude) || enchantMagnitude < 0.f)
            return kNullFormId;
        Synthetic s;
        s.mKind = SyntheticKind::Gear;
        s.mName = std::move(name);
        s.mValue = value;
        s.mWeight = weight;
        s.mCategory = category;
        s.mBase = base;
        s.mTemperPermille = temperPermille;
        s.mEnchantment = enchantment;
        s.mEnchantMagnitude = enchantMagnitude;
        for (const Synthetic& other : mItems)
            if (sameDefinition(s, other))
                return other.mId;
        if (mItems.size() >= kMaxSynthetic)
            return kNullFormId;
        s.mId = kSyntheticPrefix | static_cast<FormId>(mItems.size() + 1);
        mItems.push_back(std::move(s));
        ++mRevision;
        return mItems.back().mId;
    }

    const Synthetic* CraftingState::find(FormId id) const
    {
        if (!isSyntheticId(id))
            return nullptr;
        const std::size_t index = (id & 0x00FFFFFFu);
        if (index == 0 || index > mItems.size())
            return nullptr;
        return &mItems[index - 1];
    }

    bool CraftingState::learnEffect(FormId effect)
    {
        if (effect == kNullFormId || mEffects.size() >= kMaxKnown || !mEffects.insert(effect).second)
            return false;
        ++mRevision;
        return true;
    }

    bool CraftingState::learnEnchantment(FormId enchantment)
    {
        if (enchantment == kNullFormId || mEnchantments.size() >= kMaxKnown
            || !mEnchantments.insert(enchantment).second)
            return false;
        ++mRevision;
        return true;
    }

    void CraftingState::clear()
    {
        mItems.clear();
        mEffects.clear();
        mEnchantments.clear();
        ++mRevision;
    }

    void CraftingState::write(openeden::sdk::ChunkWriter& writer) const
    {
        writer.writeValue(static_cast<std::uint32_t>(mItems.size()));
        for (const Synthetic& s : mItems)
        {
            writer.writeValue(s.mId);
            writer.writeValue(static_cast<std::uint8_t>(s.mKind));
            writer.writeString(s.mName);
            writer.writeValue(s.mValue);
            writer.writeValue(s.mWeight);
            writer.writeValue(static_cast<std::uint8_t>(s.mCategory));
            writer.writeValue(static_cast<std::uint8_t>(s.mPoison ? 1 : 0));
            writer.writeValue(static_cast<std::uint8_t>(s.mEffects.size()));
            for (const EffectEntry& e : s.mEffects)
            {
                writer.writeValue(e.mEffect);
                writer.writeValue(e.mMagnitude);
                writer.writeValue(e.mArea);
                writer.writeValue(e.mDuration);
            }
            writer.writeValue(s.mBase);
            writer.writeValue(s.mTemperPermille);
            writer.writeValue(s.mEnchantment);
            writer.writeValue(s.mEnchantMagnitude);
        }
        writer.writeValue(static_cast<std::uint32_t>(mEffects.size()));
        for (const FormId id : mEffects)
            writer.writeValue(id);
        writer.writeValue(static_cast<std::uint32_t>(mEnchantments.size()));
        for (const FormId id : mEnchantments)
            writer.writeValue(id);
    }

    bool CraftingState::read(openeden::sdk::ChunkReader& reader, std::string& error)
    {
        std::vector<Synthetic> items;
        std::set<FormId> effects, enchantments;
        const auto bad = [&](const char* why) {
            error = why;
            return false;
        };
        std::uint32_t count = 0;
        if (!reader.readValue(count) || count > kMaxSynthetic)
            return bad("the item count is damaged");
        for (std::uint32_t i = 0; i < count; ++i)
        {
            Synthetic s;
            std::uint8_t kind = 0, category = 0, poison = 0, effectCount = 0;
            if (!reader.readValue(s.mId) || !reader.readValue(kind) || !reader.readString(s.mName)
                || !reader.readValue(s.mValue) || !reader.readValue(s.mWeight) || !reader.readValue(category)
                || !reader.readValue(poison) || !reader.readValue(effectCount))
                return bad("an item is cut short");
            if (s.mId != (kSyntheticPrefix | (i + 1)))
                return bad("an item id is damaged");
            if (kind != static_cast<std::uint8_t>(SyntheticKind::Potion)
                && kind != static_cast<std::uint8_t>(SyntheticKind::Gear))
                return bad("an item kind is damaged");
            s.mKind = static_cast<SyntheticKind>(kind);
            if (s.mName.empty() || s.mName.size() > kMaxName || poison > 1 || effectCount > kMaxEffects
                || !std::isfinite(s.mWeight) || category > static_cast<std::uint8_t>(inventory::Category::Misc))
                return bad("an item is damaged");
            s.mCategory = static_cast<inventory::Category>(category);
            s.mPoison = poison == 1;
            for (std::uint8_t k = 0; k < effectCount; ++k)
            {
                EffectEntry e;
                if (!reader.readValue(e.mEffect) || !reader.readValue(e.mMagnitude) || !reader.readValue(e.mArea)
                    || !reader.readValue(e.mDuration))
                    return bad("an effect is cut short");
                if (!std::isfinite(e.mMagnitude))
                    return bad("an effect is damaged");
                s.mEffects.push_back(e);
            }
            if (!reader.readValue(s.mBase) || !reader.readValue(s.mTemperPermille) || !reader.readValue(s.mEnchantment)
                || !reader.readValue(s.mEnchantMagnitude))
                return bad("an item is cut short");
            if (!std::isfinite(s.mEnchantMagnitude) || s.mEnchantMagnitude < 0.f)
                return bad("an item is damaged");
            if (s.mKind == SyntheticKind::Potion)
            {
                if (s.mEffects.empty() || s.mCategory != inventory::Category::Potion || s.mBase != 0
                    || s.mTemperPermille != 0 || s.mEnchantment != 0 || s.mEnchantMagnitude != 0.f)
                    return bad("a potion is damaged");
            }
            else if (s.mBase == kNullFormId || !s.mEffects.empty() || s.mPoison
                || (s.mCategory != inventory::Category::Weapon && s.mCategory != inventory::Category::Armor)
                || s.mTemperPermille < 0 || s.mTemperPermille > 10000)
                return bad("a piece of gear is damaged");
            for (const Synthetic& other : items)
                if (sameDefinition(s, other))
                    return bad("an item is listed twice");
            items.push_back(std::move(s));
        }
        const auto readSet = [&](std::set<FormId>& out) {
            std::uint32_t n = 0;
            if (!reader.readValue(n) || n > kMaxKnown || static_cast<std::uint64_t>(n) * 4 > reader.remaining())
                return false;
            FormId previous = 0;
            for (std::uint32_t i = 0; i < n; ++i)
            {
                FormId id = 0;
                if (!reader.readValue(id) || id == 0 || (i > 0 && id <= previous))
                    return false;
                out.insert(id);
                previous = id;
            }
            return true;
        };
        if (!readSet(effects) || !readSet(enchantments))
            return bad("the knowledge lists are damaged");
        if (reader.remaining() != 0)
            return bad("the chunk has trailing bytes");
        mItems = std::move(items);
        mEffects = std::move(effects);
        mEnchantments = std::move(enchantments);
        ++mRevision;
        return true;
    }

    bool CraftingChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        std::string error;
        bool ok = true;
        if (version != kVersion)
        {
            error = "the crafting chunk has version " + std::to_string(version) + ", this build reads "
                + std::to_string(kVersion);
            ok = false;
        }
        else
            ok = mState.read(reader, error);
        if (!ok)
        {
            mRefusal = error;
            if (mSink)
                mSink(error);
            return false;
        }
        mRefusal.clear();
        if (mRestored)
            mRestored("restored: crafting, " + std::to_string(mState.all().size()) + " made items, "
                + std::to_string(mState.knownEffects().size()) + " effects and "
                + std::to_string(mState.knownEnchantments().size()) + " enchantments known");
        return true;
    }
}
