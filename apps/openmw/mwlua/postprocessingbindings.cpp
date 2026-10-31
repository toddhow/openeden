#include "postprocessingbindings.hpp"
#include "context.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwrender/postprocess.hpp"


#include <components/debug/debuglog.hpp>
#include <components/fx/technique.hpp>
#include <components/fx/uniformsettings.hpp>
#include <components/misc/strings/format.hpp>
#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include "luamanagerimp.hpp"

namespace MWLua
{
    struct Shader;
}

namespace sol
{
    template <>
    struct is_automagical<MWLua::Shader> : std::false_type
    {
    };
}

namespace MWLua
{
    namespace
    {
        MWRender::PostProcess* getPostProcess()
        {
            return MWBase::Environment::get().getWorld()->getPostProcess();
        }
    }

    struct Shader
    {
        std::shared_ptr<Fx::Technique> mShader;

        explicit Shader(std::shared_ptr<Fx::Technique> shader)
            : mShader(std::move(shader))
        {
        }

        std::string toString() const
        {
            if (!mShader)
                return "Shader(nil)";

            return Misc::StringUtils::format(
                "Shader(%s, %s)", mShader->getName(), std::string(mShader->getFileName().value()));
        }

        enum
        {
            Action_None,
            Action_Enable,
            Action_Disable
        } mQueuedAction
            = Action_None;
    };

    template <class T>
    auto getSetter(const Context& context)
    {
        return [context](const Shader& shader, const std::string& name, const T& value) {
            context.mLuaManager->addAction(
                [=] {
                    auto it = shader.mShader->findUniform(name);
                    if (it == shader.mShader->getUniformMap().end())
                        return;
                    if ((*it)->mStatic)
                    {
                        Log(Debug::Warning) << "Attempting to set a configuration variable [" << name
                                            << "] as a uniform";
                        return;
                    }
                    (*it)->setValue(value);
                    Fx::storeValue(**it); // as OpenMW's UniformBase::setValue
                },
                "SetUniformShaderAction");
        };
    }

    template <class T>
    auto getArraySetter(const Context& context)
    {
        return [context](const Shader& shader, const std::string& name, const sol::table& table) {
            const auto uniform = shader.mShader->findUniform(name);
            std::optional<std::size_t> targetSize;
            if (uniform != shader.mShader->getUniformMap().end())
                targetSize = (*uniform)->getNumElements();

            if (!targetSize.has_value())
                throw std::runtime_error(Misc::StringUtils::format("Failed setting uniform array '%s'", name));

            if (*targetSize != table.size())
                throw std::runtime_error(Misc::StringUtils::format(
                    "Mismatching uniform array size, got %zu expected %zu", table.size(), *targetSize));

            using Value = T;
            std::vector<Value> values;
            values.reserve(*targetSize);

            for (size_t i = 0; i < *targetSize; ++i)
            {
                sol::object obj = table[i + 1];
                if (!obj.is<T>())
                    throw std::runtime_error("Invalid type for uniform array");
                values.push_back(obj.as<T>());
            }

            context.mLuaManager->addAction(
                [=] {
                    auto it = shader.mShader->findUniform(name);
                    if (it != shader.mShader->getUniformMap().end())
                        (*it)->setValue(values);
                },
                "SetUniformShaderAction");
        };
    }

    sol::table initPostprocessingPackage(const Context& context)
    {
        sol::state_view lua = context.sol();
        sol::table api(lua, sol::create);

        sol::usertype<Shader> shaderType = lua.new_usertype<Shader>("Shader");
        shaderType[sol::meta_function::to_string] = [](const Shader& shader) { return shader.toString(); };

        shaderType["enable"] = [context](Shader& shader, sol::optional<int> optPos) {
            std::optional<int> pos = std::nullopt;
            if (optPos)
                pos = optPos.value();

            if (shader.mShader && shader.mShader->isValid())
                shader.mQueuedAction = Shader::Action_Enable;

            context.mLuaManager->addAction([=, &shader] {
                shader.mQueuedAction = Shader::Action_None;
                if (!getPostProcess()->enableTechnique(shader.mShader, pos))
                    throw std::runtime_error("Failed enabling shader '" + shader.mShader->getName() + "'");
            });
        };

        shaderType["disable"] = [context](Shader& shader) {
            shader.mQueuedAction = Shader::Action_Disable;

            context.mLuaManager->addAction([&] {
                shader.mQueuedAction = Shader::Action_None;
                getPostProcess()->disableTechnique(shader.mShader);
            });
        };

        shaderType["isEnabled"] = [](const Shader& shader) {
            if (shader.mQueuedAction == Shader::Action_Enable)
                return true;
            else if (shader.mQueuedAction == Shader::Action_Disable)
                return false;
            return getPostProcess()->isTechniqueEnabled(shader.mShader);
        };

        shaderType["setBool"] = getSetter<bool>(context);
        shaderType["setFloat"] = getSetter<float>(context);
        shaderType["setInt"] = getSetter<int>(context);
        shaderType["setVector2"] = getSetter<vsg::vec2>(context);
        shaderType["setVector3"] = getSetter<vsg::vec3>(context);
        shaderType["setVector4"] = getSetter<vsg::vec4>(context);

        shaderType["setFloatArray"] = getArraySetter<float>(context);
        shaderType["setIntArray"] = getArraySetter<int>(context);
        shaderType["setVector2Array"] = getArraySetter<vsg::vec2>(context);
        shaderType["setVector3Array"] = getArraySetter<vsg::vec3>(context);
        shaderType["setVector4Array"] = getArraySetter<vsg::vec4>(context);

        api["load"] = [](const std::string& name) {
            MWRender::PostProcess* postProcess = getPostProcess();
            if (!postProcess)
                throw std::runtime_error(
                    "Failed loading shader '" + name + "': post-processing is off ([Post Processing] enabled)");
            Shader shader{ postProcess->loadTechnique(name) };

            if (!shader.mShader || !shader.mShader->isValid())
                throw std::runtime_error("Failed loading shader '" + name + "'");

            if (!shader.mShader->getDynamic())
                throw std::runtime_error(Misc::StringUtils::format("Shader '%s' is not marked as dynamic", name));

            return shader;
        };

        return LuaUtil::makeReadOnly(api);
    }

}
