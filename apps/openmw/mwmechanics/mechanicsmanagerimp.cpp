#include "mechanicsmanagerimp.hpp"

#include <cassert>

#include <components/debug/debuglog.hpp>
#include <components/misc/rng.hpp>

#include <components/esm/refid.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/stolenitems.hpp>
#include <components/esmbridge/records.hpp>

#include <components/mwanimation/position.hpp>
#include <components/vsgutil/updatethreads.hpp>

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/globals.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"

#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/rulesregistry.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwsound/constants.hpp"

#include "actor.hpp"
#include "actors.hpp"
#include "actorutil.hpp"
#include "aicombat.hpp"
#include "aipursue.hpp"
#include "autocalcspell.hpp"
#include "combat.hpp"
#include "justicerules.hpp"
#include "npcstats.hpp"
#include "socialrules.hpp"
#include "spellutil.hpp"

namespace
{

    float getFightDispositionBias(float disposition)
    {
        static const float fFightDispMult = MWBase::Environment::get()
                                                .getESMStore()
                                                ->get<ESM::GameSetting>()
                                                .find("fFightDispMult")
                                                ->mValue.getFloat();
        return ((50.f - disposition) * fFightDispMult);
    }
}

namespace MWMechanics
{
    void MechanicsManager::setUpdateThreads(vsg::ref_ptr<vsgUtil::UpdateThreads> threads)
    {
        mActors.setUpdateThreads(threads);
    }

    void MechanicsManager::buildPlayer()
    {
        MWWorld::Ptr ptr = getPlayer();

        MWMechanics::CreatureStats& creatureStats = ptr.getClass().getCreatureStats(ptr);
        MWMechanics::NpcStats& npcStats = ptr.getClass().getNpcStats(ptr);

        npcStats.recalculateMagicka();

        const ESM::NPC* player = ptr.get<ESM::NPC>()->mBase;

        // reset
        creatureStats.setLevel(player->mNpdt.mLevel);
        creatureStats.getSpells().clear(true);
        creatureStats.getActiveSpells().clear(ptr);

        for (size_t i = 0; i < player->mNpdt.mSkills.size(); ++i)
            npcStats.getSkill(ESM::Skill::indexToRefId(static_cast<int>(i))).setBase(player->mNpdt.mSkills[i]);

        for (size_t i = 0; i < player->mNpdt.mAttributes.size(); ++i)
            npcStats.setAttribute(ESM::Attribute::indexToRefId(static_cast<int>(i)), player->mNpdt.mSkills[i]);

        const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();

        // race
        if (mRaceSelected)
        {
            const ESM::Race* race = esmStore.get<ESM::Race>().find(player->mRace);

            bool male = (player->mFlags & ESM::NPC::Female) == 0;

            for (const ESM::Attribute& attribute : esmStore.get<ESM::Attribute>())
                creatureStats.setAttribute(
                    attribute.mId, static_cast<float>(race->mData.getAttribute(attribute.mId, male)));

            for (const ESM::Skill& skill : esmStore.get<ESM::Skill>())
            {
                int bonus = 0;
                int index = ESM::Skill::refIdToIndex(skill.mId);
                auto bonusIt = std::find_if(race->mData.mBonus.begin(), race->mData.mBonus.end(),
                    [&](const auto& v) { return v.mSkill == index; });
                if (bonusIt != race->mData.mBonus.end())
                    bonus = bonusIt->mBonus;

                npcStats.getSkill(skill.mId).setBase(5.f + bonus);
            }

            for (const ESM::RefId& power : race->mPowers.mList)
            {
                creatureStats.getSpells().add(power);
            }
        }

        // birthsign
        const ESM::RefId& signId = MWBase::Environment::get().getWorld()->getPlayer().getBirthSign();

        if (!signId.empty())
        {
            const ESM::BirthSign* sign = esmStore.get<ESM::BirthSign>().find(signId);

            for (const ESM::RefId& power : sign->mPowers.mList)
            {
                creatureStats.getSpells().add(power);
            }
        }

        // class
        if (mClassSelected)
        {
            const ESM::Class* playerClass = esmStore.get<ESM::Class>().find(player->mClass);

            for (int attribute : playerClass->mData.mAttribute)
            {
                ESM::RefId id = ESM::Attribute::indexToRefId(attribute);
                if (!id.empty())
                    creatureStats.setAttribute(id, creatureStats.getAttribute(id).getBase() + 10);
            }

            for (int i = 0; i < 2; ++i)
            {
                int bonus = i == 0 ? 10 : 25;

                for (const auto& skills : playerClass->mData.mSkills)
                {
                    ESM::RefId id = ESM::Skill::indexToRefId(skills[i]);
                    if (!id.empty())
                        npcStats.getSkill(id).setBase(npcStats.getSkill(id).getBase() + bonus);
                }
            }

            for (const ESM::Skill& skill : esmStore.get<ESM::Skill>())
            {
                if (skill.mData.mSpecialization == playerClass->mData.mSpecialization)
                    npcStats.getSkill(skill.mId).setBase(npcStats.getSkill(skill.mId).getBase() + 5);
            }
        }

        // F_PCStart spells
        const ESM::Race* race = nullptr;
        if (mRaceSelected)
            race = esmStore.get<ESM::Race>().find(player->mRace);

        npcStats.updateHealth();

        std::vector<ESM::RefId> selectedSpells
            = autoCalcPlayerSpells(npcStats.getSkills(), npcStats.getAttributes(), race);

        for (const ESM::RefId& spell : selectedSpells)
            creatureStats.getSpells().add(spell);

        // forced update and current value adjustments
        mActors.updateActor(ptr, 0);

        for (int i = 0; i < 3; ++i)
        {
            DynamicStat<float> stat = creatureStats.getDynamic(i);
            stat.setCurrent(stat.getModified());
            creatureStats.setDynamic(i, stat);
        }

        // auto-equip again. we need this for when the race is changed to a beast race and shoes are no longer
        // equippable
        MWWorld::InventoryStore& invStore = ptr.getClass().getInventoryStore(ptr);
        for (int i = 0; i < MWWorld::InventoryStore::Slots; ++i)
            invStore.unequipAll();
        invStore.autoEquip();
    }

    MechanicsManager::MechanicsManager()
        : mUpdatePlayer(true)
        , mClassSelected(false)
        , mRaceSelected(false)
        , mAI(true)
    {
        // buildPlayer no longer here, needs to be done explicitly after all subsystems are up and running
    }

    void MechanicsManager::add(const MWWorld::Ptr& ptr)
    {
        if (ptr.getClass().isActor())
            mActors.addActor(ptr);
        else
            mObjects.addObject(ptr);
    }

    void MechanicsManager::castSpell(const MWWorld::Ptr& ptr, const ESM::RefId& spellId, bool scriptedSpell)
    {
        if (ptr.getClass().isActor())
            mActors.castSpell(ptr, spellId, scriptedSpell);
    }

    void MechanicsManager::remove(const MWWorld::Ptr& ptr, bool keepActive)
    {
        if (ptr == MWBase::Environment::get().getWindowManager()->getWatchedActor())
            MWBase::Environment::get().getWindowManager()->watchActor(MWWorld::Ptr());
        mActors.removeActor(ptr, keepActive);
        mObjects.removeObject(ptr);
    }

    void MechanicsManager::updateCell(const MWWorld::Ptr& old, const MWWorld::Ptr& ptr)
    {
        if (old == MWBase::Environment::get().getWindowManager()->getWatchedActor())
            MWBase::Environment::get().getWindowManager()->watchActor(ptr);

        if (ptr.getClass().isActor())
            mActors.updateActor(old, ptr);
        else
            mObjects.updateObject(old, ptr);
    }

    void MechanicsManager::drop(const MWWorld::CellStore* cellStore)
    {
        mActors.dropActors(cellStore, getPlayer());
        mObjects.dropObjects(cellStore);
    }

    void MechanicsManager::update(float duration, bool paused)
    {
        // Note: we should do it here since game mechanics and world updates use these values
        MWWorld::Ptr ptr = getPlayer();
        MWBase::WindowManager* winMgr = MWBase::Environment::get().getWindowManager();

        MWWorld::InventoryStore& inv = ptr.getClass().getInventoryStore(ptr);
        MWWorld::ContainerStoreIterator weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);

        // Update the selected spell icon
        MWWorld::ContainerStoreIterator enchantItem = inv.getSelectedEnchantItem();
        if (enchantItem != inv.end())
            winMgr->setSelectedEnchantItem(*enchantItem);
        else
        {
            const ESM::RefId& spell = winMgr->getSelectedSpell();
            if (!spell.empty())
                winMgr->setSelectedSpell(spell, int(MWMechanics::getSpellSuccessChance(spell, ptr)));
            else
                winMgr->unsetSelectedSpell();
        }

        // Update the equipped weapon icon
        if (weapon == inv.end())
            winMgr->unsetSelectedWeapon();
        else
            winMgr->setSelectedWeapon(*weapon);

        if (mUpdatePlayer)
        {
            mUpdatePlayer = false;

            // HACK? The player has been changed, so a new Animation object may
            // have been made for them. Make sure they're properly updated.
            mActors.removeActor(ptr, true);
            mActors.addActor(ptr, true);
        }

        mActors.update(duration, paused);
        mObjects.update(duration, paused);
    }

    void MechanicsManager::processChangedSettings(const Settings::CategorySettingVector& changed)
    {
        for (Settings::CategorySettingVector::const_iterator it = changed.begin(); it != changed.end(); ++it)
        {
            if (it->first == "Game" && it->second == "actors processing range")
            {
                int state = MWBase::Environment::get().getStateManager()->getState();
                if (state != MWBase::StateManager::State_Running)
                    continue;

                // Update mechanics for new processing range immediately
                update(0.f, false);
            }
        }
    }

    void MechanicsManager::notifyDied(const MWWorld::Ptr& actor)
    {
        mActors.notifyDied(actor);
    }

    bool MechanicsManager::isActorDetected(const MWWorld::Ptr& actor, const MWWorld::Ptr& observer)
    {
        return mActors.isActorDetected(actor, observer);
    }

    bool MechanicsManager::isAttackPreparing(const MWWorld::Ptr& ptr)
    {
        return mActors.isAttackPreparing(ptr);
    }

    bool MechanicsManager::isRunning(const MWWorld::Ptr& ptr)
    {
        CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
        if (!stats.getStance(MWMechanics::CreatureStats::Stance_Run))
            return false;

        if (mActors.isRunning(ptr))
            return true;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        return !world->isOnGround(ptr) && !world->isSwimming(ptr) && !world->isFlying(ptr);
    }

    bool MechanicsManager::isSneaking(const MWWorld::Ptr& ptr)
    {
        CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
        if (!stats.getStance(MWMechanics::CreatureStats::Stance_Sneak))
            return false;

        if (mActors.isSneaking(ptr))
            return true;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        return !world->isOnGround(ptr) && !world->isSwimming(ptr) && !world->isFlying(ptr);
    }

    void MechanicsManager::rest(double hours, bool sleep)
    {
        if (sleep)
            MWBase::Environment::get().getWorld()->rest(hours);

        mActors.rest(hours, sleep);
    }

    void MechanicsManager::restoreDynamicStats(const MWWorld::Ptr& actor, double hours, bool sleep)
    {
        mActors.restoreDynamicStats(actor, hours, sleep);
    }

    int MechanicsManager::getHoursToRest() const
    {
        return mActors.getHoursToRest(getPlayer());
    }

    void MechanicsManager::setPlayerName(const std::string& name)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();

        ESM::NPC player = *world->getPlayerPtr().get<ESM::NPC>()->mBase;
        player.mName = name;

        world->getStore().insert(player);

        mUpdatePlayer = true;
    }

    void MechanicsManager::setPlayerRace(
        const ESM::RefId& race, bool male, const ESM::RefId& head, const ESM::RefId& hair)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();

        ESM::NPC player = *world->getPlayerPtr().get<ESM::NPC>()->mBase;

        if (player.mRace != race || player.mHead != head || player.mHair != hair || player.isMale() != male)
        {
            player.mRace = race;
            player.mHead = head;
            player.mHair = hair;
            player.setIsMale(male);

            world->getStore().insert(player);
            world->renderPlayer();
        }

        mRaceSelected = true;
        buildPlayer();
        mUpdatePlayer = true;
    }

    void MechanicsManager::setPlayerBirthsign(const ESM::RefId& id)
    {
        MWBase::Environment::get().getWorld()->getPlayer().setBirthSign(id);
        buildPlayer();
        mUpdatePlayer = true;
    }

    void MechanicsManager::setPlayerClass(const ESM::RefId& id)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();

        ESM::NPC player = *world->getPlayerPtr().get<ESM::NPC>()->mBase;
        player.mClass = id;

        world->getStore().insert(player);

        mClassSelected = true;
        buildPlayer();
        mUpdatePlayer = true;
    }

    void MechanicsManager::setPlayerClass(const ESM::Class& value)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();

        const ESM::Class* ptr = world->getStore().insert(value);

        ESM::NPC player = *world->getPlayerPtr().get<ESM::NPC>()->mBase;
        player.mClass = ptr->mId;

        world->getStore().insert(player);

        mClassSelected = true;
        buildPlayer();
        mUpdatePlayer = true;
    }

    int MechanicsManager::getDerivedDisposition(const MWWorld::Ptr& ptr, bool clamp)
    {
        // The rule is the game pack's (task 0098 move 6).
        return MWBase::RulesRegistry::social().derivedDisposition(ptr, clamp);
    }

    int MechanicsManager::getBarterOffer(const MWWorld::Ptr& ptr, int basePrice, bool buying)
    {
        return MWBase::RulesRegistry::social().barterOffer(ptr, basePrice, buying);
    }

    int MechanicsManager::countDeaths(const ESM::RefId& id) const
    {
        return mActors.countDeaths(id);
    }

    void MechanicsManager::getPersuasionDispositionChange(
        const MWWorld::Ptr& npc, PersuasionType type, bool& success, int& tempChange, int& permChange)
    {
        MWBase::RulesRegistry::social().persuasionDispositionChange(npc, type, success, tempChange, permChange);
    }

    void MechanicsManager::forceStateUpdate(const MWWorld::Ptr& ptr)
    {
        if (ptr.getClass().isActor())
            mActors.forceStateUpdate(ptr);
    }

    bool MechanicsManager::playAnimationGroup(
        const MWWorld::Ptr& ptr, std::string_view groupName, int mode, uint32_t number, bool scripted)
    {
        if (ptr.getClass().isActor())
            return mActors.playAnimationGroup(ptr, groupName, mode, number, scripted);
        else
            return mObjects.playAnimationGroup(ptr, groupName, mode, number, scripted);
    }
    std::string MechanicsManager::cmStyleCommand(const MWWorld::Ptr& ptr, std::string_view argument)
    {
        if (ptr.isEmpty() || !ptr.getClass().isActor())
            return "cmStyle needs an actor: select one in the console, or use it with none selected for the player";
        const std::string message = mActors.cmStyleCommand(ptr, argument);
        // Also in the log, so that a scripted run (--script-run) can be checked without the console window.
        Log(Debug::Info) << "cmStyle " << argument << ": " << message;
        return message;
    }

    bool MechanicsManager::playAnimationGroupLua(const MWWorld::Ptr& ptr, std::string_view groupName, uint32_t loops,
        float speed, std::string_view startKey, std::string_view stopKey, bool forceLoop)
    {
        if (ptr.getClass().isActor())
            return mActors.playAnimationGroupLua(ptr, groupName, loops, speed, startKey, stopKey, forceLoop);
        else
            return mObjects.playAnimationGroupLua(ptr, groupName, loops, speed, startKey, stopKey, forceLoop);
    }
    void MechanicsManager::enableLuaAnimations(const MWWorld::Ptr& ptr, bool enable)
    {
        if (ptr.getClass().isActor())
            mActors.enableLuaAnimations(ptr, enable);
        else
            mObjects.enableLuaAnimations(ptr, enable);
    }
    void MechanicsManager::skipAnimation(const MWWorld::Ptr& ptr)
    {
        if (ptr.getClass().isActor())
            mActors.skipAnimation(ptr);
        else
            mObjects.skipAnimation(ptr);
    }

    bool MechanicsManager::checkAnimationPlaying(const MWWorld::Ptr& ptr, std::string_view groupName)
    {
        if (ptr.getClass().isActor())
            return mActors.checkAnimationPlaying(ptr, groupName);
        else
            return false;
    }

    bool MechanicsManager::checkScriptedAnimationPlaying(const MWWorld::Ptr& ptr) const
    {
        if (ptr.getClass().isActor())
            return mActors.checkScriptedAnimationPlaying(ptr);

        return false;
    }

    bool MechanicsManager::onOpen(const MWWorld::Ptr& ptr)
    {
        if (ptr.getClass().isActor())
            return true;
        else
            return mObjects.onOpen(ptr);
    }

    void MechanicsManager::onClose(const MWWorld::Ptr& ptr)
    {
        if (!ptr.getClass().isActor())
            mObjects.onClose(ptr);
    }

    void MechanicsManager::persistAnimationStates()
    {
        mActors.persistAnimationStates();
        mObjects.persistAnimationStates();
    }

    void MechanicsManager::clearAnimationQueue(const MWWorld::Ptr& ptr, bool clearScripted)
    {
        if (ptr.getClass().isActor())
            mActors.clearAnimationQueue(ptr, clearScripted);
        else
            mObjects.clearAnimationQueue(ptr, clearScripted);
    }

    void MechanicsManager::updateMagicEffects(const MWWorld::Ptr& ptr)
    {
        mActors.updateMagicEffects(ptr);
    }

    bool MechanicsManager::toggleAI()
    {
        mAI = !mAI;
        return mAI;
    }

    bool MechanicsManager::isAIActive()
    {
        return mAI;
    }

    void MechanicsManager::playerLoaded()
    {
        mUpdatePlayer = true;
        mClassSelected = true;
        mRaceSelected = true;
        mAI = true;
    }

    bool MechanicsManager::isBoundItem(const MWWorld::Ptr& item)
    {
        // The rule is the game pack's (task 0098 move 7).
        return MWBase::RulesRegistry::justice().isBoundItem(item);
    }

    bool MechanicsManager::isAllowedToUse(const MWWorld::Ptr& ptr, const MWWorld::Ptr& target, MWWorld::Ptr& victim)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        return MWBase::RulesRegistry::justice().isAllowedToUse(context, ptr, target, victim);
    }

    bool MechanicsManager::sleepInBed(const MWWorld::Ptr& ptr, const MWWorld::Ptr& bed)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        return MWBase::RulesRegistry::justice().sleepInBed(context, ptr, bed);
    }

    void MechanicsManager::unlockAttempted(const MWWorld::Ptr& ptr, const MWWorld::Ptr& item)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        MWBase::RulesRegistry::justice().unlockAttempted(context, ptr, item);
    }

    std::vector<std::pair<ESM::RefId, int>> MechanicsManager::getStolenItemOwners(const ESM::RefId& itemid)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        return MWBase::RulesRegistry::justice().getStolenItemOwners(context, itemid);
    }

    bool MechanicsManager::isItemStolenFrom(const ESM::RefId& itemid, const MWWorld::Ptr& ptr)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        return MWBase::RulesRegistry::justice().isItemStolenFrom(context, itemid, ptr);
    }

    void MechanicsManager::confiscateStolenItemToOwner(
        const MWWorld::Ptr& player, const MWWorld::Ptr& item, const MWWorld::Ptr& victim, int count)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        MWBase::RulesRegistry::justice().confiscateStolenItemToOwner(context, player, item, victim, count);
    }

    void MechanicsManager::confiscateStolenItems(const MWWorld::Ptr& player, const MWWorld::Ptr& targetContainer)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        MWBase::RulesRegistry::justice().confiscateStolenItems(context, player, targetContainer);
    }

    void MechanicsManager::itemTaken(
        const MWWorld::Ptr& ptr, const MWWorld::Ptr& item, const MWWorld::Ptr& container, int count, bool alarm)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        MWBase::RulesRegistry::justice().itemTaken(context, ptr, item, container, count, alarm);
    }

    bool MechanicsManager::commitCrime(const MWWorld::Ptr& player, const MWWorld::Ptr& victim, OffenseType type,
        const ESM::RefId& factionId, int arg, bool victimAware)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        return MWBase::RulesRegistry::justice().commitCrime(context, player, victim, type, factionId, arg, victimAware);
    }

    bool MechanicsManager::actorAttacked(const MWWorld::Ptr& target, const MWWorld::Ptr& attacker)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        return MWBase::RulesRegistry::justice().actorAttacked(context, target, attacker);
    }

    void MechanicsManager::actorKilled(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker)
    {
        JusticeContext context{ *this, mActors, mStolenItems };
        MWBase::RulesRegistry::justice().actorKilled(context, victim, attacker);
    }

    bool MechanicsManager::awarenessCheck(const MWWorld::Ptr& ptr, const MWWorld::Ptr& observer, bool useCache)
    {
        if (observer.getClass().getCreatureStats(observer).isDead() || !observer.getRefData().isEnabled())
            return false;

        const MWWorld::Store<ESM::GameSetting>& store
            = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();

        CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);

        float sneakTerm = 0;
        if (isSneaking(ptr))
        {
            static float fSneakSkillMult = store.find("fSneakSkillMult")->mValue.getFloat();
            static float fSneakBootMult = store.find("fSneakBootMult")->mValue.getFloat();
            float sneak = static_cast<float>(ptr.getClass().getSkill(ptr, ESM::Skill::Sneak));
            float agility = stats.getAttribute(ESM::Attribute::Agility).getModified();
            float luck = stats.getAttribute(ESM::Attribute::Luck).getModified();
            float bootWeight = 0;
            if (ptr.getClass().isNpc() && MWBase::Environment::get().getWorld()->isOnGround(ptr))
            {
                const MWWorld::InventoryStore& inv = ptr.getClass().getInventoryStore(ptr);
                MWWorld::ConstContainerStoreIterator it = inv.getSlot(MWWorld::InventoryStore::Slot_Boots);
                if (it != inv.end())
                    bootWeight = it->getClass().getWeight(*it);
            }
            sneakTerm = fSneakSkillMult * sneak + 0.2f * agility + 0.1f * luck + bootWeight * fSneakBootMult;
        }

        static float fSneakDistBase = store.find("fSneakDistanceBase")->mValue.getFloat();
        static float fSneakDistMult = store.find("fSneakDistanceMultiplier")->mValue.getFloat();

        vsg::vec3 pos1(ptr.getRefData().getPosition().asVec3());
        vsg::vec3 pos2(observer.getRefData().getPosition().asVec3());
        float distTerm = fSneakDistBase + fSneakDistMult * vsg::length(pos1 - pos2);

        float chameleon = stats.getMagicEffects().getOrDefault(ESM::MagicEffect::Chameleon).getMagnitude();
        float invisibility = stats.getMagicEffects().getOrDefault(ESM::MagicEffect::Invisibility).getMagnitude();
        float x = sneakTerm * distTerm * stats.getFatigueTerm() + chameleon;
        if (invisibility > 0.f)
            x += 100.f;

        CreatureStats& observerStats = observer.getClass().getCreatureStats(observer);
        float obsAgility = observerStats.getAttribute(ESM::Attribute::Agility).getModified();
        float obsLuck = observerStats.getAttribute(ESM::Attribute::Luck).getModified();
        float obsBlind = observerStats.getMagicEffects().getOrDefault(ESM::MagicEffect::Blind).getMagnitude();
        float obsSneak = observer.getClass().getSkill(observer, ESM::Skill::Sneak);

        float obsTerm = obsSneak + 0.2f * obsAgility + 0.1f * obsLuck - obsBlind;

        // is ptr behind the observer?
        static float fSneakNoViewMult = store.find("fSneakNoViewMult")->mValue.getFloat();
        static float fSneakViewMult = store.find("fSneakViewMult")->mValue.getFloat();
        float y = 0;
        auto vec = pos1 - pos2;
        auto observerDir = MWAnim::forward(observer.getRefData().getPosition());

        float angleRadians = std::acos(vsg::dot(observerDir, vec) / (vsg::length(observerDir) * vsg::length(vec)));
        if (angleRadians > Misc::VsgMath::inDegrees(90.f))
            y = obsTerm * observerStats.getFatigueTerm() * fSneakNoViewMult;
        else
            y = obsTerm * observerStats.getFatigueTerm() * fSneakViewMult;

        float target = x - y;
        if (useCache)
            return observerStats.getAwarenessRoll() >= target;
        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        return Misc::Rng::roll0to99(prng) >= target;
    }

    void MechanicsManager::startCombat(
        const MWWorld::Ptr& ptr, const MWWorld::Ptr& target, const std::set<MWWorld::Ptr>* targetAllies)
    {
        CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);

        // Don't add duplicate packages nor add packages to dead actors.
        if (stats.isDead() || stats.getAiSequence().isInCombat(target))
            return;

        // The target is somehow the same as the actor. Early-out.
        if (ptr == target)
        {
            // We don't care about dialogue filters since the target is invalid.
            // We still want to play the combat taunt.
            MWBase::Environment::get().getDialogueManager()->say(ptr, ESM::RefId::stringRefId("attack"));
            if (!stats.getAiSequence().isInCombat())
                stats.resetFriendlyHits();
            return;
        }

        const bool inCombat = stats.getAiSequence().isInCombat();
        bool shout = !inCombat;
        if (inCombat)
        {
            const auto isInCombatWithOneOf = [&](const auto& allies) {
                for (const MWWorld::Ptr& ally : allies)
                {
                    if (stats.getAiSequence().isInCombat(ally))
                        return true;
                }
                return false;
            };
            if (targetAllies)
                shout = !isInCombatWithOneOf(*targetAllies);
            else
            {
                shout = stats.getAiSequence().isInCombat(target);
                if (!shout)
                {
                    std::set<MWWorld::Ptr> sidingActors;
                    getActorsSidingWith(target, sidingActors);
                    shout = !isInCombatWithOneOf(sidingActors);
                }
            }
        }
        stats.getAiSequence().stack(MWMechanics::AiCombat(target), ptr);
        if (target == getPlayer())
        {
            // if guard starts combat with player, guards pursuing player should do the same
            if (ptr.getClass().isClass(ptr, "Guard"))
            {
                const ESM::RefNum playerNum = target.getCellRef().getRefNum();
                // Stops guard from ending combat if player is unreachable
                stats.setHitAttemptActor(playerNum);
                for (const Actor& actor : mActors)
                {
                    if (actor.isInvalid())
                        continue;
                    if (actor.getPtr().getClass().isClass(actor.getPtr(), "Guard"))
                    {
                        MWMechanics::AiSequence& aiSeq
                            = actor.getPtr().getClass().getCreatureStats(actor.getPtr()).getAiSequence();
                        if (aiSeq.getTypeId() == MWMechanics::AiPackageTypeId::Pursue)
                        {
                            aiSeq.stopPursuit();
                            aiSeq.stack(MWMechanics::AiCombat(target), ptr);
                            // Stops guard from ending combat if player is unreachable
                            actor.getPtr().getClass().getCreatureStats(actor.getPtr()).setHitAttemptActor(playerNum);
                        }
                    }
                }
            }
        }

        // Must be done after the target is set up, so that CreatureTargetted dialogue filter works properly
        if (shout)
            MWBase::Environment::get().getDialogueManager()->say(ptr, ESM::RefId::stringRefId("attack"));
    }

    void MechanicsManager::stopCombat(const MWWorld::Ptr& actor)
    {
        mActors.stopCombat(actor);
    }

    void MechanicsManager::getObjectsInRange(
        const vsg::vec3& position, float radius, std::vector<MWWorld::Ptr>& objects)
    {
        mActors.getObjectsInRange(position, radius, objects);
        mObjects.getObjectsInRange(position, radius, objects);
    }

    void MechanicsManager::getActorsInRange(const vsg::vec3& position, float radius, std::vector<MWWorld::Ptr>& objects)
    {
        mActors.getObjectsInRange(position, radius, objects);
    }

    bool MechanicsManager::isAnyActorInRange(const vsg::vec3& position, float radius)
    {
        return mActors.isAnyObjectInRange(position, radius);
    }

    std::vector<MWWorld::Ptr> MechanicsManager::getActorsSidingWith(const MWWorld::Ptr& actor)
    {
        return mActors.getActorsSidingWith(actor);
    }

    std::vector<MWWorld::Ptr> MechanicsManager::getActorsFollowing(const MWWorld::Ptr& actor)
    {
        return mActors.getActorsFollowing(actor);
    }

    std::vector<int> MechanicsManager::getActorsFollowingIndices(const MWWorld::Ptr& actor)
    {
        return mActors.getActorsFollowingIndices(actor);
    }

    std::map<int, MWWorld::Ptr> MechanicsManager::getActorsFollowingByIndex(const MWWorld::Ptr& actor)
    {
        return mActors.getActorsFollowingByIndex(actor);
    }

    std::vector<MWWorld::Ptr> MechanicsManager::getActorsFighting(const MWWorld::Ptr& actor)
    {
        return mActors.getActorsFighting(actor);
    }

    std::vector<MWWorld::Ptr> MechanicsManager::getEnemiesNearby(const MWWorld::Ptr& actor)
    {
        return mActors.getEnemiesNearby(actor);
    }

    void MechanicsManager::getActorsFollowing(const MWWorld::Ptr& actor, std::set<MWWorld::Ptr>& out)
    {
        mActors.getActorsFollowing(actor, out);
    }

    void MechanicsManager::getActorsSidingWith(const MWWorld::Ptr& actor, std::set<MWWorld::Ptr>& out)
    {
        mActors.getActorsSidingWith(actor, out);
    }

    size_t MechanicsManager::countSavedGameRecords() const
    {
        return 1 // Death counter
            + 1; // Stolen items
    }

    void MechanicsManager::write(ESM::ESMWriter& writer, Loading::Listener& listener) const
    {
        mActors.write(writer, listener);

        ESM::StolenItems items;
        items.mStolenItems = mStolenItems;
        writer.startRecord(ESM::REC_STLN);
        items.write(writer);
        writer.endRecord(ESM::REC_STLN);
    }

    void MechanicsManager::readRecord(ESM::ESMReader& reader, uint32_t type)
    {
        if (type == ESM::REC_STLN)
        {
            ESM::StolenItems items;
            items.load(reader);
            mStolenItems = items.mStolenItems;
        }
        else
            mActors.readRecord(reader, type);
    }

    void MechanicsManager::clear()
    {
        mActors.clear();
        mStolenItems.clear();
        mClassSelected = false;
        mRaceSelected = false;
    }

    bool MechanicsManager::isAggressive(const MWWorld::Ptr& ptr, const MWWorld::Ptr& target)
    {
        // If already in combat with target, consider aggressive
        if (ptr.getClass().getCreatureStats(ptr).getAiSequence().isInCombat(target))
            return true;

        // Don't become aggressive if a calm effect is active, since it would cause combat to cycle on/off as
        // combat is activated here and then canceled by the calm effect
        if ((ptr.getClass().isNpc()
                && ptr.getClass()
                        .getCreatureStats(ptr)
                        .getMagicEffects()
                        .getOrDefault(ESM::MagicEffect::CalmHumanoid)
                        .getMagnitude()
                    > 0)
            || (!ptr.getClass().isNpc()
                && ptr.getClass()
                        .getCreatureStats(ptr)
                        .getMagicEffects()
                        .getOrDefault(ESM::MagicEffect::CalmCreature)
                        .getMagnitude()
                    > 0))
            return false;

        int disposition = 50;
        if (ptr.getClass().isNpc())
            disposition = getDerivedDisposition(ptr);

        int fight = ptr.getClass().getCreatureStats(ptr).getAiSetting(AiSetting::Fight).getModified()
            + static_cast<int>(
                getFightDistanceBias(ptr, target) + getFightDispositionBias(static_cast<float>(disposition)));

        if (ptr.getClass().isNpc() && target.getClass().isNpc())
        {
            if (target.getClass().getNpcStats(target).isWerewolf()
                || (target == getPlayer()
                    && MWBase::Environment::get().getWorld()->getGlobalInt(MWWorld::Globals::sPCKnownWerewolf)))
            {
                const ESM::GameSetting* iWerewolfFightMod
                    = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>().find("iWerewolfFightMod");
                fight += iWerewolfFightMod->mValue.getInteger();
            }
        }

        return (fight >= 100);
    }

    void MechanicsManager::resurrect(const MWWorld::Ptr& ptr)
    {
        CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
        if (stats.isDead())
        {
            stats.resurrect();
            mActors.resurrect(ptr);
        }
    }

    bool MechanicsManager::isCastingSpell(const MWWorld::Ptr& ptr) const
    {
        return mActors.isCastingSpell(ptr);
    }

    bool MechanicsManager::isReadyToBlock(const MWWorld::Ptr& ptr) const
    {
        return mActors.isReadyToBlock(ptr);
    }

    bool MechanicsManager::isAttackingOrSpell(const MWWorld::Ptr& ptr) const
    {
        return mActors.isAttackingOrSpell(ptr);
    }

    void MechanicsManager::setWerewolf(const MWWorld::Ptr& actor, bool werewolf)
    {
        MWMechanics::NpcStats& npcStats = actor.getClass().getNpcStats(actor);

        // The actor does not have to change state
        if (npcStats.isWerewolf() == werewolf)
            return;

        MWWorld::Player* player = &MWBase::Environment::get().getWorld()->getPlayer();

        // Werewolfs can not cast spells, so we need to unset the prepared spell if there is one.
        if (npcStats.getDrawState() == MWMechanics::DrawState::Spell)
            npcStats.setDrawState(MWMechanics::DrawState::Nothing);

        npcStats.setWerewolf(werewolf);

        MWWorld::InventoryStore& inv = actor.getClass().getInventoryStore(actor);

        if (werewolf)
        {
            inv.unequipAll();
            inv.equip(MWWorld::InventoryStore::Slot_Robe,
                inv.ContainerStore::add(ESM::RefId::stringRefId("werewolfrobe"), 1));
        }
        else
        {
            inv.unequipSlot(MWWorld::InventoryStore::Slot_Robe);
            inv.ContainerStore::remove(ESM::RefId::stringRefId("werewolfrobe"), 1);
        }

        if (actor == player->getPlayer())
        {
            MWBase::Environment::get().getWorld()->reattachPlayerCamera();

            // Update the GUI only when called on the player
            MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();

            // Transforming removes all temporary effects
            actor.getClass().getCreatureStats(actor).getActiveSpells().purge(
                [](const auto& params) { return params.hasFlag(ESM::ActiveSpells::Flag_Temporary); }, actor);
            mActors.updateActor(actor, 0.f);

            if (werewolf)
            {
                player->saveStats();
                player->setWerewolfStats();
                windowManager->forceHide(MWGui::GW_Inventory);
                windowManager->forceHide(MWGui::GW_Magic);
            }
            else
            {
                player->restoreStats();
                windowManager->unsetForceHide(MWGui::GW_Inventory);
                windowManager->unsetForceHide(MWGui::GW_Magic);
            }

            windowManager->setWerewolfOverlay(werewolf);

            // Witnesses of the player's transformation will make them a globally known werewolf
            std::vector<MWWorld::Ptr> neighbors;
            const MWWorld::Store<ESM::GameSetting>& gmst
                = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();
            getActorsInRange(
                actor.getRefData().getPosition().asVec3(), gmst.find("fAlarmRadius")->mValue.getFloat(), neighbors);

            bool detected = false, reported = false;
            for (const MWWorld::Ptr& neighbor : neighbors)
            {
                if (neighbor == actor || !neighbor.getClass().isNpc())
                    continue;

                if (MWBase::Environment::get().getWorld()->getLOS(neighbor, actor) && awarenessCheck(actor, neighbor))
                {
                    detected = true;
                    if (neighbor.getClass()
                            .getCreatureStats(neighbor)
                            .getAiSetting(MWMechanics::AiSetting::Alarm)
                            .getModified()
                        > 0)
                    {
                        reported = true;
                        break;
                    }
                }
            }

            if (detected)
            {
                windowManager->messageBox("#{sWerewolfAlarmMessage}");
                MWBase::Environment::get().getWorld()->setGlobalInt(MWWorld::Globals::sPCKnownWerewolf, 1);

                if (reported)
                {
                    npcStats.setBounty(
                        std::max(0, npcStats.getBounty() + gmst.find("iWereWolfBounty")->mValue.getInteger()));
                }
            }
        }
    }

    void MechanicsManager::applyWerewolfAcrobatics(const MWWorld::Ptr& actor)
    {
        const ESM::Skill* acrobatics
            = MWBase::Environment::get().getESMStore()->get<ESM::Skill>().find(ESM::Skill::Acrobatics);
        MWMechanics::NpcStats& stats = actor.getClass().getNpcStats(actor);
        auto& skill = stats.getSkill(acrobatics->mId);
        skill.setBase(skill.getBase(), true);
        skill.setModifier(acrobatics->mWerewolfValue - skill.getBase());
    }

    void MechanicsManager::cleanupSummonedCreature(ESM::RefNum creature)
    {
        mActors.cleanupSummonedCreature(creature);
    }

    void MechanicsManager::reportStats(unsigned int frameNumber, Misc::Stats& stats) const {}

    int MechanicsManager::getGreetingTimer(const MWWorld::Ptr& ptr) const
    {
        return mActors.getGreetingTimer(ptr);
    }

    float MechanicsManager::getAngleToPlayer(const MWWorld::Ptr& ptr) const
    {
        return mActors.getAngleToPlayer(ptr);
    }

    GreetingState MechanicsManager::getGreetingState(const MWWorld::Ptr& ptr) const
    {
        return mActors.getGreetingState(ptr);
    }

    void MechanicsManager::fastForwardAi() const
    {
        mActors.fastForwardAi();
    }
}
