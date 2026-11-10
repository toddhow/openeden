#ifndef GAME_MWMECHANICS_JUSTICERULES_H
#define GAME_MWMECHANICS_JUSTICERULES_H

#include <map>
#include <utility>
#include <vector>

#include <components/esm/refid.hpp>
#include <components/esm3/stolenitems.hpp>

#include "../mwbase/mechanicsmanager.hpp"
#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    class Actors;

    /// What the player has stolen: item id, then (owner id, owner is a faction) to the number of items taken from that
    /// owner. The core owns the map and writes it as ESM::REC_STLN (MechanicsManager::write and readRecord), so a
    /// save's bytes do not depend on which game pack supplies the rules.
    using StolenItemsMap = ESM::StolenItems::StolenItemsMap;

    /// What the justice rules may touch of the core, handed to every call: the manager (its public functions: combat,
    /// witnesses, awareness, disposition), the actor list (witnesses within the alarm radius) and the stolen items.
    struct JusticeContext
    {
        MWBase::MechanicsManager& mechanics;
        const Actors& actors;
        StolenItemsMap& stolen;
    };

    /// The justice rules (task 0098 move 7): what is a crime, who sees it, what it costs, what is stolen and how it
    /// is given back. Morrowind's live in the Morrowind game pack (game_packs/morrowind); the core's MechanicsManager
    /// forwards the matching MWBase::MechanicsManager functions to MWBase::RulesRegistry::justice() and never names
    /// the pack. Main thread only. The bounty lives in NpcStats, the crime id counter in MWWorld::Player; both stay
    /// in the core.
    class JusticeRules
    {
    public:
        virtual ~JusticeRules() = default;

        virtual bool isBoundItem(const MWWorld::Ptr& item) = 0;
        virtual bool isAllowedToUse(
            JusticeContext& context, const MWWorld::Ptr& ptr, const MWWorld::Ptr& target, MWWorld::Ptr& victim)
            = 0;
        virtual bool sleepInBed(JusticeContext& context, const MWWorld::Ptr& ptr, const MWWorld::Ptr& bed) = 0;
        virtual void unlockAttempted(JusticeContext& context, const MWWorld::Ptr& ptr, const MWWorld::Ptr& item) = 0;
        virtual std::vector<std::pair<ESM::RefId, int>> getStolenItemOwners(
            JusticeContext& context, const ESM::RefId& itemid)
            = 0;
        virtual bool isItemStolenFrom(JusticeContext& context, const ESM::RefId& itemid, const MWWorld::Ptr& ptr) = 0;
        virtual void confiscateStolenItemToOwner(JusticeContext& context, const MWWorld::Ptr& player,
            const MWWorld::Ptr& item, const MWWorld::Ptr& victim, int count)
            = 0;
        virtual void confiscateStolenItems(
            JusticeContext& context, const MWWorld::Ptr& player, const MWWorld::Ptr& targetContainer)
            = 0;
        virtual void itemTaken(JusticeContext& context, const MWWorld::Ptr& ptr, const MWWorld::Ptr& item,
            const MWWorld::Ptr& container, int count, bool alarm)
            = 0;
        virtual bool commitCrime(JusticeContext& context, const MWWorld::Ptr& player, const MWWorld::Ptr& victim,
            MWBase::MechanicsManager::OffenseType type, const ESM::RefId& factionId, int arg, bool victimAware)
            = 0;
        virtual bool actorAttacked(JusticeContext& context, const MWWorld::Ptr& target, const MWWorld::Ptr& attacker)
            = 0;
        virtual void actorKilled(JusticeContext& context, const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker) = 0;
    };
}

#endif
