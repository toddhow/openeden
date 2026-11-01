#ifndef OPENMW_MWPHYSICS_RAGDOLLRIGDATA_H
#define OPENMW_MWPHYSICS_RAGDOLLRIGDATA_H

#include <string_view>

namespace MWPhysics
{
    /// CreativeMotion's humanoid rig and its default profile as JSON text (the debug ragdoll, task 0035).
    std::string_view humanoidRigJson();
    std::string_view humanoidProfileJson();
}

#endif
