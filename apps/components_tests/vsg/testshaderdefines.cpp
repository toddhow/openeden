// Task 0072, stage b: the defines a GLSL stage really compiles with, and the settings constants of the Slang twins
// (components/vsgutil/shaderpermutation.hpp, components/resource/shadersettings.hpp). Written before the
// implementation.
#include <gtest/gtest.h>

#include <components/resource/shadersettings.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/io/spirv.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/utils/ShaderCompiler.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using vsgUtil::effectiveDefines;
    using vsgUtil::importedDefines;

    using Names = std::set<std::string>;

    TEST(ShaderDefines, one_pragma_lists_its_names)
    {
        EXPECT_EQ(importedDefines("#pragma import_defines (VERTEX, NORMAL, COLOR)\nvoid main() {}\n"),
            (Names{ "COLOR", "NORMAL", "VERTEX" }));
    }

    TEST(ShaderDefines, the_pragma_without_a_space_before_the_parenthesis_counts)
    {
        EXPECT_EQ(importedDefines("#pragma import_defines(GLOW_MAP, DIFFUSE_MAP, MATERIAL)\n"),
            (Names{ "DIFFUSE_MAP", "GLOW_MAP", "MATERIAL" }));
    }

    TEST(ShaderDefines, spaces_tabs_and_windows_line_ends_are_trimmed)
    {
        EXPECT_EQ(importedDefines("  \t#pragma import_defines ( A ,\tB\t)\r\nx\r\n"), (Names{ "A", "B" }));
    }

    TEST(ShaderDefines, every_pragma_of_the_expanded_source_counts)
    {
        // An included file's pragma is part of the source once the reader has inserted the include.
        EXPECT_EQ(importedDefines("#pragma import_defines(A)\nint x;\n#pragma import_defines(B, C)\n"),
            (Names{ "A", "B", "C" }));
    }

    TEST(ShaderDefines, other_lines_and_a_source_without_the_pragma_import_nothing)
    {
        EXPECT_EQ(importedDefines(""), Names{});
        EXPECT_EQ(importedDefines("void main() {}\n"), Names{});
        EXPECT_EQ(importedDefines("// #pragma import_defines(A)\n#define B\n"), Names{});
        EXPECT_EQ(importedDefines("#pragma import_defines ()\n"), Names{});
    }

    TEST(ShaderDefines, the_effective_set_keeps_only_imported_names)
    {
        const std::string source = "#pragma import_defines(DIFFUSE_MAP, MATERIAL)\nvoid main() {}\n";
        EXPECT_EQ(effectiveDefines({ "COVER", "DIFFUSE_MAP", "GI", "MATERIAL", "PBR" }, source),
            (Names{ "DIFFUSE_MAP", "MATERIAL" }));
        EXPECT_EQ(effectiveDefines({ "COVER" }, source), Names{});
        EXPECT_EQ(effectiveDefines({ "COVER", "MATERIAL" }, "void main() {}\n"), Names{});
    }

    // The set must be what VSG's own compiler writes as "#define" lines, or a twin would be chosen for code the GLSL
    // does not compile.
    TEST(ShaderDefines, the_effective_set_is_what_vsg_defines)
    {
        const std::string source
            = "#version 450\n#pragma import_defines (COLOR, TEXCOORD, MATERIAL, DIFFUSE_MAP)\nvoid main() {}\n"
              "  #pragma import_defines(EXTRA)\n";
        const std::vector<std::string> requested{ "COLOR", "COVER", "DIFFUSE_MAP", "EXTRA", "MATERIAL", "NORMAL" };
        // Kept for the whole run: each vsg::ShaderCompiler starts glslang and the last one destroyed stops it, and a
        // stop with no compile in between made the next test's compile crash (an access violation in
        // SlangPermutations). A compiler that lives until exit keeps glslang started.
        static const auto compiler = vsg::ShaderCompiler::create();
        const std::string combined = compiler->combineSourceAndDefines(source, requested);
        Names written;
        std::istringstream lines(combined);
        for (std::string line; std::getline(lines, line);)
        {
            const std::string prefix = "#define ";
            if (line.rfind(prefix, 0) == 0)
                written.insert(line.substr(prefix.size()));
        }
        EXPECT_EQ(effectiveDefines(Names(requested.begin(), requested.end()), source), written);
        EXPECT_EQ(written, (Names{ "COLOR", "DIFFUSE_MAP", "EXTRA", "MATERIAL" }));
    }

    vsg::ref_ptr<vsg::Options> optionsWithConstants()
    {
        auto constants = vsgUtil::SettingsConstants::create();
        constants->constants[1000] = vsg::Value<VkBool32>::create(VK_TRUE);
        constants->constants[1004] = vsg::floatValue::create(0.5f);
        auto options = vsg::Options::create();
        options->setObject(vsgUtil::settingsConstantsKey, constants);
        return options;
    }

    TEST(ShaderDefines, a_precompiled_twin_gets_the_settings_constants)
    {
        auto stage = vsg::ShaderStage::create(
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(std::vector<uint32_t>{ 0x07230203u }));
        stage->specializationConstants[0] = vsg::intValue::create(1);
        vsgUtil::addSettingsConstants(*stage, optionsWithConstants().get());
        ASSERT_EQ(stage->specializationConstants.size(), 3u);
        EXPECT_EQ(stage->specializationConstants.count(0), 1u);
        EXPECT_EQ(stage->specializationConstants.count(1000), 1u);
        EXPECT_EQ(stage->specializationConstants.count(1004), 1u);
    }

    TEST(ShaderDefines, a_constant_the_stage_sets_already_is_kept)
    {
        auto stage = vsg::ShaderStage::create(
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(std::vector<uint32_t>{ 0x07230203u }));
        auto own = vsg::Value<VkBool32>::create(VK_FALSE);
        stage->specializationConstants[1000] = own;
        vsgUtil::addSettingsConstants(*stage, optionsWithConstants().get());
        EXPECT_EQ(stage->specializationConstants.at(1000), own);
        EXPECT_EQ(stage->specializationConstants.count(1004), 1u);
    }

    TEST(ShaderDefines, a_glsl_stage_or_options_without_constants_are_left_alone)
    {
        auto glsl = vsg::ShaderStage::create(
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(std::string("void main() {}")));
        vsgUtil::addSettingsConstants(*glsl, optionsWithConstants().get());
        EXPECT_TRUE(glsl->specializationConstants.empty());

        auto twin = vsg::ShaderStage::create(
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(std::vector<uint32_t>{ 0x07230203u }));
        vsgUtil::addSettingsConstants(*twin, vsg::Options::create().get());
        EXPECT_TRUE(twin->specializationConstants.empty());
        vsgUtil::addSettingsConstants(*twin, nullptr);
        EXPECT_TRUE(twin->specializationConstants.empty());
    }

    // A folder with one stage's permutations, read through vsg's SPIR-V reader as the game reads them.
    class SlangTwinFolder
    {
    public:
        SlangTwinFolder()
            : mDir(std::filesystem::temp_directory_path() / uniqueName())
        {
            std::filesystem::create_directories(mDir / "sky/moon");
            std::filesystem::create_directories(mDir / "comp/fogofwar");
            mOptions = vsg::Options::create();
            mOptions->paths = { vsg::Path(mDir.generic_string()) };
            mOptions->readerWriters = { vsg::spirv::create() };
        }
        ~SlangTwinFolder()
        {
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        void write(const std::string& file, const std::vector<uint32_t>& words)
        {
            std::ofstream out(mDir / file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(words.data()),
                static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
        }
        void writeText(const std::string& file, const std::string& text)
        {
            std::ofstream out(mDir / file, std::ios::binary);
            out << text;
        }
        vsg::ref_ptr<const vsg::Options> options() const { return mOptions; }
        // The options as the game makes them with `[Rendering] slang shaders` on, with two settings constants.
        vsg::ref_ptr<const vsg::Options> slangOptions() const
        {
            auto options = vsg::Options::create(*mOptions);
            options->setValue(vsgUtil::preferSlangKey, true);
            auto constants = vsgUtil::SettingsConstants::create();
            constants->constants[1000] = vsg::Value<VkBool32>::create(VK_TRUE);
            constants->constants[1004] = vsg::floatValue::create(0.5f);
            options->setObject(vsgUtil::settingsConstantsKey, constants);
            return options;
        }

    private:
        // The manifests and modules are cached per process by folder, so no two fixtures may share one, not even
        // two that live at the same address one after the other.
        static std::string uniqueName()
        {
            static std::atomic<unsigned> counter{ 0 };
            return "openeden_slangtwin_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "_" + std::to_string(counter++);
        }

        std::filesystem::path mDir;
        vsg::ref_ptr<vsg::Options> mOptions;
    };

    // SPIR-V headers told apart by their bound (word 3).
    const std::vector<uint32_t> sPermutationWords{ 0x07230203u, 0x00010000u, 0x00280000u, 7u, 0u };
    const std::vector<uint32_t> sSingleFileWords{ 0x07230203u, 0x00010000u, 0x00280000u, 9u, 0u };
    const std::string sMoonFrag = "#pragma import_defines(GLOW_MAP, DIFFUSE_MAP, MATERIAL)\nvoid main() {}\n";

    TEST(ShaderDefines, the_twin_is_chosen_by_the_set_the_glsl_compiles_with)
    {
        SlangTwinFolder folder;
        folder.write("sky/moon/main.frag.p1a2b3c4d.spv", sPermutationWords);
        folder.writeText("sky/moon/main.frag.permutations", "main.frag.p1a2b3c4d.spv\tDIFFUSE_MAP GLOW_MAP MATERIAL\n");
        // The settings-driven defines the moon does not import do not stop the match.
        const auto module = vsgUtil::slangTwin("sky/moon/main.frag",
            { "COVER", "DIFFUSE_MAP", "GI", "GLOW_MAP", "MATERIAL", "PBR" }, sMoonFrag, true, folder.options());
        ASSERT_TRUE(module);
        EXPECT_EQ(module->code, sPermutationWords);
    }

    TEST(ShaderDefines, with_the_switch_off_no_twin_is_chosen)
    {
        SlangTwinFolder folder;
        folder.write("sky/moon/main.frag.p1a2b3c4d.spv", sPermutationWords);
        folder.writeText("sky/moon/main.frag.permutations", "main.frag.p1a2b3c4d.spv\tDIFFUSE_MAP GLOW_MAP MATERIAL\n");
        EXPECT_FALSE(vsgUtil::slangTwin(
            "sky/moon/main.frag", { "DIFFUSE_MAP", "GLOW_MAP", "MATERIAL" }, sMoonFrag, false, folder.options()));
    }

    TEST(ShaderDefines, an_unlisted_set_keeps_the_glsl_even_where_a_single_file_exists)
    {
        // The single "<stage>.spv" of task 0023 must never be drawn for a set it was not built for.
        SlangTwinFolder folder;
        folder.write("sky/moon/main.frag.p1a2b3c4d.spv", sPermutationWords);
        folder.writeText("sky/moon/main.frag.permutations", "main.frag.p1a2b3c4d.spv\tDIFFUSE_MAP GLOW_MAP MATERIAL\n");
        folder.write("sky/moon/main.frag.spv", sSingleFileWords);
        EXPECT_FALSE(
            vsgUtil::slangTwin("sky/moon/main.frag", { "DIFFUSE_MAP", "MATERIAL" }, sMoonFrag, true, folder.options()));
        // ... and a stage with no manifest at all keeps the GLSL too.
        SlangTwinFolder bare;
        bare.write("sky/moon/main.frag.spv", sSingleFileWords);
        EXPECT_FALSE(vsgUtil::slangTwin(
            "sky/moon/main.frag", { "DIFFUSE_MAP", "GLOW_MAP", "MATERIAL" }, sMoonFrag, true, bare.options()));
    }

    TEST(ShaderDefines, a_listed_file_that_cannot_be_read_keeps_the_glsl)
    {
        SlangTwinFolder folder;
        folder.writeText("sky/moon/main.frag.permutations", "main.frag.p1a2b3c4d.spv\tDIFFUSE_MAP GLOW_MAP MATERIAL\n");
        for (int attempt = 0; attempt < 2; ++attempt)
            EXPECT_FALSE(vsgUtil::slangTwin(
                "sky/moon/main.frag", { "DIFFUSE_MAP", "GLOW_MAP", "MATERIAL" }, sMoonFrag, true, folder.options()));
    }

    // Task 0072, stage c: the compute passes are read with vsgUtil::readShader and compiled with no defines.
    const std::string sComputeGlsl = "#version 450\nlayout(local_size_x = 8) in;\nvoid main() {}\n";
    const std::string sComputeManifest = "update.comp.pda39a3ee.spv\t\n";

    vsg::ref_ptr<vsg::ShaderStage> computeGlslStage()
    {
        return vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT, "main", vsg::ShaderModule::create(sComputeGlsl));
    }

    TEST(ShaderDefines, a_compute_stage_takes_a_copy_with_its_twin_when_the_switch_is_on)
    {
        SlangTwinFolder folder;
        folder.write("comp/fogofwar/update.comp.pda39a3ee.spv", sPermutationWords);
        folder.writeText("comp/fogofwar/update.comp.permutations", sComputeManifest);
        const auto glsl = computeGlslStage();
        const auto stage = vsgUtil::withSlangTwin(glsl, "comp/fogofwar/update.comp", folder.slangOptions());
        ASSERT_TRUE(stage);
        ASSERT_TRUE(stage->module);
        EXPECT_EQ(stage->module->code, sPermutationWords);
        EXPECT_TRUE(stage->module->source.empty());
        EXPECT_EQ(stage->stage, VK_SHADER_STAGE_COMPUTE_BIT);
        EXPECT_EQ(stage->entryPointName, "main");
        // The twin takes the settings constants, as a graphics twin does.
        EXPECT_EQ(stage->specializationConstants.count(1000), 1u);
        EXPECT_EQ(stage->specializationConstants.count(1004), 1u);
        // readShader may share the GLSL stage between pipelines: it is left as it was.
        EXPECT_NE(stage, glsl);
        EXPECT_EQ(glsl->module->source, sComputeGlsl);
        EXPECT_TRUE(glsl->module->code.empty());
        EXPECT_TRUE(glsl->specializationConstants.empty());
    }

    TEST(ShaderDefines, a_compute_stage_keeps_the_glsl_with_the_switch_off)
    {
        SlangTwinFolder folder;
        folder.write("comp/fogofwar/update.comp.pda39a3ee.spv", sPermutationWords);
        folder.writeText("comp/fogofwar/update.comp.permutations", sComputeManifest);
        const auto glsl = computeGlslStage();
        EXPECT_EQ(vsgUtil::withSlangTwin(glsl, "comp/fogofwar/update.comp", folder.options()), glsl);
        EXPECT_EQ(vsgUtil::withSlangTwin(glsl, "comp/fogofwar/update.comp", nullptr), glsl);
        EXPECT_TRUE(glsl->specializationConstants.empty());
    }

    TEST(ShaderDefines, a_compute_stage_without_a_built_twin_keeps_the_glsl)
    {
        // No manifest at all, then a manifest that lists another set only.
        SlangTwinFolder bare;
        const auto glsl = computeGlslStage();
        EXPECT_EQ(vsgUtil::withSlangTwin(glsl, "comp/fogofwar/update.comp", bare.slangOptions()), glsl);
        SlangTwinFolder other;
        other.write("comp/fogofwar/update.comp.p1a2b3c4d.spv", sPermutationWords);
        other.writeText("comp/fogofwar/update.comp.permutations", "update.comp.p1a2b3c4d.spv\tCOLOR\n");
        EXPECT_EQ(vsgUtil::withSlangTwin(glsl, "comp/fogofwar/update.comp", other.slangOptions()), glsl);
        EXPECT_FALSE(vsgUtil::withSlangTwin({}, "comp/fogofwar/update.comp", other.slangOptions()));
    }

    TEST(ShaderDefines, the_settings_constant_table_has_unique_names_and_ids_above_the_glsl_ones)
    {
        const auto table = Resource::settingsConstantTable();
        ASSERT_FALSE(table.empty());
        std::set<std::string> names;
        std::set<uint32_t> ids;
        for (const auto& entry : table)
        {
            SCOPED_TRACE(std::string(entry.mName));
            EXPECT_TRUE(names.insert(std::string(entry.mName)).second);
            EXPECT_TRUE(ids.insert(entry.mId).second);
            // GLSL stages use constant ids 0 to START_UVSET_CONSTANTS + 17; the settings stay far above them.
            EXPECT_GE(entry.mId, Resource::settingsConstantBase);
            EXPECT_TRUE(entry.mType == 'b' || entry.mType == 'i' || entry.mType == 'f');
        }
        // The sky needs these (task 0072, stage b).
        for (const char* name : { "cRadialFog", "cExponentialFog", "cSunGlow", "cSunGlowDebug", "cSunGlowStrength",
                 "cVolumetrics", "cVolumetricsDebug", "cAerialHaze", "cAerialVisibility", "cAerialScaleHeight",
                 "cAerialWeatherGain", "cCloudsLit", "cVolumetricClouds" })
            EXPECT_EQ(names.count(name), 1u) << name;
    }
}
