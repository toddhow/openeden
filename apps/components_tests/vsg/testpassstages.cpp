// Task 0072, stage g and task 0363, stage g: the stages of the passes whose pipelines C++ builds by hand
// (vsgUtil::readPassStages, components/vsgutil/passstages.hpp). Their shaders are Slang twins listed per define set at
// build time; there is no GLSL beside them, so a stage with no twin gives no stages and the reason.
#include <gtest/gtest.h>

#include <components/vsgutil/passstages.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/io/spirv.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/utils/SharedObjects.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    // A SPIR-V module that declares one float specialization constant with the given id, as
    // vsgUtil::addSettingsConstants reads it; the bound (word 3) tells two such modules apart.
    std::vector<uint32_t> twinWords(uint32_t bound, uint32_t constantId)
    {
        const auto op = [](uint32_t opcode, uint32_t count) { return (count << 16) | opcode; };
        std::vector<uint32_t> words = { 0x07230203u, 0x00010000u, 0u, bound, 0u };
        words.insert(words.end(), { op(17, 2), 1u }); // OpCapability Shader
        words.insert(words.end(), { op(22, 3), 2u, 32u }); // %2 = OpTypeFloat 32
        words.insert(words.end(), { op(71, 4), 10u, 1u, constantId }); // OpDecorate %10 SpecId constantId
        words.insert(words.end(), { op(50, 4), 2u, 10u, 0x3f000000u }); // %10 = OpSpecConstant 0.5
        return words;
    }
    const std::vector<uint32_t> sVertexTwin = twinWords(100, 1004);
    const std::vector<uint32_t> sFragmentTwin = twinWords(200, 1004);
    const std::vector<uint32_t> sPlainFragmentTwin = twinWords(300, 1004);

    // A folder of Slang twins, read as the game reads the shaders (with shared objects).
    class PassFolder
    {
    public:
        PassFolder()
            : mDir(std::filesystem::temp_directory_path() / uniqueName())
        {
            std::filesystem::create_directories(mDir / "post");
            mOptions = vsg::Options::create();
            mOptions->paths = { vsg::Path(mDir.generic_string()) };
            mOptions->readerWriters = { vsg::spirv::create() };
            mOptions->sharedObjects = vsg::SharedObjects::create();
            auto constants = vsgUtil::SettingsConstants::create();
            constants->constants[1004] = vsg::floatValue::create(0.25f);
            mOptions->setObject(vsgUtil::settingsConstantsKey, constants);
        }
        ~PassFolder()
        {
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        void writeText(const std::string& file, const std::string& text)
        {
            std::ofstream out(mDir / file, std::ios::binary);
            out << text;
        }
        void write(const std::string& file, const std::vector<uint32_t>& words)
        {
            std::ofstream out(mDir / file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(words.data()),
                static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
        }
        // The vertex twin for the set "-"; the fragment's for "RED" and, with `plain`, for "-" too. The fragment
        // stage imports the name RED.
        void writeTwins(bool vertex, bool fragment, bool plain = false)
        {
            if (vertex)
            {
                write("post/pass.vert.p00000001.spv", sVertexTwin);
                writeText("post/pass.vert.permutations", "pass.vert.p00000001.spv\t\n");
                writeText("post/pass.vert.slang-stage", "pass.slang\tvertMain\tvertex\t-\n");
            }
            if (fragment)
            {
                write("post/pass.frag.p00000002.spv", sFragmentTwin);
                std::string manifest = "pass.frag.p00000002.spv\tRED\n";
                if (plain)
                {
                    write("post/pass.frag.p00000003.spv", sPlainFragmentTwin);
                    manifest += "pass.frag.p00000003.spv\t\n";
                }
                writeText("post/pass.frag.permutations", manifest);
                writeText("post/pass.frag.slang-stage", "pass.slang\tfragMain\tfragment\tRED\n");
            }
        }
        vsg::ref_ptr<const vsg::Options> options() const { return mOptions; }

    private:
        // Manifests are cached per process by folder, so no two fixtures share one.
        static std::string uniqueName()
        {
            static std::atomic<unsigned> counter{ 0 };
            return "openeden_passstages_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "_" + std::to_string(counter++);
        }

        std::filesystem::path mDir;
        vsg::ref_ptr<vsg::Options> mOptions;
    };

    const std::vector<vsgUtil::PassStage> sRed = { { "post/pass.vert", {} }, { "post/pass.frag", { "RED" } } };

    TEST(PassStages, every_stage_takes_its_twin_and_its_constants)
    {
        PassFolder folder;
        folder.writeTwins(true, true);
        const auto stages = vsgUtil::readPassStages(sRed, folder.options());
        ASSERT_EQ(stages.size(), 2u);
        EXPECT_EQ(stages[0]->module->code, sVertexTwin);
        EXPECT_EQ(stages[1]->module->code, sFragmentTwin);
        EXPECT_TRUE(stages[0]->module->source.empty());
        EXPECT_TRUE(stages[1]->module->source.empty());
        EXPECT_EQ(stages[0]->stage, VK_SHADER_STAGE_VERTEX_BIT);
        EXPECT_EQ(stages[1]->stage, VK_SHADER_STAGE_FRAGMENT_BIT);
        EXPECT_EQ(stages[0]->entryPointName, "main");
        EXPECT_EQ(stages[0]->specializationConstants.count(1004), 1u);
        EXPECT_EQ(stages[1]->specializationConstants.count(1004), 1u);
    }

    TEST(PassStages, a_name_the_stage_does_not_import_is_not_part_of_the_set)
    {
        PassFolder folder;
        folder.writeTwins(true, true, true);
        const auto stages
            = vsgUtil::readPassStages({ { "post/pass.vert", {} }, { "post/pass.frag", { "BLUE" } } }, folder.options());
        ASSERT_EQ(stages.size(), 2u);
        EXPECT_EQ(stages[1]->module->code, sPlainFragmentTwin);
        const auto red = vsgUtil::readPassStages(sRed, folder.options());
        ASSERT_EQ(red.size(), 2u);
        EXPECT_EQ(red[1]->module->code, sFragmentTwin);
    }

    TEST(PassStages, a_stage_without_a_twin_gives_no_stages_in_either_position)
    {
        for (const bool vertexTwin : { true, false })
        {
            SCOPED_TRACE(vertexTwin ? "only the vertex twin" : "only the fragment twin");
            PassFolder folder;
            folder.writeTwins(vertexTwin, !vertexTwin);
            std::string error;
            EXPECT_TRUE(vsgUtil::readPassStages(sRed, folder.options(), &error).empty());
            EXPECT_NE(error.find(vertexTwin ? "post/pass.frag" : "post/pass.vert"), std::string::npos) << error;
        }
    }

    TEST(PassStages, an_unlisted_set_gives_no_stages_and_names_the_stage)
    {
        PassFolder folder;
        folder.writeTwins(true, true); // the fragment twin is built for RED only
        std::string error;
        const auto stages
            = vsgUtil::readPassStages({ { "post/pass.vert", {} }, { "post/pass.frag", {} } }, folder.options(), &error);
        EXPECT_TRUE(stages.empty());
        EXPECT_NE(error.find("post/pass.frag"), std::string::npos) << error;
    }

    TEST(PassStages, a_stage_the_build_did_not_make_is_named_in_the_error)
    {
        PassFolder folder;
        folder.writeTwins(true, false);
        std::string error;
        EXPECT_TRUE(
            vsgUtil::readPassStages({ { "post/pass.vert", {} }, { "post/missing.frag", {} } }, folder.options(), &error)
                .empty());
        EXPECT_NE(error.find("\"post/missing.frag\""), std::string::npos) << error;
        EXPECT_NE(error.find("slang-stage"), std::string::npos) << error;
        EXPECT_TRUE(vsgUtil::readPassStages({ { "post/pass.vert", {} } }, nullptr).empty());
    }

    TEST(PassStages, every_call_gets_a_stage_of_its_own_and_the_twin_is_shared)
    {
        PassFolder folder;
        folder.writeTwins(true, true);
        const auto first = vsgUtil::readPassStages(sRed, folder.options());
        const auto second = vsgUtil::readPassStages(sRed, folder.options());
        ASSERT_EQ(first.size(), 2u);
        ASSERT_EQ(second.size(), 2u);
        EXPECT_NE(first[1], second[1]);
        // The twin's SPIR-V is read once and never changed; what is per stage is its constants.
        EXPECT_EQ(first[1]->module->code, sFragmentTwin);
        first[1]->specializationConstants.clear();
        EXPECT_EQ(second[1]->specializationConstants.count(1004), 1u);
    }

    TEST(PassStages, a_success_leaves_no_error)
    {
        PassFolder folder;
        folder.writeTwins(true, true);
        std::string error = "an earlier error";
        EXPECT_EQ(vsgUtil::readPassStages(sRed, folder.options(), &error).size(), 2u);
        EXPECT_TRUE(error.empty()) << error;
    }
}
