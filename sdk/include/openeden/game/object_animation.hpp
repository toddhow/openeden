#pragma once

#include <string_view>

#include <openeden/game/world_source.hpp>
#include <openeden/version.hpp>

// Task 0289 (dungeon mechanics, first slice): how a game pack makes a placed object play one of its own animations
// (a puzzle pillar turning, a gate opening). An OPTIONAL interface found by dynamic_cast on the object that
// Host::getWorldSourceService() returns, so WorldSourceService (frozen at 1.3) is not changed. A host that cannot
// animate objects does not implement it.
//
// Version policy (DRAFT 0.1): a new virtual function goes at the END with a default that means "not supported" and
// raises the minor number; anything else is a major change. No engine, renderer, Havok or game type appears here.
// Main thread only, like the source interface.

namespace openeden::sdk::game
{
    inline constexpr Version kObjectAnimationVersion{ 0, 1 };

    class ObjectAnimationService
    {
    public:
        virtual ~ObjectAnimationService() = default;

        /// Queues "play the animation the game's scripts call `event` (a name such as Trigger01, Open, Close,
        /// StartState02)" for a placement of `source`, in the one ordered queue that moves and fades objects use. The
        /// host plays it when it applies the queue; an object without such an animation ignores it. False when the
        /// request was refused (unknown source, an empty or very long name, a full queue).
        [[nodiscard]] virtual bool playObjectAnimation(WorldCellSource& source, WorldId cellHint, WorldId placement,
            std::string_view event)
        {
            (void)source, (void)cellHint, (void)placement, (void)event;
            return false;
        }

        /// How many seconds the animation takes for a placement that is loaded and drawn; 0 when the host does not
        /// know (the object is not loaded, has no such animation, or the host animates nothing). Lets a script that
        /// waits for the end of an animation wait as long as it takes.
        [[nodiscard]] virtual float objectAnimationSeconds(
            WorldCellSource& source, WorldId cellHint, WorldId placement, std::string_view event)
        {
            (void)source, (void)cellHint, (void)placement, (void)event;
            return 0.f;
        }
    };
}
