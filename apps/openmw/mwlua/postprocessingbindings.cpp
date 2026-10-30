#include "postprocessingbindings.hpp"
#include "context.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
//#include "../mwrender/postprocessor.hpp"

#include <osg/Vec4f>
#include <osg/Vec3f>
#include <osg/Vec2f>
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
    struct Shader
    {
        /*
        std::shared_ptr<fx::Technique> mShader;

        Shader(std::shared_ptr<fx::Technique> shader)
            : mShader(std::move(shader))
        {
        }
        */

        std::string toString() const
        {
            //if (!mShader)
                return "Shader(nil)";

            //return Misc::StringUtils::format("Shader(%s, %s)", mShader->getName(), mShader->getFileName());
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
                    //MWBase::Environment::get().getWorld()->getPostProcessor()->setUniform(shader.mShader, name, value);
                },
                "SetUniformShaderAction");
        };
    }

    template <class T>
    auto getArraySetter(const Context& context)
    {
        return [context](const Shader& shader, const std::string& name, const sol::table& table) {
            /*
            auto targetSize
                = MWBase::Environment::get().getWorld()->getPostProcessor()->getUniformSize(shader.mShader, name);

            if (!targetSize.has_value())
                throw std::runtime_error(Misc::StringUtils::format("Failed setting uniform array '%s'", name));

            if (*targetSize != table.size())
                throw std::runtime_error(Misc::StringUtils::format(
                    "Mismatching uniform array size, got %zu expected %zu", table.size(), *targetSize));
*/
            std::vector<T> values;
            /*
            values.reserve(*targetSize);

            for (size_t i = 0; i < *targetSize; ++i)
            {
                sol::object obj = table[i + 1];
                if (!obj.is<T>())
                    throw std::runtime_error("Invalid type for uniform array");
                values.push_back(obj.as<T>());
            }*/

            context.mLuaManager->addAction(
                [=] {
                    //MWBase::Environment::get().getWorld()->getPostProcessor()->setUniform(shader.mShader, name, values);
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

            //if (shader.mShader && shader.mShader->isValid())
                shader.mQueuedAction = Shader::Action_Enable;

            context.mLuaManager->addAction([=, &shader] {
                shader.mQueuedAction = Shader::Action_None;
/*
                if (MWBase::Environment::get().getWorld()->getPostProcessor()->enableTechnique(shader.mShader, pos)
                    == MWRender::PostProcessor::Status_Error)
                    throw std::runtime_error("Failed enabling shader '" + shader.mShader->getName() + "'");*/
            });
        };

        shaderType["disable"] = [context](Shader& shader) {
            shader.mQueuedAction = Shader::Action_Disable;

            context.mLuaManager->addAction([&] {
                shader.mQueuedAction = Shader::Action_None;
/*
                if (MWBase::Environment::get().getWorld()->getPostProcessor()->disableTechnique(shader.mShader)
                    == MWRender::PostProcessor::Status_Error)
                    throw std::runtime_error("Failed disabling shader '" + shader.mShader->getName() + "'");*/
            });
        };

        shaderType["isEnabled"] = [](const Shader& shader) {
            if (shader.mQueuedAction == Shader::Action_Enable)
                return true;
            else if (shader.mQueuedAction == Shader::Action_Disable)
                return false;
            return false;//MWBase::Environment::get().getWorld()->getPostProcessor()->isTechniqueEnabled(shader.mShader);
        };

        shaderType["setBool"] = getSetter<bool>(context);
        shaderType["setFloat"] = getSetter<float>(context);
        shaderType["setInt"] = getSetter<int>(context);
        shaderType["setVector2"] = getSetter<osg::Vec2f>(context);
        shaderType["setVector3"] = getSetter<osg::Vec3f>(context);
        shaderType["setVector4"] = getSetter<osg::Vec4f>(context);

        shaderType["setFloatArray"] = getArraySetter<float>(context);
        shaderType["setIntArray"] = getArraySetter<int>(context);
        shaderType["setVector2Array"] = getArraySetter<osg::Vec2f>(context);
        shaderType["setVector3Array"] = getArraySetter<osg::Vec3f>(context);
        shaderType["setVector4Array"] = getArraySetter<osg::Vec4f>(context);

        api["load"] = [](const std::string& name) {
            //Shader shader{ MWBase::Environment::get().getWorld()->getPostProcessor()->loadTechnique(name, false) };

            //if (!shader.mShader || !shader.mShader->isValid())
            // openeden-vsg: post-processing returns with the Vulkan renderer's post-processing framework (task 0012);
            // until then every shader fails to load with this message rather than appearing to work.
            throw std::runtime_error("Failed loading shader '" + name
                + "': post-processing is not available on this build yet (Vulkan renderer, OpenEden task 0012)");
/*
            if (!shader.mShader->getDynamic())
                throw std::runtime_error(Misc::StringUtils::format("Shader '%s' is not marked as dynamic", name));
*/
            //return shader;
        };

        return LuaUtil::makeReadOnly(api);
    }

}
