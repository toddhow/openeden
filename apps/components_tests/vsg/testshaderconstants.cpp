// Task 0363, stage b: the settings-driven defines of the water's fragment stage become specialization constants
// (`[Rendering] shader constants`), so one Slang permutation serves every combination of the shadow settings.
#include <gtest/gtest.h>

#include <components/resource/shadersettings.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include <vsg/core/Value.h>
#include <vsg/io/Options.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace
{
    using Defines = std::set<std::string>;

    vsg::ref_ptr<vsg::Options> optionsWith(bool constantDefines)
    {
        auto settings = vsgUtil::SettingsConstants::create();
        settings->constantDefines = constantDefines;
        auto options = vsg::Options::create();
        options->setObject(vsgUtil::settingsConstantsKey, settings);
        return options;
    }

    std::string readSource(const std::string& relative)
    {
        std::ifstream file(std::string(OPENEDEN_SHADER_SOURCE_DIR) + "/" + relative, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }

    class Scope
    {
    public:
        Scope()
            : mHdr(Settings::rendering().mHdr.get())
            , mAtmosphere(Settings::rendering().mAtmosphere.get())
            , mCloudShadows(Settings::rendering().mCloudShadows.get())
            , mVolumetric(Settings::rendering().mVolumetricClouds.get())
            , mVolumetricShadows(Settings::rendering().mVolumetricCloudsShadows.get())
            , mConstants(Settings::rendering().mShaderConstants.get())
        {
        }
        ~Scope()
        {
            Settings::rendering().mHdr.set(mHdr);
            Settings::rendering().mAtmosphere.set(mAtmosphere);
            Settings::rendering().mCloudShadows.set(mCloudShadows);
            Settings::rendering().mVolumetricClouds.set(mVolumetric);
            Settings::rendering().mVolumetricCloudsShadows.set(mVolumetricShadows);
            Settings::rendering().mShaderConstants.set(mConstants);
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        bool mHdr, mAtmosphere, mCloudShadows, mVolumetric, mVolumetricShadows, mConstants;
    };

    bool boolConstant(const Resource::ShaderSettings& settings, std::string_view name)
    {
        for (const auto& entry : Resource::settingsConstantTable())
        {
            if (entry.mName != name)
                continue;
            const auto it = settings.constants->constants.find(entry.mId);
            if (it == settings.constants->constants.end())
                return false;
            const auto* value = dynamic_cast<const vsg::Value<VkBool32>*>(it->second.get());
            return value != nullptr && value->value() != VK_FALSE;
        }
        ADD_FAILURE() << name << " is not in the settings constant table";
        return false;
    }

    TEST(ShaderConstants, the_water_fragment_drops_its_shadow_defines_for_one_pseudo_define_when_on)
    {
        const Defines in{ "BUMP_MAP", "CLOUD_SHADOW", "CLOUD_SHADOW_DEBUG", "DEBUG_SHADOW", "VOLUME_CLOUD_SHADOW" };
        EXPECT_EQ(vsgUtil::constantDefinesFor("water/main.frag", in, optionsWith(true).get()),
            (Defines{ "BUMP_MAP", "SHADOW_CONSTANTS" }));
        EXPECT_EQ(vsgUtil::constantDefinesFor("water/main.frag", { "BUMP_MAP" }, optionsWith(true).get()),
            (Defines{ "BUMP_MAP", "SHADOW_CONSTANTS" }));
    }

    TEST(ShaderConstants, nothing_changes_when_off_or_for_another_stage_or_without_options)
    {
        const Defines in{ "BUMP_MAP", "CLOUD_SHADOW", "VOLUME_CLOUD_SHADOW" };
        EXPECT_EQ(vsgUtil::constantDefinesFor("water/main.frag", in, optionsWith(false).get()), in);
        EXPECT_EQ(vsgUtil::constantDefinesFor("water/main.frag", in, nullptr), in);
        // Task 0363 (stage c): the terrain takes the constants too now (testterrainconstants.cpp); the other stage files do not.
        EXPECT_EQ(vsgUtil::constantDefinesFor("default/main.frag", in, optionsWith(true).get()), in);
        EXPECT_EQ(vsgUtil::constantDefinesFor("water/main.vert", in, optionsWith(true).get()), in);
    }

    TEST(ShaderConstants, the_shader_settings_give_the_three_cloud_shadow_constants_and_the_switch)
    {
        Scope scope;
        Settings::rendering().mHdr.set(true);
        Settings::rendering().mAtmosphere.set(true);
        Settings::rendering().mCloudShadows.set(true);
        Settings::rendering().mVolumetricClouds.set(false);
        Settings::rendering().mShaderConstants.set(true);
        {
            Resource::ShaderSettings settings;
            EXPECT_TRUE(settings.constants->constantDefines);
            EXPECT_TRUE(boolConstant(settings, "cCloudShadow"));
            EXPECT_FALSE(boolConstant(settings, "cVolumeCloudShadow"));
        }
        Settings::rendering().mShaderConstants.set(false);
        Settings::rendering().mCloudShadows.set(false);
        {
            Resource::ShaderSettings settings;
            EXPECT_FALSE(settings.constants->constantDefines);
            EXPECT_FALSE(boolConstant(settings, "cCloudShadow"));
            EXPECT_FALSE(boolConstant(settings, "cCloudShadowDebug"));
        }
    }

    TEST(ShaderConstants, the_switch_is_off_by_default)
    {
        EXPECT_NE(readSource("../settings-default.cfg").find("shader constants = false"), std::string::npos);
    }

    // The constants are read as constants only: no expression over them (files/shaders/CLAUDE.md, OpSpecConstantOp).
    TEST(ShaderConstants, the_slang_shadow_include_branches_on_each_constant_alone)
    {
        const std::string text = readSource("lib/view/shadow.slang");
        EXPECT_NE(text.find("SHADOW_CONSTANTS"), std::string::npos);
        for (const char* name : { "cCloudShadow", "cCloudShadowDebug", "cVolumeCloudShadow" })
        {
            EXPECT_NE(text.find(std::string("constant_id")), std::string::npos);
            EXPECT_NE(text.find(name), std::string::npos) << name;
            EXPECT_EQ(text.find(std::string(name) + " &&"), std::string::npos) << name;
            EXPECT_EQ(text.find(std::string(name) + " ||"), std::string::npos) << name;
            EXPECT_EQ(text.find("&& " + std::string(name)), std::string::npos) << name;
            EXPECT_EQ(text.find("|| " + std::string(name)), std::string::npos) << name;
        }
    }

    TEST(ShaderConstants, the_water_sets_list_the_constants_permutation_once)
    {
        const std::string text = readSource("water/main.frag.sets");
        std::size_t count = 0;
        for (std::size_t at = text.find("SHADOW_CONSTANTS"); at != std::string::npos;
             at = text.find("SHADOW_CONSTANTS", at + 1))
            ++count;
        // A comment may name it too; the set line is "BUMP_MAP SHADOW_CONSTANTS" exactly.
        EXPECT_NE(text.find("\nBUMP_MAP SHADOW_CONSTANTS"), std::string::npos);
        EXPECT_GE(count, 1u);
    }

    // The cell-bounded water (task 0129) had no Slang twin before this stage: its vertex set WATER_CELL is listed.
    TEST(ShaderConstants, the_water_vertex_sets_list_the_cell_surface)
    {
        const std::string text = readSource("water/main.vert.sets");
        EXPECT_NE(text.find("\nWATER_CELL"), std::string::npos);
        EXPECT_NE(text.find("\n-"), std::string::npos);
    }
}
