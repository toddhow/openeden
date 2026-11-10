// Task 0098, move 8, stage 8b: characterization tests of the item classes (armor, weapon, clothing, book, ingredient,
// potion, miscellaneous, light, lockpick, probe, repair, apparatus). They run the REAL MWClass item classes
// (registered the way the game registers them) on records in an in-memory ESMStore, behind test doubles of the
// MWBase managers, and pin what every MWWorld::Class query answers for them, so that moving the classes into the
// Morrowind pack cannot change a result. Written against the OLD code (apps/openmw/mwclass) first; the file and its
// recordings (apps/openmw_tests/mwmechanics/data/items_*.golden.txt, taken from the old code) are not edited by the
// move.
//
// Two kinds of test: a "table" per class (every query of the class on several records, recorded from the old code),
// and a few tests with hand-derived expectations (the rules the table would otherwise only restate).
// Records and numbers are TEST INPUTS, not claimed to be Morrowind's data.
#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadweap.hpp>

#include "apps/openmw/mwclass/apparatus.hpp"
#include "apps/openmw/mwclass/armor.hpp"
#include "apps/openmw/mwclass/book.hpp"
#include "apps/openmw/mwclass/clothing.hpp"
#include "apps/openmw/mwclass/ingredient.hpp"
#include "apps/openmw/mwclass/light.hpp"
#include "apps/openmw/mwclass/lockpick.hpp"
#include "apps/openmw/mwclass/misc.hpp"
#include "apps/openmw/mwclass/potion.hpp"
#include "apps/openmw/mwclass/probe.hpp"
#include "apps/openmw/mwclass/repair.hpp"
#include "apps/openmw/mwclass/weapon.hpp"
#include "apps/openmw/mwgui/tooltips.hpp"
#include "apps/openmw/mwgui/widgets.hpp"
#include "apps/openmw/mwworld/actionalchemy.hpp"
#include "apps/openmw/mwworld/actionapply.hpp"
#include "apps/openmw/mwworld/actioneat.hpp"
#include "apps/openmw/mwworld/actionequip.hpp"
#include "apps/openmw/mwworld/actionread.hpp"
#include "apps/openmw/mwworld/actionrepair.hpp"
#include "apps/openmw/mwworld/actionsoulgem.hpp"
#include "apps/openmw/mwworld/actiontake.hpp"
#include "apps/openmw/mwworld/failedaction.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"
#include "apps/openmw/mwworld/nullaction.hpp"

#include "apps/openmw_tests/mwmechanics/rulesfixture.hpp"

namespace CraftingTest
{
    namespace
    {
        class ItemWindow : public FakeWindowManager
        {
        public:
            bool mInventoryAllowed = true;
            bool mFullHelp = false;
            bool mGuiMode = false;
            bool isAllowed(MWGui::GuiWindow wnd) const override { return mInventoryAllowed; }
            bool getFullHelp() const override { return mFullHelp; }
            bool isGuiMode() const override { return mGuiMode; }
        };

        class ItemMechanics : public RulesMechanics
        {
        public:
            bool mAttacking = false;
            bool mCasting = false;
            bool isAttackingOrSpell(const MWWorld::Ptr&) const override { return mAttacking; }
            bool isCastingSpell(const MWWorld::Ptr&) const override { return mCasting; }
            /// Items whose id ends in "_worn" were stolen from one owner (the full-help tooltip lists the owners).
            std::vector<std::pair<ESM::RefId, int>> getStolenItemOwners(const ESM::RefId& itemid) override
            {
                if (itemid.getRefIdString().ends_with("_worn"))
                    return { { ESM::RefId::stringRefId("t_owner"), 2 } };
                return {};
            }
        };

        template <class F>
        std::string attempt(F&& f)
        {
            try
            {
                return f();
            }
            catch (const std::exception& e)
            {
                return std::string("throws(") + e.what() + ")";
            }
        }

        std::string onOff(bool b)
        {
            return b ? "1" : "0";
        }

        std::string actionKind(const MWWorld::Action& action)
        {
            if (dynamic_cast<const MWWorld::ActionEquip*>(&action))
                return "equip";
            if (dynamic_cast<const MWWorld::ActionTake*>(&action))
                return "take";
            if (dynamic_cast<const MWWorld::ActionRead*>(&action))
                return "read";
            if (dynamic_cast<const MWWorld::ActionApply*>(&action))
                return "apply";
            if (dynamic_cast<const MWWorld::ActionEat*>(&action))
                return "eat";
            if (dynamic_cast<const MWWorld::ActionSoulgem*>(&action))
                return "soulgem";
            if (dynamic_cast<const MWWorld::ActionAlchemy*>(&action))
                return "alchemy";
            if (dynamic_cast<const MWWorld::ActionRepair*>(&action))
                return "repair";
            if (dynamic_cast<const MWWorld::FailedAction*>(&action))
                return "failed";
            if (dynamic_cast<const MWWorld::NullAction*>(&action))
                return "null";
            return "other";
        }

        class ItemClassTest : public RulesTest
        {
        protected:
            void SetUp() override
            {
                RulesTest::SetUp();
                MWClass::Light::registerSelf();

                // The GMSTs the equipment rules read (test inputs).
                addIntGmst("iHelmWeight", 5);
                addIntGmst("iCuirassWeight", 30);
                addIntGmst("iPauldronWeight", 10);
                addIntGmst("iGreavesWeight", 15);
                addIntGmst("iBootsWeight", 20);
                addIntGmst("iGauntletWeight", 5);
                addIntGmst("iShieldWeight", 15);
                addFloatGmst("fLightMaxMod", 0.6f);
                addFloatGmst("fMedMaxMod", 0.9f);
                addIntGmst("iBaseArmorSkill", 15);
                addStringGmst("sOneHanded", "One Handed");
                addStringGmst("sTwoHanded", "Two Handed");
                addFloatGmst("fCombatDistance", 128.f);
                addEffect(rid("fx_a"), 2.f, 0);
                addEffect(rid("fx_b"), 4.f, 0);
                addEffect(rid("fx_c"), 6.f, 0);
                addEffect(rid("fx_d"), 8.f, 0);

                ESM::Race beast;
                beast.blank();
                beast.mId = rid("beastrace");
                beast.mData.mFlags = ESM::Race::Beast;
                mStore.insertStatic(beast);

                mEnvironment->setMechanicsManager(mItemMechanics);
                mEnvironment->setWindowManager(mItemWindow);
            }

            void TearDown() override
            {
                mHolders.clear();
                mBeastNpc.reset();
                RulesTest::TearDown();
            }

            ESM::CellRef cellRefFor(const ESM::RefId& id)
            {
                ESM::CellRef cellRef;
                cellRef.blank();
                cellRef.mRefID = id;
                cellRef.mRefNum = ESM::RefNum{ .mIndex = mNextIndex++, .mContentFile = 0 };
                return cellRef;
            }

            /// Stores the record and makes a reference to it that lives until the test ends.
            template <class T>
            MWWorld::Ptr make(T record)
            {
                const T* stored = mStore.insertStatic(record);
                auto ref = std::make_shared<MWWorld::LiveCellRef<T>>(cellRefFor(stored->mId), stored);
                mHolders.push_back(ref);
                return MWWorld::Ptr(ref.get());
            }

            // ---- record builders (test inputs) --------------------------------------------------------------

            MWWorld::Ptr armor(const std::string& id, int type, float weight, int value, int health, int armorRating,
                const std::string& enchant = "", int enchantPoints = 0)
            {
                ESM::Armor r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Armor " + id;
                r.mModel = "a/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mData.mType = type;
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                r.mData.mHealth = health;
                r.mData.mArmor = armorRating;
                r.mData.mEnchant = enchantPoints;
                if (!enchant.empty())
                    r.mEnchant = rid(enchant);
                return make(r);
            }

            MWWorld::Ptr weapon(const std::string& id, int type, float weight, int value, int health,
                const std::string& enchant = "", int enchantPoints = 0)
            {
                ESM::Weapon r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Weapon " + id;
                r.mModel = "w/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mData.mType = type;
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                r.mData.mHealth = health;
                r.mData.mSpeed = 1.25f;
                r.mData.mReach = 1.5f;
                r.mData.mEnchant = enchantPoints;
                r.mData.mChop = { 2, 9 };
                r.mData.mSlash = { 3, 11 };
                r.mData.mThrust = { 1, 7 };
                r.mData.mFlags = 0;
                if (!enchant.empty())
                    r.mEnchant = rid(enchant);
                return make(r);
            }

            MWWorld::Ptr clothing(const std::string& id, int type, float weight, int value,
                const std::string& enchant = "", int enchantPoints = 0)
            {
                ESM::Clothing r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Clothing " + id;
                r.mModel = "c/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mData.mType = type;
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                r.mData.mEnchant = enchantPoints;
                if (!enchant.empty())
                    r.mEnchant = rid(enchant);
                return make(r);
            }

            MWWorld::Ptr book(const std::string& id, bool scroll, int skill, float weight, int value,
                const std::string& enchant = "", int enchantPoints = 0)
            {
                ESM::Book r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Book " + id;
                r.mModel = "b/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mText = "text of " + id;
                r.mData.mIsScroll = scroll ? 1 : 0;
                r.mData.mSkillId = skill;
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                r.mData.mEnchant = enchantPoints;
                if (!enchant.empty())
                    r.mEnchant = rid(enchant);
                return make(r);
            }

            MWWorld::Ptr ingredient(
                const std::string& id, const std::vector<std::string>& effects, float weight, int value)
            {
                ESM::Ingredient r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Ingredient " + id;
                r.mModel = "i/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                for (size_t i = 0; i < 4; ++i)
                {
                    r.mData.mEffectID[i] = ESM::RefId();
                    r.mData.mSkills[i] = ESM::RefId();
                    r.mData.mAttributes[i] = ESM::RefId();
                    if (i < effects.size())
                        r.mData.mEffectID[i] = rid(effects[i]);
                }
                return make(r);
            }

            MWWorld::Ptr potion(
                const std::string& id, const std::vector<std::string>& effects, float weight, int value, int flags = 0)
            {
                ESM::Potion r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Potion " + id;
                r.mModel = "p/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                r.mData.mFlags = flags;
                r.mEffects.mList.clear();
                uint32_t index = 0;
                for (const std::string& effect : effects)
                {
                    ESM::IndexedENAMstruct e{};
                    e.mData.mEffectID = rid(effect);
                    e.mData.mRange = 0;
                    e.mData.mArea = 0;
                    e.mData.mDuration = 30;
                    e.mData.mMagnMin = 5;
                    e.mData.mMagnMax = 10;
                    e.mIndex = index++;
                    r.mEffects.mList.push_back(e);
                }
                return make(r);
            }

            MWWorld::Ptr misc(const std::string& id, float weight, int value, int flags = 0)
            {
                ESM::Miscellaneous r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Misc " + id;
                r.mModel = "m/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                r.mData.mFlags = flags;
                return make(r);
            }

            MWWorld::Ptr light(const std::string& id, int flags, int time, int radius, float weight, int value,
                const std::string& model = "l/lamp.nif", const std::string& sound = "")
            {
                ESM::Light r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Light " + id;
                r.mModel = model;
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                if (!sound.empty())
                    r.mSound = rid(sound);
                r.mData.mFlags = flags;
                r.mData.mTime = time;
                r.mData.mRadius = radius;
                r.mData.mColor = 0xff8040c0;
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                return make(r);
            }

            MWWorld::Ptr lockpick(const std::string& id, float quality, int uses, float weight, int value)
            {
                ESM::Lockpick r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Lockpick " + id;
                r.mModel = "t/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mData.mQuality = quality;
                r.mData.mUses = uses;
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                return make(r);
            }

            MWWorld::Ptr probe(const std::string& id, float quality, int uses, float weight, int value)
            {
                ESM::Probe r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Probe " + id;
                r.mModel = "t/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mData.mQuality = quality;
                r.mData.mUses = uses;
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                return make(r);
            }

            MWWorld::Ptr repair(const std::string& id, float quality, int uses, float weight, int value)
            {
                ESM::Repair r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Repair " + id;
                r.mModel = "t/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mData.mQuality = quality;
                r.mData.mUses = uses;
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                return make(r);
            }

            MWWorld::Ptr apparatus(const std::string& id, int type, float quality, float weight, int value)
            {
                ESM::Apparatus r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Apparatus " + id;
                r.mModel = "q/" + id + ".nif";
                r.mIcon = "icons/" + id + ".dds";
                r.mScript = rid(id + "_script");
                r.mData.mType = type;
                r.mData.mQuality = quality;
                r.mData.mWeight = weight;
                r.mData.mValue = value;
                return make(r);
            }

            /// An NPC of the beast race (it cannot wear boots or full helms).
            MWWorld::Ptr beastNpc()
            {
                ESM::NPC record = mNpc;
                record.mId = rid("beast_npc");
                record.mRace = rid("beastrace");
                const ESM::NPC* stored = mStore.insertStatic(record);
                mBeastNpc = std::make_unique<MWWorld::LiveCellRef<ESM::NPC>>(cellRefFor(stored->mId), stored);
                return MWWorld::Ptr(mBeastNpc.get());
            }

            // ---- the table -----------------------------------------------------------------------------------

            static std::string toolTipText(const MWGui::ToolTipInfo& info)
            {
                std::ostringstream out;
                out << "caption=[" << info.caption << "] icon=[" << info.icon << "] imageSize=" << info.imageSize
                    << " enchant=[" << info.enchant.getRefIdString() << "] remaining=" << info.remainingEnchantCharge
                    << " isPotion=" << onOff(info.isPotion) << " isIngredient=" << onOff(info.isIngredient)
                    << " wordWrap=" << onOff(info.wordWrap) << "\n    text=[" << info.text << "]\n    extra=["
                    << info.extra << "]\n    effects=";
                for (const MWGui::Widgets::SpellEffectParams& e : info.effects)
                    out << "(" << e.mEffectID.getRefIdString() << "," << e.mSkill.getRefIdString() << ","
                        << e.mAttribute.getRefIdString() << ",known=" << onOff(e.mKnown) << ",mag=" << e.mMagnMin
                        << ".." << e.mMagnMax << ",dur=" << e.mDuration << ",range=" << e.mRange << ")";
                return out.str();
            }

            /// Every query of the item's class, as text. \a player is the NPC the equip query runs for.
            std::string describe(const MWWorld::Ptr& item, const MWWorld::Ptr& player)
            {
                const MWWorld::Class& c = item.getClass();
                MWWorld::ConstPtr constItem(item);
                std::ostringstream out;
                out << "[" << item.getCellRef().getRefId().getRefIdString() << "]\n";
                out << "  name=[" << attempt([&] { return std::string(c.getName(constItem)); }) << "] model=["
                    << attempt([&] { return std::string(c.getModel(constItem)); }) << "] script=["
                    << attempt([&] { return c.getScript(constItem).getRefIdString(); }) << "] icon=["
                    << attempt([&] { return c.getInventoryIcon(constItem); }) << "]\n";
                out << "  value=" << attempt([&] { return std::to_string(c.getValue(constItem)); })
                    << " weight=" << attempt([&] { return std::to_string(c.getWeight(constItem)); }) << " enchant=["
                    << attempt([&] { return c.getEnchantment(constItem).getRefIdString(); })
                    << "] enchPoints=" << attempt([&] { return std::to_string(c.getEnchantmentPoints(constItem)); })
                    << "\n";
                out << "  up=[" << attempt([&] { return c.getUpSoundId(constItem).getRefIdString(); }) << "] down=["
                    << attempt([&] { return c.getDownSoundId(constItem).getRefIdString(); }) << "] sound=["
                    << attempt([&] { return c.getSound(constItem).getRefIdString(); }) << "]\n";
                out << "  hasHealth=" << onOff(c.hasItemHealth(constItem))
                    << " maxHealth=" << attempt([&] { return std::to_string(c.getItemMaxHealth(constItem)); })
                    << " health=" << attempt([&] { return std::to_string(c.getItemHealth(constItem)); })
                    << " usage=" << attempt([&] { return std::to_string(c.getRemainingUsageTime(constItem)); }) << "\n";
                out << "  slots=" << attempt([&] {
                    std::ostringstream s;
                    const auto slots = c.getEquipmentSlots(constItem);
                    for (int slot : slots.first)
                        s << slot << ",";
                    s << "/" << onOff(slots.second);
                    return s.str();
                }) << " skill=["
                    << attempt([&] { return c.getEquipmentSkill(constItem, false).toString(); })
                    << "] isItem=" << onOff(c.isItem(constItem)) << " key=" << onOff(c.isKey(constItem))
                    << " gold=" << onOff(c.isGold(constItem)) << " soulgem=" << onOff(c.isSoulGem(constItem))
                    << " tooltip=" << onOff(c.hasToolTip(constItem))
                    << " inInventory=" << onOff(c.showsInInventory(constItem)) << " anim=" << onOff(c.useAnim())
                    << " telekinesis=" << onOff(c.allowTelekinesis(constItem)) << "\n";
                out << "  sell=";
                for (int bit = 0; bit < 16; ++bit)
                    out << onOff(c.canSell(constItem, 1 << bit));
                out << " sellAll=" << onOff(c.canSell(constItem, 0xffff)) << "\n";
                out << "  equip=" << attempt([&] {
                    const auto r = c.canBeEquipped(constItem, player);
                    return std::to_string(r.first) + ":" + std::string(r.second);
                });
                out << " use=" << attempt([&] { return actionKind(*c.use(item)); })
                    << " useForced=" << attempt([&] { return actionKind(*c.use(item, true)); });
                mItemWindow.mInventoryAllowed = true;
                out << " activate=" << attempt([&] { return actionKind(*c.activate(item, player)); });
                mItemWindow.mInventoryAllowed = false;
                out << " activateBlocked=" << attempt([&] { return actionKind(*c.activate(item, player)); });
                mItemWindow.mInventoryAllowed = true;
                out << "\n";
                out << "  armorRating=" << attempt([&] {
                    return std::to_string(c.getSkillAdjustedArmorRating(constItem, player, false));
                }) << "\n";
                for (int count : { 1, 3 })
                    for (bool full : { false, true })
                    {
                        mItemWindow.mFullHelp = full;
                        out << "  tip(count=" << count << ",full=" << onOff(full) << ") "
                            << attempt([&] { return toolTipText(c.getToolTipInfo(constItem, count)); }) << "\n";
                    }
                mItemWindow.mFullHelp = false;
                return out.str();
            }

            std::string table(const std::vector<MWWorld::Ptr>& items)
            {
                std::string result;
                for (const MWWorld::Ptr& item : items)
                    result += describe(item, mPlayer);
                return result;
            }

            ItemWindow mItemWindow;
            ItemMechanics mItemMechanics;
            std::vector<std::shared_ptr<void>> mHolders;
            std::unique_ptr<MWWorld::LiveCellRef<ESM::NPC>> mBeastNpc;
            uint32_t mNextIndex = 1000;
        };
    }

    // ============================================================================================================
    // The tables (recorded from the old code in apps/openmw/mwclass)
    // ============================================================================================================

    TEST_F(ItemClassTest, armor_table)
    {
        std::vector<MWWorld::Ptr> items;
        items.push_back(armor("t_a_cuirass_light", ESM::Armor::Cuirass, 9.f, 120, 400, 20));
        items.push_back(armor("t_a_cuirass_medium", ESM::Armor::Cuirass, 25.f, 480, 800, 40, "t_ench_a", 150));
        items.push_back(armor("t_a_cuirass_heavy", ESM::Armor::Cuirass, 31.f, 900, 1200, 60));
        items.push_back(armor("t_a_helm", ESM::Armor::Helmet, 2.f, 30, 100, 5));
        items.push_back(armor("t_a_lpauldron", ESM::Armor::LPauldron, 4.f, 30, 100, 5));
        items.push_back(armor("t_a_rpauldron", ESM::Armor::RPauldron, 12.f, 30, 100, 5));
        items.push_back(armor("t_a_greaves", ESM::Armor::Greaves, 8.f, 30, 100, 5));
        items.push_back(armor("t_a_boots", ESM::Armor::Boots, 30.f, 30, 100, 5));
        items.push_back(armor("t_a_lgauntlet", ESM::Armor::LGauntlet, 1.f, 30, 100, 5));
        items.push_back(armor("t_a_rgauntlet", ESM::Armor::RGauntlet, 6.f, 30, 100, 5));
        items.push_back(armor("t_a_shield", ESM::Armor::Shield, 13.f, 30, 100, 5));
        items.push_back(armor("t_a_lbracer", ESM::Armor::LBracer, 3.f, 30, 100, 5));
        items.push_back(armor("t_a_rbracer", ESM::Armor::RBracer, 6.f, 30, 100, 5));
        items.push_back(armor("t_a_weightless", ESM::Armor::Cuirass, 0.f, 10, 100, 17));
        items.push_back(armor("t_a_unknown_type", 99, 5.f, 10, 100, 17));
        MWWorld::Ptr broken = armor("t_a_broken", ESM::Armor::Cuirass, 9.f, 120, 400, 20);
        broken.getCellRef().setCharge(0);
        items.push_back(broken);
        MWWorld::Ptr worn = armor("t_a_worn", ESM::Armor::Helmet, 2.f, 30, 100, 5, "t_ench_b", 40);
        worn.getCellRef().setCharge(37);
        worn.getCellRef().setEnchantmentCharge(12.f);
        items.push_back(worn);
        expectRecording("items_armor.golden.txt", table(items));
    }

    TEST_F(ItemClassTest, weapon_table)
    {
        std::vector<MWWorld::Ptr> items;
        items.push_back(weapon("t_w_shortblade", ESM::Weapon::ShortBladeOneHand, 3.f, 50, 300));
        items.push_back(weapon("t_w_longblade", ESM::Weapon::LongBladeOneHand, 8.f, 200, 500, "t_ench_w", 220));
        items.push_back(weapon("t_w_longblade2h", ESM::Weapon::LongBladeTwoHand, 14.f, 400, 700));
        items.push_back(weapon("t_w_blunt", ESM::Weapon::BluntOneHand, 6.f, 90, 400));
        items.push_back(weapon("t_w_blunt2c", ESM::Weapon::BluntTwoClose, 18.f, 90, 400));
        items.push_back(weapon("t_w_blunt2w", ESM::Weapon::BluntTwoWide, 12.f, 90, 400));
        items.push_back(weapon("t_w_spear", ESM::Weapon::SpearTwoWide, 7.f, 90, 400));
        items.push_back(weapon("t_w_axe", ESM::Weapon::AxeOneHand, 9.f, 90, 400));
        items.push_back(weapon("t_w_axe2h", ESM::Weapon::AxeTwoHand, 19.f, 90, 400));
        items.push_back(weapon("t_w_bow", ESM::Weapon::MarksmanBow, 5.f, 90, 400));
        items.push_back(weapon("t_w_crossbow", ESM::Weapon::MarksmanCrossbow, 10.f, 90, 400));
        items.push_back(weapon("t_w_thrown", ESM::Weapon::MarksmanThrown, 0.2f, 5, 0));
        items.push_back(weapon("t_w_arrow", ESM::Weapon::Arrow, 0.1f, 1, 0));
        items.push_back(weapon("t_w_bolt", ESM::Weapon::Bolt, 0.1f, 1, 0));
        MWWorld::Ptr broken = weapon("t_w_broken", ESM::Weapon::LongBladeOneHand, 8.f, 200, 500);
        broken.getCellRef().setCharge(0);
        items.push_back(broken);
        MWWorld::Ptr worn = weapon("t_w_worn", ESM::Weapon::AxeOneHand, 9.f, 90, 400, "t_ench_x", 80);
        worn.getCellRef().setCharge(111);
        worn.getCellRef().setEnchantmentCharge(31.f);
        items.push_back(worn);
        expectRecording("items_weapon.golden.txt", table(items));
    }

    TEST_F(ItemClassTest, clothing_table)
    {
        std::vector<MWWorld::Ptr> items;
        items.push_back(clothing("t_c_pants", ESM::Clothing::Pants, 2.f, 20));
        items.push_back(clothing("t_c_shoes", ESM::Clothing::Shoes, 3.f, 25));
        items.push_back(clothing("t_c_shirt", ESM::Clothing::Shirt, 1.f, 15));
        items.push_back(clothing("t_c_belt", ESM::Clothing::Belt, 1.f, 10));
        items.push_back(clothing("t_c_robe", ESM::Clothing::Robe, 4.f, 200, "t_ench_r", 90));
        items.push_back(clothing("t_c_rglove", ESM::Clothing::RGlove, 1.f, 10));
        items.push_back(clothing("t_c_lglove", ESM::Clothing::LGlove, 1.f, 10));
        items.push_back(clothing("t_c_skirt", ESM::Clothing::Skirt, 2.f, 12));
        items.push_back(clothing("t_c_ring", ESM::Clothing::Ring, 0.1f, 400, "t_ench_ring", 60));
        items.push_back(clothing("t_c_amulet", ESM::Clothing::Amulet, 0.2f, 800));
        items.push_back(clothing("t_c_unknown", 99, 0.2f, 8));
        MWWorld::Ptr worn = clothing("t_c_worn", ESM::Clothing::Ring, 0.1f, 400, "t_ench_ring2", 60);
        worn.getCellRef().setEnchantmentCharge(44.f);
        items.push_back(worn);
        expectRecording("items_clothing.golden.txt", table(items));
    }

    TEST_F(ItemClassTest, book_table)
    {
        std::vector<MWWorld::Ptr> items;
        items.push_back(book("t_b_book", false, -1, 1.5f, 40));
        items.push_back(book("t_b_skillbook", false, 3, 1.5f, 150));
        items.push_back(book("t_b_scroll", true, -1, 0.5f, 60, "t_ench_s", 30));
        MWWorld::Ptr worn = book("t_b_scroll_charged", true, -1, 0.5f, 60, "t_ench_s2", 30);
        worn.getCellRef().setEnchantmentCharge(9.f);
        items.push_back(worn);
        expectRecording("items_book.golden.txt", table(items));
    }

    TEST_F(ItemClassTest, ingredient_table)
    {
        std::vector<MWWorld::Ptr> items;
        items.push_back(ingredient("t_i_four", { "fx_a", "fx_b", "fx_c", "fx_d" }, 0.5f, 7));
        items.push_back(ingredient("t_i_two", { "fx_a", "fx_b" }, 0.1f, 3));
        items.push_back(ingredient("t_i_none", {}, 0.1f, 1));
        expectRecording("items_ingredient.golden.txt", table(items));
    }

    TEST_F(ItemClassTest, potion_table)
    {
        std::vector<MWWorld::Ptr> items;
        items.push_back(potion("t_p_one", { "fx_a" }, 0.5f, 25));
        items.push_back(potion("t_p_three", { "fx_a", "fx_b", "fx_c" }, 0.4f, 90, 1));
        items.push_back(potion("t_p_none", {}, 0.5f, 2));
        expectRecording("items_potion.golden.txt", table(items));
    }

    TEST_F(ItemClassTest, miscellaneous_table)
    {
        std::vector<MWWorld::Ptr> items;
        items.push_back(misc("t_m_junk", 2.f, 11));
        items.push_back(misc("t_m_key", 0.5f, 5, ESM::Miscellaneous::Key));
        items.push_back(misc("gold_001", 0.f, 1));
        items.push_back(misc("gold_100", 0.f, 100));
        items.push_back(misc("misc_soulgem_petty", 0.2f, 80));
        MWWorld::Ptr gem = misc("misc_soulgem_grand", 0.2f, 800);
        ESM::Creature creature;
        creature.blank();
        creature.mId = rid("t_soul_creature");
        creature.mData.mSoul = 60;
        mStore.insertStatic(creature);
        gem.getCellRef().setSoul(rid("t_soul_creature"));
        items.push_back(gem);
        MWWorld::Ptr stack = misc("t_m_stack", 1.f, 4);
        stack.getCellRef().setCount(5);
        items.push_back(stack);
        MWWorld::Ptr goldStack = misc("gold_025", 0.f, 25);
        goldStack.getCellRef().setCount(7);
        items.push_back(goldStack);
        expectRecording("items_misc.golden.txt", table(items));
    }

    TEST_F(ItemClassTest, light_table)
    {
        std::vector<MWWorld::Ptr> items;
        items.push_back(light("t_l_torch", ESM::Light::Carry | ESM::Light::Dynamic | ESM::Light::Flicker, 300, 120, 1.f,
            10, "l/torch.nif", "t_torch_sound"));
        items.push_back(light("t_l_lantern_off", ESM::Light::Carry | ESM::Light::OffDefault, 600, 150, 2.f, 40));
        items.push_back(light("t_l_fixed", ESM::Light::Dynamic | ESM::Light::Fire, 100, 200, 5.f, 3));
        items.push_back(light("t_l_nomodel", ESM::Light::Carry, 100, 200, 5.f, 3, ""));
        MWWorld::Ptr burnt = light("t_l_burnt", ESM::Light::Carry, 300, 120, 1.f, 10);
        burnt.getCellRef().setChargeFloat(41.5f);
        items.push_back(burnt);
        expectRecording("items_light.golden.txt", table(items));
    }

    TEST_F(ItemClassTest, tools_table)
    {
        std::vector<MWWorld::Ptr> items;
        items.push_back(lockpick("t_t_lockpick", 1.25f, 25, 1.f, 80));
        items.push_back(lockpick("t_t_lockpick_zero", 0.f, 0, 0.f, 0));
        items.push_back(probe("t_t_probe", 0.75f, 20, 1.f, 70));
        items.push_back(repair("t_t_hammer", 1.5f, 30, 2.f, 60));
        items.push_back(repair("t_t_hammer_zero", 0.f, 0, 2.f, 60));
        MWWorld::Ptr worn = lockpick("t_t_lockpick_worn", 1.25f, 25, 1.f, 80);
        worn.getCellRef().setCharge(3);
        items.push_back(worn);
        items.push_back(apparatus("t_t_mortar", ESM::Apparatus::MortarPestle, 1.5f, 3.f, 100));
        items.push_back(apparatus("t_t_alembic", ESM::Apparatus::Alembic, 2.f, 3.f, 100));
        items.push_back(apparatus("t_t_calcinator", ESM::Apparatus::Calcinator, 0.5f, 3.f, 100));
        items.push_back(apparatus("t_t_retort", ESM::Apparatus::Retort, 1.f, 3.f, 100));
        expectRecording("items_tools.golden.txt", table(items));
    }

    TEST_F(ItemClassTest, the_equip_query_for_a_beast_race_npc)
    {
        const MWWorld::Ptr beast = beastNpc();
        std::string result;
        std::vector<MWWorld::Ptr> items;
        ESM::Armor helm;
        helm.blank();
        helm.mId = rid("t_beast_helm");
        helm.mData.mType = ESM::Armor::Helmet;
        helm.mData.mHealth = 100;
        helm.mParts.mParts.push_back({ ESM::PRT_Head, rid(""), rid("") });
        items.push_back(make(helm));
        ESM::Armor boots;
        boots.blank();
        boots.mId = rid("t_beast_boots");
        boots.mData.mType = ESM::Armor::Boots;
        boots.mData.mHealth = 100;
        boots.mParts.mParts.push_back({ ESM::PRT_LFoot, rid(""), rid("") });
        items.push_back(make(boots));
        items.push_back(armor("t_beast_cuirass", ESM::Armor::Cuirass, 9.f, 1, 100, 1));
        ESM::Clothing shoes;
        shoes.blank();
        shoes.mId = rid("t_beast_shoes");
        shoes.mData.mType = ESM::Clothing::Shoes;
        shoes.mParts.mParts.push_back({ ESM::PRT_RFoot, rid(""), rid("") });
        items.push_back(make(shoes));
        items.push_back(clothing("t_beast_robe", ESM::Clothing::Robe, 1.f, 1));
        for (const MWWorld::Ptr& item : items)
        {
            const auto r = item.getClass().canBeEquipped(item, beast);
            result += item.getCellRef().getRefId().getRefIdString() + " -> " + std::to_string(r.first) + " ["
                + std::string(r.second) + "]\n";
        }
        expectRecording("items_beast_equip.golden.txt", result);
    }

    // ============================================================================================================
    // Hand-derived expectations (the rules, not only a recording)
    // ============================================================================================================

    TEST_F(ItemClassTest, armor_weight_class_follows_the_gmst_limits)
    {
        // cuirass: iCuirassWeight 30, light up to 30 * 0.6 = 18 (+ 0.0005), medium up to 27
        const auto skillOf = [&](float weight) {
            MWWorld::Ptr p = armor("t_x" + std::to_string(weight), ESM::Armor::Cuirass, weight, 1, 100, 1);
            return p.getClass().getEquipmentSkill(p, false);
        };
        EXPECT_EQ(skillOf(18.f), ESM::Skill::LightArmor);
        EXPECT_EQ(skillOf(18.1f), ESM::Skill::MediumArmor);
        EXPECT_EQ(skillOf(27.f), ESM::Skill::MediumArmor);
        EXPECT_EQ(skillOf(27.1f), ESM::Skill::HeavyArmor);
    }

    TEST_F(ItemClassTest, armor_sounds_follow_the_weight_class)
    {
        MWWorld::Ptr light = armor("t_s1", ESM::Armor::Cuirass, 10.f, 1, 100, 1);
        MWWorld::Ptr heavy = armor("t_s2", ESM::Armor::Cuirass, 29.f, 1, 100, 1);
        EXPECT_EQ(light.getClass().getUpSoundId(light).getRefIdString(), "Item Armor Light Up");
        EXPECT_EQ(heavy.getClass().getDownSoundId(heavy).getRefIdString(), "Item Armor Heavy Down");
    }

    TEST_F(ItemClassTest, armor_rating_scales_with_the_skill_unless_weightless)
    {
        // player skills are 30, iBaseArmorSkill is 15: a weighted armor of 20 rates 20 * 30 / 15 = 40
        MWWorld::Ptr weighted = armor("t_r1", ESM::Armor::Cuirass, 10.f, 1, 100, 20);
        MWWorld::Ptr weightless = armor("t_r2", ESM::Armor::Cuirass, 0.f, 1, 100, 20);
        EXPECT_FLOAT_EQ(weighted.getClass().getSkillAdjustedArmorRating(weighted, mPlayer, false), 40.f);
        EXPECT_FLOAT_EQ(weightless.getClass().getSkillAdjustedArmorRating(weightless, mPlayer, false), 20.f);
    }

    TEST_F(ItemClassTest, armor_slots_and_equip_results)
    {
        MWWorld::Ptr shield = armor("t_q1", ESM::Armor::Shield, 10.f, 1, 100, 1);
        MWWorld::Ptr bracer = armor("t_q2", ESM::Armor::LBracer, 3.f, 1, 100, 1);
        MWWorld::Ptr odd = armor("t_q3", 99, 3.f, 1, 100, 1);
        MWWorld::Ptr broken = armor("t_q4", ESM::Armor::Cuirass, 3.f, 1, 100, 1);
        broken.getCellRef().setCharge(0);
        EXPECT_EQ(shield.getClass().getEquipmentSlots(shield).first,
            std::vector<int>{ MWWorld::InventoryStore::Slot_CarriedLeft });
        EXPECT_EQ(bracer.getClass().getEquipmentSlots(bracer).first,
            std::vector<int>{ MWWorld::InventoryStore::Slot_LeftGauntlet });
        EXPECT_EQ(shield.getClass().canBeEquipped(shield, mPlayer).first, 1);
        EXPECT_EQ(odd.getClass().canBeEquipped(odd, mPlayer).first, 0);
        const auto refused = broken.getClass().canBeEquipped(broken, mPlayer);
        EXPECT_EQ(refused.first, 0);
        EXPECT_EQ(refused.second, "#{sInventoryMessage1}");
    }

    TEST_F(ItemClassTest, armor_beast_races_cannot_wear_boots_or_full_helms)
    {
        const MWWorld::Ptr beast = beastNpc();
        ESM::Armor helm;
        helm.blank();
        helm.mId = rid("t_h1");
        helm.mData.mType = ESM::Armor::Helmet;
        helm.mData.mHealth = 100;
        helm.mParts.mParts.push_back({ ESM::PRT_Head, rid(""), rid("") });
        MWWorld::Ptr fullHelm = make(helm);
        ESM::Armor boots;
        boots.blank();
        boots.mId = rid("t_h2");
        boots.mData.mType = ESM::Armor::Boots;
        boots.mData.mHealth = 100;
        boots.mParts.mParts.push_back({ ESM::PRT_RFoot, rid(""), rid("") });
        MWWorld::Ptr shoes = make(boots);
        EXPECT_EQ(fullHelm.getClass().canBeEquipped(fullHelm, beast).second, "#{sNotifyMessage13}");
        EXPECT_EQ(shoes.getClass().canBeEquipped(shoes, beast).second, "#{sNotifyMessage14}");
        EXPECT_EQ(fullHelm.getClass().canBeEquipped(fullHelm, mPlayer).first, 1);
        EXPECT_EQ(shoes.getClass().canBeEquipped(shoes, mPlayer).first, 1);
    }

    TEST_F(ItemClassTest, weapon_equip_results_and_the_attack_refusal)
    {
        MWWorld::Ptr oneHanded = weapon("t_e1", ESM::Weapon::LongBladeOneHand, 8.f, 1, 500);
        MWWorld::Ptr twoHanded = weapon("t_e2", ESM::Weapon::AxeTwoHand, 19.f, 1, 500);
        MWWorld::Ptr broken = weapon("t_e3", ESM::Weapon::LongBladeOneHand, 8.f, 1, 500);
        broken.getCellRef().setCharge(0);
        MWWorld::Ptr arrow = weapon("t_e4", ESM::Weapon::Arrow, 0.1f, 1, 0);
        EXPECT_EQ(oneHanded.getClass().canBeEquipped(oneHanded, mPlayer).first, 1);
        EXPECT_EQ(twoHanded.getClass().canBeEquipped(twoHanded, mPlayer).first, 2);
        EXPECT_EQ(broken.getClass().canBeEquipped(broken, mPlayer).second, "#{sInventoryMessage1}");
        // an arrow has no health: the health rule does not apply
        EXPECT_EQ(arrow.getClass().canBeEquipped(arrow, mPlayer).first, 1);
        EXPECT_TRUE(oneHanded.getClass().hasItemHealth(oneHanded));
        EXPECT_FALSE(arrow.getClass().hasItemHealth(arrow));
        EXPECT_EQ(oneHanded.getClass().getItemMaxHealth(oneHanded), 500);
    }

    TEST_F(ItemClassTest, weapon_sounds_and_skill_come_from_the_weapon_type)
    {
        MWWorld::Ptr blade = weapon("t_k1", ESM::Weapon::LongBladeOneHand, 8.f, 1, 500);
        MWWorld::Ptr bow = weapon("t_k2", ESM::Weapon::MarksmanBow, 5.f, 1, 500);
        EXPECT_EQ(blade.getClass().getEquipmentSkill(blade, false), ESM::Skill::LongBlade);
        EXPECT_EQ(bow.getClass().getEquipmentSkill(bow, false), ESM::Skill::Marksman);
        EXPECT_EQ(blade.getClass().getUpSoundId(blade).getRefIdString(), "Item Weapon Longblade Up");
        EXPECT_EQ(bow.getClass().getDownSoundId(bow).getRefIdString(), "Item Weapon Bow Down");
    }

    TEST_F(ItemClassTest, clothing_slots_and_the_ring_slots)
    {
        MWWorld::Ptr ring = clothing("t_cs1", ESM::Clothing::Ring, 0.1f, 1);
        MWWorld::Ptr robe = clothing("t_cs2", ESM::Clothing::Robe, 1.f, 1);
        const std::vector<int> rings{ MWWorld::InventoryStore::Slot_LeftRing, MWWorld::InventoryStore::Slot_RightRing };
        EXPECT_EQ(ring.getClass().getEquipmentSlots(ring).first, rings);
        EXPECT_EQ(
            robe.getClass().getEquipmentSlots(robe).first, std::vector<int>{ MWWorld::InventoryStore::Slot_Robe });
    }

    TEST_F(ItemClassTest, light_carry_flag_decides_item_slot_and_activation)
    {
        MWWorld::Ptr carry = light("t_lc1", ESM::Light::Carry, 300, 100, 1.f, 1);
        MWWorld::Ptr fixed = light("t_lc2", ESM::Light::Dynamic, 300, 100, 1.f, 1);
        EXPECT_TRUE(carry.getClass().isItem(carry));
        EXPECT_FALSE(fixed.getClass().isItem(fixed));
        EXPECT_EQ(carry.getClass().getEquipmentSlots(carry).first,
            std::vector<int>{ MWWorld::InventoryStore::Slot_CarriedLeft });
        EXPECT_TRUE(fixed.getClass().getEquipmentSlots(fixed).first.empty());
        EXPECT_EQ(carry.getClass().canBeEquipped(carry, mPlayer).first, 1);
        EXPECT_EQ(fixed.getClass().canBeEquipped(fixed, mPlayer).first, 0);
        EXPECT_EQ(actionKind(*carry.getClass().activate(carry, mPlayer)), "take");
        EXPECT_EQ(actionKind(*fixed.getClass().activate(fixed, mPlayer)), "failed");
        mItemWindow.mInventoryAllowed = false;
        EXPECT_EQ(actionKind(*carry.getClass().activate(carry, mPlayer)), "null");
    }

    TEST_F(ItemClassTest, light_remaining_time_is_the_record_time_until_charged)
    {
        MWWorld::Ptr torch = light("t_lt1", ESM::Light::Carry, 300, 100, 1.f, 1);
        EXPECT_FLOAT_EQ(torch.getClass().getRemainingUsageTime(torch), 300.f);
        torch.getClass().setRemainingUsageTime(torch, 42.5f);
        EXPECT_FLOAT_EQ(torch.getClass().getRemainingUsageTime(torch), 42.5f);
    }

    TEST_F(ItemClassTest, light_without_a_model_has_no_name)
    {
        MWWorld::Ptr unnamed = light("t_ln1", ESM::Light::Carry, 300, 100, 1.f, 1, "");
        MWWorld::Ptr named = light("t_ln2", ESM::Light::Carry, 300, 100, 1.f, 1);
        EXPECT_TRUE(unnamed.getClass().getName(unnamed).empty());
        EXPECT_EQ(named.getClass().getName(named), "Light t_ln2");
    }

    TEST_F(ItemClassTest, misc_keys_gold_and_soul_gems)
    {
        MWWorld::Ptr key = misc("t_mk1", 0.5f, 5, ESM::Miscellaneous::Key);
        MWWorld::Ptr gold = misc("gold_001", 0.f, 1);
        MWWorld::Ptr gem = misc("misc_soulgem_common", 0.2f, 100);
        MWWorld::Ptr junk = misc("t_mk2", 1.f, 5);
        EXPECT_TRUE(key.getClass().isKey(key));
        EXPECT_FALSE(junk.getClass().isKey(junk));
        EXPECT_TRUE(gold.getClass().isGold(gold));
        EXPECT_TRUE(gem.getClass().isSoulGem(gem));
        EXPECT_EQ(actionKind(*gem.getClass().use(gem)), "soulgem");
        EXPECT_EQ(actionKind(*junk.getClass().use(junk)), "null");
        EXPECT_EQ(gold.getClass().getUpSoundId(gold).getRefIdString(), "Item Gold Up");
        EXPECT_EQ(junk.getClass().getDownSoundId(junk).getRefIdString(), "Item Misc Down");
        // keys and gold cannot be sold; junk can to a merchant of miscellaneous items
        EXPECT_FALSE(key.getClass().canSell(key, ESM::NPC::Misc));
        EXPECT_FALSE(gold.getClass().canSell(gold, ESM::NPC::Misc));
        EXPECT_TRUE(junk.getClass().canSell(junk, ESM::NPC::Misc));
        EXPECT_FALSE(junk.getClass().canSell(junk, ESM::NPC::Weapon));
    }

    TEST_F(ItemClassTest, misc_value_of_a_filled_soul_gem_is_multiplied_by_the_soul)
    {
        ESM::Creature creature;
        creature.blank();
        creature.mId = rid("t_soul");
        creature.mData.mSoul = 7;
        mStore.insertStatic(creature);
        MWWorld::Ptr gem = misc("misc_soulgem_x", 0.2f, 10);
        EXPECT_EQ(gem.getClass().getValue(gem), 10);
        gem.getCellRef().setSoul(rid("t_soul"));
        EXPECT_EQ(gem.getClass().getValue(gem), 10 * 7);
    }

    TEST_F(ItemClassTest, magic_item_merchants_buy_enchanted_equipment)
    {
        MWWorld::Ptr plain = armor("t_ms1", ESM::Armor::Cuirass, 9.f, 1, 100, 1);
        MWWorld::Ptr magic = armor("t_ms2", ESM::Armor::Cuirass, 9.f, 1, 100, 1, "t_ench_m", 10);
        EXPECT_FALSE(plain.getClass().canSell(plain, ESM::NPC::MagicItems));
        EXPECT_TRUE(magic.getClass().canSell(magic, ESM::NPC::MagicItems));
        EXPECT_TRUE(plain.getClass().canSell(plain, ESM::NPC::Armor));
    }

    TEST_F(ItemClassTest, tool_equip_is_refused_in_the_gui_during_an_attack)
    {
        MWWorld::Ptr pick = lockpick("t_tp1", 1.f, 10, 1.f, 1);
        MWWorld::Ptr prb = probe("t_tp2", 1.f, 10, 1.f, 1);
        EXPECT_EQ(pick.getClass().canBeEquipped(pick, mPlayer).first, 1);
        mItemMechanics.mAttacking = true;
        EXPECT_EQ(pick.getClass().canBeEquipped(pick, mPlayer).first, 1);
        mItemWindow.mGuiMode = true;
        EXPECT_EQ(pick.getClass().canBeEquipped(pick, mPlayer).second, "#{sCantEquipWeapWarning}");
        EXPECT_EQ(prb.getClass().canBeEquipped(prb, mPlayer).second, "#{sCantEquipWeapWarning}");
        mItemMechanics.mCasting = true;
        EXPECT_EQ(pick.getClass().canBeEquipped(pick, mPlayer).first, 1);
    }

    TEST_F(ItemClassTest, tools_health_is_their_uses)
    {
        MWWorld::Ptr pick = lockpick("t_th1", 1.f, 25, 1.f, 1);
        MWWorld::Ptr prb = probe("t_th2", 1.f, 20, 1.f, 1);
        MWWorld::Ptr hammer = repair("t_th3", 1.f, 30, 1.f, 1);
        EXPECT_EQ(pick.getClass().getItemMaxHealth(pick), 25);
        EXPECT_EQ(prb.getClass().getItemMaxHealth(prb), 20);
        EXPECT_EQ(hammer.getClass().getItemMaxHealth(hammer), 30);
        EXPECT_TRUE(pick.getClass().hasItemHealth(pick));
        EXPECT_EQ(pick.getClass().getItemHealth(pick), 25);
        pick.getCellRef().setCharge(4);
        EXPECT_EQ(pick.getClass().getItemHealth(pick), 4);
    }

    TEST_F(ItemClassTest, use_actions_of_the_consumables_and_tools)
    {
        MWWorld::Ptr drink = potion("t_u1", { "fx_a" }, 0.5f, 1);
        MWWorld::Ptr herb = ingredient("t_u2", { "fx_a" }, 0.5f, 1);
        MWWorld::Ptr nothing = ingredient("t_u3", {}, 0.5f, 1);
        MWWorld::Ptr hammer = repair("t_u4", 1.f, 10, 1.f, 1);
        MWWorld::Ptr still = apparatus("t_u5", ESM::Apparatus::Alembic, 1.f, 1.f, 1);
        MWWorld::Ptr tome = book("t_u6", false, -1, 1.f, 1);
        EXPECT_EQ(actionKind(*drink.getClass().use(drink)), "apply");
        EXPECT_EQ(actionKind(*herb.getClass().use(herb)), "eat");
        EXPECT_EQ(actionKind(*nothing.getClass().use(nothing)), "null");
        EXPECT_EQ(actionKind(*hammer.getClass().use(hammer)), "repair");
        EXPECT_EQ(actionKind(*still.getClass().use(still)), "alchemy");
        EXPECT_EQ(actionKind(*tome.getClass().activate(tome, mPlayer)), "read");
    }

    TEST_F(ItemClassTest, a_werewolf_cannot_read_or_take)
    {
        MWWorld::Ptr tome = book("t_ww1", false, -1, 1.f, 1);
        MWWorld::Ptr coin = misc("t_ww2", 1.f, 1);
        mPlayer.getClass().getNpcStats(mPlayer).setWerewolf(true);
        EXPECT_EQ(actionKind(*tome.getClass().activate(tome, mPlayer)), "failed");
        EXPECT_EQ(actionKind(*coin.getClass().activate(coin, mPlayer)), "failed");
        mPlayer.getClass().getNpcStats(mPlayer).setWerewolf(false);
        EXPECT_EQ(actionKind(*tome.getClass().activate(tome, mPlayer)), "read");
        EXPECT_EQ(actionKind(*coin.getClass().activate(coin, mPlayer)), "take");
    }

    TEST_F(ItemClassTest, scrolls_are_read_and_enchanted_ones_report_their_enchantment)
    {
        MWWorld::Ptr scroll = book("t_sc1", true, -1, 0.5f, 5, "t_ench_sc", 12);
        EXPECT_EQ(scroll.getClass().getEnchantment(scroll).getRefIdString(), "t_ench_sc");
        EXPECT_EQ(scroll.getClass().getEnchantmentPoints(scroll), 12);
    }

    TEST_F(ItemClassTest, ingredient_effects_are_known_by_alchemy_skill)
    {
        // fWortChanceValue is 15; the player's Alchemy is 30: effects 1 and 2 are known, 3 and 4 are not
        MWWorld::Ptr herb = ingredient("t_ik1", { "fx_a", "fx_b", "fx_c", "fx_d" }, 0.5f, 1);
        const MWGui::ToolTipInfo info = herb.getClass().getToolTipInfo(herb, 1);
        ASSERT_EQ(info.effects.size(), 4u);
        EXPECT_TRUE(info.effects[0].mKnown);
        EXPECT_TRUE(info.effects[1].mKnown);
        EXPECT_FALSE(info.effects[2].mKnown);
        EXPECT_FALSE(info.effects[3].mKnown);
        EXPECT_TRUE(info.isIngredient);
    }
}
