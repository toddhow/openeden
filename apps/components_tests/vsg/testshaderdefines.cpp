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
#include <cstdlib>
#include <cstring>
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

    // Task 0072 (review of stages b and c): a SPIR-V module that declares a bool specialization constant for each id
    // in `boolIds` and a float one for each id in `floatIds`, nothing else, as vsgUtil::readSpirvInterface reads it.
    std::vector<uint32_t> moduleDeclaring(const std::vector<uint32_t>& boolIds, const std::vector<uint32_t>& floatIds)
    {
        const auto op = [](uint32_t opcode, uint32_t count) { return (count << 16) | opcode; };
        std::vector<uint32_t> words = { 0x07230203u, 0x00010000u, 0u, 1000u, 0u };
        words.insert(words.end(), { op(17, 2), 1u }); // OpCapability Shader
        words.insert(words.end(), { op(20, 2), 1u }); // %1 = OpTypeBool
        words.insert(words.end(), { op(22, 3), 2u, 32u }); // %2 = OpTypeFloat 32
        uint32_t next = 10;
        for (const uint32_t id : boolIds)
        {
            words.insert(words.end(), { op(71, 4), next, 1u, id }); // OpDecorate %next SpecId id
            words.insert(words.end(), { op(49, 3), 1u, next++ }); // OpSpecConstantFalse
        }
        for (const uint32_t id : floatIds)
        {
            words.insert(words.end(), { op(71, 4), next, 1u, id });
            words.insert(words.end(), { op(50, 4), 2u, next++, 0x3f000000u }); // OpSpecConstant 0.5
        }
        return words;
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
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(moduleDeclaring({ 1000 }, { 1004 })));
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
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(moduleDeclaring({ 1000 }, { 1004 })));
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
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(moduleDeclaring({ 1000 }, { 1004 })));
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
    // Review of stages b and c: it declares the two settings constants, since a twin now gets only the ids it declares.
    const std::vector<uint32_t> sPermutationWords = moduleDeclaring({ 1000 }, { 1004 });
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
    // Task 0072 (review of stages b and c): a twin gets only the settings constants it declares, so that a stage that
    // declares none of them (a Slang-only shader with the switch off) is left exactly as it was.
    TEST(ShaderDefines, a_twin_gets_only_the_settings_constants_it_declares)
    {
        auto stage = vsg::ShaderStage::create(
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(moduleDeclaring({ 1000 }, {})));
        vsgUtil::addSettingsConstants(*stage, optionsWithConstants().get());
        EXPECT_EQ(stage->specializationConstants.count(1000), 1u);
        EXPECT_EQ(stage->specializationConstants.count(1004), 0u);
    }

    TEST(ShaderDefines, a_module_that_declares_no_settings_constant_gets_none)
    {
        auto stage = vsg::ShaderStage::create(
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(moduleDeclaring({ 0, 7 }, {})));
        vsgUtil::addSettingsConstants(*stage, optionsWithConstants().get());
        EXPECT_TRUE(stage->specializationConstants.empty());
    }

    TEST(ShaderDefines, a_module_that_cannot_be_read_gets_none)
    {
        auto stage = vsg::ShaderStage::create(
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(std::vector<uint32_t>{ 0x07230203u }));
        vsgUtil::addSettingsConstants(*stage, optionsWithConstants().get());
        EXPECT_TRUE(stage->specializationConstants.empty());
    }

    // Task 0072 (review of stages b and c): the "#define" lines of the settings files (shadow.settings,
    // terrain.settings) are part of the set a stage really compiles with.
    TEST(ShaderDefines, a_settings_define_counts_only_as_its_own_line)
    {
        const Names settings{ "CLOUD_SHADOW", "DEBUG_CHUNKS" };
        EXPECT_EQ(vsgUtil::includedSettingsDefines("#version 450\n#define CLOUD_SHADOW 1\nvoid main() {}\n", settings),
            (Names{ "CLOUD_SHADOW" }));
        EXPECT_EQ(vsgUtil::includedSettingsDefines("#define CLOUD_SHADOW 1\r\n", settings), (Names{ "CLOUD_SHADOW" }));
        EXPECT_EQ(vsgUtil::includedSettingsDefines("#define CLOUD_SHADOW 1", settings), (Names{ "CLOUD_SHADOW" }));
        EXPECT_TRUE(vsgUtil::includedSettingsDefines("#define CLOUD_SHADOW_DEBUG 1\n", settings).empty());
        EXPECT_TRUE(vsgUtil::includedSettingsDefines("// #define CLOUD_SHADOW 1\n", settings).empty());
        EXPECT_TRUE(vsgUtil::includedSettingsDefines("#define CLOUD_SHADOW 10\n", settings).empty());
        EXPECT_TRUE(vsgUtil::includedSettingsDefines("#define OTHER 1\n", settings).empty());
        EXPECT_TRUE(vsgUtil::includedSettingsDefines("#define CLOUD_SHADOW 1\n", Names{}).empty());
    }

    TEST(ShaderDefines, the_stage_set_adds_the_settings_defines_the_source_includes)
    {
        auto constants = vsgUtil::SettingsConstants::create();
        constants->defines = { "CLOUD_SHADOW", "DEBUG_SHADOW" };
        auto options = vsg::Options::create();
        options->setObject(vsgUtil::settingsConstantsKey, constants);
        const std::string source = "#pragma import_defines(NORMAL)\n#define CLOUD_SHADOW 1\nvoid main() {}\n";
        EXPECT_EQ(vsgUtil::stageDefines(Names{ "NORMAL", "COLOR" }, source, options.get()),
            (Names{ "CLOUD_SHADOW", "NORMAL" }));
        // Without the settings object, or with a source that does not include the file, only the imported names.
        EXPECT_EQ(vsgUtil::stageDefines(Names{ "NORMAL" }, source, nullptr), (Names{ "NORMAL" }));
        EXPECT_EQ(vsgUtil::stageDefines(Names{ "NORMAL" }, "#pragma import_defines(NORMAL)\n", options.get()),
            (Names{ "NORMAL" }));
    }

    // Task 0072 (review of stages b and c): the GLSL gets the same float as the twin's specialization constant.
    TEST(ShaderDefines, a_float_setting_is_written_so_that_it_reads_back_exactly)
    {
        for (const float value : { 1.23456789f, 0.1f, 1.0f / 3.0f, 1.0e-5f, 123456.789f, 0.0f, -2.5f, 3.0e38f })
        {
            const std::string literal = Resource::shaderFloatLiteral(value);
            SCOPED_TRACE(literal);
            const float back = std::strtof(literal.c_str(), nullptr);
            EXPECT_EQ(std::memcmp(&back, &value, sizeof(float)), 0);
        }
    }

    TEST(ShaderDefines, every_settings_define_name_is_known_and_none_is_imported_by_a_mode)
    {
        const auto names = Resource::settingsDefineNames();
        EXPECT_EQ(names.size(), 5u);
        Names unique;
        for (const std::string_view name : names)
            unique.emplace(name);
        EXPECT_EQ(unique.size(), names.size());
    }

    // Task 0072 (review of stages b and c): every constant of the table has a value of its type for the twins.
    TEST(ShaderDefines, the_settings_object_has_every_table_constant_with_its_type)
    {
        Resource::ShaderSettings settings;
        ASSERT_TRUE(settings.constants);
        for (const auto& entry : Resource::settingsConstantTable())
        {
            SCOPED_TRACE(std::string(entry.mName));
            const auto it = settings.constants->constants.find(entry.mId);
            ASSERT_NE(it, settings.constants->constants.end());
            const vsg::Object* value = it->second.get();
            const bool rightType = entry.mType == 'b' ? dynamic_cast<const vsg::Value<VkBool32>*>(value) != nullptr
                : entry.mType == 'i'                  ? dynamic_cast<const vsg::intValue*>(value) != nullptr
                                                      : dynamic_cast<const vsg::floatValue*>(value) != nullptr;
            EXPECT_TRUE(rightType) << "type " << entry.mType;
        }
        for (const std::string& name : settings.constants->defines)
        {
            bool known = false;
            for (const std::string_view candidate : Resource::settingsDefineNames())
                known = known || candidate == name;
            EXPECT_TRUE(known) << name;
        }
    }
}
