#ifndef OPENEDEN_ANIMATION_ANIMSET_H
#define OPENEDEN_ANIMATION_ANIMSET_H

#include <map>
#include <string>
#include <string_view>

#include <vsg/core/Object.h>
#include <vsg/core/ref_ptr.h>

#include "controllermap.hpp"
#include "tags.hpp"

namespace Anim
{
    /// One animation source as the engine knows it (task 0124, stage C1): the keyframe controllers a reader made
    /// (`controllers`, which stay the way bones are written until stage C2) and ONE group table built from the
    /// source's text keys. A Morrowind .kf, a COLLADA model and CreativeMotion's glTF clips all arrive as a
    /// ControllerMap with Tags; the group table is derived from them here and nowhere else, so every source answers
    /// "which groups, where do they start and stop" the same way.
    ///
    /// Immutable after creation, so one set may be shared by all actors of a skeleton and read from several
    /// threads. Times are the source's own seconds.
    class AnimSet : public vsg::Object
    {
    public:
        static constexpr float sAbsent = -1.f;

        struct Group
        {
            /// Time of the earliest text key that names the group (what a group's start time query answers).
            float firstKey = sAbsent;
            /// The LAST `start`, `stop` (any key that begins with "stop", so "stop." counts), `loop start` and
            /// `loop stop` key of the group in time order, or sAbsent. The last, because a source that repeats a
            /// group (undeadwolf_2.nif has two walkforward sections) plays the last one.
            float start = sAbsent;
            float stop = sAbsent;
            float loopStart = sAbsent;
            float loopStop = sAbsent;
        };

        /// A source with no tags has an empty table.
        explicit AnimSet(vsg::ref_ptr<const ControllerMap> controllers, std::string name = {});

        /// A name for logs: the file, or what made the source.
        const std::string name;
        const vsg::ref_ptr<const ControllerMap> controllers;
        /// Every text key in time order (shared with `controllers`; an empty table when the source has none).
        const vsg::ref_ptr<const Tags> tags;

        const std::map<std::string, Group, std::less<>>& groups() const { return mGroups; }
        const Group* find(std::string_view group) const;
        bool hasGroup(std::string_view group) const { return find(group) != nullptr; }
        /// The group's first key time, or sAbsent.
        float startTime(std::string_view group) const;
        /// The time of the first key (in time order) whose text begins with `prefix`, or sAbsent.
        float textKeyTime(std::string_view prefix) const;

    private:
        std::map<std::string, Group, std::less<>> mGroups;
    };
}

#endif
