#include <compat_morrowind/scene_options.hpp>

#include <components/nif/exception.hpp>
#include <components/nif/niffile.hpp>
#include <components/nifscene/build.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../nif/nifbuilder.hpp"

// Task 0082, item (b): Morrowind's reading rules are supplied by the compatibility pack. The nif reader keeps no
// Morrowind name of its own: it applies the rule it is given. These tests pin that the pack's options give the scene
// the reader's default (fork point) behaviour, and that the reader honours the name it is told.
namespace
{
    using Nif::Testing::ByteWriter;

    constexpr VFS::Path::NormalizedView path("compat-scene-test.nif");

    // A 4.0.0.2 file: record 0 is a NiNode called `rootName` translated by (7, 0, 0); record 1 a NiNode child.
    std::string morrowindFile(const std::string& rootName)
    {
        ByteWriter out;
        out.raw("NetImmerse File Format, Version 4.0.0.2\n");
        out.put<std::uint32_t>(0x04000002);
        out.put<std::uint32_t>(2);
        auto node = [&](const std::string& name, float x, const std::vector<std::int32_t>& children) {
            out.sized("NiNode");
            out.sized(name);
            out.put<std::int32_t>(-1);
            out.put<std::int32_t>(-1);
            out.put<std::uint16_t>(0);
            out.floats({ x, 0.f, 0.f });
            out.floats({ 1, 0, 0, 0, 1, 0, 0, 0, 1 });
            out.put(1.f);
            out.floats({ 0, 0, 0 });
            out.put<std::int32_t>(0);
            out.put<std::int32_t>(0);
            out.put(static_cast<std::int32_t>(children.size()));
            for (std::int32_t child : children)
                out.put(child);
            out.put<std::int32_t>(0);
        };
        node(rootName, 7.f, { 1 });
        node("child", 3.f, {});
        out.put<std::uint32_t>(1);
        out.put<std::int32_t>(0);
        return out.bytes();
    }

    NifScene::Scene read(const std::string& bytes, const NifScene::SceneOptions& options)
    {
        auto input = std::make_unique<std::istringstream>(bytes);
        input->exceptions(std::ios::failbit | std::ios::badbit);
        return NifScene::readScene(std::move(input), path, options);
    }

    NifScene::SceneOptions defaultReaderOptions()
    {
        NifScene::SceneOptions options;
        options.mRead = Nif::Reader::getDefaultReadOptions();
        return options;
    }

    TEST(CompatMorrowindScene, the_read_options_are_morrowinds_versions_with_the_root_rule_and_its_exempt_name)
    {
        const Nif::ReadOptions options = compat_morrowind::morrowindReadOptions();
        EXPECT_EQ(options.mAllowedVersions, Nif::VersionSet::Morrowind);
        EXPECT_FALSE(options.mStrict);
        EXPECT_TRUE(options.mMorrowindRootRule);
        EXPECT_EQ(options.mRootRuleExemptName, "bip01");
    }

    // Task 0083 review 1, finding 5: the engine does not call the pack for its read options (the core may not depend
    // on an extension), it relies on ReadOptions' own member defaults being Morrowind's. This pins every member of
    // the pack's value against a default-constructed ReadOptions, so a changed default or a changed pack value fails.
    TEST(CompatMorrowindScene, the_pack_read_options_equal_the_read_options_member_defaults_field_by_field)
    {
        const Nif::ReadOptions pack = compat_morrowind::morrowindReadOptions();
        const Nif::ReadOptions core;
        EXPECT_EQ(pack.mAllowedVersions, core.mAllowedVersions);
        EXPECT_EQ(pack.mStrict, core.mStrict);
        EXPECT_EQ(pack.mMorrowindRootRule, core.mMorrowindRootRule);
        EXPECT_EQ(pack.mRootRuleExemptName, core.mRootRuleExemptName);
    }

    TEST(CompatMorrowindScene, the_scene_options_carry_the_read_options)
    {
        const NifScene::SceneOptions options = compat_morrowind::morrowindSceneOptions();
        const Nif::ReadOptions expected = compat_morrowind::morrowindReadOptions();
        EXPECT_EQ(options.mRead.mAllowedVersions, expected.mAllowedVersions);
        EXPECT_EQ(options.mRead.mMorrowindRootRule, expected.mMorrowindRootRule);
        EXPECT_EQ(options.mRead.mRootRuleExemptName, expected.mRootRuleExemptName);
    }

    TEST(CompatMorrowindScene, the_pack_options_make_the_scene_the_reader_makes_by_default)
    {
        for (const char* name : { "Scene Root", "bip01", "BIP01", "Bip01 Pelvis" })
        {
            const std::string bytes = morrowindFile(name);
            EXPECT_EQ(NifScene::dump(read(bytes, compat_morrowind::morrowindSceneOptions())),
                NifScene::dump(read(bytes, defaultReaderOptions())))
                << name;
        }
    }

    TEST(CompatMorrowindScene, a_root_not_called_bip01_loses_its_transform_under_the_pack_options)
    {
        const NifScene::Scene scene = read(morrowindFile("Scene Root"), compat_morrowind::morrowindSceneOptions());
        ASSERT_EQ(scene.mNodes.size(), 2u);
        EXPECT_EQ(scene.mNodes[0].mTransform.mTranslation, (NifScene::Vec3{ 0.f, 0.f, 0.f }));
        // Only record 0 is touched.
        EXPECT_EQ(scene.mNodes[1].mTransform.mTranslation, (NifScene::Vec3{ 3.f, 0.f, 0.f }));
    }

    TEST(CompatMorrowindScene, a_root_called_bip01_in_any_case_keeps_its_transform)
    {
        for (const char* name : { "bip01", "Bip01", "BIP01" })
        {
            const NifScene::Scene scene = read(morrowindFile(name), compat_morrowind::morrowindSceneOptions());
            EXPECT_EQ(scene.mNodes.at(0).mTransform.mTranslation, (NifScene::Vec3{ 7.f, 0.f, 0.f })) << name;
        }
    }

    TEST(CompatMorrowindScene, the_reader_exempts_the_name_it_is_told_and_not_a_fixed_one)
    {
        NifScene::SceneOptions options = compat_morrowind::morrowindSceneOptions();
        options.mRead.mRootRuleExemptName = "Scene Root";
        // Now "Scene Root" keeps its transform and "bip01" loses it.
        EXPECT_EQ(read(morrowindFile("Scene Root"), options).mNodes.at(0).mTransform.mTranslation,
            (NifScene::Vec3{ 7.f, 0.f, 0.f }));
        EXPECT_EQ(read(morrowindFile("bip01"), options).mNodes.at(0).mTransform.mTranslation,
            (NifScene::Vec3{ 0.f, 0.f, 0.f }));
    }

    TEST(CompatMorrowindScene, the_reader_default_exempt_name_is_still_bip01)
    {
        // The game's default path does not go through the pack (the core never depends on an extension): its
        // behaviour is the fork point's.
        const Nif::ReadOptions defaults;
        EXPECT_EQ(defaults.mRootRuleExemptName, "bip01");
        EXPECT_TRUE(defaults.mMorrowindRootRule);
    }

    std::string lowerExtension(const std::filesystem::path& file)
    {
        std::string extension = file.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    // Not run by default: with OPENEDEN_NIF_MORROWIND_DIR set to a folder of the user's own loose Morrowind meshes,
    // reads every .nif twice (the reader's default options and the pack's) and compares the scene dumps, then prints
    // aggregates only. Nothing is written.
    TEST(CompatMorrowindScene, real_files_give_the_same_scene_with_the_pack_options_when_asked)
    {
        const char* dir = std::getenv("OPENEDEN_NIF_MORROWIND_DIR");
        if (dir == nullptr)
            GTEST_SKIP() << "OPENEDEN_NIF_MORROWIND_DIR is not set";
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
        {
            if (entry.is_regular_file() && lowerExtension(entry.path()) == ".nif")
                files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        std::size_t readCount = 0;
        std::size_t errors = 0;
        std::size_t different = 0;
        std::size_t shapes = 0;
        std::size_t skinned = 0;
        std::size_t influences = 0;
        for (const auto& file : files)
        {
            std::string bytes;
            {
                std::ifstream in(file, std::ios::binary);
                std::ostringstream all;
                all << in.rdbuf();
                bytes = all.str();
            }
            try
            {
                const NifScene::Scene a = read(bytes, defaultReaderOptions());
                const NifScene::Scene b = read(bytes, compat_morrowind::morrowindSceneOptions());
                ++readCount;
                if (NifScene::dump(a) != NifScene::dump(b))
                    ++different;
                for (const auto& g : b.mGeometries)
                {
                    ++shapes;
                    if (g.mSkin != NifScene::sNone)
                        ++skinned;
                    influences += g.mInfluences.size();
                }
            }
            catch (const std::exception&)
            {
                ++errors;
            }
        }
        std::cout << "[morrowind scene] files " << files.size() << " read " << readCount << " errors " << errors
                  << " different " << different << " geometries " << shapes << " skinned " << skinned << " influences "
                  << influences << "\n";
        EXPECT_EQ(different, 0u);
    }
}
