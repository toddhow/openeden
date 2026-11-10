#ifndef GAME_MWMECHANICS_SOCIALRULES_H
#define GAME_MWMECHANICS_SOCIALRULES_H

#include "../mwbase/mechanicsmanager.hpp"
#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    /// The social rules (task 0098 move 6): how an actor feels about the player (the derived disposition), what a
    /// merchant asks or pays, and how a persuasion attempt changes an actor's disposition. Morrowind's live in the
    /// Morrowind game pack (game_packs/morrowind); the core's MechanicsManager forwards its three public functions to
    /// MWBase::RulesRegistry::social() and never names the pack. Main thread only (the callers are the dialogue and
    /// trade windows, the dialogue manager, mwscript and Lua).
    class SocialRules
    {
    public:
        virtual ~SocialRules() = default;

        /// How \a npc feels about the player now, from its base disposition, race, faction, the player's personality,
        /// bounty, diseases, drawn weapon and charm. \a clamp limits the result to 0 to 100.
        virtual int derivedDisposition(const MWWorld::Ptr& npc, bool clamp) = 0;

        /// The price \a seller asks (\a buying) or pays for something worth \a basePrice. Never below 1 unless
        /// \a basePrice is 0; a creature merchant gets \a basePrice unchanged.
        virtual int barterOffer(const MWWorld::Ptr& seller, int basePrice, bool buying) = 0;

        /// Works out a persuasion attempt on \a npc: draws one number from the world's generator, may change the
        /// actor's flee and fight settings (intimidate, taunt), and writes the outcome to the three references.
        virtual void persuasionDispositionChange(const MWWorld::Ptr& npc, MWBase::MechanicsManager::PersuasionType type,
            bool& success, int& tempChange, int& permChange)
            = 0;
    };
}

#endif
