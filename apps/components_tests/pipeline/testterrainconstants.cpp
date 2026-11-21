// Task 0363, stage c: the settings-driven defines of the terrain stages become specialization constants
// (`[Rendering] shader constants`), so 20 fragment permutations (5 map classes x 4 G-buffer states) and 2 vertex ones
// replace the 43,320 reachable define sets. Pipeline::terrainConstants gives the values, vsgUtil::constantDefinesFor
// the one pseudo define MODE_CONSTANTS, the settings table the two debug switches.
#include <gtest/gtest.h>

#include <components/pipeline/terrain.hpp>
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
#include <vector>

namespace
{
    using Defines = std::set<std::string>;
    namespace Id = Pipeline::TerrainConstantId;

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

    bool boolOf(const vsg::ShaderStage::SpecializationConstants& constants, uint32_t id)
    {
        const auto it = constants.find(id);
        if (it == constants.end())
        {
            ADD_FAILURE() << "constant " << id << " is not set";
            return false;
        }
        const auto* value = dynamic_cast<const vsg::Value<VkBool32>*>(it->second.get());
        if (value == nullptr)
        {
            ADD_FAILURE() << "constant " << id << " is not a bool";
            return false;
        }
        return value->value() != VK_FALSE;
    }

    int intOf(const vsg::ShaderStage::SpecializationConstants& constants, uint32_t id)
    {
        const auto it = constants.find(id);
        const auto* value = it == constants.end() ? nullptr : dynamic_cast<const vsg::intValue*>(it->second.get());
        if (value == nullptr)
        {
            ADD_FAILURE() << "constant " << id << " is not an int";
            return 0;
        }
        return value->value();
    }

    const uint32_t allBoolIds[] = { Id::GiSky, Id::Wetness, Id::WetnessPuddles, Id::WetnessDebug, Id::Cover,
        Id::CoverDebug, Id::AmbientCube, Id::PointShadow, Id::AoTerrain };

    TEST(TerrainConstants, nothing_on_sets_every_flag_false_and_the_medium_parallax_numbers)
    {
        const auto c = Pipeline::terrainConstants(false, 0, false, {}, false, {}, 0, false, false, false);
        for (uint32_t id : allBoolIds)
            EXPECT_FALSE(boolOf(c, id)) << id;
        EXPECT_EQ(intOf(c, Id::PomMinSteps), 10);
        EXPECT_EQ(intOf(c, Id::PomMaxSteps), 20);
        EXPECT_EQ(intOf(c, Id::PomRefineTaps), 4);
    }

    // The same rules as Pipeline::terrainDefines: a flag is on exactly when the define would be.
    TEST(TerrainConstants, a_flag_is_on_exactly_when_terrainDefines_has_its_define)
    {
        const Pipeline::WetnessOptions wetness{ true, true, true };
        const Pipeline::CoverOptions cover{ true, true };
        for (bool maps : { false, true })
            for (unsigned gbuffer : { 0u, 1u, 4u, 5u })
                for (bool aoTerrain : { false, true })
                    for (bool pointShadow : { false, true })
                    {
                        const auto defines = Pipeline::terrainDefines(
                            maps, gbuffer, true, wetness, true, cover, 2, aoTerrain, pointShadow, true);
                        const auto c = Pipeline::terrainConstants(
                            maps, gbuffer, true, wetness, true, cover, 2, aoTerrain, pointShadow, true);
                        EXPECT_EQ(boolOf(c, Id::GiSky), defines.count("GI_SKY") != 0);
                        EXPECT_EQ(boolOf(c, Id::Wetness), defines.count("WETNESS") != 0);
                        EXPECT_EQ(boolOf(c, Id::WetnessPuddles), defines.count("WETNESS_PUDDLES") != 0);
                        EXPECT_EQ(boolOf(c, Id::WetnessDebug), defines.count("WETNESS_DEBUG") != 0);
                        EXPECT_EQ(boolOf(c, Id::Cover), defines.count("COVER") != 0);
                        EXPECT_EQ(boolOf(c, Id::CoverDebug), defines.count("COVER_DEBUG") != 0);
                        EXPECT_EQ(boolOf(c, Id::AmbientCube), defines.count("AMBIENT_CUBE") != 0);
                        EXPECT_EQ(boolOf(c, Id::PointShadow), defines.count("POINT_SHADOW") != 0)
                            << maps << pointShadow;
                        EXPECT_EQ(boolOf(c, Id::AoTerrain), defines.count("AO_TERRAIN") != 0) << gbuffer << aoTerrain;
                    }
    }

    TEST(TerrainConstants, the_parallax_quality_gives_the_step_counts_of_the_define_form)
    {
        // lib/material/parallax.slang: low 8, 12, 0; high 16, 48, 4; medium (and none) 10, 20, 4.
        const auto low = Pipeline::terrainConstants(true, 0, false, {}, false, {}, 1, false, false, false);
        EXPECT_EQ(intOf(low, Id::PomMinSteps), 8);
        EXPECT_EQ(intOf(low, Id::PomMaxSteps), 12);
        EXPECT_EQ(intOf(low, Id::PomRefineTaps), 0);
        const auto high = Pipeline::terrainConstants(true, 0, false, {}, false, {}, 3, false, false, false);
        EXPECT_EQ(intOf(high, Id::PomMinSteps), 16);
        EXPECT_EQ(intOf(high, Id::PomMaxSteps), 48);
        EXPECT_EQ(intOf(high, Id::PomRefineTaps), 4);
    }

    TEST(TerrainConstants, the_ids_are_distinct_and_clear_of_the_settings_table)
    {
        std::set<uint32_t> ids;
        for (uint32_t id : allBoolIds)
            EXPECT_TRUE(ids.insert(id).second) << id;
        for (uint32_t id : { Id::PomMinSteps, Id::PomMaxSteps, Id::PomRefineTaps })
            EXPECT_TRUE(ids.insert(id).second) << id;
        for (const auto& entry : Resource::settingsConstantTable())
            EXPECT_EQ(ids.count(entry.mId), 0u) << entry.mName;
    }

    TEST(TerrainConstants, the_settings_table_has_the_two_debug_switches)
    {
        std::set<std::string> names;
        for (const auto& entry : Resource::settingsConstantTable())
            names.insert(std::string(entry.mName));
        EXPECT_EQ(names.count("cDebugShadow"), 1u);
        EXPECT_EQ(names.count("cDebugChunks"), 1u);
    }

    TEST(TerrainConstants, the_shader_settings_give_the_debug_switches)
    {
        const bool shadowHud = Settings::shadows().mEnableDebugHud.get();
        const bool chunks = Settings::terrain().mDebugChunks.get();
        Settings::shadows().mEnableDebugHud.set(true);
        Settings::terrain().mDebugChunks.set(false);
        {
            Resource::ShaderSettings settings;
            std::size_t seen = 0;
            for (const auto& entry : Resource::settingsConstantTable())
            {
                if (entry.mName == "cDebugShadow")
                {
                    EXPECT_TRUE(boolOf(settings.constants->constants, entry.mId));
                    ++seen;
                }
                if (entry.mName == "cDebugChunks")
                {
                    EXPECT_FALSE(boolOf(settings.constants->constants, entry.mId));
                    ++seen;
                }
            }
            EXPECT_EQ(seen, 2u);
        }
        Settings::shadows().mEnableDebugHud.set(shadowHud);
        Settings::terrain().mDebugChunks.set(chunks);
    }

    // What stays a define (it changes the interface): the maps and their classes, and the G-buffer outputs.
    TEST(TerrainConstants, the_fragment_keeps_its_map_class_and_gbuffer_defines_and_drops_the_rest)
    {
        const Defines in{ "AMBIENT_CUBE", "AO_TERRAIN", "CLOUD_SHADOW", "CLOUD_SHADOW_DEBUG", "COVER", "COVER_DEBUG",
            "DEBUG_CHUNKS", "DEBUG_SHADOW", "GBUFFER", "GBUFFER_AMBIENT", "GBUFFER_MATERIAL", "GI_SKY", "PARALLAX_POM",
            "PARALLAX_POM_HIGH", "PARALLAX_POM_LOW", "POINT_SHADOW", "TERRAIN_MAPS", "TERRAIN_PBR",
            "VOLUME_CLOUD_SHADOW", "WETNESS", "WETNESS_DEBUG", "WETNESS_PUDDLES" };
        EXPECT_EQ(vsgUtil::constantDefinesFor("terrain/main.frag", in, optionsWith(true).get()),
            (Defines{ "GBUFFER", "GBUFFER_AMBIENT", "GBUFFER_MATERIAL", "MODE_CONSTANTS", "PARALLAX_POM",
                "TERRAIN_MAPS", "TERRAIN_PBR" }));
        EXPECT_EQ(vsgUtil::constantDefinesFor("terrain/main.frag", {}, optionsWith(true).get()),
            (Defines{ "MODE_CONSTANTS" }));
    }

    TEST(TerrainConstants, the_vertex_keeps_the_maps_and_drops_the_chunk_debug)
    {
        EXPECT_EQ(vsgUtil::constantDefinesFor(
                      "terrain/main.vert", { "DEBUG_CHUNKS", "TERRAIN_MAPS" }, optionsWith(true).get()),
            (Defines{ "MODE_CONSTANTS", "TERRAIN_MAPS" }));
        EXPECT_EQ(vsgUtil::constantDefinesFor("terrain/main.vert", {}, optionsWith(true).get()),
            (Defines{ "MODE_CONSTANTS" }));
    }

    TEST(TerrainConstants, off_or_without_options_nothing_changes)
    {
        const Defines in{ "COVER", "GBUFFER", "GI_SKY", "TERRAIN_MAPS", "DEBUG_CHUNKS" };
        for (const char* stage : { "terrain/main.frag", "terrain/main.vert" })
        {
            EXPECT_EQ(vsgUtil::constantDefinesFor(stage, in, optionsWith(false).get()), in);
            EXPECT_EQ(vsgUtil::constantDefinesFor(stage, in, nullptr), in);
        }
    }

    std::vector<std::string> modeConstantLines(const std::string& relative)
    {
        std::vector<std::string> out;
        std::istringstream lines(readSource(relative));
        for (std::string line; std::getline(lines, line);)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty() && line[0] != '#' && line.find("MODE_CONSTANTS") != std::string::npos)
                out.push_back(line);
        }
        return out;
    }

    // 5 map classes (off; maps; maps and pbr; maps and parallax; maps, pbr and parallax) x 4 G-buffer states.
    TEST(TerrainConstants, the_fragment_lists_twenty_constants_permutations)
    {
        const auto lines = modeConstantLines("terrain/main.frag.sets");
        const std::set<std::string> unique(lines.begin(), lines.end());
        EXPECT_EQ(lines.size(), 20u);
        EXPECT_EQ(unique.size(), 20u);
        const std::set<std::string> allowed{ "GBUFFER", "GBUFFER_AMBIENT", "GBUFFER_MATERIAL", "MODE_CONSTANTS",
            "PARALLAX_POM", "TERRAIN_MAPS", "TERRAIN_PBR" };
        for (const std::string& line : lines)
        {
            std::istringstream names(line);
            std::string previous;
            for (std::string name; names >> name; previous = name)
            {
                EXPECT_EQ(allowed.count(name), 1u) << line;
                EXPECT_LT(previous, name) << "not canonical: " << line;
            }
        }
    }

    TEST(TerrainConstants, the_vertex_lists_two_constants_permutations)
    {
        const auto lines = modeConstantLines("terrain/main.vert.sets");
        EXPECT_EQ(lines, (std::vector<std::string>{ "MODE_CONSTANTS", "MODE_CONSTANTS TERRAIN_MAPS" }));
    }

    // No expression over the constants (OpSpecConstantOp); every one is declared and branched on alone.
    TEST(TerrainConstants, the_terrain_fragment_branches_on_each_constant_alone)
    {
        const std::string text = readSource("terrain/terrain.frag.slang");
        EXPECT_NE(text.find("MODE_CONSTANTS"), std::string::npos);
        for (const char* name : { "cGiSky", "cWetness", "cWetnessPuddles", "cWetnessDebug", "cCover", "cCoverDebug",
                 "cAmbientCube", "cPointShadow", "cAoTerrain", "cDebugShadow", "cDebugChunks" })
        {
            EXPECT_NE(text.find(name), std::string::npos) << name;
            EXPECT_EQ(text.find(std::string(name) + " &&"), std::string::npos) << name;
            EXPECT_EQ(text.find(std::string(name) + " ||"), std::string::npos) << name;
            EXPECT_EQ(text.find("&& " + std::string(name)), std::string::npos) << name;
            EXPECT_EQ(text.find("|| " + std::string(name)), std::string::npos) << name;
        }
    }

    // The define form is untouched: every new branch sits under MODE_CONSTANTS in the shared includes.
    TEST(TerrainConstants, the_shared_includes_name_the_pseudo_define_where_they_branch)
    {
        for (const char* file : { "lib/light/pixellighting.slang", "lib/light/pbrlighting.slang",
                 "lib/material/parallax.slang", "terrain/inout.slang", "lib/view/shadow.slang" })
            EXPECT_NE(readSource(file).find("MODE_CONSTANTS"), std::string::npos) << file;
    }
}
