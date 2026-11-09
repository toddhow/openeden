#include <game_morrowind/crafting_rules.hpp>

#include <exception>
#include <iterator>
#include <string>
#include <utility>

#include <components/esm3/effectlist.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadrepa.hpp>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/magiceffects.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/itemhandles.hpp>

#include <game_morrowind/alchemy.hpp>
#include <game_morrowind/enchanting.hpp>
#include <game_morrowind/recharge.hpp>
#include <game_morrowind/repair.hpp>

// The Morrowind crafting rules behind the game-pack contract (task 0085 move 2, stage 2c). Everything here is a thin
// translation between plain values and handles on one side and the unchanged classes of this pack on the other: no rule
// is repeated and no random number is drawn here.
namespace game_morrowind
{
    namespace
    {
        namespace sdk = openeden::sdk::game;

        // The contract's numbers are the game's; a change in the game's enums would otherwise change them silently.
        static_assert(static_cast<int>(sdk::AlchemyResult::Success) == MWMechanics::Alchemy::Result_Success);
        static_assert(
            static_cast<int>(sdk::AlchemyResult::NoMortarAndPestle) == MWMechanics::Alchemy::Result_NoMortarAndPestle);
        static_assert(static_cast<int>(sdk::AlchemyResult::LessThanTwoIngredients)
            == MWMechanics::Alchemy::Result_LessThanTwoIngredients);
        static_assert(static_cast<int>(sdk::AlchemyResult::NoName) == MWMechanics::Alchemy::Result_NoName);
        static_assert(static_cast<int>(sdk::AlchemyResult::NoEffects) == MWMechanics::Alchemy::Result_NoEffects);
        static_assert(
            static_cast<int>(sdk::AlchemyResult::RandomFailure) == MWMechanics::Alchemy::Result_RandomFailure);
        static_assert(static_cast<int>(sdk::CastStyle::CastOnce) == ESM::Enchantment::CastOnce);
        static_assert(static_cast<int>(sdk::CastStyle::WhenStrikes) == ESM::Enchantment::WhenStrikes);
        static_assert(static_cast<int>(sdk::CastStyle::WhenUsed) == ESM::Enchantment::WhenUsed);
        static_assert(static_cast<int>(sdk::CastStyle::ConstantEffect) == ESM::Enchantment::ConstantEffect);
        static_assert(static_cast<int>(sdk::ApparatusSlot::MortarPestle) == ESM::Apparatus::MortarPestle);
        static_assert(static_cast<int>(sdk::ApparatusSlot::Alembic) == ESM::Apparatus::Alembic);
        static_assert(static_cast<int>(sdk::ApparatusSlot::Calcinator) == ESM::Apparatus::Calcinator);
        static_assert(static_cast<int>(sdk::ApparatusSlot::Retort) == ESM::Apparatus::Retort);

        std::string idText(const ESM::RefId& id)
        {
            return id.serializeText();
        }

        // Ids cross the contract as opaque text tokens (crafting.hpp). The core writes them with
        // ESM::RefId::serializeText (components/esm/refid.hpp, used by mwgui/enchantingdialog.cpp and
        // alchemywindow.cpp) and this pack reads them back with deserializeText. deserializeText throws on a malformed
        // token; no exception may cross the contract, so a token that cannot be read is no id (the empty RefId).
        ESM::RefId idFromText(const std::string& text)
        {
            try
            {
                return ESM::RefId::deserializeText(text);
            }
            catch (const std::exception&)
            {
                return ESM::RefId();
            }
        }

        class MorrowindAlchemySession final : public sdk::AlchemySession
        {
        public:
            void setAlchemist(sdk::ItemHandle actor) override { mAlchemy.setAlchemist(MWWorld::resolveItem(actor)); }

            void clear() override
            {
                mAlchemy.clear();
                mScope.release();
            }

            void setPotionName(std::string_view name) override { mAlchemy.setPotionName(std::string(name)); }

            int countPotionsToBrew() const override { return mAlchemy.countPotionsToBrew(); }

            int addIngredient(sdk::ItemHandle ingredient) override
            {
                return mAlchemy.addIngredient(MWWorld::resolveItem(ingredient));
            }

            void addApparatus(sdk::ItemHandle apparatus) override
            {
                mAlchemy.addApparatus(MWWorld::resolveItem(apparatus));
            }

            void removeIngredient(std::size_t index) override { mAlchemy.removeIngredient(index); }

            void removeApparatus(std::size_t index) override { mAlchemy.removeApparatus(index); }

            sdk::ItemHandle ingredientSlot(std::size_t index) override
            {
                return slot(mAlchemy.beginIngredients(), mAlchemy.endIngredients(), index);
            }

            sdk::ItemHandle apparatusSlot(std::size_t index) override
            {
                return slot(mAlchemy.beginTools(), mAlchemy.endTools(), index);
            }

            std::vector<sdk::AlchemyEffectLine> effects() const override
            {
                // The window asks about the player, as it always did, not about the session's alchemist.
                const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();

                std::vector<sdk::AlchemyEffectLine> lines;
                std::size_t index = 0;
                for (const MWMechanics::EffectKey& key : mAlchemy.listEffects())
                {
                    sdk::AlchemyEffectLine line;
                    line.effectId = idText(key.mId);
                    const ESM::MagicEffect* effect
                        = MWBase::Environment::get().getESMStore()->get<ESM::MagicEffect>().find(key.mId);
                    if (effect->mData.mFlags & ESM::MagicEffect::TargetSkill)
                        line.skill = idText(key.mArg);
                    else if (effect->mData.mFlags & ESM::MagicEffect::TargetAttribute)
                        line.attribute = idText(key.mArg);
                    line.known = MWMechanics::Alchemy::knownEffect(index, player);
                    lines.push_back(std::move(line));
                    ++index;
                }
                return lines;
            }

            std::string suggestPotionName() override { return mAlchemy.suggestPotionName(); }

            sdk::AlchemyResult create(std::string_view name, int& count) override
            {
                return static_cast<sdk::AlchemyResult>(mAlchemy.create(std::string(name), count));
            }

        private:
            template <class Iterator>
            sdk::ItemHandle slot(Iterator begin, Iterator end, std::size_t index)
            {
                if (index >= static_cast<std::size_t>(std::distance(begin, end)))
                    return sdk::kNoItem;
                return mScope.make(*std::next(begin, static_cast<std::ptrdiff_t>(index)));
            }

            MWMechanics::Alchemy mAlchemy;
            MWWorld::ItemHandleScope mScope; // owns the handles of the slots handed out
        };

        ESM::EffectList toEffectList(std::span<const sdk::EffectLine> lines)
        {
            ESM::EffectList list;
            for (const sdk::EffectLine& line : lines)
            {
                ESM::IndexedENAMstruct effect{};
                effect.mData.mEffectID = idFromText(line.effectId);
                effect.mData.mSkill = idFromText(line.skill);
                effect.mData.mAttribute = idFromText(line.attribute);
                effect.mData.mRange = line.range;
                effect.mData.mArea = line.area;
                effect.mData.mDuration = line.duration;
                effect.mData.mMagnMin = line.magnitudeMin;
                effect.mData.mMagnMax = line.magnitudeMax;
                list.mList.push_back(effect);
            }
            list.updateIndexes();
            return list;
        }

        class MorrowindEnchantingSession final : public sdk::EnchantingSession
        {
        public:
            void setEnchanter(sdk::ItemHandle actor) override { mEnchanting.setEnchanter(MWWorld::resolveItem(actor)); }

            void setSelfEnchanting(bool selfEnchanting) override { mEnchanting.setSelfEnchanting(selfEnchanting); }

            void setOldItem(sdk::ItemHandle item) override { mEnchanting.setOldItem(MWWorld::resolveItem(item)); }

            void setSoulGem(sdk::ItemHandle gem) override { mEnchanting.setSoulGem(MWWorld::resolveItem(gem)); }

            sdk::ItemHandle oldItem() override { return mScope.make(mEnchanting.getOldItem()); }

            sdk::ItemHandle soulGem() override { return mScope.make(mEnchanting.getGem()); }

            void setNewItemName(std::string_view name) override { mEnchanting.setNewItemName(std::string(name)); }

            void setEffects(std::span<const sdk::EffectLine> effects) override
            {
                mEnchanting.setEffect(toEffectList(effects));
            }

            bool create() override { return mEnchanting.create(); }

            void nextCastStyle() override { mEnchanting.nextCastStyle(); }

            sdk::CastStyle castStyle() const override
            {
                return static_cast<sdk::CastStyle>(mEnchanting.getCastStyle());
            }

            float enchantPoints(bool precise) const override { return mEnchanting.getEnchantPoints(precise); }

            int effectiveCastCost() const override { return mEnchanting.getEffectiveCastCost(); }

            int enchantPrice() const override { return mEnchanting.getEnchantPrice(); }

            int maxEnchantValue() const override { return mEnchanting.getMaxEnchantValue(); }

            int gemCharge() const override { return mEnchanting.getGemCharge(); }

            int enchantChance() const override { return mEnchanting.getEnchantChance(); }

            bool soulEmpty() const override { return mEnchanting.soulEmpty(); }

            bool itemEmpty() const override { return mEnchanting.itemEmpty(); }

        private:
            MWMechanics::Enchanting mEnchanting;
            MWWorld::ItemHandleScope mScope;
        };

        class MorrowindRepairSession final : public sdk::RepairSession
        {
        public:
            void setTool(sdk::ItemHandle tool) override { mRepair.setTool(MWWorld::resolveItem(tool)); }

            // A tool that was used up and replaced by another stack is a different Ptr, so the scope hands out a new
            // handle for it (ItemHandleScope::make).
            sdk::ItemHandle tool() override { return mScope.make(mRepair.getTool()); }

            int toolUses() const override
            {
                const MWWorld::Ptr tool = mRepair.getTool();
                return tool.getClass().getItemHealth(tool);
            }

            float toolQuality() const override
            {
                const MWWorld::Ptr tool = mRepair.getTool();
                return tool.get<ESM::Repair>()->mBase->mData.mQuality;
            }

            int toolCount() const override
            {
                const MWWorld::Ptr tool = mRepair.getTool();
                return tool.getCellRef().getCount();
            }

            void repair(sdk::ItemHandle itemToRepair) override { mRepair.repair(MWWorld::resolveItem(itemToRepair)); }

        private:
            MWMechanics::Repair mRepair;
            MWWorld::ItemHandleScope mScope;
        };

        class MorrowindCraftingRules final : public sdk::CraftingRules
        {
        public:
            std::unique_ptr<sdk::AlchemySession> beginAlchemy() override
            {
                return std::make_unique<MorrowindAlchemySession>();
            }

            std::unique_ptr<sdk::EnchantingSession> beginEnchanting() override
            {
                return std::make_unique<MorrowindEnchantingSession>();
            }

            std::unique_ptr<sdk::RepairSession> beginRepair() override
            {
                return std::make_unique<MorrowindRepairSession>();
            }

            bool rechargeWithGem(sdk::ItemHandle item, sdk::ItemHandle gem) override
            {
                const MWWorld::Ptr itemPtr = MWWorld::resolveItem(item);
                const MWWorld::Ptr gemPtr = MWWorld::resolveItem(gem);
                if (itemPtr.isEmpty() || gemPtr.isEmpty())
                    return false;
                return MWMechanics::rechargeItem(itemPtr, gemPtr);
            }

            int gemSoul(sdk::ItemHandle gem) override
            {
                const MWWorld::Ptr gemPtr = MWWorld::resolveItem(gem);
                if (gemPtr.isEmpty())
                    return 0;
                const ESM::Creature* creature = MWBase::Environment::get().getESMStore()->get<ESM::Creature>().find(
                    gemPtr.getCellRef().getSoul());
                return creature->mData.mSoul;
            }

            float rechargedCharge(float charge, float maxCharge, float duration) override
            {
                return MWMechanics::rechargedCharge(charge, maxCharge, duration);
            }

            bool knownAlchemyEffect(std::size_t potionEffectIndex, sdk::ItemHandle actor) override
            {
                const MWWorld::Ptr ptr = MWWorld::resolveItem(actor);
                return !ptr.isEmpty() && MWMechanics::Alchemy::knownEffect(potionEffectIndex, ptr);
            }

            std::vector<std::string> ingredientEffectNames(sdk::ItemHandle ingredient, float alchemySkill) override
            {
                const MWWorld::Ptr ptr = MWWorld::resolveItem(ingredient);
                if (ptr.isEmpty())
                    return {};
                return MWMechanics::Alchemy::effectsDescription(ptr, alchemySkill);
            }

            int apparatusSlotOf(sdk::ItemHandle apparatus) override
            {
                const MWWorld::Ptr ptr = MWWorld::resolveItem(apparatus);
                if (ptr.isEmpty() || ptr.getType() != ESM::Apparatus::sRecordId)
                    return -1;
                return ptr.get<ESM::Apparatus>()->mBase->mData.mType;
            }
        };
    }

    std::unique_ptr<openeden::sdk::game::CraftingRules> makeCraftingRules()
    {
        return std::make_unique<MorrowindCraftingRules>();
    }
}
