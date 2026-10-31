#include "uniformsettings.hpp"

#include <components/settings/shadermanager.hpp>

namespace Fx
{
    void applyStoredValue(Types::UniformBase& uniform)
    {
        if (uniform.mSamplerType)
            return;

        std::visit(
            [&](auto& data) {
                using T = typename std::decay_t<decltype(data)>::value_type;
                if (data.isArray())
                    return;
                if (const auto stored
                    = Settings::ShaderManager::get().getValue<T>(uniform.mTechniqueName, uniform.mName))
                    data.mValue = *stored;
            },
            uniform.mData);
    }

    void storeValue(const Types::UniformBase& uniform)
    {
        if (uniform.mSamplerType)
            return;

        std::visit(
            [&](const auto& data) {
                if (!data.isArray())
                    Settings::ShaderManager::get().setValue(uniform.mTechniqueName, uniform.mName, data.getValue());
            },
            uniform.mData);
    }
}
