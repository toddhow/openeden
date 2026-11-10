#include <algorithm>
#include <set>
#include <vector>

#include <components/debug/debuglog.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/misc/strings/lower.hpp>

#include <apps/openmw/mwbase/dialoguemanager.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/actors.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwmechanics/aipursue.hpp>
#include <apps/openmw/mwmechanics/combat.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/justicerules.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <apps/openmw/mwworld/player.hpp>
#include <apps/openmw/mwworld/ptr.hpp>

#include <game_morrowind/rules.hpp>

// Morrowind's crime and stolen-item rules (task 0098 move 7): moved from MWMechanics::MechanicsManager
// (apps/openmw/mwmechanics/mechanicsmanagerimp.cpp: isBoundItem, isAllowedToUse, sleepInBed, unlockAttempted,
// getStolenItemOwners, isItemStolenFrom, confiscateStolenItemToOwner, confiscateStolenItems, itemTaken, commitCrime,
// canReportCrime, reportCrime, actorAttacked, canCommitCrimeAgainst, actorKilled) with the bodies unchanged. The
// stolen-items map and the actor list stay the manager's: each call gets them in a JusticeContext, and the class
// Justice below only gives the old bodies the names they used (mStolenItems, mActors, the manager's own virtual
// functions) so that they read as before. Main thread only, as before (the callers are the windows, mwscript, Lua
// and the actor update's attack and death notifications, which the manager's callers make on the main thread).
namespace MWMechanics::Morrowind
{
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

        bool isOwned(const MWWorld::Ptr& ptr, const MWWorld::Ptr& target, MWWorld::Ptr& victim)
        {
            const MWWorld::CellRef& cellref = target.getCellRef();

            const ESM::RefId& owner = cellref.getOwner();
            bool isOwned = !owner.empty() && owner != ESM::RefId::stringRefId("Player");

            const ESM::RefId& faction = cellref.getFaction();
            bool isFactionOwned = false;
            if (!faction.empty() && ptr.getClass().isNpc())
            {
                const std::map<ESM::RefId, int>& factions = ptr.getClass().getNpcStats(ptr).getFactionRanks();
                auto found = factions.find(faction);
                if (found == factions.end() || found->second < cellref.getFactionRank())
                    isFactionOwned = true;
            }

            const std::string& globalVariable = cellref.getGlobalVariable();
            if (!globalVariable.empty() && MWBase::Environment::get().getWorld()->getGlobalInt(globalVariable))
            {
                isOwned = false;
                isFactionOwned = false;
            }

            if (!cellref.getOwner().empty())
                victim = MWBase::Environment::get().getWorld()->searchPtr(cellref.getOwner(), true, false);

            return isOwned || isFactionOwned;
        }

        namespace
        {
            std::set<ESM::RefId> makeBoundItemIdCache()
            {
                std::set<ESM::RefId> boundItemIDCache;

                // Build a list of known bound item ID's
                const MWWorld::Store<ESM::GameSetting>& gameSettings
                    = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();

                for (const ESM::GameSetting& currentSetting : gameSettings)
                {
                    // Don't bother checking this GMST if it's not a sMagicBound* one.
                    if (!currentSetting.mId.startsWith("smagicbound"))
                        continue;

                    // All sMagicBound* GMST's should be of type string
                    std::string currentGMSTValue = currentSetting.mValue.getString();
                    Misc::StringUtils::lowerCaseInPlace(currentGMSTValue);

                    boundItemIDCache.insert(ESM::RefId::stringRefId(currentGMSTValue));
                }

                return boundItemIDCache;
            }
        }

        using OffenseType = MWBase::MechanicsManager::OffenseType;
        using enum MWBase::MechanicsManager::OffenseType;

        class Justice
        {
            JusticeContext& mContext;
            StolenItemsMap& mStolenItems;
            const Actors& mActors;

            using Owner = std::pair<ESM::RefId, bool>;
            using OwnerMap = std::map<Owner, int>;

            // The manager's own functions that the old bodies called by their plain names.
            void getActorsSidingWith(const MWWorld::Ptr& actor, std::set<MWWorld::Ptr>& out)
            {
                mContext.mechanics.getActorsSidingWith(actor, out);
            }
            bool awarenessCheck(const MWWorld::Ptr& ptr, const MWWorld::Ptr& observer)
            {
                return mContext.mechanics.awarenessCheck(ptr, observer);
            }
            void startCombat(
                const MWWorld::Ptr& ptr, const MWWorld::Ptr& target, const std::set<MWWorld::Ptr>* targetAllies)
            {
                mContext.mechanics.startCombat(ptr, target, targetAllies);
            }
            bool isAggressive(const MWWorld::Ptr& ptr, const MWWorld::Ptr& target)
            {
                return mContext.mechanics.isAggressive(ptr, target);
            }
            int getDerivedDisposition(const MWWorld::Ptr& ptr) { return mContext.mechanics.getDerivedDisposition(ptr); }
            bool isSneaking(const MWWorld::Ptr& ptr) { return mContext.mechanics.isSneaking(ptr); }

            bool canCommitCrimeAgainst(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker);
            bool canReportCrime(
                const MWWorld::Ptr& actor, const MWWorld::Ptr& victim, std::set<MWWorld::Ptr>& playerFollowers);
            bool reportCrime(const MWWorld::Ptr& ptr, const MWWorld::Ptr& victim, OffenseType type,
                const ESM::RefId& factionId, int arg = 0);

        public:
            explicit Justice(JusticeContext& context)
                : mContext(context)
                , mStolenItems(context.stolen)
                , mActors(context.actors)
            {
            }

            static bool isBoundItem(const MWWorld::Ptr& item);
            bool isAllowedToUse(const MWWorld::Ptr& ptr, const MWWorld::Ptr& target, MWWorld::Ptr& victim);
            bool sleepInBed(const MWWorld::Ptr& ptr, const MWWorld::Ptr& bed);
            void unlockAttempted(const MWWorld::Ptr& ptr, const MWWorld::Ptr& item);
            std::vector<std::pair<ESM::RefId, int>> getStolenItemOwners(const ESM::RefId& itemid);
            bool isItemStolenFrom(const ESM::RefId& itemid, const MWWorld::Ptr& ptr);
            void confiscateStolenItemToOwner(
                const MWWorld::Ptr& player, const MWWorld::Ptr& item, const MWWorld::Ptr& victim, int count);
            void confiscateStolenItems(const MWWorld::Ptr& player, const MWWorld::Ptr& targetContainer);
            void itemTaken(const MWWorld::Ptr& ptr, const MWWorld::Ptr& item, const MWWorld::Ptr& container, int count,
                bool alarm);
            bool commitCrime(const MWWorld::Ptr& player, const MWWorld::Ptr& victim, OffenseType type,
                const ESM::RefId& factionId = ESM::RefId(), int arg = 0, bool victimAware = false);
            bool actorAttacked(const MWWorld::Ptr& target, const MWWorld::Ptr& attacker);
            void actorKilled(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker);
        };

        bool Justice::isBoundItem(const MWWorld::Ptr& item)
        {
            static const std::set<ESM::RefId> boundItemIdCache = makeBoundItemIdCache();

            return boundItemIdCache.find(item.getCellRef().getRefId()) != boundItemIdCache.end();
        }

        bool Justice::isAllowedToUse(const MWWorld::Ptr& ptr, const MWWorld::Ptr& target, MWWorld::Ptr& victim)
        {
            if (target.isEmpty())
                return true;

            const MWWorld::CellRef& cellref = target.getCellRef();
            // there is no harm to use unlocked doors
            if (target.getClass().isDoor() && !cellref.isLocked() && cellref.getTrap().empty())
            {
                return true;
            }

            if (!target.getClass().hasToolTip(target))
                return true;

            // TODO: implement a better check to check if target is owned bed
            if (target.getClass().isActivator() && !target.getClass().getScript(target).startsWith("Bed"))
                return true;

            if (target.getClass().isNpc())
            {
                if (target.getClass().getCreatureStats(target).isDead())
                    return true;

                if (target.getClass().getCreatureStats(target).getAiSequence().isInCombat())
                    return true;

                // check if a player tries to pickpocket a target NPC
                if (target.getClass().getCreatureStats(target).getKnockedDown() || isSneaking(ptr))
                    return false;

                return true;
            }

            if (isOwned(ptr, target, victim))
                return false;

            // A special case for evidence chest - we should not allow to take items even if it is technically permitted
            return !(cellref.getRefId() == "stolen_goods");
        }

        bool Justice::sleepInBed(const MWWorld::Ptr& ptr, const MWWorld::Ptr& bed)
        {
            if (ptr.getClass().getNpcStats(ptr).isWerewolf())
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sWerewolfRefusal}");
                return true;
            }

            if (MWBase::Environment::get().getWorld()->getPlayer().enemiesNearby())
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage2}");
                return true;
            }

            MWWorld::Ptr victim;
            if (isAllowedToUse(ptr, bed, victim))
                return false;

            if (commitCrime(ptr, victim, OT_SleepingInOwnedBed, bed.getCellRef().getFaction()))
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage64}");
                return true;
            }
            else
                return false;
        }

        void Justice::unlockAttempted(const MWWorld::Ptr& ptr, const MWWorld::Ptr& item)
        {
            MWWorld::Ptr victim;
            if (isOwned(ptr, item, victim))
            {
                // Note that attempting to unlock something that has ever been locked is a crime even if it's already
                // unlocked. Likewise, it's illegal to unlock something that has a trap but isn't otherwise locked.
                const auto& cellref = item.getCellRef();
                if (cellref.getLockLevel() || cellref.isLocked() || !cellref.getTrap().empty())
                    commitCrime(ptr, victim, OT_Trespassing, item.getCellRef().getFaction());
            }
        }

        std::vector<std::pair<ESM::RefId, int>> Justice::getStolenItemOwners(const ESM::RefId& itemid)
        {
            std::vector<std::pair<ESM::RefId, int>> result;
            StolenItemsMap::const_iterator it = mStolenItems.find(itemid);
            if (it == mStolenItems.end())
                return result;
            else
            {
                const OwnerMap& owners = it->second;
                for (OwnerMap::const_iterator ownerIt = owners.begin(); ownerIt != owners.end(); ++ownerIt)
                    result.emplace_back(ownerIt->first.first, ownerIt->second);
                return result;
            }
        }

        bool Justice::isItemStolenFrom(const ESM::RefId& itemid, const MWWorld::Ptr& ptr)
        {
            StolenItemsMap::const_iterator it = mStolenItems.find(itemid);
            if (it == mStolenItems.end())
                return false;

            const OwnerMap& owners = it->second;
            const ESM::RefId& ownerid = ptr.getCellRef().getRefId();
            OwnerMap::const_iterator ownerFound = owners.find(std::make_pair(ownerid, false));
            if (ownerFound != owners.end())
                return true;

            const ESM::RefId& factionid = ptr.getClass().getPrimaryFaction(ptr);
            if (!factionid.empty())
            {
                OwnerMap::const_iterator factionOwnerFound = owners.find(std::make_pair(factionid, true));
                return factionOwnerFound != owners.end();
            }

            return false;
        }

        void Justice::confiscateStolenItemToOwner(
            const MWWorld::Ptr& player, const MWWorld::Ptr& item, const MWWorld::Ptr& victim, int count)
        {
            if (player != getPlayer())
                return;

            const ESM::RefId& itemId = item.getCellRef().getRefId();

            StolenItemsMap::iterator stolenIt = mStolenItems.find(itemId);
            if (stolenIt == mStolenItems.end())
                return;

            Owner owner;
            owner.first = victim.getCellRef().getRefId();
            owner.second = false;

            const ESM::RefId& victimFaction = victim.getClass().getPrimaryFaction(victim);
            if (!victimFaction.empty() && item.getCellRef().getFaction() == victimFaction) // Is the item faction-owned?
            {
                owner.first = victimFaction;
                owner.second = true;
            }

            // decrease count of stolen items
            int toRemove = std::min(count, mStolenItems[itemId][owner]);
            mStolenItems[itemId][owner] -= toRemove;
            if (mStolenItems[itemId][owner] == 0)
            {
                // erase owner from stolen items owners
                OwnerMap& owners = stolenIt->second;
                OwnerMap::iterator ownersIt = owners.find(owner);
                if (ownersIt != owners.end())
                    owners.erase(ownersIt);
            }

            MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);

            // move items from player to owner and report about theft
            victim.getClass().getContainerStore(victim).add(item, toRemove);
            store.remove(item, toRemove);
            commitCrime(
                player, victim, OT_Theft, item.getCellRef().getFaction(), item.getClass().getValue(item) * toRemove);
        }

        void Justice::confiscateStolenItems(const MWWorld::Ptr& player, const MWWorld::Ptr& targetContainer)
        {
            MWWorld::ContainerStore& store = player.getClass().getContainerStore(player);
            MWWorld::ContainerStore& containerStore = targetContainer.getClass().getContainerStore(targetContainer);
            for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
            {
                StolenItemsMap::iterator stolenIt = mStolenItems.find(it->getCellRef().getRefId());
                if (stolenIt == mStolenItems.end())
                    continue;
                OwnerMap& owners = stolenIt->second;
                int itemCount = it->getCellRef().getCount();
                for (OwnerMap::iterator ownerIt = owners.begin(); ownerIt != owners.end();)
                {
                    int toRemove = std::min(itemCount, ownerIt->second);
                    itemCount -= toRemove;
                    ownerIt->second -= toRemove;
                    if (ownerIt->second == 0)
                        owners.erase(ownerIt++);
                    else
                        ++ownerIt;
                }

                int toMove = it->getCellRef().getCount() - itemCount;

                containerStore.add(*it, toMove);
                store.remove(*it, toMove);
            }
            // TODO: unhardcode the locklevel
            targetContainer.getCellRef().lock(50);
        }

        void Justice::itemTaken(
            const MWWorld::Ptr& ptr, const MWWorld::Ptr& item, const MWWorld::Ptr& container, int count, bool alarm)
        {
            if (ptr != getPlayer())
                return;

            MWWorld::Ptr victim;

            bool isAllowed = true;
            const MWWorld::CellRef* ownerCellRef = &item.getCellRef();
            if (!container.isEmpty())
            {
                // Inherit the owner of the container
                ownerCellRef = &container.getCellRef();
                isAllowed = isAllowedToUse(ptr, container, victim);
            }
            else
            {
                isAllowed = isAllowedToUse(ptr, item, victim);
            }

            if (isAllowed)
                return;

            Owner owner;
            owner.second = false;
            if (!container.isEmpty() && container.getClass().isActor())
            {
                // "container" is an actor inventory, so just take actor's ID
                owner.first = ownerCellRef->getRefId();
            }
            else
            {
                owner.first = ownerCellRef->getOwner();
                if (owner.first.empty())
                {
                    owner.first = ownerCellRef->getFaction();
                    owner.second = true;
                }
            }

            const bool isGold = item.getClass().isGold(item);
            if (!isGold)
            {
                if (victim.isEmpty()
                    || (victim.getClass().isActor() && victim.getCellRef().getCount() > 0
                        && !victim.getClass().getCreatureStats(victim).isDead()))
                    mStolenItems[item.getCellRef().getRefId()][owner] += count;
            }
            if (alarm)
            {
                int value = count;
                if (!isGold)
                    value *= item.getClass().getValue(item);
                commitCrime(ptr, victim, OT_Theft, ownerCellRef->getFaction(), value);
            }
        }

        bool Justice::commitCrime(const MWWorld::Ptr& player, const MWWorld::Ptr& victim, OffenseType type,
            const ESM::RefId& factionId, int arg, bool victimAware)
        {
            // NOTE: victim may be empty

            // Only player can commit crime
            if (player != getPlayer())
                return false;

            if (type == OT_Assault)
                victimAware = true;

            // Find all the actors within the alarm radius
            std::vector<MWWorld::Ptr> neighbors;

            vsg::vec3 from(player.getRefData().getPosition().asVec3());
            const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();
            float radius = esmStore.get<ESM::GameSetting>().find("fAlarmRadius")->mValue.getFloat();

            mActors.getObjectsInRange(from, radius, neighbors);

            // victim should be considered even beyond alarm radius
            if (!victim.isEmpty() && vsg::length2(from - victim.getRefData().getPosition().asVec3()) > radius * radius)
                neighbors.push_back(victim);

            // get the player's followers / allies (works recursively) that will not report crimes
            std::set<MWWorld::Ptr> playerFollowers;
            getActorsSidingWith(player, playerFollowers);

            // Did anyone see it?
            bool crimeSeen = false;
            for (const MWWorld::Ptr& neighbor : neighbors)
            {
                if (!canReportCrime(neighbor, victim, playerFollowers))
                    continue;

                if ((neighbor == victim && victimAware)
                    // Murder crime can be reported even if no one saw it (hearing is enough, I guess).
                    // TODO: Add mod support for stealth executions!
                    || (type == OT_Murder && neighbor != victim)
                    || (MWBase::Environment::get().getWorld()->getLOS(player, neighbor)
                        && awarenessCheck(player, neighbor)))
                {
                    // NPC will complain about theft even if he will do nothing about it
                    if (type == OT_Theft || type == OT_Pickpocket)
                        MWBase::Environment::get().getDialogueManager()->say(
                            neighbor, ESM::RefId::stringRefId("thief"));

                    crimeSeen = true;
                }
            }

            if (crimeSeen)
                reportCrime(player, victim, type, factionId, arg);
            else if (type == OT_Assault && !victim.isEmpty())
            {
                bool reported = false;
                if (victim.getClass().isClass(victim, "guard")
                    && !victim.getClass().getCreatureStats(victim).getAiSequence().hasPackage(AiPackageTypeId::Pursue))
                    reported = reportCrime(player, victim, type, ESM::RefId(), arg);

                // TODO: combat should be started with an "unaware" flag, which makes the victim flee?
                if (!reported)
                    startCombat(victim, player, &playerFollowers);
            }
            return crimeSeen;
        }

        bool Justice::canReportCrime(
            const MWWorld::Ptr& actor, const MWWorld::Ptr& victim, std::set<MWWorld::Ptr>& playerFollowers)
        {
            if (actor == getPlayer() || !actor.getClass().isNpc() || actor.getClass().getCreatureStats(actor).isDead())
                return false;

            if (actor.getClass().getCreatureStats(actor).getAiSequence().isInCombat(victim))
                return false;

            // Unconsious actor can not report about crime and should not become hostile
            if (actor.getClass().getCreatureStats(actor).getKnockedDown())
                return false;

            // Player's followers should not attack player, or try to arrest him
            if (actor.getClass().getCreatureStats(actor).getAiSequence().hasPackage(AiPackageTypeId::Follow))
            {
                if (playerFollowers.find(actor) != playerFollowers.end())
                    return false;
            }

            return true;
        }

        bool Justice::reportCrime(const MWWorld::Ptr& player, const MWWorld::Ptr& victim, OffenseType type,
            const ESM::RefId& factionId, int arg)
        {
            const MWWorld::Store<ESM::GameSetting>& store
                = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();

            if (type == OT_Murder && !victim.isEmpty())
                victim.getClass().getCreatureStats(victim).notifyMurder();

            // Bounty and disposition penalty for each type of crime
            int bounty;
            float disp, dispVictim;
            if (type == OT_Trespassing || type == OT_SleepingInOwnedBed)
            {
                bounty = store.find("iCrimeTresspass")->mValue.getInteger();
                disp = dispVictim = store.find("iDispTresspass")->mValue.getFloat();
            }
            else if (type == OT_Pickpocket)
            {
                bounty = store.find("iCrimePickPocket")->mValue.getInteger();
                disp = dispVictim = store.find("fDispPickPocketMod")->mValue.getFloat();
            }
            else if (type == OT_Assault)
            {
                bounty = store.find("iCrimeAttack")->mValue.getInteger();
                disp = store.find("iDispAttackMod")->mValue.getFloat();
                dispVictim = store.find("fDispAttacking")->mValue.getFloat();
            }
            else if (type == OT_Murder)
            {
                bounty = store.find("iCrimeKilling")->mValue.getInteger();
                disp = dispVictim = store.find("iDispKilling")->mValue.getFloat();
            }
            else if (type == OT_Theft)
            {
                bounty = static_cast<int>(arg * store.find("fCrimeStealing")->mValue.getFloat());
                bounty = std::max(1, bounty); // Minimum bounty of 1, in case items with zero value are stolen
                disp = dispVictim = store.find("fDispStealing")->mValue.getFloat() * arg;
            }
            else
            {
                bounty = arg;
                disp = dispVictim = 0.f;
            }

            // Make surrounding actors within alarm distance respond to the crime
            std::vector<MWWorld::Ptr> neighbors;

            const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();

            vsg::vec3 from(player.getRefData().getPosition().asVec3());
            float radius = esmStore.get<ESM::GameSetting>().find("fAlarmRadius")->mValue.getFloat();

            mActors.getObjectsInRange(from, radius, neighbors);

            // victim should be considered even beyond alarm radius
            if (!victim.isEmpty() && vsg::length2(from - victim.getRefData().getPosition().asVec3()) > radius * radius)
                neighbors.push_back(victim);

            int id = MWBase::Environment::get().getWorld()->getPlayer().getNewCrimeId();

            // What amount of provocation did this crime generate?
            // Controls whether witnesses will engage combat with the criminal.
            int fight = 0, fightVictim = 0;
            if (type == OT_Trespassing || type == OT_SleepingInOwnedBed)
                fight = fightVictim = esmStore.get<ESM::GameSetting>().find("iFightTrespass")->mValue.getInteger();
            else if (type == OT_Pickpocket)
            {
                fight = esmStore.get<ESM::GameSetting>().find("iFightPickpocket")->mValue.getInteger();
                fightVictim = esmStore.get<ESM::GameSetting>().find("iFightPickpocket")->mValue.getInteger()
                    * 4; // *4 according to research wiki
            }
            else if (type == OT_Assault)
            {
                fight = esmStore.get<ESM::GameSetting>().find("iFightAttacking")->mValue.getInteger();
                fightVictim = esmStore.get<ESM::GameSetting>().find("iFightAttack")->mValue.getInteger();
            }
            else if (type == OT_Murder)
                fight = fightVictim = esmStore.get<ESM::GameSetting>().find("iFightKilling")->mValue.getInteger();
            else if (type == OT_Theft)
                fight = fightVictim
                    = esmStore.get<ESM::GameSetting>().find("fFightStealing")->mValue.getInteger() * arg;

            bool reported = false;

            std::set<MWWorld::Ptr> playerFollowers;
            getActorsSidingWith(player, playerFollowers);

            // Tell everyone (including the original reporter) in alarm range
            for (const MWWorld::Ptr& actor : neighbors)
            {
                if (!canReportCrime(actor, victim, playerFollowers))
                    continue;

                // Will the witness report the crime?
                if (actor.getClass().getCreatureStats(actor).getAiSetting(AiSetting::Alarm).getBase() >= 100)
                {
                    reported = true;

                    if (type == OT_Trespassing)
                        MWBase::Environment::get().getDialogueManager()->say(
                            actor, ESM::RefId::stringRefId("intruder"));
                }
            }

            for (const MWWorld::Ptr& actor : neighbors)
            {
                if (!canReportCrime(actor, victim, playerFollowers))
                    continue;

                NpcStats& observerStats = actor.getClass().getNpcStats(actor);

                int alarm = observerStats.getAiSetting(AiSetting::Alarm).getBase();
                float alarmTerm = 0.01f * alarm;

                bool isActorVictim = actor == victim;
                float dispTerm = isActorVictim ? dispVictim : disp;

                bool isActorGuard = actor.getClass().isClass(actor, "guard");

                int currentDisposition = getDerivedDisposition(actor);

                bool isPermanent = false;
                bool applyOnlyIfHostile = false;
                int dispositionModifier = 0;
                // Murdering and trespassing seem to do not affect disposition
                if (type == OT_Theft)
                {
                    dispositionModifier = static_cast<int>(dispTerm * alarmTerm);
                }
                else if (type == OT_Pickpocket)
                {
                    if (alarm >= 100 && isActorGuard)
                        dispositionModifier = static_cast<int>(dispTerm);
                    else if (isActorVictim && isActorGuard)
                    {
                        isPermanent = true;
                        dispositionModifier = static_cast<int>(dispTerm * alarmTerm);
                    }
                    else if (isActorVictim)
                    {
                        isPermanent = true;
                        dispositionModifier = static_cast<int>(dispTerm);
                    }
                }
                else if (type == OT_Assault)
                {
                    if (isActorVictim && !isActorGuard)
                    {
                        isPermanent = true;
                        dispositionModifier = static_cast<int>(dispTerm);
                    }
                    else if (alarm >= 100)
                        dispositionModifier = static_cast<int>(dispTerm);
                    else if (isActorVictim && isActorGuard)
                    {
                        isPermanent = true;
                        dispositionModifier = static_cast<int>(dispTerm * alarmTerm);
                    }
                    else
                    {
                        applyOnlyIfHostile = true;
                        dispositionModifier = static_cast<int>(dispTerm * alarmTerm);
                    }
                }

                bool setCrimeId = false;
                if (isPermanent && dispositionModifier != 0 && !applyOnlyIfHostile)
                {
                    setCrimeId = true;
                    dispositionModifier
                        = std::clamp(dispositionModifier, -currentDisposition, 100 - currentDisposition);
                    int baseDisposition = observerStats.getBaseDisposition();
                    observerStats.setBaseDisposition(baseDisposition + dispositionModifier);
                }
                else if (dispositionModifier != 0 && !applyOnlyIfHostile)
                {
                    setCrimeId = true;
                    dispositionModifier
                        = std::clamp(dispositionModifier, -currentDisposition, 100 - currentDisposition);
                    observerStats.modCrimeDispositionModifier(dispositionModifier);
                }

                if (isActorGuard && alarm >= 100)
                {
                    // Mark as Alarmed for dialogue
                    observerStats.setAlarmed(true);

                    setCrimeId = true;

                    if (!observerStats.getAiSequence().isInPursuit())
                    {
                        observerStats.getAiSequence().stack(AiPursue(player), actor);
                    }
                }
                else
                {
                    // If Alarm is 0, treat it like 100 to calculate a Fight modifier for a victim of pickpocketing.
                    // Observers which do not try to arrest player do not care about pickpocketing at all.
                    if (type == OT_Pickpocket && isActorVictim && alarmTerm == 0.0)
                        alarmTerm = 1.0;
                    else if (type == OT_Pickpocket && !isActorVictim)
                        alarmTerm = 0.0;

                    float fightTerm = static_cast<float>(isActorVictim ? fightVictim : fight);
                    fightTerm += getFightDispositionBias(dispTerm);
                    fightTerm += getFightDistanceBias(actor, player);
                    fightTerm *= alarmTerm;

                    const int observerFightRating = observerStats.getAiSetting(AiSetting::Fight).getBase();
                    if (observerFightRating + fightTerm > 100)
                        fightTerm = static_cast<float>(100 - observerFightRating);
                    fightTerm = std::max(0.f, fightTerm);

                    if (observerFightRating + fightTerm >= 100)
                    {
                        if (dispositionModifier != 0 && applyOnlyIfHostile)
                        {
                            dispositionModifier
                                = std::clamp(dispositionModifier, -currentDisposition, 100 - currentDisposition);
                            observerStats.modCrimeDispositionModifier(dispositionModifier);
                        }

                        startCombat(actor, player, &playerFollowers);
                        observerStats.setHitAttemptActor(player.getCellRef().getRefNum());

                        // Apply aggression value to the base Fight rating, so that the actor can continue fighting
                        // after a Calm spell wears off
                        observerStats.setAiSetting(AiSetting::Fight, observerFightRating + static_cast<int>(fightTerm));

                        setCrimeId = true;

                        // Mark as Alarmed for dialogue
                        observerStats.setAlarmed(true);
                    }
                }

                // Set the crime ID, which we will use to calm down participants
                // once the bounty has been paid and restore their disposition to player character.
                if (setCrimeId)
                    observerStats.setCrimeId(id);
            }

            if (reported)
            {
                player.getClass().getNpcStats(player).setBounty(
                    std::max(0, player.getClass().getNpcStats(player).getBounty() + bounty));

                // If committing a crime against a faction member, expell from the faction
                if (!victim.isEmpty() && victim.getClass().isNpc())
                {
                    const ESM::RefId& factionID = victim.getClass().getPrimaryFaction(victim);

                    const std::map<ESM::RefId, int>& playerRanks
                        = player.getClass().getNpcStats(player).getFactionRanks();
                    if (playerRanks.find(factionID) != playerRanks.end())
                    {
                        player.getClass().getNpcStats(player).expell(factionID, true);
                    }
                }
                else if (!factionId.empty())
                {
                    const std::map<ESM::RefId, int>& playerRanks
                        = player.getClass().getNpcStats(player).getFactionRanks();
                    if (playerRanks.find(factionId) != playerRanks.end())
                    {
                        player.getClass().getNpcStats(player).expell(factionId, true);
                    }
                }

                if (type == OT_Assault && !victim.isEmpty()
                    && !victim.getClass().getCreatureStats(victim).getAiSequence().isInCombat(player)
                    && victim.getClass().isNpc())
                {
                    // Attacker is in combat with us, but we are not in combat with the attacker yet. Time to fight
                    // back. Note: accidental or collateral damage attacks are ignored.
                    if (!victim.getClass().getCreatureStats(victim).getAiSequence().isInPursuit())
                        startCombat(victim, player, &playerFollowers);

                    // Set the crime ID, which we will use to calm down participants
                    // once the bounty has been paid.
                    victim.getClass().getNpcStats(victim).setCrimeId(id);
                }
            }

            return reported;
        }

        bool Justice::actorAttacked(const MWWorld::Ptr& target, const MWWorld::Ptr& attacker)
        {
            const MWWorld::Ptr& player = getPlayer();
            if (target == player || !attacker.getClass().isActor())
                return false;

            if (canCommitCrimeAgainst(target, attacker))
                commitCrime(attacker, target, MWBase::MechanicsManager::OT_Assault);

            MWMechanics::CreatureStats& statsTarget = target.getClass().getCreatureStats(target);
            AiSequence& seq = statsTarget.getAiSequence();

            if (!attacker.isEmpty()
                && (attacker.getClass().getCreatureStats(attacker).getAiSequence().isInCombat(target)
                    || attacker == player)
                && !seq.isInCombat(attacker))
            {
                // Attacker is in combat with us, but we are not in combat with the attacker yet. Time to fight back.
                // Note: accidental or collateral damage attacks are ignored.
                if (!target.getClass().getCreatureStats(target).getAiSequence().isInPursuit())
                {
                    // If an actor has OnPCHitMe declared in his script, his Fight = 0 and the attacker is player,
                    // he will attack the player only if we will force him (e.g. via StartCombat console command)
                    bool peaceful = false;
                    const ESM::RefId& script = target.getClass().getScript(target);
                    if (!script.empty() && target.getRefData().getLocals().hasVar(script, "onpchitme")
                        && attacker == player)
                    {
                        const int fight
                            = target.getClass().getCreatureStats(target).getAiSetting(AiSetting::Fight).getModified();
                        peaceful = (fight == 0);
                    }

                    if (!peaceful)
                    {
                        SidingCache cachedAllies{ mActors, false };
                        const std::set<MWWorld::Ptr>& attackerAllies = cachedAllies.getActorsSidingWith(attacker);
                        startCombat(target, attacker, &attackerAllies);
                        // Force friendly actors into combat to prevent infighting between followers
                        for (const auto& follower : cachedAllies.getActorsSidingWith(target))
                        {
                            if (follower != attacker && follower != player)
                                startCombat(follower, attacker, &attackerAllies);
                        }
                    }
                }
            }

            return true;
        }

        bool Justice::canCommitCrimeAgainst(const MWWorld::Ptr& target, const MWWorld::Ptr& attacker)
        {
            const MWWorld::Class& cls = target.getClass();
            const MWMechanics::CreatureStats& stats = cls.getCreatureStats(target);
            const MWMechanics::AiSequence& seq = stats.getAiSequence();
            return cls.isNpc() && !attacker.isEmpty() && !isAggressive(target, attacker) && !seq.isEngagedWithActor()
                && !stats.getAiSequence().isInPursuit() && !cls.getNpcStats(target).isWerewolf()
                && stats.getMagicEffects().getOrDefault(ESM::MagicEffect::Vampirism).getMagnitude() <= 0;
        }

        void Justice::actorKilled(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker)
        {
            if (attacker.isEmpty() || victim.isEmpty())
                return;

            if (victim == attacker)
                return; // known to happen

            if (!victim.getClass().isNpc())
                return; // TODO: implement animal rights

            const MWMechanics::NpcStats& victimStats = victim.getClass().getNpcStats(victim);
            const MWWorld::Ptr& player = getPlayer();
            bool canCommit = attacker == player && canCommitCrimeAgainst(victim, attacker);

            // For now we report only about crimes of player and player's followers
            if (attacker != player)
            {
                std::set<MWWorld::Ptr> playerFollowers;
                getActorsSidingWith(player, playerFollowers);
                if (playerFollowers.find(attacker) == playerFollowers.end())
                    return;
            }

            if (!canCommit && victimStats.getCrimeId() == -1)
                return;

            // Simple check for who attacked first: if the player attacked first, a crimeId should be set
            // Doesn't handle possible edge case where no one reported the assault, but in such a case,
            // for bystanders it is not possible to tell who attacked first, anyway.
            commitCrime(player, victim, MWBase::MechanicsManager::OT_Murder);
        }

        class MorrowindJusticeRules final : public JusticeRules
        {
        public:
            bool isBoundItem(const MWWorld::Ptr& item) override { return Justice::isBoundItem(item); }

            bool isAllowedToUse(JusticeContext& context, const MWWorld::Ptr& ptr, const MWWorld::Ptr& target,
                MWWorld::Ptr& victim) override
            {
                return Justice(context).isAllowedToUse(ptr, target, victim);
            }
            bool sleepInBed(JusticeContext& context, const MWWorld::Ptr& ptr, const MWWorld::Ptr& bed) override
            {
                return Justice(context).sleepInBed(ptr, bed);
            }
            void unlockAttempted(JusticeContext& context, const MWWorld::Ptr& ptr, const MWWorld::Ptr& item) override
            {
                Justice(context).unlockAttempted(ptr, item);
            }
            std::vector<std::pair<ESM::RefId, int>> getStolenItemOwners(
                JusticeContext& context, const ESM::RefId& itemid) override
            {
                return Justice(context).getStolenItemOwners(itemid);
            }
            bool isItemStolenFrom(JusticeContext& context, const ESM::RefId& itemid, const MWWorld::Ptr& ptr) override
            {
                return Justice(context).isItemStolenFrom(itemid, ptr);
            }
            void confiscateStolenItemToOwner(JusticeContext& context, const MWWorld::Ptr& player,
                const MWWorld::Ptr& item, const MWWorld::Ptr& victim, int count) override
            {
                Justice(context).confiscateStolenItemToOwner(player, item, victim, count);
            }
            void confiscateStolenItems(
                JusticeContext& context, const MWWorld::Ptr& player, const MWWorld::Ptr& targetContainer) override
            {
                Justice(context).confiscateStolenItems(player, targetContainer);
            }
            void itemTaken(JusticeContext& context, const MWWorld::Ptr& ptr, const MWWorld::Ptr& item,
                const MWWorld::Ptr& container, int count, bool alarm) override
            {
                Justice(context).itemTaken(ptr, item, container, count, alarm);
            }
            bool commitCrime(JusticeContext& context, const MWWorld::Ptr& player, const MWWorld::Ptr& victim,
                MWBase::MechanicsManager::OffenseType type, const ESM::RefId& factionId, int arg,
                bool victimAware) override
            {
                return Justice(context).commitCrime(player, victim, type, factionId, arg, victimAware);
            }
            bool actorAttacked(
                JusticeContext& context, const MWWorld::Ptr& target, const MWWorld::Ptr& attacker) override
            {
                return Justice(context).actorAttacked(target, attacker);
            }
            void actorKilled(JusticeContext& context, const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker) override
            {
                Justice(context).actorKilled(victim, attacker);
            }
        };
    }
}

namespace game_morrowind
{
    std::shared_ptr<MWMechanics::JusticeRules> makeJusticeRules()
    {
        return std::make_shared<MWMechanics::Morrowind::MorrowindJusticeRules>();
    }
}
