#ifndef VSGOPENMW_MWRENDER_EFFECT_H
#define VSGOPENMW_MWRENDER_EFFECT_H

#include <optional>
#include <string>
#include <string_view>

#include "../mwworld/ptr.hpp"

namespace ESM
{
    class RefId;
}

namespace MWRender
{
    /// @param effectId identifies the effect for removeEffect (OpenMW 0.51 uses the magic effect RefId as text)
    void addEffect(const MWWorld::Ptr& ptr, const ESM::RefId& effect, std::string_view effectId = {}, bool loop = false,
        const std::string& bone = {}, const std::string& overrideTexture = {});
    /// As addEffect, for an effect mesh given by its model path rather than a static record (as Lua does).
    void addEffectModel(const MWWorld::Ptr& ptr, const std::string& model, std::string_view effectId = {},
        bool loop = false, const std::string& bone = {}, const std::string& overrideTexture = {});
    /// Removes the effects with this ID, or all effects when none is given.
    void removeEffect(const MWWorld::Ptr& ptr, std::optional<std::string_view> effectId = {});
}

#endif
