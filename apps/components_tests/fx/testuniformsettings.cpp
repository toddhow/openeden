#include <components/fx/types.hpp>
#include <components/fx/uniformsettings.hpp>
#include <components/settings/shadermanager.hpp>
#include <components/testing/util.hpp>

#include <filesystem>

#include <gtest/gtest.h>

namespace
{
    using namespace Fx;

    template <class T>
    Types::UniformBase makeUniform(const std::string& name, const T& defaultValue)
    {
        Types::UniformBase uniform;
        uniform.mName = name;
        uniform.mTechniqueName = "technique";
        Types::Uniform<T> data;
        data.mDefault = defaultValue;
        uniform.mData = data;
        return uniform;
    }

    struct FxUniformSettingsTest : testing::Test
    {
        std::filesystem::path mPath = TestingOpenMW::outputFilePath(
            std::string(testing::UnitTest::GetInstance()->current_test_info()->name()) + ".yaml");

        void SetUp() override
        {
            std::filesystem::remove(mPath);
            ASSERT_TRUE(Settings::ShaderManager::get().load(mPath));
        }
    };

    TEST_F(FxUniformSettingsTest, values_survive_a_save_and_load)
    {
        auto vec2 = makeUniform("uVec2", vsg::vec2(0, 0));
        auto vec3 = makeUniform("uVec3", vsg::vec3(0, 0, 0));
        auto vec4 = makeUniform("uVec4", vsg::vec4(0, 0, 0, 0));
        auto number = makeUniform("uFloat", 0.f);
        auto integer = makeUniform("uInt", 0);
        auto boolean = makeUniform("uBool", false);
        vec2.setValue(vsg::vec2(1, 2));
        vec3.setValue(vsg::vec3(1, 2, 3));
        vec4.setValue(vsg::vec4(1, 2, 3, 4));
        number.setValue(0.5f);
        integer.setValue(7);
        boolean.setValue(true);
        for (const auto* uniform : { &vec2, &vec3, &vec4, &number, &integer, &boolean })
            storeValue(*uniform);

        ASSERT_TRUE(Settings::ShaderManager::get().save());
        ASSERT_TRUE(Settings::ShaderManager::get().load(mPath));

        auto vec2Loaded = makeUniform("uVec2", vsg::vec2(0, 0));
        auto vec3Loaded = makeUniform("uVec3", vsg::vec3(0, 0, 0));
        auto vec4Loaded = makeUniform("uVec4", vsg::vec4(0, 0, 0, 0));
        auto numberLoaded = makeUniform("uFloat", 0.f);
        auto integerLoaded = makeUniform("uInt", 0);
        auto booleanLoaded = makeUniform("uBool", false);
        for (auto* uniform : { &vec2Loaded, &vec3Loaded, &vec4Loaded, &numberLoaded, &integerLoaded, &booleanLoaded })
            applyStoredValue(*uniform);

        EXPECT_TRUE(vec2Loaded.getValue<vsg::vec2>() == vsg::vec2(1, 2));
        EXPECT_TRUE(vec3Loaded.getValue<vsg::vec3>() == vsg::vec3(1, 2, 3));
        EXPECT_TRUE(vec4Loaded.getValue<vsg::vec4>() == vsg::vec4(1, 2, 3, 4));
        EXPECT_EQ(numberLoaded.getValue<float>(), 0.5f);
        EXPECT_EQ(integerLoaded.getValue<int>(), 7);
        EXPECT_EQ(booleanLoaded.getValue<bool>(), true);
    }

    TEST_F(FxUniformSettingsTest, a_stored_value_reaches_the_baked_constant)
    {
        auto uniform = makeUniform("uFloat", 1.f);
        uniform.setValue(2.f);
        storeValue(uniform);

        auto loaded = makeUniform("uFloat", 1.f);
        applyStoredValue(loaded);

        EXPECT_EQ(loaded.getGLSL(false), "const float uFloat=2.000000;");
    }

    TEST_F(FxUniformSettingsTest, no_stored_value_keeps_the_default)
    {
        auto uniform = makeUniform("uFloat", 3.f);
        applyStoredValue(uniform);
        EXPECT_EQ(uniform.getValue<float>(), 3.f);
    }

    TEST_F(FxUniformSettingsTest, a_stored_value_of_the_wrong_type_keeps_the_default)
    {
        auto vector = makeUniform("uniform", vsg::vec3(1, 2, 3));
        storeValue(vector);

        auto number = makeUniform("uniform", 3.f);
        applyStoredValue(number);
        EXPECT_EQ(number.getValue<float>(), 3.f);
    }
}
