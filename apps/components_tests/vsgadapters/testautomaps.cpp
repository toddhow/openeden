// Tests for the renderer-free auto-detection of object normal and specular maps
// (task 0012, Step 1 (V4h), "Finding the maps" and "Two-channel normal maps").
//
// Written from the spec alone. Assumptions and ambiguities:
// - Everything lives in namespace vsgAdapters; AutoMapSettings has the fields
//   autoUseNormalMaps, autoUseSpecularMaps, normalMapPattern, normalHeightMapPattern and
//   specularMapPattern, and is set field by field here (no defaults assumed).
// - autoMapPath replaces the last '.' of the whole path, literally, as
//   Misc::StringUtils::replaceLast did. So a dot in a directory name is used when the file
//   name has none ("textures/v1.0/foo" -> "textures/v1_n.0/foo"). The test pins this
//   literal reading; it is not a claim that such a path is useful.
// - When the normal-height candidate exists but equals bumpPath, the spec does not say
//   whether the plain normal candidate is then tried (OpenMW tried it only when the
//   normal-height file did not exist). The tests avoid that case: the rejection is tested
//   with no other candidate present.
// - The exists callback is a lookup in a std::set of exact paths; no case folding or
//   path normalisation is assumed inside findAutoMaps.
// - isTwoChannelFormat is tested on the formats the spec names (R8G8_*, R16G16_*, BC5);
//   other members of those families (UINT, SINT, SRGB, ...) are not pinned.

#include <components/vsgadapters/nif/automaps.hpp>

#include <vsg/vk/vulkan.h>

#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace
{
    vsgAdapters::AutoMapSettings settings(bool normals, bool speculars)
    {
        vsgAdapters::AutoMapSettings result;
        result.autoUseNormalMaps = normals;
        result.autoUseSpecularMaps = speculars;
        result.normalMapPattern = "_n";
        result.normalHeightMapPattern = "_nh";
        result.specularMapPattern = "_spec";
        return result;
    }

    std::function<bool(std::string_view)> existsIn(const std::set<std::string>& files)
    {
        return [files](std::string_view path) { return files.count(std::string(path)) != 0; };
    }

    vsgAdapters::AutoMaps find(std::string_view diffuse, std::string_view bump,
        const vsgAdapters::AutoMapSettings& config, const std::set<std::string>& files)
    {
        return vsgAdapters::findAutoMaps(diffuse, bump, config, existsIn(files));
    }

    void expectNothing(const vsgAdapters::AutoMaps& maps)
    {
        EXPECT_TRUE(maps.normalMap.empty());
        EXPECT_FALSE(maps.normalHeight);
        EXPECT_TRUE(maps.specularMap.empty());
    }

    // ---- autoMapPath ---------------------------------------------------------------------------

    TEST(VsgAdaptersAutoMapsTest, autoMapPathInsertsThePatternBeforeTheExtension)
    {
        EXPECT_EQ(vsgAdapters::autoMapPath("textures/tx_wood.dds", "_n"), "textures/tx_wood_n.dds");
        EXPECT_EQ(vsgAdapters::autoMapPath("textures/tx_wood.dds", "_nh"), "textures/tx_wood_nh.dds");
        EXPECT_EQ(vsgAdapters::autoMapPath("textures/tx_wood.dds", "_spec"), "textures/tx_wood_spec.dds");
    }

    TEST(VsgAdaptersAutoMapsTest, autoMapPathUsesTheLastDot)
    {
        EXPECT_EQ(vsgAdapters::autoMapPath("textures/a.b.c.dds", "_n"), "textures/a.b.c_n.dds");
    }

    TEST(VsgAdaptersAutoMapsTest, autoMapPathWithoutDotIsEmpty)
    {
        EXPECT_EQ(vsgAdapters::autoMapPath("textures/tx_wood", "_n"), "");
        EXPECT_EQ(vsgAdapters::autoMapPath("", "_n"), "");
    }

    TEST(VsgAdaptersAutoMapsTest, autoMapPathReplacesALiteralLastDotEvenInADirectory)
    {
        // Literal "last '.'" reading (see the header comment).
        EXPECT_EQ(vsgAdapters::autoMapPath("textures/v1.0/foo", "_n"), "textures/v1_n.0/foo");
    }

    TEST(VsgAdaptersAutoMapsTest, autoMapPathWithEmptyPatternKeepsThePath)
    {
        EXPECT_EQ(vsgAdapters::autoMapPath("textures/a.dds", ""), "textures/a.dds");
    }

    // ---- findAutoMaps: normal maps -------------------------------------------------------------

    TEST(VsgAdaptersAutoMapsTest, normalHeightIsPreferredOverNormal)
    {
        const auto maps = find("textures/a.dds", "", settings(true, false),
            { "textures/a.dds", "textures/a_n.dds", "textures/a_nh.dds" });
        EXPECT_EQ(maps.normalMap, "textures/a_nh.dds");
        EXPECT_TRUE(maps.normalHeight);
        EXPECT_TRUE(maps.specularMap.empty());
    }

    TEST(VsgAdaptersAutoMapsTest, normalHeightAlone)
    {
        const auto maps = find("textures/a.dds", "", settings(true, false), { "textures/a_nh.dds" });
        EXPECT_EQ(maps.normalMap, "textures/a_nh.dds");
        EXPECT_TRUE(maps.normalHeight);
    }

    TEST(VsgAdaptersAutoMapsTest, plainNormalWhenNoNormalHeight)
    {
        const auto maps = find("textures/a.dds", "", settings(true, false), { "textures/a.dds", "textures/a_n.dds" });
        EXPECT_EQ(maps.normalMap, "textures/a_n.dds");
        EXPECT_FALSE(maps.normalHeight);
        EXPECT_TRUE(maps.specularMap.empty());
    }

    TEST(VsgAdaptersAutoMapsTest, nothingFoundWhenNoCandidateExists)
    {
        expectNothing(find("textures/a.dds", "", settings(true, true), { "textures/a.dds", "textures/b_n.dds" }));
    }

    TEST(VsgAdaptersAutoMapsTest, normalMapEqualToBumpMapIsRejected)
    {
        const auto maps = find("textures/a.dds", "textures/a_n.dds", settings(true, false), { "textures/a_n.dds" });
        EXPECT_TRUE(maps.normalMap.empty());
        EXPECT_FALSE(maps.normalHeight);
    }

    TEST(VsgAdaptersAutoMapsTest, normalHeightMapEqualToBumpMapIsRejectedAndNotMarkedAsHeight)
    {
        const auto maps = find("textures/a.dds", "textures/a_nh.dds", settings(true, false), { "textures/a_nh.dds" });
        EXPECT_TRUE(maps.normalMap.empty());
        EXPECT_FALSE(maps.normalHeight);
    }

    TEST(VsgAdaptersAutoMapsTest, differentBumpMapDoesNotRejectTheNormalMap)
    {
        const auto maps
            = find("textures/a.dds", "textures/a_bump.dds", settings(true, false), { "textures/a_n.dds" });
        EXPECT_EQ(maps.normalMap, "textures/a_n.dds");
        EXPECT_FALSE(maps.normalHeight);
    }

    TEST(VsgAdaptersAutoMapsTest, bumpRejectionKeepsTheSpecularMap)
    {
        const auto maps = find("textures/a.dds", "textures/a_n.dds", settings(true, true),
            { "textures/a_n.dds", "textures/a_spec.dds" });
        EXPECT_TRUE(maps.normalMap.empty());
        EXPECT_FALSE(maps.normalHeight);
        EXPECT_EQ(maps.specularMap, "textures/a_spec.dds");
    }

    TEST(VsgAdaptersAutoMapsTest, normalMapsOffFindsNoNormalMap)
    {
        const auto maps = find("textures/a.dds", "", settings(false, true),
            { "textures/a_n.dds", "textures/a_nh.dds", "textures/a_spec.dds" });
        EXPECT_TRUE(maps.normalMap.empty());
        EXPECT_FALSE(maps.normalHeight);
        EXPECT_EQ(maps.specularMap, "textures/a_spec.dds");
    }

    // ---- findAutoMaps: specular maps -----------------------------------------------------------

    TEST(VsgAdaptersAutoMapsTest, specularMapFoundWhenOn)
    {
        const auto maps = find("textures/a.dds", "", settings(false, true), { "textures/a_spec.dds" });
        EXPECT_EQ(maps.specularMap, "textures/a_spec.dds");
        EXPECT_TRUE(maps.normalMap.empty());
    }

    TEST(VsgAdaptersAutoMapsTest, specularMapIgnoredWhenOff)
    {
        const auto maps = find("textures/a.dds", "", settings(true, false), { "textures/a_n.dds", "textures/a_spec.dds" });
        EXPECT_TRUE(maps.specularMap.empty());
        EXPECT_EQ(maps.normalMap, "textures/a_n.dds");
    }

    TEST(VsgAdaptersAutoMapsTest, normalAndSpecularTogether)
    {
        const auto maps = find("textures/a.dds", "", settings(true, true),
            { "textures/a_nh.dds", "textures/a_n.dds", "textures/a_spec.dds" });
        EXPECT_EQ(maps.normalMap, "textures/a_nh.dds");
        EXPECT_TRUE(maps.normalHeight);
        EXPECT_EQ(maps.specularMap, "textures/a_spec.dds");
    }

    // ---- findAutoMaps: switches, empty input, patterns -----------------------------------------

    TEST(VsgAdaptersAutoMapsTest, bothSwitchesOffFindNothingEvenIfFilesExist)
    {
        expectNothing(find("textures/a.dds", "", settings(false, false),
            { "textures/a.dds", "textures/a_n.dds", "textures/a_nh.dds", "textures/a_spec.dds" }));
    }

    TEST(VsgAdaptersAutoMapsTest, emptyDiffusePathFindsNothing)
    {
        // Paths a careless implementation could build from an empty diffuse path.
        expectNothing(find("", "", settings(true, true), { "", "_n.", "_nh.", "_spec.", "_n", "_nh", "_spec" }));
    }

    TEST(VsgAdaptersAutoMapsTest, diffusePathWithoutDotFindsNothing)
    {
        expectNothing(find("textures/a", "", settings(true, true), { "textures/a", "textures/a_n", "textures/a_spec" }));
    }

    TEST(VsgAdaptersAutoMapsTest, customPatternsAreUsed)
    {
        vsgAdapters::AutoMapSettings config = settings(true, true);
        config.normalMapPattern = "_norm";
        config.normalHeightMapPattern = "_height";
        config.specularMapPattern = "_s";

        const auto plain = find("textures/a.dds", "", config,
            { "textures/a_n.dds", "textures/a_nh.dds", "textures/a_spec.dds", "textures/a_norm.dds", "textures/a_s.dds" });
        EXPECT_EQ(plain.normalMap, "textures/a_norm.dds");
        EXPECT_FALSE(plain.normalHeight);
        EXPECT_EQ(plain.specularMap, "textures/a_s.dds");

        const auto height = find("textures/a.dds", "", config, { "textures/a_norm.dds", "textures/a_height.dds" });
        EXPECT_EQ(height.normalMap, "textures/a_height.dds");
        EXPECT_TRUE(height.normalHeight);
        EXPECT_TRUE(height.specularMap.empty());
    }

    TEST(VsgAdaptersAutoMapsTest, defaultPatternFilesAreIgnoredWithCustomPatterns)
    {
        vsgAdapters::AutoMapSettings config = settings(true, true);
        config.normalMapPattern = "_norm";
        config.normalHeightMapPattern = "_height";
        config.specularMapPattern = "_s";
        expectNothing(find("textures/a.dds", "", config, { "textures/a_n.dds", "textures/a_nh.dds", "textures/a_spec.dds" }));
    }

    TEST(VsgAdaptersAutoMapsTest, diffusePathWithSeveralDotsUsesTheLastOne)
    {
        const auto maps = find("textures/a.b.dds", "", settings(true, true),
            { "textures/a_n.b.dds", "textures/a.b_n.dds", "textures/a.b_spec.dds" });
        EXPECT_EQ(maps.normalMap, "textures/a.b_n.dds");
        EXPECT_EQ(maps.specularMap, "textures/a.b_spec.dds");
    }

    // ---- isTwoChannelFormat --------------------------------------------------------------------

    TEST(VsgAdaptersAutoMapsTest, twoChannelFormats)
    {
        EXPECT_TRUE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_R8G8_UNORM));
        EXPECT_TRUE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_R8G8_SNORM));
        EXPECT_TRUE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_R16G16_UNORM));
        EXPECT_TRUE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_R16G16_SFLOAT));
        EXPECT_TRUE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_BC5_UNORM_BLOCK));
        EXPECT_TRUE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_BC5_SNORM_BLOCK));
    }

    TEST(VsgAdaptersAutoMapsTest, otherFormatsAreNotTwoChannel)
    {
        EXPECT_FALSE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_UNDEFINED));
        EXPECT_FALSE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_R8_UNORM));
        EXPECT_FALSE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_R8G8B8_UNORM));
        EXPECT_FALSE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_R8G8B8A8_UNORM));
        EXPECT_FALSE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_B8G8R8A8_UNORM));
        EXPECT_FALSE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_BC1_RGB_UNORM_BLOCK));
        EXPECT_FALSE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_BC1_RGBA_UNORM_BLOCK));
        EXPECT_FALSE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_BC2_UNORM_BLOCK));
        EXPECT_FALSE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_BC3_UNORM_BLOCK));
        EXPECT_FALSE(vsgAdapters::isTwoChannelFormat(VK_FORMAT_BC4_UNORM_BLOCK));
    }
}
