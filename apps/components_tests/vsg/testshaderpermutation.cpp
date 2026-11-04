// Task 0072, stage a: the pure parts of the Slang permutation files (components/vsgutil/shaderpermutation.hpp), written
// before the implementation.
#include <gtest/gtest.h>

#include <components/vsgutil/shaderpermutation.hpp>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace
{
    using vsgUtil::canonicalDefines;
    using vsgUtil::parsePermutationManifest;
    using vsgUtil::selectPermutation;

    TEST(ShaderPermutation, canonical_defines_are_sorted_and_space_separated)
    {
        EXPECT_EQ(canonicalDefines({ "MATERIAL", "DIFFUSE_MAP", "NORMAL" }), "DIFFUSE_MAP MATERIAL NORMAL");
    }

    TEST(ShaderPermutation, the_empty_set_is_the_empty_text)
    {
        EXPECT_EQ(canonicalDefines({}), "");
    }

    TEST(ShaderPermutation, byte_order_puts_upper_case_before_lower_case)
    {
        EXPECT_EQ(canonicalDefines({ "b", "B", "_a", "A1" }), "A1 B _a b");
    }

    TEST(ShaderPermutation, a_name_that_is_not_an_identifier_is_refused)
    {
        EXPECT_THROW(canonicalDefines({ "GOOD", "BAD NAME" }), std::invalid_argument);
        EXPECT_THROW(canonicalDefines({ "1ST" }), std::invalid_argument);
        EXPECT_THROW(canonicalDefines({ "" }), std::invalid_argument);
        EXPECT_THROW(canonicalDefines({ "A=1" }), std::invalid_argument);
        EXPECT_THROW(canonicalDefines({ std::string(65, 'A') }), std::invalid_argument);
        EXPECT_NO_THROW(canonicalDefines({ std::string(64, 'A') }));
    }

    TEST(ShaderPermutation, a_manifest_maps_define_lists_to_files)
    {
        const auto manifest = parsePermutationManifest(
            "main.frag.p0000.spv\t\n"
            "main.frag.p0001.spv\tDIFFUSE_MAP MATERIAL\n");
        ASSERT_EQ(manifest.mFiles.size(), 2u);
        EXPECT_EQ(manifest.mFiles.at(""), "main.frag.p0000.spv");
        EXPECT_EQ(manifest.mFiles.at("DIFFUSE_MAP MATERIAL"), "main.frag.p0001.spv");
    }

    TEST(ShaderPermutation, windows_line_ends_and_a_missing_last_newline_are_accepted)
    {
        const auto manifest = parsePermutationManifest("main.vert.p0000.spv\tNORMAL\r\nmain.vert.p0001.spv\tCOLOR");
        ASSERT_EQ(manifest.mFiles.size(), 2u);
        EXPECT_EQ(manifest.mFiles.at("COLOR"), "main.vert.p0001.spv");
    }

    TEST(ShaderPermutation, the_plain_file_name_without_an_index_is_accepted)
    {
        const auto manifest = parsePermutationManifest("main.comp.spv\t\n");
        EXPECT_EQ(manifest.mFiles.at(""), "main.comp.spv");
    }

    TEST(ShaderPermutation, a_bad_manifest_is_refused)
    {
        // no tab
        EXPECT_THROW(parsePermutationManifest("main.frag.p0000.spv\n"), std::runtime_error);
        // a directory in the file name
        EXPECT_THROW(parsePermutationManifest("../main.frag.p0000.spv\t\n"), std::runtime_error);
        EXPECT_THROW(parsePermutationManifest("sub/main.frag.p0000.spv\t\n"), std::runtime_error);
        // an unknown stage or index shape
        EXPECT_THROW(parsePermutationManifest("main.geom.p0000.spv\t\n"), std::runtime_error);
        EXPECT_THROW(parsePermutationManifest("main.frag.p12.spv\t\n"), std::runtime_error);
        // a define list that is not canonical (unsorted, double space, trailing space, bad name)
        EXPECT_THROW(parsePermutationManifest("main.frag.p0000.spv\tMATERIAL DIFFUSE_MAP\n"), std::runtime_error);
        EXPECT_THROW(parsePermutationManifest("main.frag.p0000.spv\tA  B\n"), std::runtime_error);
        EXPECT_THROW(parsePermutationManifest("main.frag.p0000.spv\tA \n"), std::runtime_error);
        EXPECT_THROW(parsePermutationManifest("main.frag.p0000.spv\tA-B\n"), std::runtime_error);
        // the same define list twice, or the same file twice
        EXPECT_THROW(parsePermutationManifest("main.frag.p0000.spv\tA\nmain.frag.p0001.spv\tA\n"), std::runtime_error);
        EXPECT_THROW(parsePermutationManifest("main.frag.p0000.spv\tA\nmain.frag.p0000.spv\tB\n"), std::runtime_error);
        // an empty line in the middle
        EXPECT_THROW(
            parsePermutationManifest("main.frag.p0000.spv\tA\n\nmain.frag.p0001.spv\tB\n"), std::runtime_error);
    }

    TEST(ShaderPermutation, the_error_names_the_line)
    {
        try
        {
            parsePermutationManifest("main.frag.p0000.spv\tA\nmain.frag.p0001.spv\tB A\n");
            FAIL() << "no exception";
        }
        catch (const std::runtime_error& e)
        {
            EXPECT_NE(std::string(e.what()).find("line 2"), std::string::npos) << e.what();
        }
    }

    TEST(ShaderPermutation, a_manifest_over_the_limits_is_refused)
    {
        std::string text;
        for (int i = 0; i < 4097; ++i)
        {
            char name[32];
            std::snprintf(name, sizeof(name), "main.frag.p%04d.spv", i % 10000);
            text += std::string(name) + "\tD" + std::to_string(i) + "\n";
        }
        EXPECT_THROW(parsePermutationManifest(text), std::runtime_error);
        EXPECT_THROW(parsePermutationManifest(std::string(65 * 1024, 'x')), std::runtime_error);
    }

    TEST(ShaderPermutation, selection_needs_the_exact_define_set)
    {
        const auto manifest = parsePermutationManifest(
            "main.frag.p0000.spv\tDIFFUSE_MAP MATERIAL\n"
            "main.frag.p0001.spv\t\n");
        EXPECT_EQ(selectPermutation(manifest, { "MATERIAL", "DIFFUSE_MAP" }), "main.frag.p0000.spv");
        EXPECT_EQ(selectPermutation(manifest, {}), "main.frag.p0001.spv");
        // a subset or a superset is a different shader
        EXPECT_EQ(selectPermutation(manifest, { "MATERIAL" }), std::nullopt);
        EXPECT_EQ(selectPermutation(manifest, { "MATERIAL", "DIFFUSE_MAP", "NORMAL" }), std::nullopt);
        // a define set that cannot be canonical selects nothing instead of throwing
        EXPECT_EQ(selectPermutation(manifest, { "NOT VALID" }), std::nullopt);
    }

    TEST(ShaderPermutation, trace_lines_use_the_canonical_list)
    {
        EXPECT_EQ(vsgUtil::traceLine("sky/moon/main.frag", { "MATERIAL", "DIFFUSE_MAP" }),
            "sky/moon/main.frag\tDIFFUSE_MAP MATERIAL");
        EXPECT_EQ(vsgUtil::traceLine("gui/gui.vert", {}), "gui/gui.vert\t");
    }

    TEST(ShaderPermutation, the_preference_is_read_from_the_options)
    {
        EXPECT_FALSE(vsgUtil::prefersSlang(nullptr));
        auto options = vsg::Options::create();
        EXPECT_FALSE(vsgUtil::prefersSlang(options.get()));
        options->setValue(vsgUtil::preferSlangKey, true);
        EXPECT_TRUE(vsgUtil::prefersSlang(options.get()));
        options->setValue(vsgUtil::preferSlangKey, false);
        EXPECT_FALSE(vsgUtil::prefersSlang(options.get()));
    }
}
