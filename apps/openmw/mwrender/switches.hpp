#ifndef OPENEDEN_MWRENDER_SWITCHES_H
#define OPENEDEN_MWRENDER_SWITCHES_H

#include <cstddef>
#include <string_view>
#include <vector>

#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>

namespace MWRender
{
    // OpenMW's built-in NIF switch nodes (task 0012, V4; OpenMW's mwrender/animation.cpp): NiSwitchNodes named
    // Constants::NightDayLabel show child 0 by day and 1 at night, and those named Constants::HerbalismLabel show child
    // 1 once an organic container has been harvested. The NIF reader keeps a NiSwitchNode's name as the switch's
    // "switch" value.

    /// The first switch below node whose "switch" value is name, or nullptr.
    const vsg::Switch* findSwitch(const vsg::Node& node, std::string_view name);
    vsg::Switch* findSwitch(vsg::Node& node, std::string_view name);

    /// Every switch at or below node whose "switch" value is name, including switched-off children.
    std::vector<vsg::Switch*> findSwitches(vsg::Node& node, std::string_view name);

    /// Shows only child index, as osg::Switch::setSingleChildOn.
    void setSingleChildOn(vsg::Switch& node, std::size_t index);

    /// Gives every child of group its own copy of the switches named name (and of their ancestors), so this instance
    /// can change them; vsgopenmw otherwise shares static models between instances. Call before compiling.
    void makeSwitchesUnique(vsg::Group& group, std::string_view name);
}

#endif
