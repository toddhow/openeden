#ifndef OPENEDEN_ANIMATION_ANIMSET_H
#define OPENEDEN_ANIMATION_ANIMSET_H

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <typeinfo>

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

        /// The set of a source, shared by everything that asks for the same ControllerMap while any of them still
        /// holds it (task 0124, stage C2): a creature type's animations are read once, and so are the clips made from
        /// them, however many actors play them. Thread safe. `name` is only used if the set is made here.
        static vsg::ref_ptr<const AnimSet> forSource(vsg::ref_ptr<const ControllerMap> controllers, std::string name = {});

        /// The clip data derived from this source (stage C2), built by `build` on the first call and then shared:
        /// `build` returns a shared_ptr<const T> (null for "nothing to play"), and every later call gets the same
        /// one. Thread safe (call_once), so actors on several animation threads may ask at the same time. The set
        /// knows nothing about T: CreativeMotion's clip type lives in the host layer (apps/openmw/mwrender/animclip.*)
        /// and one set holds one T (asking with another type gives null).
        template <class T, class Build>
        std::shared_ptr<const T> clip(Build&& build) const
        {
            std::call_once(mClipOnce, [&] {
                std::shared_ptr<const T> made = build();
                mClip = std::shared_ptr<const void>(made, made.get());
                mClipType = &typeid(T);
            });
            return mClipType != nullptr && *mClipType == typeid(T) ? std::static_pointer_cast<const T>(mClip) : nullptr;
        }

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
        mutable std::once_flag mClipOnce;
        mutable std::shared_ptr<const void> mClip;
        mutable const std::type_info* mClipType = nullptr;
    };
}

#endif
