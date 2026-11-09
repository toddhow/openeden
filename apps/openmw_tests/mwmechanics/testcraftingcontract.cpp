// Task 0085, move 2, stage 2c: Morrowind's crafting rules through the game-pack contract
// (sdk/include/openeden/game/crafting.hpp, game_morrowind::makeCraftingRules). Written from the decisions of 2026-11-08
// before the adapter: items cross as opaque handles, values are plain, and the rules, numbers and random draws are the
// ones the 81 characterization tests pin on the classes themselves. Every "same as direct" test runs the unchanged class
// and the contract on the same inputs and the same seed and compares results and the state of the generator.
#include <sstream>

#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadweap.hpp>

#include <game_morrowind/alchemy.hpp>
#include <game_morrowind/crafting_rules.hpp>
#include <game_morrowind/enchanting.hpp>
#include <game_morrowind/recharge.hpp>
#include <game_morrowind/repair.hpp>

#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwmechanics/magiceffects.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"
#include "apps/openmw/mwworld/craftingrules.hpp"
#include "apps/openmw/mwworld/itemhandles.hpp"

#include "craftingfixture.hpp"

namespace CraftingTest
{
    namespace
    {
        namespace sdk = openeden::sdk::game;
        using sdk::ItemHandle;
        using sdk::kNoItem;

        class CraftingContractTest : public CraftingRulesTest
        {
        protected:
            void SetUp() override
            {
                CraftingRulesTest::SetUp();
                mRules = game_morrowind::makeCraftingRules();
                ASSERT_NE(mRules, nullptr);

                addEffect(ESM::MagicEffect::RestoreHealth, 5.f, 0);
                addEffect(ESM::MagicEffect::RestoreFatigue, 4.f, 0);
                addEffect(ESM::MagicEffect::FortifyAttribute, 2.f, ESM::MagicEffect::TargetAttribute);
                addEffect(ESM::MagicEffect::DamageHealth, 3.f, ESM::MagicEffect::Harmful);
                addEffect(ESM::MagicEffect::DrainSkill, 2.f, ESM::MagicEffect::Harmful | ESM::MagicEffect::TargetSkill);
                addEffect(ESM::MagicEffect::Light, 0.5f, ESM::MagicEffect::NoMagnitude);

                const ESM::RefId none;
                const IngredientEffect restoreHealth{ ESM::MagicEffect::RestoreHealth, none, none };
                const IngredientEffect restoreFatigue{ ESM::MagicEffect::RestoreFatigue, none, none };
                const IngredientEffect damageHealth{ ESM::MagicEffect::DamageHealth, none, none };
                const IngredientEffect light{ ESM::MagicEffect::Light, none, none };
                const IngredientEffect fortifyStrength{ ESM::MagicEffect::FortifyAttribute, none,
                    ESM::Attribute::Strength };
                const IngredientEffect drainArmorer{ ESM::MagicEffect::DrainSkill, ESM::Skill::Armorer, none };

                addIngredient("ing_a", { restoreHealth, fortifyStrength, damageHealth, light }, 0.5f, 10);
                addIngredient("ing_c", { restoreFatigue, fortifyStrength, restoreHealth, drainArmorer }, 0.25f, 5);
                addIngredient("ing_d", { drainArmorer, damageHealth, restoreFatigue, light }, 2.f, 40);

                addApparatus("mortar_1", ESM::Apparatus::MortarPestle, 1.f);
                addApparatus("alembic", ESM::Apparatus::Alembic, 1.2f);
                addApparatus("calcinator", ESM::Apparatus::Calcinator, 1.5f);
                addApparatus("retort", ESM::Apparatus::Retort, 2.f);

                ESM::Repair tool;
                tool.blank();
                tool.mId = rid("test_prong");
                tool.mName = "test prong";
                tool.mData.mUses = 3;
                tool.mData.mQuality = 1.5f;
                tool.mData.mWeight = 1.f;
                tool.mData.mValue = 10;
                mStore.insertStatic(tool);

                ESM::Enchantment enchantment;
                enchantment.blank();
                enchantment.mId = rid("test_enchantment");
                enchantment.mData.mType = ESM::Enchantment::CastOnce;
                enchantment.mData.mCost = 10;
                enchantment.mData.mCharge = 500;
                enchantment.mData.mFlags = 0;
                mStore.insertStatic(enchantment);

                ESM::Weapon sword;
                sword.blank();
                sword.mId = rid("test_sword");
                sword.mName = "test sword";
                sword.mData.mType = ESM::Weapon::LongBladeOneHand;
                sword.mData.mEnchant = 200;
                sword.mData.mHealth = 100;
                sword.mData.mWeight = 5.f;
                sword.mData.mValue = 50;
                mStore.insertStatic(sword);

                ESM::Weapon blade = sword;
                blade.mId = rid("test_blade");
                blade.mName = "test blade";
                blade.mEnchant = rid("test_enchantment");
                mStore.insertStatic(blade);

                ESM::Creature creature;
                creature.blank();
                creature.mId = rid("test_creature");
                creature.mData.mSoul = 300;
                mStore.insertStatic(creature);

                ESM::Miscellaneous gem;
                gem.blank();
                gem.mId = rid("test_gem");
                gem.mName = "test gem";
                gem.mData.mWeight = 1.f;
                gem.mData.mValue = 100;
                mStore.insertStatic(gem);
            }

            void TearDown() override
            {
                MWWorld::installCraftingRules(nullptr);
                CraftingRulesTest::TearDown();
            }

            void clearInventory()
            {
                MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
                std::vector<MWWorld::Ptr> all;
                for (auto it = store.begin(); it != store.end(); ++it)
                    all.push_back(*it);
                for (const MWWorld::Ptr& item : all)
                    store.remove(item, item.getCellRef().getCount());
            }

            /// "name v=value w=weight [effect d.. m.. ...]" of the first potion in the player's inventory.
            std::string potion()
            {
                MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
                auto it = store.begin(MWWorld::ContainerStore::Type_Potion);
                if (it == store.end())
                    return "none";
                const ESM::Potion& record = *it->get<ESM::Potion>()->mBase;
                std::ostringstream out;
                out << record.mName << " v=" << record.mData.mValue << " w=" << record.mData.mWeight;
                for (const ESM::IndexedENAMstruct& effect : record.mEffects.mList)
                    out << " [" << effect.mData.mEffectID.getRefIdString() << " d" << effect.mData.mDuration << " m"
                        << effect.mData.mMagnMin << "-" << effect.mData.mMagnMax << "]";
                return out.str();
            }

            MWWorld::ItemHandleScope mScope;
            std::unique_ptr<sdk::CraftingRules> mRules;
        };

        // ---- the installed rules ----

        // Review 2c, M1: a host that installs nothing (a test fixture, a tool) must not throw from a potion tooltip, an
        // item filter or a window constructor. activeCraftingRules() is then an inert set of rules with documented
        // answers.
        TEST_F(CraftingContractTest, nothingInstalledMeansNoRulesAndTheActiveRulesAreInertNotAThrow)
        {
            MWWorld::installCraftingRules(nullptr);
            EXPECT_EQ(MWWorld::craftingRules(), nullptr);
            sdk::CraftingRules* inert = nullptr;
            ASSERT_NO_THROW(inert = &MWWorld::activeCraftingRules());
            ASSERT_NE(inert, nullptr);
            EXPECT_EQ(&MWWorld::activeCraftingRules(), inert) << "one inert object, not a new one per call";
        }

        TEST_F(CraftingContractTest, theInertRulesKnowNothingAndDoNothing)
        {
            MWWorld::installCraftingRules(nullptr);
            sdk::CraftingRules& rules = MWWorld::activeCraftingRules();
            const ItemHandle gem = mScope.make(give(rid("test_gem"), 1));

            EXPECT_FALSE(rules.knownAlchemyEffect(0, mScope.make(mPlayer)));
            EXPECT_TRUE(rules.ingredientEffectNames(gem, 100.f).empty());
            EXPECT_EQ(rules.apparatusSlotOf(gem), -1);
            EXPECT_EQ(rules.gemSoul(gem), 0);
            EXPECT_FALSE(rules.rechargeWithGem(gem, gem));
            EXPECT_FLOAT_EQ(rules.rechargedCharge(10.f, 100.f, 5.f), 10.f);

            auto alchemy = rules.beginAlchemy();
            ASSERT_NE(alchemy, nullptr);
            alchemy->setAlchemist(mScope.make(mPlayer));
            EXPECT_EQ(alchemy->addIngredient(gem), -1);
            EXPECT_EQ(alchemy->countPotionsToBrew(), 0);
            EXPECT_TRUE(alchemy->effects().empty());
            EXPECT_EQ(alchemy->ingredientSlot(0), kNoItem);
            EXPECT_EQ(alchemy->apparatusSlot(0), kNoItem);
            int count = 3;
            EXPECT_EQ(alchemy->create("x", count), sdk::AlchemyResult::NoMortarAndPestle);
            EXPECT_EQ(count, 0);
            alchemy->clear();

            auto enchanting = rules.beginEnchanting();
            ASSERT_NE(enchanting, nullptr);
            enchanting->setOldItem(gem);
            EXPECT_TRUE(enchanting->itemEmpty());
            EXPECT_TRUE(enchanting->soulEmpty());
            EXPECT_FALSE(enchanting->create());
            EXPECT_EQ(enchanting->oldItem(), kNoItem);
            EXPECT_EQ(enchanting->castStyle(), sdk::CastStyle::CastOnce);
            EXPECT_EQ(enchanting->enchantChance(), 0);

            auto repair = rules.beginRepair();
            ASSERT_NE(repair, nullptr);
            repair->setTool(gem);
            EXPECT_EQ(repair->tool(), kNoItem);
            EXPECT_EQ(repair->toolCount(), 0);
            EXPECT_EQ(repair->toolUses(), 0);
            EXPECT_NO_THROW(repair->repair(gem));
        }

        TEST_F(CraftingContractTest, installedRulesWinOverTheInertOnes)
        {
            sdk::CraftingRules* raw = mRules.get();
            MWWorld::installCraftingRules(std::move(mRules));
            EXPECT_EQ(&MWWorld::activeCraftingRules(), raw);
        }

        // The pack reads ids the core wrote (opaque tokens, crafting.hpp). A token it cannot read is no id, not a throw
        // out of the pack (the contract has no exception across it).
        TEST_F(CraftingContractTest, anIdTokenThePackCannotReadIsNoIdAndDoesNotThrow)
        {
            const MWWorld::Ptr sword = give(rid("test_sword"), 1);
            auto enchanting = mRules->beginEnchanting();
            ASSERT_NE(enchanting, nullptr);
            enchanting->setEnchanter(mScope.make(mPlayer));
            enchanting->setOldItem(mScope.make(sword));
            sdk::EffectLine line;
            line.effectId = "FormId:not-hex";
            line.skill = "Generated:zz";
            line.attribute = "Index:";
            line.duration = 1;
            line.magnitudeMin = 1;
            line.magnitudeMax = 1;
            const std::vector<sdk::EffectLine> lines{ line };
            EXPECT_NO_THROW(enchanting->setEffects(lines));
        }

        TEST_F(CraftingContractTest, installedRulesAreHandedBack)
        {
            sdk::CraftingRules* raw = mRules.get();
            MWWorld::installCraftingRules(std::move(mRules));
            EXPECT_EQ(MWWorld::craftingRules(), raw);
            EXPECT_EQ(&MWWorld::activeCraftingRules(), raw);
        }

        // ---- alchemy ----

        TEST_F(CraftingContractTest, alchemySlotsHandBackTheItemsThatWereAdded)
        {
            const MWWorld::Ptr mortar = give(rid("mortar_1"), 1);
            const MWWorld::Ptr ingredient = give(rid("ing_a"), 5);
            auto alchemy = mRules->beginAlchemy();
            ASSERT_NE(alchemy, nullptr);
            alchemy->setAlchemist(mScope.make(mPlayer));
            EXPECT_EQ(alchemy->addIngredient(mScope.make(ingredient)), 0);

            EXPECT_EQ(MWWorld::resolveItem(alchemy->ingredientSlot(0)), ingredient);
            EXPECT_EQ(alchemy->ingredientSlot(1), kNoItem);
            EXPECT_EQ(alchemy->ingredientSlot(sdk::kAlchemyIngredientSlots), kNoItem);
            EXPECT_EQ(MWWorld::resolveItem(alchemy->apparatusSlot(0)), mortar)
                << "setAlchemist picks the mortar from the inventory";
            EXPECT_EQ(alchemy->apparatusSlot(1), kNoItem);
            EXPECT_EQ(alchemy->apparatusSlot(99), kNoItem);
        }

        TEST_F(CraftingContractTest, removingFromASlotEmptiesIt)
        {
            give(rid("mortar_1"), 1);
            const MWWorld::Ptr a = give(rid("ing_a"), 5);
            const MWWorld::Ptr c = give(rid("ing_c"), 5);
            auto alchemy = mRules->beginAlchemy();
            alchemy->setAlchemist(mScope.make(mPlayer));
            alchemy->addIngredient(mScope.make(a));
            alchemy->addIngredient(mScope.make(c));
            alchemy->removeIngredient(0);
            EXPECT_EQ(alchemy->ingredientSlot(0), kNoItem);
            EXPECT_EQ(MWWorld::resolveItem(alchemy->ingredientSlot(1)), c);
            alchemy->removeIngredient(7); // past the slots: a no-op
            alchemy->removeApparatus(0);
            EXPECT_EQ(alchemy->apparatusSlot(0), kNoItem);
        }

        TEST_F(CraftingContractTest, alchemyResultsAreTheDirectClassesResults)
        {
            stats().getSkill(ESM::Skill::Alchemy).setBase(100.f);
            using Direct = MWMechanics::Alchemy;
            struct Case
            {
                bool mMortar;
                int mIngredients;
                const char* mName;
            };
            for (const Case& c : { Case{ true, 2, "" }, Case{ false, 2, "x" }, Case{ true, 1, "x" }, Case{ true, 2, "x" } })
            {
                clearInventory();
                if (c.mMortar)
                    give(rid("mortar_1"), 1);
                const MWWorld::Ptr a = give(rid("ing_a"), 5);
                const MWWorld::Ptr cc = give(rid("ing_c"), 5);

                seedWorld(1);
                Direct direct;
                direct.setAlchemist(mPlayer);
                direct.addIngredient(a);
                if (c.mIngredients > 1)
                    direct.addIngredient(cc);
                int directCount = 1;
                const Direct::Result directResult = direct.create(c.mName, directCount);
                const Misc::Rng::Generator directGenerator = mWorld.mPrng;
                const std::string directPotion = potion();

                clearInventory();
                if (c.mMortar)
                    give(rid("mortar_1"), 1);
                const MWWorld::Ptr a2 = give(rid("ing_a"), 5);
                const MWWorld::Ptr c2 = give(rid("ing_c"), 5);

                seedWorld(1);
                auto alchemy = mRules->beginAlchemy();
                alchemy->setAlchemist(mScope.make(mPlayer));
                alchemy->addIngredient(mScope.make(a2));
                if (c.mIngredients > 1)
                    alchemy->addIngredient(mScope.make(c2));
                int count = 1;
                const sdk::AlchemyResult result = alchemy->create(c.mName, count);

                EXPECT_EQ(static_cast<int>(result), static_cast<int>(directResult)) << c.mName << c.mMortar;
                EXPECT_EQ(count, directCount);
                EXPECT_EQ(mWorld.mPrng, directGenerator) << "the contract adds no random draw and takes none away";
                EXPECT_EQ(potion(), directPotion);
            }
        }

        TEST_F(CraftingContractTest, aBrewedPotionIsTheDirectClassesPotion)
        {
            stats().getSkill(ESM::Skill::Alchemy).setBase(100.f);
            give(rid("mortar_1"), 1);
            give(rid("alembic"), 1);
            MWMechanics::Alchemy direct;
            direct.setAlchemist(mPlayer);
            direct.addIngredient(give(rid("ing_a"), 5));
            direct.addIngredient(give(rid("ing_c"), 5));
            seedWorld(1);
            int directCount = 2;
            ASSERT_EQ(direct.create("brew", directCount), MWMechanics::Alchemy::Result_Success);
            const std::string directPotion = potion();
            ASSERT_NE(directPotion, "none");

            clearInventory();
            give(rid("mortar_1"), 1);
            give(rid("alembic"), 1);
            auto alchemy = mRules->beginAlchemy();
            alchemy->setAlchemist(mScope.make(mPlayer));
            alchemy->addIngredient(mScope.make(give(rid("ing_a"), 5)));
            alchemy->addIngredient(mScope.make(give(rid("ing_c"), 5)));
            alchemy->setPotionName("brew"); // without a name the session is not ready and brews none
            EXPECT_EQ(alchemy->countPotionsToBrew(), 5);
            seedWorld(1);
            int count = 2;
            EXPECT_EQ(alchemy->create("brew", count), sdk::AlchemyResult::Success);
            EXPECT_EQ(count, directCount);
            EXPECT_EQ(potion(), directPotion);
        }

        TEST_F(CraftingContractTest, effectLinesAreTheDirectClassesEffectsWithWhatTheyTarget)
        {
            stats().getSkill(ESM::Skill::Alchemy).setBase(45.f);
            give(rid("mortar_1"), 1);
            const MWWorld::Ptr a = give(rid("ing_a"), 5);
            const MWWorld::Ptr c = give(rid("ing_c"), 5);
            const MWWorld::Ptr d = give(rid("ing_d"), 5);

            MWMechanics::Alchemy direct;
            direct.setAlchemist(mPlayer);
            direct.addIngredient(a);
            direct.addIngredient(c);
            direct.addIngredient(d);
            const std::vector<MWMechanics::EffectKey> keys = direct.listEffects();
            ASSERT_FALSE(keys.empty());

            auto alchemy = mRules->beginAlchemy();
            alchemy->setAlchemist(mScope.make(mPlayer));
            alchemy->addIngredient(mScope.make(a));
            alchemy->addIngredient(mScope.make(c));
            alchemy->addIngredient(mScope.make(d));
            const std::vector<sdk::AlchemyEffectLine> lines = alchemy->effects();
            ASSERT_EQ(lines.size(), keys.size());

            bool sawAttribute = false, sawSkill = false;
            for (size_t i = 0; i < keys.size(); ++i)
            {
                EXPECT_EQ(ESM::RefId::deserializeText(lines[i].effectId), keys[i].mId) << i;
                const ESM::MagicEffect* effect = mStore.get<ESM::MagicEffect>().find(keys[i].mId);
                if (effect->mData.mFlags & ESM::MagicEffect::TargetSkill)
                {
                    EXPECT_EQ(ESM::RefId::deserializeText(lines[i].skill), keys[i].mArg);
                    EXPECT_TRUE(lines[i].attribute.empty());
                    sawSkill = true;
                }
                else if (effect->mData.mFlags & ESM::MagicEffect::TargetAttribute)
                {
                    EXPECT_EQ(ESM::RefId::deserializeText(lines[i].attribute), keys[i].mArg);
                    EXPECT_TRUE(lines[i].skill.empty());
                    sawAttribute = true;
                }
                else
                {
                    EXPECT_TRUE(lines[i].skill.empty());
                    EXPECT_TRUE(lines[i].attribute.empty());
                }
                EXPECT_EQ(lines[i].known, MWMechanics::Alchemy::knownEffect(i, mPlayer)) << i;
            }
            EXPECT_TRUE(sawAttribute);
            EXPECT_TRUE(sawSkill);
        }

        TEST_F(CraftingContractTest, theSuggestedNameAndCountAreTheDirectClassesAndClearEndsTheHandles)
        {
            stats().getSkill(ESM::Skill::Alchemy).setBase(100.f);
            give(rid("mortar_1"), 1);
            const MWWorld::Ptr a = give(rid("ing_a"), 5);
            const MWWorld::Ptr c = give(rid("ing_c"), 3);
            MWMechanics::Alchemy direct;
            direct.setAlchemist(mPlayer);
            direct.addIngredient(a);
            direct.addIngredient(c);

            auto alchemy = mRules->beginAlchemy();
            alchemy->setAlchemist(mScope.make(mPlayer));
            alchemy->addIngredient(mScope.make(a));
            alchemy->addIngredient(mScope.make(c));
            EXPECT_EQ(alchemy->suggestPotionName(), direct.suggestPotionName());
            EXPECT_EQ(alchemy->countPotionsToBrew(), direct.countPotionsToBrew());

            const ItemHandle slot = alchemy->ingredientSlot(0);
            ASSERT_FALSE(slot.empty());
            alchemy->clear();
            EXPECT_TRUE(MWWorld::resolveItem(slot).isEmpty()) << "clear() ends the handles the session made";
            EXPECT_EQ(alchemy->ingredientSlot(0), kNoItem);
        }

        TEST_F(CraftingContractTest, aSessionsHandlesDieWithIt)
        {
            const std::size_t before = MWWorld::liveItemHandleCount();
            give(rid("mortar_1"), 1);
            ItemHandle slot;
            {
                auto alchemy = mRules->beginAlchemy();
                alchemy->setAlchemist(mScope.make(mPlayer));
                alchemy->addIngredient(mScope.make(give(rid("ing_a"), 5)));
                slot = alchemy->ingredientSlot(0);
                ASSERT_FALSE(MWWorld::resolveItem(slot).isEmpty());
            }
            EXPECT_TRUE(MWWorld::resolveItem(slot).isEmpty());
            mScope.release();
            EXPECT_EQ(MWWorld::liveItemHandleCount(), before);
        }

        TEST_F(CraftingContractTest, staticQuestionsAreTheDirectClassesAnswers)
        {
            const MWWorld::Ptr a = give(rid("ing_a"), 1);
            const ItemHandle player = mScope.make(mPlayer);
            for (const float skill : { 0.f, 15.f, 30.f, 60.f, 100.f })
            {
                stats().getSkill(ESM::Skill::Alchemy).setBase(skill);
                for (size_t i = 0; i < 9; ++i)
                    EXPECT_EQ(mRules->knownAlchemyEffect(i, player), MWMechanics::Alchemy::knownEffect(i, mPlayer))
                        << skill << " " << i;
                EXPECT_EQ(mRules->ingredientEffectNames(mScope.make(a), skill),
                    MWMechanics::Alchemy::effectsDescription(a, skill))
                    << skill;
            }
            EXPECT_EQ(mRules->apparatusSlotOf(mScope.make(give(rid("mortar_1"), 1))),
                static_cast<int>(sdk::ApparatusSlot::MortarPestle));
            EXPECT_EQ(mRules->apparatusSlotOf(mScope.make(give(rid("alembic"), 1))),
                static_cast<int>(sdk::ApparatusSlot::Alembic));
            EXPECT_EQ(mRules->apparatusSlotOf(mScope.make(give(rid("calcinator"), 1))),
                static_cast<int>(sdk::ApparatusSlot::Calcinator));
            EXPECT_EQ(mRules->apparatusSlotOf(mScope.make(give(rid("retort"), 1))),
                static_cast<int>(sdk::ApparatusSlot::Retort));
            EXPECT_EQ(mRules->apparatusSlotOf(mScope.make(a)), -1) << "an ingredient is no apparatus";
            EXPECT_EQ(mRules->apparatusSlotOf(kNoItem), -1);
        }

        // ---- enchanting ----

        ESM::EffectList directList(const std::vector<sdk::EffectLine>& lines)
        {
            ESM::EffectList list;
            for (const sdk::EffectLine& line : lines)
            {
                ESM::IndexedENAMstruct effect{};
                effect.mData.mEffectID = ESM::RefId::deserializeText(line.effectId);
                effect.mData.mSkill = ESM::RefId::deserializeText(line.skill);
                effect.mData.mAttribute = ESM::RefId::deserializeText(line.attribute);
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

        std::vector<sdk::EffectLine> twoLines()
        {
            sdk::EffectLine first;
            first.effectId = ESM::RefId(ESM::MagicEffect::RestoreHealth).serializeText();
            first.range = 0;
            first.duration = 10;
            first.magnitudeMin = first.magnitudeMax = 10;
            sdk::EffectLine second;
            second.effectId = ESM::RefId(ESM::MagicEffect::FortifyAttribute).serializeText();
            second.attribute = ESM::RefId(ESM::Attribute::Strength).serializeText();
            second.range = 2;
            second.duration = 5;
            second.magnitudeMin = 2;
            second.magnitudeMax = 4;
            return { first, second };
        }

        TEST_F(CraftingContractTest, enchantingNumbersAreTheDirectClassesNumbers)
        {
            const MWWorld::Ptr sword = give(rid("test_sword"), 1);
            const MWWorld::Ptr gem = give(rid("test_gem"), 1);
            gem.getCellRef().setSoul(rid("test_creature"));
            const std::vector<sdk::EffectLine> lines = twoLines();

            MWMechanics::Enchanting direct;
            direct.setEnchanter(mPlayer);
            direct.setSelfEnchanting(true);
            direct.setOldItem(sword);
            direct.setSoulGem(gem);
            direct.setEffect(directList(lines));

            auto enchanting = mRules->beginEnchanting();
            ASSERT_NE(enchanting, nullptr);
            enchanting->setEnchanter(mScope.make(mPlayer));
            enchanting->setSelfEnchanting(true);
            enchanting->setOldItem(mScope.make(sword));
            enchanting->setSoulGem(mScope.make(gem));
            enchanting->setEffects(lines);

            EXPECT_FLOAT_EQ(enchanting->enchantPoints(true), direct.getEnchantPoints(true));
            EXPECT_FLOAT_EQ(enchanting->enchantPoints(false), direct.getEnchantPoints(false));
            EXPECT_EQ(enchanting->effectiveCastCost(), direct.getEffectiveCastCost());
            EXPECT_EQ(enchanting->enchantPrice(), direct.getEnchantPrice());
            EXPECT_EQ(enchanting->maxEnchantValue(), direct.getMaxEnchantValue());
            EXPECT_EQ(enchanting->gemCharge(), direct.getGemCharge());
            EXPECT_EQ(enchanting->enchantChance(), direct.getEnchantChance());
            EXPECT_EQ(enchanting->soulEmpty(), direct.soulEmpty());
            EXPECT_EQ(enchanting->itemEmpty(), direct.itemEmpty());
            EXPECT_EQ(MWWorld::resolveItem(enchanting->oldItem()), sword);
            EXPECT_EQ(MWWorld::resolveItem(enchanting->soulGem()), gem);
            EXPECT_GT(enchanting->enchantPoints(true), 0.f);
        }

        TEST_F(CraftingContractTest, emptySlotsOfAnEnchantingSessionAreNoItem)
        {
            auto enchanting = mRules->beginEnchanting();
            EXPECT_EQ(enchanting->oldItem(), kNoItem);
            EXPECT_EQ(enchanting->soulGem(), kNoItem);
            EXPECT_TRUE(enchanting->soulEmpty());
            EXPECT_TRUE(enchanting->itemEmpty());
            enchanting->setOldItem(mScope.make(give(rid("test_sword"), 1)));
            EXPECT_FALSE(enchanting->itemEmpty());
            enchanting->setOldItem(kNoItem);
            EXPECT_TRUE(enchanting->itemEmpty());
        }

        TEST_F(CraftingContractTest, castStylesFollowTheDirectClass)
        {
            const MWWorld::Ptr sword = give(rid("test_sword"), 1);
            MWMechanics::Enchanting direct;
            direct.setOldItem(sword);
            auto enchanting = mRules->beginEnchanting();
            enchanting->setOldItem(mScope.make(sword));
            for (int i = 0; i < 6; ++i)
            {
                EXPECT_EQ(static_cast<int>(enchanting->castStyle()), direct.getCastStyle()) << i;
                direct.nextCastStyle();
                enchanting->nextCastStyle();
            }
        }

        TEST_F(CraftingContractTest, aSelfEnchantmentIsTheDirectClassesWithTheSameDraws)
        {
            stats().getSkill(ESM::Skill::Enchant).setBase(100.f);
            const std::vector<sdk::EffectLine> lines = twoLines();
            auto giveSet = [&](MWWorld::Ptr& sword, MWWorld::Ptr& gem) {
                clearInventory();
                sword = give(rid("test_sword"), 1);
                gem = give(rid("test_gem"), 1);
                gem.getCellRef().setSoul(rid("test_creature"));
            };
            MWWorld::Ptr sword, gem;

            giveSet(sword, gem);
            MWMechanics::Enchanting direct;
            direct.setEnchanter(mPlayer);
            direct.setSelfEnchanting(true);
            direct.setOldItem(sword);
            direct.setSoulGem(gem);
            direct.setNewItemName("self");
            direct.setEffect(directList(lines));
            seedWorld(seedWithRoll(3));
            const bool directResult = direct.create();
            const Misc::Rng::Generator directGenerator = mWorld.mPrng;
            const int directSkillUses = static_cast<int>(mLua.mSkillUses.size());

            giveSet(sword, gem);
            auto enchanting = mRules->beginEnchanting();
            enchanting->setEnchanter(mScope.make(mPlayer));
            enchanting->setSelfEnchanting(true);
            enchanting->setOldItem(mScope.make(sword));
            enchanting->setSoulGem(mScope.make(gem));
            enchanting->setNewItemName("self");
            enchanting->setEffects(lines);
            seedWorld(seedWithRoll(3));
            EXPECT_EQ(enchanting->create(), directResult);
            EXPECT_EQ(mWorld.mPrng, directGenerator);
            EXPECT_EQ(static_cast<int>(mLua.mSkillUses.size()), 2 * directSkillUses);
        }

        // ---- repair ----

        TEST_F(CraftingContractTest, aRepairSessionReportsItsToolInPlainValues)
        {
            const MWWorld::Ptr tool = give(rid("test_prong"), 1);
            auto repair = mRules->beginRepair();
            ASSERT_NE(repair, nullptr);
            repair->setTool(mScope.make(tool));
            EXPECT_EQ(MWWorld::resolveItem(repair->tool()), tool);
            EXPECT_EQ(repair->toolUses(), 3);
            EXPECT_FLOAT_EQ(repair->toolQuality(), 1.5f);
            EXPECT_EQ(repair->toolCount(), 1);
        }

        TEST_F(CraftingContractTest, repairingIsTheDirectClassesRepairWithTheSameDraw)
        {
            stats().getSkill(ESM::Skill::Armorer).setBase(30.f);
            auto damaged = [&](MWWorld::Ptr& tool, MWWorld::Ptr& sword) {
                clearInventory();
                tool = give(rid("test_prong"), 1);
                sword = give(rid("test_sword"), 1);
                sword.getCellRef().setCharge(10);
            };
            MWWorld::Ptr tool, sword;

            damaged(tool, sword);
            MWMechanics::Repair direct;
            direct.setTool(tool);
            seedWorld(seedWithRoll(20));
            direct.repair(sword);
            const int directCharge = sword.getCellRef().getCharge();
            const int directUses = direct.getTool().getCellRef().getCharge();
            const Misc::Rng::Generator directGenerator = mWorld.mPrng;
            ASSERT_GT(directCharge, 10);

            damaged(tool, sword);
            auto repair = mRules->beginRepair();
            repair->setTool(mScope.make(tool));
            seedWorld(seedWithRoll(20));
            repair->repair(mScope.make(sword));
            EXPECT_EQ(sword.getCellRef().getCharge(), directCharge);
            EXPECT_EQ(repair->toolUses(), directUses);
            EXPECT_EQ(mWorld.mPrng, directGenerator);
        }

        TEST_F(CraftingContractTest, aToolThatIsUsedUpIsReplacedByAnotherStackAndANewHandle)
        {
            stats().getSkill(ESM::Skill::Armorer).setBase(100.f);
            const MWWorld::Ptr tools = give(rid("test_prong"), 2);
            const MWWorld::Ptr sword = give(rid("test_sword"), 1);
            sword.getCellRef().setCharge(10);
            tools.getCellRef().setCharge(1); // one use left

            auto repair = mRules->beginRepair();
            const ItemHandle before = mScope.make(tools);
            repair->setTool(before);
            const ItemHandle first = repair->tool();
            seedWorld(1);
            repair->repair(mScope.make(sword));

            const ItemHandle after = repair->tool();
            EXPECT_NE(after, first) << "the tool is another stack now: a new handle";
            EXPECT_NE(after, kNoItem);
            EXPECT_EQ(repair->toolCount(), 1);
            EXPECT_EQ(repair->toolUses(), 1) << "the other copy of the stack had the same one use left";
        }

        // ---- recharge and the rest ----

        TEST_F(CraftingContractTest, rechargeOverTimeIsRateTimesDurationCappedAtTheMaximum)
        {
            EXPECT_FLOAT_EQ(mRules->rechargedCharge(10.f, 100.f, 20.f), 11.f) << "fMagicItemRechargePerSecond is 0.05";
            EXPECT_FLOAT_EQ(mRules->rechargedCharge(99.9f, 100.f, 20.f), 100.f);
            EXPECT_FLOAT_EQ(mRules->rechargedCharge(10.f, 100.f, 0.f), 10.f);
            // the same expression the moved rechargeItem uses
            EXPECT_FLOAT_EQ(MWMechanics::rechargedCharge(10.f, 100.f, 20.f), 11.f);
        }

        TEST_F(CraftingContractTest, rechargingWithAnUnknownItemOrGemDoesNothing)
        {
            const MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(0.f);
            EXPECT_FALSE(mRules->rechargeWithGem(mScope.make(blade), kNoItem));
            EXPECT_FALSE(mRules->rechargeWithGem(kNoItem, kNoItem));
            EXPECT_FALSE(mRules->rechargeWithGem(ItemHandle{ 0x7777 }, mScope.make(blade)));
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), 0.f);
        }

        TEST_F(CraftingContractTest, rechargingWithAGemIsTheDirectClassesWithTheSameDraw)
        {
            stats().getSkill(ESM::Skill::Enchant).setBase(100.f);
            auto fresh = [&](MWWorld::Ptr& blade, MWWorld::Ptr& gem) {
                clearInventory();
                blade = give(rid("test_blade"), 1);
                blade.getCellRef().setEnchantmentCharge(0.f);
                gem = give(rid("test_gem"), 2);
                gem.getCellRef().setSoul(rid("test_creature"));
            };
            MWWorld::Ptr blade, gem;
            fresh(blade, gem);
            seedWorld(seedWithRoll(5));
            ASSERT_TRUE(MWMechanics::rechargeItem(blade, gem));
            const float directCharge = blade.getCellRef().getEnchantmentCharge();
            const Misc::Rng::Generator directGenerator = mWorld.mPrng;
            ASSERT_GT(directCharge, 0.f);

            fresh(blade, gem);
            seedWorld(seedWithRoll(5));
            EXPECT_TRUE(mRules->rechargeWithGem(mScope.make(blade), mScope.make(gem)));
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), directCharge);
            EXPECT_EQ(mWorld.mPrng, directGenerator);
        }

        // ---- the per-frame path: a non-virtual helper of the core that asks the rules only when a change is due ----

        // Rules that count what the core asks them, to show that the per-frame helper does not ask for nothing.
        class CountingRules final : public sdk::CraftingRules
        {
        public:
            int mRechargedChargeCalls = 0;

            std::unique_ptr<sdk::AlchemySession> beginAlchemy() override { return nullptr; }
            std::unique_ptr<sdk::EnchantingSession> beginEnchanting() override { return nullptr; }
            std::unique_ptr<sdk::RepairSession> beginRepair() override { return nullptr; }
            bool rechargeWithGem(ItemHandle, ItemHandle) override { return false; }
            int gemSoul(ItemHandle) override { return 0; }
            float rechargedCharge(float charge, float maxCharge, float duration) override
            {
                ++mRechargedChargeCalls;
                return std::min(charge + 2.f * duration, maxCharge);
            }
            bool knownAlchemyEffect(std::size_t, ItemHandle) override { return false; }
            std::vector<std::string> ingredientEffectNames(ItemHandle, float) override { return {}; }
            int apparatusSlotOf(ItemHandle) override { return -1; }
        };

        TEST_F(CraftingContractTest, theFrameHelperAsksTheRulesOnlyWhenAChangeIsDue)
        {
            auto counting = std::make_unique<CountingRules>();
            CountingRules* rules = counting.get();
            MWWorld::installCraftingRules(std::move(counting));
            const MWWorld::Ptr blade = give(rid("test_blade"), 1);

            blade.getCellRef().setEnchantmentCharge(-1.f); // never charged: nothing to recharge
            EXPECT_FALSE(MWWorld::rechargeItemOverTime(blade, 100.f, 1.f));
            blade.getCellRef().setEnchantmentCharge(100.f); // full
            EXPECT_FALSE(MWWorld::rechargeItemOverTime(blade, 100.f, 1.f));
            EXPECT_EQ(rules->mRechargedChargeCalls, 0) << "no change due: no call into the pack";

            blade.getCellRef().setEnchantmentCharge(10.f);
            EXPECT_TRUE(MWWorld::rechargeItemOverTime(blade, 100.f, 3.f));
            EXPECT_EQ(rules->mRechargedChargeCalls, 1);
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), 16.f) << "the rules' answer is stored";
        }

        TEST_F(CraftingContractTest, theFrameHelperWithMorrowindsRulesIsRechargeItem)
        {
            MWWorld::installCraftingRules(std::move(mRules));
            const MWWorld::Ptr blade = give(rid("test_blade"), 1);
            const MWWorld::Ptr other = give(rid("test_sword"), 1);
            for (const float start : { 0.f, 10.f, 99.9f })
            {
                blade.getCellRef().setEnchantmentCharge(start);
                other.getCellRef().setEnchantmentCharge(start);
                EXPECT_TRUE(MWWorld::rechargeItemOverTime(blade, 100.f, 20.f));
                EXPECT_TRUE(MWMechanics::rechargeItem(other, 100.f, 20.f));
                EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), other.getCellRef().getEnchantmentCharge());
            }
        }

        // Review 2c, L3: with no rules the helper stops recharging, says so once in the log, and leaves the charge
        // alone.
        TEST_F(CraftingContractTest, theFrameHelperDoesNothingWithoutRules)
        {
            MWWorld::installCraftingRules(nullptr);
            const MWWorld::Ptr blade = give(rid("test_blade"), 1);
            blade.getCellRef().setEnchantmentCharge(10.f);
            EXPECT_FALSE(MWWorld::rechargeItemOverTime(blade, 100.f, 20.f));
            EXPECT_FALSE(MWWorld::rechargeItemOverTime(blade, 100.f, 20.f)) << "a second call is as quiet and as inert";
            EXPECT_FLOAT_EQ(blade.getCellRef().getEnchantmentCharge(), 10.f);
        }

        TEST_F(CraftingContractTest, theSoulOfAGemIsTheCreaturesSoul)
        {
            const MWWorld::Ptr gem = give(rid("test_gem"), 1);
            gem.getCellRef().setSoul(rid("test_creature"));
            EXPECT_EQ(mRules->gemSoul(mScope.make(gem)), 300);
            EXPECT_EQ(mRules->gemSoul(kNoItem), 0);
        }
    }
}
