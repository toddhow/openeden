// OpenEden (task 0095, stage W-c): the text parity of the puddle map's shader twins with the C++ twin for the height
// test (constants, the tops region, who passes what), and a check of the source that the renderer ends the map's worker
// before the World goes (the review of stage W-d). Reads the source tree; a missing file fails the test. A check of the
// text only: that the shaders compute what the C++ twin does is the frame check (the stage log).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>

#include <components/puddle/puddlemap.hpp>
#include <components/render/puddlemapgpu.hpp>

namespace
{
    namespace PM = Render::PuddleMap;

    std::string readSource(const std::string& relativeToApps)
    {
        const std::filesystem::path source
            = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / std::filesystem::path(relativeToApps);
        std::ifstream in(source, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    std::string readShader(const std::string& relative)
    {
        return readSource("../files/shaders/" + relative);
    }

    std::optional<double> constantOf(const std::string& text, const std::string& name)
    {
        const std::regex re("\\b" + name + "\\s*=\\s*(-?[0-9]*\\.?[0-9]+)u?\\s*;");
        std::smatch m;
        if (!std::regex_search(text, m, re))
            return std::nullopt;
        return std::stod(m[1].str());
    }

    std::optional<double> hexConstantOf(const std::string& text, const std::string& name)
    {
        const std::regex re("\\b" + name + "\\s*=\\s*0x([0-9A-Fa-f]+)u?\\s*;");
        std::smatch m;
        if (!std::regex_search(text, m, re))
            return std::nullopt;
        return static_cast<double>(std::stoul(m[1].str(), nullptr, 16));
    }

    class PuddleMapTopsShader : public testing::TestWithParam<const char*>
    {
    };

    TEST_P(PuddleMapTopsShader, the_height_test_constants_equal_the_cpp_twins)
    {
        const std::string text = readShader(GetParam());
        ASSERT_FALSE(text.empty()) << GetParam();
        const std::optional<double> tolerance = constantOf(text, "puddleZTolerance");
        ASSERT_TRUE(tolerance.has_value());
        EXPECT_NEAR(*tolerance, PM::zTolerance, 1e-6);
        const std::optional<double> normal = constantOf(text, "puddleObjectMinNormalZ");
        ASSERT_TRUE(normal.has_value());
        EXPECT_NEAR(*normal, PM::objectMinNormalZ, 1e-6);
        const std::optional<double> topsBase = constantOf(text, "PUDDLE_TOPS_BASE");
        ASSERT_TRUE(topsBase.has_value());
        EXPECT_EQ(static_cast<std::uint32_t>(*topsBase), PM::wordTopsBase);
        const std::optional<double> options = constantOf(text, "PUDDLE_OPTIONS");
        ASSERT_TRUE(options.has_value());
        EXPECT_EQ(static_cast<std::uint32_t>(*options), PM::wordOptions);
        const std::optional<double> keepNoise = constantOf(text, "PUDDLE_OPTION_KEEP_STATIC_NOISE");
        ASSERT_TRUE(keepNoise.has_value());
        EXPECT_EQ(static_cast<std::uint32_t>(*keepNoise), PM::optionKeepStaticNoise);
        const std::optional<double> staticBit = hexConstantOf(text, "PUDDLE_TEXEL_STATIC");
        ASSERT_TRUE(staticBit.has_value());
        EXPECT_EQ(static_cast<std::uint32_t>(*staticBit), PM::texelStaticBit);
        EXPECT_EQ(PM::texelStaticBit, static_cast<std::uint32_t>(Puddle::texelStatic));
    }

    TEST_P(PuddleMapTopsShader, the_tops_are_read_the_way_the_cpp_twin_does)
    {
        const std::string text = readShader(GetParam());
        ASSERT_FALSE(text.empty());
        // base + (low 16 bits) / 8, from the region after the texels: bases by slot, then the cells' top words.
        EXPECT_NE(text.find("& 0xFFFFu) * 0.125"), std::string::npos);
        EXPECT_NE(text.find("topsBase + cells + uint(slot) * uint(side * side) + local"), std::string::npos);
        EXPECT_NE(text.find("[topsBase + uint(slot)]"), std::string::npos);
        EXPECT_FLOAT_EQ(Render::Puddle::heightStepsPerUnit, 8.f);
        // The test: objects always, terrain only where a static tap contributes; objects must face up.
        EXPECT_NE(text.find("(objectMode || staticWeight > 0.0)"), std::string::npos);
        EXPECT_NE(text.find("abs(worldPos.z - s.top) <= puddleZTolerance"), std::string::npos);
        EXPECT_NE(text.find("normalZ >= puddleObjectMinNormalZ"), std::string::npos);
        // Static columns without a pit keep the noise puddles on objects, when the header asks for it.
        EXPECT_NE(text.find("staticWeight >= 0.5"), std::string::npos);
        EXPECT_NE(text.find("PUDDLE_OPTION_KEEP_STATIC_NOISE"), std::string::npos);
        // A buffer without tops cannot be tested.
        EXPECT_NE(text.find("topWeight > 0.0"), std::string::npos);
        // The statics debug view.
        EXPECT_NE(text.find("mode == 4u"), std::string::npos);
    }

    INSTANTIATE_TEST_SUITE_P(
        Twins, PuddleMapTopsShader, testing::Values("lib/material/puddlemap.glsl", "lib/material/puddlemap.slang"));

    TEST(PuddleMapTopsShaderUse, terrain_asks_without_the_object_test_and_objects_ask_with_it)
    {
        for (const char* file : { "terrain/main.frag", "terrain/terrain.frag.slang" })
        {
            const std::string text = readShader(file);
            ASSERT_FALSE(text.empty()) << file;
            // Stage W-e: the terrain passes its real normal's Z (the topographic fallback needs the flatness).
            EXPECT_NE(text.find("puddleShadeAt("), std::string::npos) << file;
            EXPECT_NE(text.find(".z, false)"), std::string::npos) << file;
        }
        for (const char* file : { "default/main.frag", "default/default.frag.slang" })
        {
            const std::string text = readShader(file);
            ASSERT_FALSE(text.empty()) << file;
            EXPECT_NE(text.find("puddleShadeAt("), std::string::npos) << file;
            EXPECT_NE(text.find(", true);"), std::string::npos) << file;
            // The puddle the map decides is darkened by its own depth tint, and the ripples are gated by depth.
            EXPECT_NE(text.find("puddleDepthTint(wetPm.depth)"), std::string::npos) << file;
            EXPECT_NE(text.find("wetRippleGate"), std::string::npos) << file;
            EXPECT_NE(text.find("puddleDebugColour(wetPm)"), std::string::npos) << file;
        }
    }

    TEST(PuddleMapTopsRenderer, the_renderer_ends_the_maps_worker_before_the_world_and_its_store_go)
    {
        // The worker reads the ESM store through OMW::StorageHeights and StaticRefs, and the renderer outlives the
        // World (World::~World calls releaseWorldLod). The fix of the review of stage W-d.
        const std::string text = readSource("openmw/mwrender/rendermanager.cpp");
        ASSERT_FALSE(text.empty());
        const std::size_t begin = text.find("void RenderManager::releaseWorldLod()");
        ASSERT_NE(begin, std::string::npos);
        const std::size_t end = text.find("\n    }", begin);
        ASSERT_NE(end, std::string::npos);
        EXPECT_NE(text.substr(begin, end - begin).find("releasePuddleMap()"), std::string::npos);
        const std::size_t release = text.find("void RenderManager::releasePuddleMap()");
        ASSERT_NE(release, std::string::npos);
        const std::size_t releaseEnd = text.find("\n    }", release);
        const std::string body = text.substr(release, releaseEnd - release);
        EXPECT_NE(body.find("mPuddleMap.reset()"), std::string::npos);
        EXPECT_NE(body.find("packOff"), std::string::npos);
        // setStore ends it through the same function, never by a bare reset that leaves the buffer on.
        const std::size_t setStore = text.find("void RenderManager::setStore(");
        ASSERT_NE(setStore, std::string::npos);
        const std::size_t setStoreEnd = text.find("\n    }", setStore);
        const std::string setStoreBody = text.substr(setStore, setStoreEnd - setStore);
        EXPECT_NE(setStoreBody.find("releasePuddleMap()"), std::string::npos);
        EXPECT_EQ(setStoreBody.find("mPuddleMap.reset()"), std::string::npos);
    }
}
