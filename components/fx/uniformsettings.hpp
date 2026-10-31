#ifndef OPENEDEN_COMPONENTS_FX_UNIFORMSETTINGS_HPP
#define OPENEDEN_COMPONENTS_FX_UNIFORMSETTINGS_HPP

#include "types.hpp"

// The link between parsed uniforms and shaders.yaml (Settings::ShaderManager). OpenMW did this inside
// Fx::Types::UniformBase at 442eec6e2f; here the callers do it, so parsing a technique reads no settings.
// Array uniforms are not stored, which OpenMW's were when set from Lua.
namespace Fx
{
    /// Sets the uniform to the value stored for it, if any. A stored value of the wrong type is logged and ignored.
    void applyStoredValue(Types::UniformBase& uniform);

    /// Stores the uniform's current value. The file is written by Settings::ShaderManager::save.
    void storeValue(const Types::UniformBase& uniform);
}

#endif
