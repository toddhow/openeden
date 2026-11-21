// Task 0363, stage d (plan M): the settings-driven defines of the object shader (default/main.frag and main.vert)
// become specialization constants (`[Rendering] shader constants`). The content defines (texture units, varyings, the
// G-buffer outputs, MATERIAL, ALPHA_TO_COVERAGE ...) stay defines because they change the interface.
// Pipeline::objectFragmentConstants and objectVertexConstants give the values from the stage's define set,
// vsgUtil::constantDefinesFor the pseudo define MODE_CONSTANTS that names the permutation which reads them so.
#include <gtest/gtest.h>

#include <components/pipeline/objectconstants.hpp>
#include <components/pipeline/terrain.hpp>
#include <components/resource/shadersettings.hpp>
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
    namespace Id = Pipeline::ObjectConstantId;
    namespace TId = Pipeline::TerrainConstantId;

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

    const uint32_t allFragmentFlags[] = { TId::GiSky, TId::Wetness, TId::WetnessPuddles, TId::WetnessDebug, TId::Cover,
        TId::CoverDebug, TId::AmbientCube, TId::PointShadow, Id::Gi, Id::PerPixelLighting, Id::PbrLit, Id::PbrPhysical,
        Id::WetLit, Id::CoverLit, Id::LodFade, Id::AdjustCoverage, Id::Pom, Id::ParallaxDiscard, Id::Parallax,
        Id::ReconstructNormalZ, Id::Rmaos, Id::PrecipLit };

    TEST(ObjectConstants, nothing_on_sets_every_flag_false_and_the_medium_parallax_numbers)
    {
        const auto c = Pipeline::objectFragmentConstants({});
        for (uint32_t id : allFragmentFlags)
            EXPECT_FALSE(boolOf(c, id)) << id;
        EXPECT_EQ(intOf(c, TId::PomMinSteps), 10);
        EXPECT_EQ(intOf(c, TId::PomMaxSteps), 20);
        EXPECT_EQ(intOf(c, TId::PomRefineTaps), 4);
    }

    TEST(ObjectConstants, a_plain_flag_is_on_exactly_when_its_define_is)
    {
        const struct
        {
            const char* define;
            uint32_t id;
        } plain[] = { { "GI", Id::Gi }, { "GI_SKY", TId::GiSky }, { "WETNESS", TId::Wetness },
            { "WETNESS_PUDDLES", TId::WetnessPuddles }, { "WETNESS_DEBUG", TId::WetnessDebug },
            { "COVER", TId::Cover }, { "COVER_DEBUG", TId::CoverDebug }, { "AMBIENT_CUBE", TId::AmbientCube },
            { "POINT_SHADOW", TId::PointShadow }, { "PBR_PHYSICAL", Id::PbrPhysical }, { "LOD_FADE", Id::LodFade },
            { "ADJUST_COVERAGE", Id::AdjustCoverage }, { "PARALLAX_DISCARD", Id::ParallaxDiscard },
            { "RECONSTRUCT_NORMAL_Z", Id::ReconstructNormalZ }, { "MATERIAL_MAP_RMAOS", Id::Rmaos },
            { "PRECIPITATION_LIT", Id::PrecipLit } };
        for (const auto& entry : plain)
        {
            const auto on = Pipeline::objectFragmentConstants({ entry.define });
            EXPECT_TRUE(boolOf(on, entry.id)) << entry.define;
            for (const auto& other : plain)
                if (other.id != entry.id)
                    EXPECT_FALSE(boolOf(on, other.id)) << entry.define << " set " << other.define;
        }
    }

    // The derived values (the shader never combines constants: OpSpecConstantOp): worked out here from defines.
    TEST(ObjectConstants, per_pixel_lighting_needs_a_normal_and_a_reason)
    {
        for (const char* reason : { "NORMAL_MAP", "SPECULAR_MAP", "PBR", "FORCE_PER_PIXEL_LIGHTING" })
        {
            EXPECT_TRUE(boolOf(Pipeline::objectFragmentConstants({ "NORMAL", reason }), Id::PerPixelLighting))
                << reason;
            EXPECT_FALSE(boolOf(Pipeline::objectFragmentConstants({ reason }), Id::PerPixelLighting)) << reason;
        }
        EXPECT_FALSE(boolOf(Pipeline::objectFragmentConstants({ "NORMAL" }), Id::PerPixelLighting));
        EXPECT_FALSE(boolOf(Pipeline::objectFragmentConstants({ "NORMAL", "TEXCOORD", "COLOR" }), Id::PerPixelLighting));
    }

    TEST(ObjectConstants, the_material_model_wetness_and_cover_are_lit_only_with_a_normal_and_pbr)
    {
        const auto lit = Pipeline::objectFragmentConstants({ "NORMAL", "PBR", "WETNESS", "COVER" });
        EXPECT_TRUE(boolOf(lit, Id::PbrLit));
        EXPECT_TRUE(boolOf(lit, Id::WetLit));
        EXPECT_TRUE(boolOf(lit, Id::CoverLit));
        const auto noNormal = Pipeline::objectFragmentConstants({ "PBR", "WETNESS", "COVER" });
        EXPECT_FALSE(boolOf(noNormal, Id::PbrLit));
        EXPECT_FALSE(boolOf(noNormal, Id::WetLit));
        EXPECT_FALSE(boolOf(noNormal, Id::CoverLit));
        const auto noPbr = Pipeline::objectFragmentConstants({ "NORMAL", "WETNESS", "COVER" });
        EXPECT_FALSE(boolOf(noPbr, Id::PbrLit));
        EXPECT_FALSE(boolOf(noPbr, Id::WetLit));
        EXPECT_FALSE(boolOf(noPbr, Id::CoverLit));
        const auto pbrOnly = Pipeline::objectFragmentConstants({ "NORMAL", "PBR" });
        EXPECT_TRUE(boolOf(pbrOnly, Id::PbrLit));
        EXPECT_FALSE(boolOf(pbrOnly, Id::WetLit));
        EXPECT_FALSE(boolOf(pbrOnly, Id::CoverLit));
    }

    TEST(ObjectConstants, the_parallax_quality_gives_the_step_counts_of_the_define_form)
    {
        const auto low = Pipeline::objectFragmentConstants({ "PARALLAX_POM", "PARALLAX_POM_LOW" });
        EXPECT_EQ(intOf(low, TId::PomMinSteps), 8);
        EXPECT_EQ(intOf(low, TId::PomMaxSteps), 12);
        EXPECT_EQ(intOf(low, TId::PomRefineTaps), 0);
        const auto high = Pipeline::objectFragmentConstants({ "PARALLAX_POM", "PARALLAX_POM_HIGH" });
        EXPECT_EQ(intOf(high, TId::PomMinSteps), 16);
        EXPECT_EQ(intOf(high, TId::PomMaxSteps), 48);
        EXPECT_EQ(intOf(high, TId::PomRefineTaps), 4);
    }

    // Plan B: parallax occlusion runs only with a height map, and the single step parallax is its fallback (the
    // shader's `#elif`), so a set with both takes the occlusion only.
    TEST(ObjectConstants, parallax_occlusion_needs_the_height_map_and_replaces_the_single_step)
    {
        EXPECT_TRUE(boolOf(Pipeline::objectFragmentConstants({ "HEIGHT_MAP", "PARALLAX_POM" }), Id::Pom));
        EXPECT_FALSE(boolOf(Pipeline::objectFragmentConstants({ "PARALLAX_POM" }), Id::Pom));
        EXPECT_FALSE(boolOf(Pipeline::objectFragmentConstants({ "HEIGHT_MAP" }), Id::Pom));
        EXPECT_TRUE(boolOf(Pipeline::objectFragmentConstants({ "PARALLAX" }), Id::Parallax));
        EXPECT_TRUE(boolOf(Pipeline::objectFragmentConstants({ "HEIGHT_MAP", "PARALLAX" }), Id::Parallax));
        const auto both = Pipeline::objectFragmentConstants({ "HEIGHT_MAP", "PARALLAX", "PARALLAX_POM" });
        EXPECT_TRUE(boolOf(both, Id::Pom));
        EXPECT_FALSE(boolOf(both, Id::Parallax));
        const auto noHeight = Pipeline::objectFragmentConstants({ "PARALLAX", "PARALLAX_POM" });
        EXPECT_FALSE(boolOf(noHeight, Id::Pom));
        EXPECT_TRUE(boolOf(noHeight, Id::Parallax));
    }

    // Plan B: the precipitation light is a constant only where the shader can draw it; any other set keeps the define.
    TEST(ObjectConstants, the_precipitation_light_is_folded_only_with_a_diffuse_map_and_coordinates)
    {
        const auto options = optionsWith(true);
        const Defines rain{ "DIFFUSE_MAP", "NORMAL", "PRECIPITATION_LIT", "TEXCOORD", "UVSETS_1" };
        EXPECT_EQ(vsgUtil::constantDefinesFor("default/main.frag", rain, options.get()),
            (Defines{ "DIFFUSE_MAP", "MODE_CONSTANTS", "NORMAL", "TEXCOORD", "UVSETS_1" }));
        const Defines bare{ "NORMAL", "PRECIPITATION_LIT" };
        EXPECT_EQ(vsgUtil::constantDefinesFor("default/main.frag", bare, options.get()),
            (Defines{ "MODE_CONSTANTS", "NORMAL", "PRECIPITATION_LIT" }));
    }

    TEST(ObjectConstants, the_plan_b_defines_are_dropped_and_their_units_stay)
    {
        const Defines in{ "DIFFUSE_MAP", "HEIGHT_MAP", "MATERIAL", "MATERIAL_MAP", "MATERIAL_MAP_RMAOS", "NORMAL",
            "NORMAL_MAP", "PARALLAX", "PARALLAX_DISCARD", "PARALLAX_POM", "RECONSTRUCT_NORMAL_Z", "TEXCOORD" };
        EXPECT_EQ(vsgUtil::constantDefinesFor("default/main.frag", in, optionsWith(true).get()),
            (Defines{ "DIFFUSE_MAP", "HEIGHT_MAP", "MATERIAL", "MATERIAL_MAP", "MODE_CONSTANTS", "NORMAL", "NORMAL_MAP",
                "TEXCOORD" }));
    }

    TEST(ObjectConstants, the_vertex_has_one_flag_for_the_far_tree_sway)
    {
        EXPECT_FALSE(boolOf(Pipeline::objectVertexConstants({ "COLOR", "NORMAL" }), Id::LodSway));
        EXPECT_TRUE(boolOf(Pipeline::objectVertexConstants({ "COLOR", "LOD_SWAY" }), Id::LodSway));
    }

    TEST(ObjectConstants, the_new_ids_are_distinct_and_clear_of_the_settings_table)
    {
        std::set<uint32_t> ids;
        for (uint32_t id : { TId::GiSky, TId::Wetness, TId::WetnessPuddles, TId::WetnessDebug, TId::Cover,
                 TId::CoverDebug, TId::AmbientCube, TId::PointShadow, TId::AoTerrain, TId::PomMinSteps,
                 TId::PomMaxSteps, TId::PomRefineTaps, Id::Gi, Id::PerPixelLighting, Id::PbrLit, Id::PbrPhysical,
                 Id::WetLit, Id::CoverLit, Id::LodFade, Id::AdjustCoverage, Id::LodSway, Id::Pom, Id::ParallaxDiscard,
                 Id::Parallax, Id::ReconstructNormalZ, Id::Rmaos, Id::PrecipLit })
            EXPECT_TRUE(ids.insert(id).second) << id;
        for (const auto& entry : Resource::settingsConstantTable())
            EXPECT_EQ(ids.count(entry.mId), 0u) << entry.mName;
    }

    const char* const controls[] = { "ADJUST_COVERAGE", "AMBIENT_CUBE", "CLOUD_SHADOW", "CLOUD_SHADOW_DEBUG", "COVER",
        "COVER_DEBUG", "DEBUG_SHADOW", "FORCE_PER_PIXEL_LIGHTING", "GI", "GI_SKY", "LOD_FADE", "PARALLAX_POM_HIGH",
        "PARALLAX_POM_LOW", "PBR", "PBR_PHYSICAL", "POINT_SHADOW", "VOLUME_CLOUD_SHADOW", "WETNESS", "WETNESS_DEBUG",
        "WETNESS_PUDDLES", "MATERIAL_MAP_RMAOS", "PARALLAX", "PARALLAX_DISCARD", "PARALLAX_POM",
        "RECONSTRUCT_NORMAL_Z" };

    // What stays a define (it changes the interface or the outputs): everything that is not in `controls`.
    TEST(ObjectConstants, the_fragment_drops_the_controls_and_keeps_the_content_defines)
    {
        Defines in{ "ALPHA_TO_COVERAGE", "COLOR", "DIFFUSE_MAP", "GBUFFER", "GBUFFER_AMBIENT", "GBUFFER_MATERIAL",
            "GBUFFER_MOTION", "HEIGHT_MAP", "MATERIAL", "NORMAL", "NORMAL_MAP", "PARTICLE", "SOFT_PARTICLE",
            "SPECULAR_MAP", "TEXCOORD" };
        const Defines content = in;
        for (const char* name : controls)
            in.insert(name);
        Defines expected = content;
        expected.insert("MODE_CONSTANTS");
        EXPECT_EQ(vsgUtil::constantDefinesFor("default/main.frag", in, optionsWith(true).get()), expected);
        EXPECT_EQ(vsgUtil::constantDefinesFor("default/main.frag", {}, optionsWith(true).get()),
            (Defines{ "MODE_CONSTANTS" }));
    }

    // Only a vertex stage that had the sway needs the permutation that reads it as a constant.
    TEST(ObjectConstants, the_vertex_takes_the_pseudo_define_only_for_the_sway)
    {
        EXPECT_EQ(vsgUtil::constantDefinesFor("default/main.vert", { "COLOR", "LOD_SWAY", "NORMAL", "TEXCOORD", "VERTEX" },
                      optionsWith(true).get()),
            (Defines{ "COLOR", "MODE_CONSTANTS", "NORMAL", "TEXCOORD", "VERTEX" }));
        const Defines plain{ "COLOR", "NORMAL", "SKIN", "TEXCOORD", "VERTEX" };
        EXPECT_EQ(vsgUtil::constantDefinesFor("default/main.vert", plain, optionsWith(true).get()), plain);
    }

    TEST(ObjectConstants, off_or_without_options_nothing_changes)
    {
        const Defines in{ "COVER", "GBUFFER", "GI_SKY", "LOD_SWAY", "NORMAL", "PBR" };
        for (const char* stage : { "default/main.frag", "default/main.vert" })
        {
            EXPECT_EQ(vsgUtil::constantDefinesFor(stage, in, optionsWith(false).get()), in);
            EXPECT_EQ(vsgUtil::constantDefinesFor(stage, in, nullptr), in);
        }
    }

    std::vector<std::string> setLines(const std::string& relative)
    {
        std::vector<std::string> out;
        std::istringstream lines(readSource(relative));
        for (std::string line; std::getline(lines, line);)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty() && line[0] != '#')
                out.push_back(line);
        }
        return out;
    }

    Defines splitSet(const std::string& line)
    {
        Defines names;
        std::istringstream in(line);
        for (std::string name; in >> name;)
            names.insert(name);
        names.erase("-");
        return names;
    }

    std::string joinSet(const Defines& names)
    {
        if (names.empty())
            return "-";
        std::string out;
        for (const std::string& name : names)
            out += (out.empty() ? "" : " ") + name;
        return out;
    }

    // The constants permutations are the define-form sets with the controls folded: listed, canonical, and each
    // define-form set of the file maps to one of them.
    void expectConstantsSetsCover(const std::string& file, const std::string& stage, std::size_t minimum)
    {
        const auto lines = setLines(file);
        std::set<std::string> listed;
        std::vector<Defines> defineForm;
        for (const std::string& line : lines)
        {
            const Defines names = splitSet(line);
            EXPECT_EQ(joinSet(names), line) << "not canonical: " << line;
            if (names.count("MODE_CONSTANTS") != 0)
            {
                for (const char* control : controls)
                    EXPECT_EQ(names.count(control), 0u) << control << " in a constants set: " << line;
                EXPECT_EQ(names.count("LOD_SWAY"), 0u) << line;
                listed.insert(line);
            }
            else
                defineForm.push_back(names);
        }
        EXPECT_GE(listed.size(), minimum);
        const auto options = optionsWith(true);
        for (const Defines& names : defineForm)
        {
            const Defines mapped = vsgUtil::constantDefinesFor(stage, names, options.get());
            if (mapped == names)
                continue; // the vertex stage's sets without the sway are the same sets
            EXPECT_EQ(listed.count(joinSet(mapped)), 1u) << "not listed: " << joinSet(mapped) << " (from " << joinSet(names) << ")";
        }
    }

    TEST(ObjectConstants, the_fragment_lists_the_constants_permutation_of_every_define_set)
    {
        expectConstantsSetsCover("default/main.frag.sets", "default/main.frag", 10u);
    }

    TEST(ObjectConstants, the_vertex_lists_the_constants_permutation_of_the_sway)
    {
        expectConstantsSetsCover("default/main.vert.sets", "default/main.vert", 1u);
    }

    // No expression over the constants (OpSpecConstantOp): each is branched on alone.
    TEST(ObjectConstants, the_object_fragment_branches_on_each_constant_alone)
    {
        const std::string text = readSource("default/default.frag.slang");
        EXPECT_NE(text.find("MODE_CONSTANTS"), std::string::npos);
        for (const char* name : { "cGi", "cGiSky", "cWetnessPuddles", "cWetnessDebug", "cCoverDebug", "cAmbientCube",
                 "cPointShadow", "cDebugShadow", "cPerPixel", "cPbrLit", "cPbrPhysical", "cWetLit", "cCoverLit",
                 "cLodFade", "cAdjustCoverage", "cPom", "cParallaxDiscard", "cParallax", "cReconstructNormalZ", "cRmaos",
                 "cPrecipLit" })
        {
            EXPECT_NE(text.find(name), std::string::npos) << name;
            EXPECT_EQ(text.find(std::string(name) + " &&"), std::string::npos) << name;
            EXPECT_EQ(text.find(std::string(name) + " ||"), std::string::npos) << name;
            EXPECT_EQ(text.find("&& " + std::string(name)), std::string::npos) << name;
            EXPECT_EQ(text.find("|| " + std::string(name)), std::string::npos) << name;
        }
    }

    TEST(ObjectConstants, the_vertex_declares_the_sway_constant)
    {
        const std::string text = readSource("default/default.vert.slang");
        EXPECT_NE(text.find("cLodSway"), std::string::npos);
        EXPECT_NE(text.find("MODE_CONSTANTS"), std::string::npos);
    }
}
