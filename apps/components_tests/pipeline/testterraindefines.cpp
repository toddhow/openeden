// Tests for Pipeline::terrainDefines (task 0072, stage e): the defines Pipeline::terrain compiles the terrain's stages
// with, and the define sets the game's settings can produce, against the sets that have a Slang twin
// (files/shaders/terrain/main.vert.sets and main.frag.sets).
//
// Written before the implementation: the first cases are the rules Pipeline::terrain applied at 1c1db899a5. The
// settings model repeats the gating of MWRender::RenderManager (apps/openmw/mwrender/rendermanager.cpp, the hdr block
// and the terrain map settings) and of Resource::ShaderSettings (components/resource/shadersettings.cpp, the
// `#define` lines of shadow.settings and terrain.settings), which this test cannot call.

#include <components/pipeline/terrain.hpp>
#include <components/render/aomath.hpp>
#include <components/render/atmospheremath.hpp>
#include <components/render/cloudmath.hpp>
#include <components/render/cloudvolumemath.hpp>
#include <components/render/gbuffermath.hpp>
#include <components/render/hdrmath.hpp>
#include <components/render/materialmath.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>

namespace
{
    using Defines = std::set<std::string>;

    Defines defines(unsigned int gbufferOutputs = 0, bool maps = false, bool giSky = false,
        Pipeline::WetnessOptions wetness = {}, bool pbr = false, Pipeline::CoverOptions cover = {},
        unsigned int parallaxQuality = 0, bool aoTerrain = false)
    {
        return Pipeline::terrainDefines(maps, gbufferOutputs, giSky, wetness, pbr, cover, parallaxQuality, aoTerrain);
    }

    TEST(PipelineTerrainDefines, nothing_on_is_no_define)
    {
        EXPECT_EQ(defines(), Defines{});
        // pbr, parallax and aoTerrain alone add nothing: they need the maps or the ambient output.
        EXPECT_EQ(defines(0, false, false, {}, true, {}, 3, true), Defines{});
    }

    TEST(PipelineTerrainDefines, the_maps_and_their_material_and_parallax_modes)
    {
        EXPECT_EQ(defines(0, true), Defines{ "TERRAIN_MAPS" });
        EXPECT_EQ(defines(0, true, false, {}, true), (Defines{ "TERRAIN_MAPS", "TERRAIN_PBR" }));
        EXPECT_EQ(
            defines(0, true, false, {}, false, {}, 1), (Defines{ "PARALLAX_POM", "PARALLAX_POM_LOW", "TERRAIN_MAPS" }));
        EXPECT_EQ(defines(0, true, false, {}, false, {}, 2), (Defines{ "PARALLAX_POM", "TERRAIN_MAPS" }));
        EXPECT_EQ(defines(0, true, false, {}, false, {}, 3),
            (Defines{ "PARALLAX_POM", "PARALLAX_POM_HIGH", "TERRAIN_MAPS" }));
    }

    TEST(PipelineTerrainDefines, the_gbuffer_outputs)
    {
        EXPECT_EQ(defines(3), Defines{ "GBUFFER" });
        EXPECT_EQ(defines(4), (Defines{ "GBUFFER", "GBUFFER_MATERIAL" }));
        EXPECT_EQ(defines(5), (Defines{ "GBUFFER", "GBUFFER_AMBIENT", "GBUFFER_MATERIAL" }));
        EXPECT_EQ(defines(5, false, false, {}, false, {}, 0, true),
            (Defines{ "AO_TERRAIN", "GBUFFER", "GBUFFER_AMBIENT", "GBUFFER_MATERIAL" }));
        // Without the ambient output the terrain has nowhere to write its ambient part.
        EXPECT_EQ(defines(4, false, false, {}, false, {}, 0, true), (Defines{ "GBUFFER", "GBUFFER_MATERIAL" }));
    }

    TEST(PipelineTerrainDefines, sky_wetness_and_cover)
    {
        EXPECT_EQ(defines(0, false, true), Defines{ "GI_SKY" });
        EXPECT_EQ(defines(0, false, false, { true, false, false }), Defines{ "WETNESS" });
        EXPECT_EQ(
            defines(0, false, false, { true, true, true }), (Defines{ "WETNESS", "WETNESS_DEBUG", "WETNESS_PUDDLES" }));
        // Puddles and the debug view are parts of the wetness.
        EXPECT_EQ(defines(0, false, false, { false, true, true }), Defines{});
        EXPECT_EQ(defines(0, false, false, {}, false, { true, false }), Defines{ "COVER" });
        EXPECT_EQ(defines(0, false, false, {}, false, { true, true }), (Defines{ "COVER", "COVER_DEBUG" }));
        EXPECT_EQ(defines(0, false, false, {}, false, { false, true }), Defines{});
    }

    TEST(PipelineTerrainDefines, point_light_shadows_need_the_maps)
    {
        EXPECT_EQ(Pipeline::terrainDefines(true, 0, false, {}, false, {}, 0, false, true),
            (Defines{ "POINT_SHADOW", "TERRAIN_MAPS" }));
        // A terrain lit per vertex cannot take one light's share out of the sum.
        EXPECT_EQ(Pipeline::terrainDefines(false, 0, false, {}, false, {}, 0, false, true), Defines{});
    }

    // The settings that reach the terrain's define sets. normalMaps stands for `[Shaders] auto use terrain normal
    // maps` or `auto use terrain specular maps` (either turns the maps on); parallax is 0 (off) or the quality, 1 to 3;
    // volumetricShadows is `volumetric clouds` with `volumetric clouds shadows`.
    struct Profile
    {
        bool hdr = false;
        bool pbr = false;
        bool gbuffer = false;
        bool ao = false;
        bool aoTerrain = false;
        bool atmosphere = false;
        bool giExterior = false;
        bool wetness = false;
        bool puddles = false;
        bool wetnessDebug = false;
        bool cover = false;
        bool coverDebug = false;
        bool normalMaps = false;
        bool materialMaps = false;
        unsigned int parallax = 0;
        bool cloudShadows = false;
        bool cloudShadowDebug = false;
        bool volumetricShadows = false;
        bool debugShadow = false;
        bool debugChunks = false;
        // `[Shadows] point light shadows` (task 0077): on only with the material output (pbr and the G-buffer).
        bool pointShadows = false;
    };

    std::string readSource(const std::string& relative)
    {
        std::ifstream in(std::string(OPENEDEN_SHADER_SOURCE_DIR) + "/" + relative, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // The canonical sets of the vertex and fragment stage for one profile, as the game asks for them.
    std::pair<std::string, std::string> stageSets(
        const Profile& p, const Defines& vertexImports, const Defines& fragmentImports)
    {
        // RenderManager: everything but the maps and parallax lives inside the hdr block (a window is assumed).
        const bool hdr = Render::Hdr::useHdrPath(p.hdr, true);
        unsigned int outputs = 0;
        bool materialOutputOn = false;
        bool pbr = false;
        bool ao = false;
        bool giSky = false;
        Pipeline::WetnessOptions wetness;
        Pipeline::CoverOptions cover;
        if (hdr)
        {
            const bool gbuffer = Render::GBuffer::useGBuffer(p.gbuffer, true);
            pbr = Render::Pbr::usePbr(p.pbr, true);
            const bool materialOutput = Render::Pbr::useMaterialOutput(pbr, gbuffer);
            materialOutputOn = materialOutput;
            ao = Render::Ao::useAo(p.ao, gbuffer, materialOutput);
            if (gbuffer)
                outputs
                    = static_cast<unsigned int>(Render::GBuffer::extraColorFormats(true, materialOutput, ao).size());
            giSky = p.giExterior && pbr && Render::Atmosphere::useAtmosphere(p.atmosphere, true);
            wetness.enabled = p.wetness && pbr;
            wetness.puddles = wetness.enabled && p.puddles;
            wetness.debug = wetness.enabled && p.wetnessDebug;
            cover.enabled = p.cover && pbr;
            cover.debug = cover.enabled && p.coverDebug;
        }
        // The terrain map settings: the material table exists with pbr; `pbr material maps` asks for the material
        // and normal maps, parallax for the height maps; any of them turns the maps on (TerrainMapSettings::enabled).
        const bool terrainPbr = pbr && p.materialMaps;
        const bool maps = p.normalMaps || terrainPbr || p.parallax > 0;
        // RenderManager: the point light shadows need the material output (pbr with the G-buffer) and the terrain's
        // maps (Pipeline::terrainDefines adds POINT_SHADOW only with them).
        const bool pointShadow = hdr && p.pointShadows && pbr && materialOutputOn;
        const Defines requested = Pipeline::terrainDefines(
            maps, outputs, giSky, wetness, terrainPbr, cover, p.parallax, ao && p.aoTerrain, pointShadow);

        // ShaderSettings reads the hdr setting itself, not the hdr path.
        Defines shadowSettings;
        if (Render::Cloud::useShadows(p.cloudShadows, p.hdr, p.atmosphere))
        {
            shadowSettings.insert("CLOUD_SHADOW");
            if (p.cloudShadowDebug)
                shadowSettings.insert("CLOUD_SHADOW_DEBUG");
        }
        if (Render::CloudVolume::useVolumetricClouds(p.volumetricShadows, p.hdr, p.atmosphere))
            shadowSettings.insert("VOLUME_CLOUD_SHADOW");
        if (p.debugShadow)
            shadowSettings.insert("DEBUG_SHADOW");

        // The vertex stage includes terrain.settings; the fragment stage that and shadow.settings (through
        // lib/view/shadow.glsl).
        Defines vertex;
        Defines fragment;
        for (const std::string& name : requested)
        {
            if (vertexImports.count(name))
                vertex.insert(name);
            if (fragmentImports.count(name))
                fragment.insert(name);
        }
        fragment.insert(shadowSettings.begin(), shadowSettings.end());
        if (p.debugChunks)
        {
            vertex.insert("DEBUG_CHUNKS");
            fragment.insert("DEBUG_CHUNKS");
        }
        return { vsgUtil::canonicalDefines(vertex), vsgUtil::canonicalDefines(fragment) };
    }

    struct Reachable
    {
        std::set<std::string> vertex;
        std::set<std::string> fragment;
    };

    // Every profile; a part of a setting without the setting itself (puddles without wetness and so on) is skipped,
    // since it changes nothing.
    const Reachable& reachable()
    {
        static const Reachable result = [] {
            Reachable r;
            // The names each stage imports (vsgUtil::effectiveDefines keeps only these), read once.
            const Defines vertexImports = vsgUtil::importedDefines(readSource("terrain/main.vert"));
            const Defines fragmentImports = vsgUtil::importedDefines(readSource("terrain/main.frag"));
            constexpr unsigned int flagCount = 20;
            for (unsigned int bits = 0; bits < (1u << flagCount); ++bits)
            {
                const auto on = [bits](unsigned int i) { return ((bits >> i) & 1u) != 0; };
                Profile p;
                p.hdr = on(0);
                p.pbr = on(1);
                p.gbuffer = on(2);
                p.ao = on(3);
                p.aoTerrain = on(4);
                p.atmosphere = on(5);
                p.giExterior = on(6);
                p.wetness = on(7);
                p.puddles = on(8);
                p.wetnessDebug = on(9);
                p.cover = on(10);
                p.coverDebug = on(11);
                p.normalMaps = on(12);
                p.materialMaps = on(13);
                p.cloudShadows = on(14);
                p.cloudShadowDebug = on(15);
                p.volumetricShadows = on(16);
                p.debugShadow = on(17);
                p.debugChunks = on(18);
                p.pointShadows = on(19);
                if ((p.aoTerrain && !p.ao) || ((p.puddles || p.wetnessDebug) && !p.wetness)
                    || (p.coverDebug && !p.cover) || (p.cloudShadowDebug && !p.cloudShadows))
                    continue;
                for (p.parallax = 0; p.parallax <= 3; ++p.parallax)
                {
                    auto [vertex, fragment] = stageSets(p, vertexImports, fragmentImports);
                    r.vertex.insert(vertex);
                    r.fragment.insert(fragment);
                }
            }
            return r;
        }();
        return result;
    }

    // The sets listed in a `.sets` file, canonical ("-" is the empty set).
    std::set<std::string> listedSets(const std::string& relative)
    {
        std::set<std::string> out;
        std::istringstream lines(readSource(relative));
        std::string line;
        while (std::getline(lines, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            out.insert(line == "-" ? std::string() : line);
        }
        return out;
    }

    TEST(PipelineTerrainDefines, the_settings_reach_four_vertex_sets_and_every_one_has_a_twin)
    {
        ASSERT_FALSE(readSource("terrain/main.vert").empty()) << "the shader sources are not beside the tests";
        const std::set<std::string> expected = { "", "DEBUG_CHUNKS", "DEBUG_CHUNKS TERRAIN_MAPS", "TERRAIN_MAPS" };
        EXPECT_EQ(reachable().vertex, expected);
        EXPECT_EQ(listedSets("terrain/main.vert.sets"), expected);
    }

    // The fragment stage's reachable sets are counted here so that a new setting cannot change the count unnoticed.
    // With pbr: 9 map states (off; on, and on with each parallax quality, each with or without TERRAIN_PBR), 4 G-buffer
    // states, 12 sky and cloud shadow states (none; or, with the atmosphere, GI_SKY or not times 6 cloud shadow states,
    // less the one equal to none), 5 wetness, 3 cover, and DEBUG_SHADOW and DEBUG_CHUNKS: 25,920. hdr without pbr adds
    // the 120 with GBUFFER alone (5 maps x 6 cloud shadow states x 4); every set without hdr is made with hdr too.
    // Task 0077 (task 0072 stage j, 2026-11-06): POINT_SHADOW adds a set to every one with the maps (8 of the 9 map
    // states) and the material output (3 of the 4 G-buffer states: the output of 4, of 5, and of 5 with AO_TERRAIN):
    // 8 x 3 x (12 x 5 x 3 x 4) = 17,280 more, 43,320 in all. Too many to build (task 0072, stage e): the listed sets
    // are a part, and a set that is not listed draws with the GLSL or is compiled at run time.
    TEST(PipelineTerrainDefines, the_settings_reach_43320_fragment_sets_and_every_listed_one_is_among_them)
    {
        ASSERT_FALSE(readSource("terrain/main.frag").empty()) << "the shader sources are not beside the tests";
        EXPECT_EQ(reachable().fragment.size(), 43320u);
        const std::set<std::string> listed = listedSets("terrain/main.frag.sets");
        EXPECT_FALSE(listed.empty());
        for (const std::string& set : listed)
            EXPECT_TRUE(reachable().fragment.count(set)) << "\"" << set << "\" is listed but no settings make it";
    }
}
