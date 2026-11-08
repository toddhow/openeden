// Task 0085, move 2, stage 2b: the crafting fixture (see craftingfixture.hpp).
#include "craftingfixture.hpp"

#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadweap.hpp>

#include "apps/openmw/mwclass/apparatus.hpp"
#include "apps/openmw/mwclass/armor.hpp"
#include "apps/openmw/mwclass/book.hpp"
#include "apps/openmw/mwclass/clothing.hpp"
#include "apps/openmw/mwclass/ingredient.hpp"
#include "apps/openmw/mwclass/misc.hpp"
#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwclass/potion.hpp"
#include "apps/openmw/mwclass/repair.hpp"
#include "apps/openmw/mwclass/weapon.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"

namespace CraftingTest
{
    void CraftingRulesTest::SetUp()
    {
        MWClass::Npc::registerSelf();
        MWClass::Potion::registerSelf();
        MWClass::Ingredient::registerSelf();
        MWClass::Apparatus::registerSelf();
        MWClass::Repair::registerSelf();
        MWClass::Miscellaneous::registerSelf();
        MWClass::Weapon::registerSelf();
        MWClass::Armor::registerSelf();
        MWClass::Clothing::registerSelf();
        MWClass::Book::registerSelf();

        for (const GmstValue& gmst : kFloatGmsts)
            addFloatGmst(gmst.mName, gmst.mFloat);
        for (const IntGmstValue& gmst : kIntGmsts)
            addIntGmst(gmst.mName, gmst.mInt);
        addStringGmst("sNotifyMessage51", "Your %s is used up.");

        // NpcStats and CreatureStats take their tables from the store's skill and attribute records.
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            ESM::Attribute attribute;
            attribute.mId = *ESM::Attribute::indexToRefId(i).getIf<ESM::Attribute::AttributeID>();
            attribute.mName = attribute.mId.getValue();
            mStore.insertStatic(attribute);
        }
        for (int i = 0; i < ESM::Skill::Length; ++i)
        {
            ESM::Skill skill;
            skill.blank();
            skill.mId = *ESM::Skill::indexToRefId(i).getIf<ESM::SkillId>();
            skill.mName = skill.mId.getValue();
            mStore.insertStatic(skill);
        }

        ESM::Race race;
        race.blank();
        race.mId = rid("testrace");
        mStore.insertStatic(race);

        mNpc.blank();
        mNpc.mId = rid("player");
        mNpc.mRace = race.mId;
        mNpc.mNpdtType = ESM::NPC::NPC_DEFAULT;
        for (auto& attribute : mNpc.mNpdt.mAttributes)
            attribute = 50;
        for (auto& skill : mNpc.mNpdt.mSkills)
            skill = 30;
        mNpc.mNpdt.mHealth = 100;
        mNpc.mNpdt.mMana = 100;
        mNpc.mNpdt.mFatigue = 200;
        mNpc.mNpdt.mLevel = 1;
        const ESM::NPC* stored = mStore.insertStatic(mNpc);

        mWorldModel = std::make_unique<MWWorld::WorldModel>(mStore, mReaders);

        mEnvironment = std::make_unique<MWBase::Environment>();
        mEnvironment->setESMStore(mStore);
        mEnvironment->setWorldModel(*mWorldModel);
        mEnvironment->setWorld(mWorld);
        mEnvironment->setWindowManager(mWindow);
        mEnvironment->setMechanicsManager(mMechanics);
        mEnvironment->setLuaManager(mLua);

        ESM::CellRef cellRef;
        cellRef.blank();
        cellRef.mRefID = stored->mId;
        cellRef.mRefNum = ESM::RefNum{ .mIndex = 1, .mContentFile = 0 };
        mPlayerRef = std::make_unique<MWWorld::LiveCellRef<ESM::NPC>>(cellRef, stored);
        mPlayer = MWWorld::Ptr(mPlayerRef.get());
        mWorld.mPlayer = mPlayer;
        mWorld.mStore = &mStore;
    }

    void CraftingRulesTest::TearDown()
    {
        mWorld.mPlayer = MWWorld::Ptr();
        mPlayer = MWWorld::Ptr();
        mPlayerRef.reset();
        mEnvironment.reset();
    }

    void CraftingRulesTest::addFloatGmst(const std::string& name, float value)
    {
        ESM::GameSetting gmst;
        gmst.blank();
        gmst.mId = rid(name);
        gmst.mValue = ESM::Variant(value);
        mStore.insertStatic(gmst);
    }

    void CraftingRulesTest::addIntGmst(const std::string& name, int value)
    {
        ESM::GameSetting gmst;
        gmst.blank();
        gmst.mId = rid(name);
        gmst.mValue = ESM::Variant(static_cast<int32_t>(value));
        mStore.insertStatic(gmst);
    }

    void CraftingRulesTest::addStringGmst(const std::string& name, const std::string& value)
    {
        ESM::GameSetting gmst;
        gmst.blank();
        gmst.mId = rid(name);
        gmst.mValue = ESM::Variant(value);
        mStore.insertStatic(gmst);
    }

    const ESM::MagicEffect* CraftingRulesTest::addEffect(const ESM::RefId& id, float baseCost, int flags)
    {
        ESM::MagicEffect effect;
        effect.blank();
        effect.mId = id;
        effect.mName = id.getRefIdString();
        effect.mData.mBaseCost = baseCost;
        effect.mData.mFlags = flags;
        return mStore.insertStatic(effect);
    }

    ESM::RefId CraftingRulesTest::addIngredient(
        const std::string& id, const std::vector<IngredientEffect>& effects, float weight, int value)
    {
        ESM::Ingredient ingredient;
        ingredient.blank();
        ingredient.mId = rid(id);
        ingredient.mName = id;
        ingredient.mData.mWeight = weight;
        ingredient.mData.mValue = value;
        // blank() fills the effect slots with a default effect; unused slots must be empty.
        for (size_t i = 0; i < 4; ++i)
        {
            ingredient.mData.mEffectID[i] = ESM::RefId();
            ingredient.mData.mSkills[i] = ESM::RefId();
            ingredient.mData.mAttributes[i] = ESM::RefId();
        }
        for (size_t i = 0; i < effects.size() && i < 4; ++i)
        {
            ingredient.mData.mEffectID[i] = effects[i].mEffect;
            ingredient.mData.mSkills[i] = effects[i].mSkill;
            ingredient.mData.mAttributes[i] = effects[i].mAttribute;
        }
        mStore.insertStatic(ingredient);
        return ingredient.mId;
    }

    ESM::RefId CraftingRulesTest::addApparatus(const std::string& id, int type, float quality)
    {
        ESM::Apparatus apparatus;
        apparatus.blank();
        apparatus.mId = rid(id);
        apparatus.mName = id;
        apparatus.mData.mType = type;
        apparatus.mData.mQuality = quality;
        apparatus.mData.mWeight = 1.f;
        apparatus.mData.mValue = 10;
        mStore.insertStatic(apparatus);
        return apparatus.mId;
    }

    MWWorld::Ptr CraftingRulesTest::give(const ESM::RefId& id, int count)
    {
        MWWorld::ContainerStore& store = mPlayer.getClass().getContainerStore(mPlayer);
        MWWorld::ContainerStoreIterator it = store.add(id, count);
        return *it;
    }

    MWMechanics::NpcStats& CraftingRulesTest::stats()
    {
        return mPlayer.getClass().getNpcStats(mPlayer);
    }
}
