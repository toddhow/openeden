// Task 0072, review of stage g: the post passes' shaders, which were GLSL strings in C++
// (components/render/tonemap.cpp, bloom.cpp, velocity.cpp, ambientocclusion.cpp and
// apps/openmw/mwrender/postprocess.cpp) until 655d60a6ff moved them into files/shaders/post.
// - Finding 3: with `[Rendering] slang shaders` off, each file compiles the way its pass compiles it now
//   (vsgUtil::readPassStages, or readShader and compileGlsl for post/omwfx.vert, with the game's shader options) to the
//   same SPIR-V as the string it replaced, compiled the way the pass compiled it before. The old strings are fixtures
//   (testpostpasses.oldstrings.inc, byte for byte from 655d60a6ff^), not golden data: both sides are compiled by this
//   build's glslang, so the test pins the equivalence of the move, not glslang's output. A post shader changed on
//   purpose later fails here; the spec of that change says what replaces the comparison.
// - Finding 2: with the switch on, the ambient occlusion pass takes its Slang twin, which gets [Rendering] ao radius,
//   ao slices and ao steps as the specialization constants 1100 to 1102 of Resource::settingsConstantTable.
// - Finding 9: every post twin's push constant members sit where its GLSL has them, by name (the tonemap's also where
//   Tonemap::setSource puts them).
#include <gtest/gtest.h>

#include <components/render/hdrmath.hpp>
#include <components/render/sssmath.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/shadersettings.hpp>
#include <components/settings/values.hpp>
#include <components/vsgutil/passstages.hpp>
#include <components/vsgutil/readshader.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/io/glsl.h>
#include <vsg/io/spirv.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/utils/FindDynamicObjects.h>
#include <vsg/utils/PropagateDynamicObjects.h>
#include <vsg/utils/ShaderCompiler.h>
#include <vsg/utils/SharedObjects.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
#include "testpostpasses.oldstrings.inc"

    // Sets [Rendering] ao radius, ao slices and ao steps for one scope and puts the earlier values back however the
    // scope ends. Resource::ShaderSettings reads them when it is made.
    class AoSettingsScope
    {
    public:
        AoSettingsScope(float radius, int slices, int steps)
            : mRadius(Settings::rendering().mAoRadius.get())
            , mSlices(Settings::rendering().mAoSlices.get())
            , mSteps(Settings::rendering().mAoSteps.get())
        {
            Settings::rendering().mAoRadius.set(radius);
            Settings::rendering().mAoSlices.set(slices);
            Settings::rendering().mAoSteps.set(steps);
        }
        ~AoSettingsScope()
        {
            Settings::rendering().mAoRadius.set(mRadius);
            Settings::rendering().mAoSlices.set(mSlices);
            Settings::rendering().mAoSteps.set(mSteps);
        }
        AoSettingsScope(const AoSettingsScope&) = delete;
        AoSettingsScope& operator=(const AoSettingsScope&) = delete;

    private:
        float mRadius;
        int mSlices;
        int mSteps;
    };

    // The game's shader options (createShaderOptions, components/resource/resourcesystem.cpp) over `paths`, made
    // now: the ShaderSettings read the settings when made.
    vsg::ref_ptr<vsg::Options> gameOptions(const std::string& path, bool preferSlang)
    {
        auto options = vsg::Options::create();
        vsg::ref_ptr<Resource::ShaderSettings> settings{ new Resource::ShaderSettings };
        options->paths = { path };
        options->readerWriters = { settings, vsg::glsl::create(), vsg::spirv::create() };
        options->sharedObjects = vsg::SharedObjects::create();
        options->findDynamicObjects = {};
        options->propagateDynamicObjects = {};
        options->setValue(vsgUtil::preferSlangKey, preferSlang);
        options->setObject(vsgUtil::settingsConstantsKey, settings->constants);
        return options;
    }

    // A string compiled as the passes compiled theirs before the move: one stage, no defines, no options.
    std::vector<uint32_t> compiledAsBefore(VkShaderStageFlagBits stage, const std::string& source)
    {
        auto shaderStage = vsg::ShaderStage::create(stage, "main", source);
        if (!vsg::ShaderCompiler::create()->compile(shaderStage))
            return {};
        return shaderStage->module->code;
    }

    // tonemap.cpp's fragmentSource at 655d60a6ff^.
    std::string tonemapSourceBefore(bool bloom, bool debug, bool ao, bool sss)
    {
        using namespace Render;
        std::ostringstream out;
        out.precision(9);
        out << "#version 450\n";
        if (ao)
            out << "#define AO 1\n";
        if (sss)
        {
            const auto& sigma = Sss::channelSigma;
            out << "#define SSS 1\n";
            out << "#define SSS_CHANNEL_SIGMA vec3(" << sigma.x << ", " << sigma.y << ", " << sigma.z << ")\n";
            out << "#define SSS_NARROW_SIGMA " << Sss::narrowSigma << "\n";
            out << "#define SSS_REACH " << Sss::reachInWidths << "\n";
            out << "#define SSS_TAPS " << Sss::tapCount << "\n";
            out << "#define SSS_EPSILON " << Sss::epsilon << "\n";
            out << "#define SSS_GAIN_MIN " << Sss::gainMin << "\n";
            out << "#define SSS_GAIN_MAX " << Sss::gainMax << "\n";
        }
        if (bloom)
            out << "#define BLOOM 1\n";
        if (debug)
            out << "#define DEBUG_VIEW 1\n";
        out << "#define DISPLAY_GAMMA " << Hdr::displayGamma << "\n";
        out << "#define REINHARD_WHITE " << Hdr::reinhardWhitePoint << "\n";
        out << "#define MAX_TARGET_VALUE " << Hdr::maxTargetValue << "\n";
        out << old_tonemap_sFragmentBody;
        return out.str();
    }

    // ambientocclusion.cpp's fragmentSource at 655d60a6ff^.
    std::string aoSourceBefore(float radius, int slices, int steps)
    {
        std::ostringstream out;
        out.precision(9);
        out << "#version 450\n";
        out << "#define AO_RADIUS " << radius << "\n";
        out << "#define AO_SLICES " << slices << "\n";
        out << "#define AO_STEPS " << steps << "\n";
        out << old_ambientocclusion_sFragmentBody;
        return out.str();
    }

    // Stage `index` of a pass read now, as the pass reads it (vsgUtil::readPassStages); empty when it fails.
    std::vector<uint32_t> compiledNow(const std::string& vertex, const std::string& fragment,
        const std::set<std::string>& defines, const vsg::ref_ptr<const vsg::Options>& options, std::size_t index)
    {
        std::string error;
        const auto stages = vsgUtil::readPassStages({ { vertex, {} }, { fragment, defines } }, options, &error);
        if (stages.size() != 2)
        {
            ADD_FAILURE() << fragment << ": " << error;
            return {};
        }
        // A GLSL stage, not a twin.
        EXPECT_FALSE(stages[index]->module->source.empty());
        return stages[index]->module->code;
    }

    void expectSameSpirv(const std::string& name, const std::vector<uint32_t>& before, const std::vector<uint32_t>& now)
    {
        EXPECT_FALSE(before.empty()) << name << ": the string did not compile";
        if (before == now)
            return;
        std::size_t first = 0;
        while (first < before.size() && first < now.size() && before[first] == now[first])
            ++first;
        std::ostringstream difference;
        difference << std::hex << std::uppercase;
        if (first < before.size() && first < now.size())
            difference << ", first at word " << std::dec << first << std::hex << ": 0x" << before[first]
                       << " before, 0x" << now[first] << " now";
        ADD_FAILURE() << name << ": " << before.size() << " words before, " << now.size() << " now" << difference.str();
    }

    const std::string sSources = OPENEDEN_SHADER_SOURCE_DIR;

    TEST(PostPassesSwitchOff, the_fullscreen_vertex_stage_is_the_four_strings_it_replaced)
    {
        // The tonemap, bloom, velocity and ambient occlusion passes had the same vertex string.
        EXPECT_EQ(std::string(old_bloom_sVertexShader), old_tonemap_sVertexShader);
        EXPECT_EQ(std::string(old_velocity_sVertexShader), old_tonemap_sVertexShader);
        EXPECT_EQ(std::string(old_ambientocclusion_sVertexShader), old_tonemap_sVertexShader);
        expectSameSpirv("post/fullscreen.vert", compiledAsBefore(VK_SHADER_STAGE_VERTEX_BIT, old_tonemap_sVertexShader),
            compiledNow("post/fullscreen.vert", "post/bloomdown.frag", {}, gameOptions(sSources, false), 0));
    }

    TEST(PostPassesSwitchOff, every_tonemap_variant_compiles_as_its_string_did)
    {
        const auto options = gameOptions(sSources, false);
        for (int index = 0; index < 16; ++index)
        {
            const bool bloom = (index & 1) != 0;
            const bool debug = (index & 2) != 0;
            const bool ao = (index & 4) != 0;
            const bool sss = (index & 8) != 0;
            std::set<std::string> defines;
            if (bloom)
                defines.insert("BLOOM");
            if (debug)
                defines.insert("DEBUG_VIEW");
            if (ao)
                defines.insert("AO");
            if (sss)
                defines.insert("SSS");
            expectSameSpirv("post/tonemap.frag " + vsgUtil::canonicalDefines(defines),
                compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, tonemapSourceBefore(bloom, debug, ao, sss)),
                compiledNow("post/fullscreen.vert", "post/tonemap.frag", defines, options, 1));
        }
    }

    TEST(PostPassesSwitchOff, bloom_and_velocity_compile_as_their_strings_did)
    {
        const auto options = gameOptions(sSources, false);
        expectSameSpirv("post/bloomdown.frag", compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, old_bloom_sDownFragment),
            compiledNow("post/fullscreen.vert", "post/bloomdown.frag", {}, options, 1));
        expectSameSpirv("post/bloomup.frag", compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, old_bloom_sUpFragment),
            compiledNow("post/fullscreen.vert", "post/bloomup.frag", {}, options, 1));
        expectSameSpirv("post/velocity.frag",
            compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, old_velocity_sFragmentShader),
            compiledNow("post/fullscreen.vert", "post/velocity.frag", {}, options, 1));
    }

    TEST(PostPassesSwitchOff, ambient_occlusion_compiles_as_its_string_did_with_the_same_settings)
    {
        struct Case
        {
            float mRadius;
            int mSlices;
            int mSteps;
        };
        // The defaults of files/settings-default.cfg, then others (radius not a whole number, the largest slices and
        // steps the sanitisers allow).
        for (const Case& c : { Case{ 100.f, 2, 6 }, Case{ 50.f, 3, 4 }, Case{ 37.25f, 8, 16 } })
        {
            const AoSettingsScope ao(c.mRadius, c.mSlices, c.mSteps);
            expectSameSpirv("post/ao.frag at " + std::to_string(c.mRadius) + ", " + std::to_string(c.mSlices) + ", "
                    + std::to_string(c.mSteps),
                compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, aoSourceBefore(c.mRadius, c.mSlices, c.mSteps)),
                compiledNow("post/fullscreen.vert", "post/ao.frag", {}, gameOptions(sSources, false), 1));
        }
    }

    TEST(PostPassesSwitchOff, post_processing_stages_compile_as_their_strings_did)
    {
        const auto options = gameOptions(sSources, false);
        // PostProcess reads post/omwfx.vert and compiles its source with compileGlsl (one stage, no defines).
        const auto omwfx = vsgUtil::readShader("post/omwfx.vert", options);
        ASSERT_TRUE(omwfx->module);
        expectSameSpirv("post/omwfx.vert", compiledAsBefore(VK_SHADER_STAGE_VERTEX_BIT, old_postprocess_sVertexShader),
            compiledAsBefore(VK_SHADER_STAGE_VERTEX_BIT, omwfx->module->source));
        expectSameSpirv("post/present.vert",
            compiledAsBefore(VK_SHADER_STAGE_VERTEX_BIT, old_postprocess_sPresentVertexShader),
            compiledNow("post/present.vert", "post/present.frag", {}, options, 0));
        expectSameSpirv("post/present.frag",
            compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, old_postprocess_sPresentFragmentShader),
            compiledNow("post/present.vert", "post/present.frag", {}, options, 1));
    }

    std::string fileBytes(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream bytes;
        bytes << in.rdbuf();
        return bytes.str();
    }

    // The options the game reads its shaders with (Resource::createShaderOptions) over `path`, made now (they read
    // the settings when made), with `[Rendering] slang shaders` off.
    vsg::ref_ptr<const vsg::Options> gameOptionsOff(const std::string& path)
    {
        auto options = vsg::Options::create(*Resource::createShaderOptions(path));
        options->setValue(vsgUtil::preferSlangKey, false);
        return options;
    }

    // Review of the stage f and g rounds, finding 1 (written before the change): the comparisons above compile the
    // source tree with a copy of the game's options, but the game reads the copies in the build's resources/shaders
    // (copy_all_resource_files, files/shaders/CMakeLists.txt) with the options Resource::createShaderOptions makes, so
    // a stale or missing copy passed them. The same 26 comparisons, against those copies with those options, and each
    // copy the same bytes as its source.
    TEST(PostPassesSwitchOff, the_files_the_game_loads_compile_as_the_strings_did)
    {
        const std::filesystem::path built(OPENEDEN_SHADER_BUILD_DIR);
        const std::filesystem::path sources = std::filesystem::path(sSources) / "post";
        // The files of post/ the passes read, as files/shaders/CMakeLists.txt copies them (SHADER_FILES). Review of the
        // member-name and fix rounds, item 3: a list, not a count of the folder, which also holds build inputs that are
        // not copied (the .slang sources, the .sets lists, and any <stage>.interface-exceptions).
        const std::set<std::string> loaded = { "ao.frag", "bloomdown.frag", "bloomup.frag", "fullscreen.vert",
            "omwfx.vert", "present.frag", "present.vert", "tonemap.frag", "tonemapconstants.glsl", "velocity.frag" };
        for (const std::string& name : loaded)
        {
            const std::filesystem::path copy = built / "post" / name;
            EXPECT_TRUE(std::filesystem::is_regular_file(copy)) << copy.generic_string() << " is missing";
            EXPECT_TRUE(fileBytes(copy) == fileBytes(sources / name)) << copy.generic_string() << " is not its source";
        }
        // A GLSL stage or include added to post/ is added to the list (and to SHADER_FILES).
        for (const auto& entry : std::filesystem::directory_iterator(sources))
        {
            const std::string extension = entry.path().extension().string();
            if (extension == ".vert" || extension == ".frag" || extension == ".comp" || extension == ".glsl")
            {
                EXPECT_EQ(loaded.count(entry.path().filename().string()), 1u)
                    << entry.path().filename().string() << " is not in this test's list";
            }
        }

        const std::string path = built.generic_string();
        const auto options = gameOptionsOff(path);
        expectSameSpirv("post/fullscreen.vert", compiledAsBefore(VK_SHADER_STAGE_VERTEX_BIT, old_tonemap_sVertexShader),
            compiledNow("post/fullscreen.vert", "post/bloomdown.frag", {}, options, 0));
        for (int index = 0; index < 16; ++index)
        {
            const bool bloom = (index & 1) != 0;
            const bool debug = (index & 2) != 0;
            const bool ao = (index & 4) != 0;
            const bool sss = (index & 8) != 0;
            std::set<std::string> defines;
            if (bloom)
                defines.insert("BLOOM");
            if (debug)
                defines.insert("DEBUG_VIEW");
            if (ao)
                defines.insert("AO");
            if (sss)
                defines.insert("SSS");
            expectSameSpirv("post/tonemap.frag " + vsgUtil::canonicalDefines(defines),
                compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, tonemapSourceBefore(bloom, debug, ao, sss)),
                compiledNow("post/fullscreen.vert", "post/tonemap.frag", defines, options, 1));
        }
        expectSameSpirv("post/bloomdown.frag", compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, old_bloom_sDownFragment),
            compiledNow("post/fullscreen.vert", "post/bloomdown.frag", {}, options, 1));
        expectSameSpirv("post/bloomup.frag", compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, old_bloom_sUpFragment),
            compiledNow("post/fullscreen.vert", "post/bloomup.frag", {}, options, 1));
        expectSameSpirv("post/velocity.frag",
            compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, old_velocity_sFragmentShader),
            compiledNow("post/fullscreen.vert", "post/velocity.frag", {}, options, 1));
        struct Case
        {
            float mRadius;
            int mSlices;
            int mSteps;
        };
        for (const Case& c : { Case{ 100.f, 2, 6 }, Case{ 50.f, 3, 4 }, Case{ 37.25f, 8, 16 } })
        {
            const AoSettingsScope aoScope(c.mRadius, c.mSlices, c.mSteps);
            expectSameSpirv("post/ao.frag at " + std::to_string(c.mRadius) + ", " + std::to_string(c.mSlices) + ", "
                    + std::to_string(c.mSteps),
                compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, aoSourceBefore(c.mRadius, c.mSlices, c.mSteps)),
                compiledNow("post/fullscreen.vert", "post/ao.frag", {}, gameOptionsOff(path), 1));
        }
        const auto omwfx = vsgUtil::readShader("post/omwfx.vert", options);
        ASSERT_TRUE(omwfx);
        ASSERT_TRUE(omwfx->module);
        expectSameSpirv("post/omwfx.vert", compiledAsBefore(VK_SHADER_STAGE_VERTEX_BIT, old_postprocess_sVertexShader),
            compiledAsBefore(VK_SHADER_STAGE_VERTEX_BIT, omwfx->module->source));
        expectSameSpirv("post/present.vert",
            compiledAsBefore(VK_SHADER_STAGE_VERTEX_BIT, old_postprocess_sPresentVertexShader),
            compiledNow("post/present.vert", "post/present.frag", {}, options, 0));
        expectSameSpirv("post/present.frag",
            compiledAsBefore(VK_SHADER_STAGE_FRAGMENT_BIT, old_postprocess_sPresentFragmentShader),
            compiledNow("post/present.vert", "post/present.frag", {}, options, 1));
    }

#if OPENEDEN_SLANG_ENABLED
    std::vector<uint32_t> readWords(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
            return {};
        const std::streamsize size = in.tellg();
        std::vector<uint32_t> words(static_cast<std::size_t>(size) / sizeof(uint32_t));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(words.data()), size);
        return words;
    }

    std::string readText(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    // Finding 2: every capture of stage g set only `ao = true`, and the twin's own defaults (post/aosettings.slang)
    // equal the settings' defaults, so no frame could tell whether the settings reached the twin.
    TEST(PostPassesSlang, the_ao_twin_takes_the_ao_settings_as_specialization_constants)
    {
        const std::filesystem::path built(OPENEDEN_SHADER_BUILD_DIR);
        const AoSettingsScope ao(37.25f, 8, 16);
        const auto options = gameOptions(built.generic_string(), true);
        std::string error;
        const auto stages
            = vsgUtil::readPassStages({ { "post/fullscreen.vert", {} }, { "post/ao.frag", {} } }, options, &error);
        ASSERT_EQ(stages.size(), 2u) << error;

        // The twin the build lists for the stage with no defines, not the GLSL.
        const auto manifest = vsgUtil::parsePermutationManifest(readText(built / "post/ao.frag.permutations"));
        const auto file = vsgUtil::selectPermutation(manifest, {});
        ASSERT_TRUE(file.has_value());
        EXPECT_TRUE(stages[1]->module->source.empty());
        const auto twin = readWords(built / "post" / *file);
        ASSERT_FALSE(twin.empty());
        EXPECT_TRUE(stages[1]->module->code == twin);

        const auto& constants = stages[1]->specializationConstants;
        const auto* radius
            = constants.count(1100) ? dynamic_cast<const vsg::floatValue*>(constants.at(1100).get()) : nullptr;
        const auto* slices
            = constants.count(1101) ? dynamic_cast<const vsg::intValue*>(constants.at(1101).get()) : nullptr;
        const auto* steps
            = constants.count(1102) ? dynamic_cast<const vsg::intValue*>(constants.at(1102).get()) : nullptr;
        ASSERT_NE(radius, nullptr);
        ASSERT_NE(slices, nullptr);
        ASSERT_NE(steps, nullptr);
        EXPECT_EQ(radius->value(), 37.25f);
        EXPECT_EQ(slices->value(), 8);
        EXPECT_EQ(steps->value(), 16);

        // The ids are the table's, by the names the twin declares.
        for (const auto& entry : Resource::settingsConstantTable())
        {
            if (std::string(entry.mName) == "AO_RADIUS")
            {
                EXPECT_EQ(entry.mId, 1100u);
            }
            else if (std::string(entry.mName) == "AO_SLICES")
            {
                EXPECT_EQ(entry.mId, 1101u);
            }
            else if (std::string(entry.mName) == "AO_STEPS")
            {
                EXPECT_EQ(entry.mId, 1102u);
            }
        }
    }

    // A SPIR-V literal string: bytes in word order, up to the first zero.
    std::string literalString(const uint32_t* first, const uint32_t* end)
    {
        std::string text;
        for (const uint32_t* word = first; word != end; ++word)
        {
            for (uint32_t byte = 0; byte < 4; ++byte)
            {
                const char c = static_cast<char>((*word >> (8 * byte)) & 0xffu);
                if (c == 0)
                    return text;
                text += c;
            }
        }
        return text;
    }

    // The members of the push constant block of a SPIR-V module: name -> byte offset. A member without a name is
    // "#<index>".
    std::map<std::string, uint32_t> pushConstantOffsets(const std::vector<uint32_t>& words)
    {
        constexpr uint32_t opMemberName = 6;
        constexpr uint32_t opTypePointer = 32;
        constexpr uint32_t opVariable = 59;
        constexpr uint32_t opMemberDecorate = 72;
        constexpr uint32_t storagePushConstant = 9;
        constexpr uint32_t decorationOffset = 35;
        std::map<uint32_t, std::map<uint32_t, std::string>> names; // struct id -> member -> name
        std::map<uint32_t, std::map<uint32_t, uint32_t>> offsets; // struct id -> member -> offset
        std::map<uint32_t, uint32_t> pointees; // push constant pointer type -> struct id
        uint32_t block = 0;
        for (std::size_t i = 5; i < words.size();)
        {
            const uint32_t opcode = words[i] & 0xffffu;
            const uint32_t count = words[i] >> 16;
            if (count == 0 || i + count > words.size())
            {
                ADD_FAILURE() << "a SPIR-V instruction runs past the end";
                return {};
            }
            const uint32_t* w = &words[i];
            if (opcode == opMemberName && count >= 4)
                names[w[1]][w[2]] = literalString(w + 3, w + count);
            else if (opcode == opMemberDecorate && count >= 5 && w[3] == decorationOffset)
                offsets[w[1]][w[2]] = w[4];
            else if (opcode == opTypePointer && count == 4 && w[2] == storagePushConstant)
                pointees[w[1]] = w[3];
            else if (opcode == opVariable && count >= 4 && w[3] == storagePushConstant)
            {
                const auto it = pointees.find(w[1]);
                block = it == pointees.end() ? 0 : it->second;
            }
            i += count;
        }
        std::map<std::string, uint32_t> members;
        for (const auto& [member, offset] : offsets[block])
        {
            const std::string& name = names[block][member];
            members[name.empty() ? "#" + std::to_string(member) : name] = offset;
        }
        return members;
    }

    // Finding 9: the interface check of every twin (SlangPermutations, vsgUtil::compareSpirvInterfaces) compares a
    // block's members by offset and type only, so two members of one type in another order pass it: in the review's
    // red run the tonemap twin of AO SSS with `ao` and `sss` swapped passed SlangPermutations. The C++ side works out
    // where the tonemap's conditional members are (Tonemap::setSource, components/render/tonemap.cpp: `ao` after the
    // parameters, `sss` after it, `debug` last), so the post twins' push constants are compared by name here.
    TEST(PostPassesSlang, push_constant_members_are_where_the_glsl_and_the_cpp_have_them_by_name)
    {
        struct Pass
        {
            std::string mStage;
            std::string mVertex;
            std::string mFragment;
            std::size_t mIndex;
        };
        const std::vector<Pass> passes = {
            { "post/fullscreen.vert", "post/fullscreen.vert", "post/bloomdown.frag", 0 },
            // Stage h: the .omwfx passes' vertex stage (toddhow, 2026-11-04), no push constants like the one above.
            { "post/omwfx.vert", "post/omwfx.vert", "post/bloomdown.frag", 0 },
            { "post/tonemap.frag", "post/fullscreen.vert", "post/tonemap.frag", 1 },
            { "post/bloomdown.frag", "post/fullscreen.vert", "post/bloomdown.frag", 1 },
            { "post/bloomup.frag", "post/fullscreen.vert", "post/bloomup.frag", 1 },
            { "post/velocity.frag", "post/fullscreen.vert", "post/velocity.frag", 1 },
            { "post/ao.frag", "post/fullscreen.vert", "post/ao.frag", 1 },
            { "post/present.vert", "post/present.vert", "post/present.frag", 0 },
            { "post/present.frag", "post/present.vert", "post/present.frag", 1 },
        };
        const std::filesystem::path built(OPENEDEN_SHADER_BUILD_DIR);
        const auto options = gameOptions(sSources, false);
        std::size_t compared = 0;
        for (const auto& entry : std::filesystem::directory_iterator(built / "post"))
        {
            if (entry.path().extension() != ".permutations")
                continue;
            const std::string stage = "post/" + entry.path().stem().string();
            const auto pass = std::find_if(
                passes.begin(), passes.end(), [&stage](const Pass& candidate) { return candidate.mStage == stage; });
            if (pass == passes.end())
            {
                ADD_FAILURE() << stage << " has twins but is not in this test's list";
                continue;
            }
            const auto manifest = vsgUtil::parsePermutationManifest(readText(entry.path()));
            for (const auto& [canonical, file] : manifest.mFiles)
            {
                std::set<std::string> defines;
                std::istringstream names(canonical);
                for (std::string name; names >> name;)
                    defines.insert(name);
                const auto glsl
                    = pushConstantOffsets(compiledNow(pass->mVertex, pass->mFragment, defines, options, pass->mIndex));
                EXPECT_EQ(pushConstantOffsets(readWords(built / "post" / file)), glsl)
                    << stage << " [" << canonical << "]";
                if (stage == "post/tonemap.frag")
                {
                    // As Tonemap::setSource computes them.
                    const bool ao = defines.count("AO") != 0;
                    const bool sss = defines.count("SSS") != 0;
                    std::map<std::string, uint32_t> expected = { { "params", 0 } };
                    if (ao)
                        expected["ao"] = 16;
                    if (sss)
                        expected["sss"] = 16 * (ao ? 2 : 1);
                    if (defines.count("DEBUG_VIEW") != 0)
                        expected["debug"] = 16 * (1 + (ao ? 1 : 0) + (sss ? 1 : 0));
                    EXPECT_EQ(glsl, expected) << "[" << canonical << "]";
                }
                ++compared;
            }
        }
        // The 23 permutations of stage g and the .omwfx passes' vertex stage of stage h.
        EXPECT_EQ(compared, 24u);
    }
#endif
}
