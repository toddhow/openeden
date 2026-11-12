#ifndef GAME_RENDERING_INTERFACE_H
#define GAME_RENDERING_INTERFACE_H

#include <array>
#include <string>

#include <openeden/game/actor_appearance.hpp>

#include "../mwworld/ptr.hpp"

namespace MWRender
{
    struct RenderingInterface
    {
        virtual void insertModel(const MWWorld::Ptr& ptr, const std::string& model, bool allowLight = true) = 0;
        virtual void insertNPC(const MWWorld::Ptr& ptr) = 0;
        virtual void insertCreature(const MWWorld::Ptr& ptr, const std::string& model, bool weaponsShields) = 0;
        RenderingInterface& getObjects() { return *this; } // vsgopenmw-delete-me
        virtual ~RenderingInterface() {}

        // Declared after the destructor on purpose: a virtual added before it would move the destructor's slot.
        /// Draws a game pack's actor (task 0121, decision 0017): its skeleton and skinned parts in the rest pose. The
        /// default does nothing, so an implementation that has no neutral actors needs no change.
        virtual void insertNeutralActor(
            const MWWorld::Ptr& /*ptr*/, const openeden::sdk::game::ActorAppearance& /*appearance*/)
        {
        }

        /// Task 0146 (`[Game] neutral lip sync`): moves the mouth of a drawn neutral actor: the 16 viseme weights in 0..1
        /// (the head's morphs Aah, BigAah, BMP, ChJSh, DST, Eee, Eh, FV, I, K, N, Oh, OohQ, R, Th, W). Does nothing for an
        /// object that has no head morph. Main thread.
        virtual void setNeutralMouth(const MWWorld::Ptr& /*ptr*/, const std::array<float, 16>& /*visemes*/) {}

        /// Task 0148 (`[Game] neutral actor locomotion`): the measured ground speed (units a second) of a drawn neutral
        /// actor, from the physics side. Does nothing for an object without a locomotion controller. Main thread; no
        /// allocation.
        virtual void setNeutralActorSpeed(const MWWorld::Ptr& /*ptr*/, float /*unitsPerSecond*/) {}
    };
}

#endif
