#ifndef OPENMW_MWRENDER_AUTOPLAYTRACE_H
#define OPENMW_MWRENDER_AUTOPLAYTRACE_H

#include <cstddef>
#include <span>
#include <string_view>

namespace Anim
{
    struct AutoPlay;
}

namespace MWRender
{
    // Task 0074b: a developer trace of an actor's auto-play clocks, to check in a running game that an equipment
    // change leaves them running. With OPENEDEN_AUTOPLAY_TRACE set to a text, every NPC and every creature that carries
    // weapons whose record id or name contains it (case-insensitive) writes an "AUTOPLAYTRACE" line to the log every
    // frame: for each group its phase group, timer and number of controllers, and the colour of every light one of
    // them drives, first for the actor's own groups (MWAnim::Object::autoPlay: inventory lights, a creature's own
    // model), then for each part's clock that has any. An equipment change writes a line of its own. The variable is
    // read once. Unset or empty, the trace costs one empty-string test per actor update, and no id is built when an
    // actor is made (autoPlayTraceOn first; task 0074d, review of 0074b, finding 7).
    bool autoPlayTraceOn();
    bool autoPlayTraced(std::string_view recordId, std::string_view name);

    // How many controllers the actor's other lists hold (Wielding's and the Npc's face).
    struct ListSizes
    {
        std::size_t mStatic = 0;
        std::size_t mWeapon = 0;
        std::size_t mFace = 0;
    };

    // One frame's line. `parts` are the parts' clocks, indexed by part (empty for none).
    void traceAutoPlay(std::string_view who, unsigned frame, float dt, const Anim::AutoPlay& own,
        std::span<const Anim::AutoPlay> parts, const ListSizes& lists);

    // An event between two frames (an equipment change).
    void traceAutoPlayEvent(std::string_view who, std::string_view event);
}

#endif
