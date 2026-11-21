// Task 0072, stage b: the defines a stage really takes, and the settings constants of the Slang twins
// (components/vsgutil/shaderpermutation.hpp, components/resource/shadersettings.hpp). Written before the
// implementation. Task 0363, stage g: there is no GLSL to fall back on or to read the imported names from: a stage
// file (".slang-stage") lists the names a stage takes, a set with no twin gives an error, and the tests that pinned
// "the GLSL draws" now pin the error.
#include <gtest/gtest.h>

#include <components/pipeline/graphics.hpp>
#include <components/pipeline/mode.hpp>
#include <components/pipeline/options.hpp>
#include <components/pipeline/terrain.hpp>
#include <components/resource/shadersettings.hpp>
#include <components/settings/settings.hpp>
#include <components/vsgutil/readshader.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include <vsg/core/Value.h>
#include <vsg/core/observer_ptr.h>
#include <vsg/io/Options.h>
#include <vsg/io/glsl.h>
#include <vsg/io/spirv.h>
#include <vsg/state/GraphicsPipeline.h>
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
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
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
        const Names imports{ "DIFFUSE_MAP", "MATERIAL" };
        EXPECT_EQ(effectiveDefines({ "COVER", "DIFFUSE_MAP", "GI", "MATERIAL", "PBR" }, imports),
            (Names{ "DIFFUSE_MAP", "MATERIAL" }));
        EXPECT_EQ(effectiveDefines({ "COVER" }, imports), Names{});
        EXPECT_EQ(effectiveDefines({ "COVER", "MATERIAL" }, Names{}), Names{});
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
        EXPECT_EQ(effectiveDefines(Names(requested.begin(), requested.end()), importedDefines(source)), written);
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
            // Task 0363, stage g: the stage files the build writes (source, entry, stage, the names the stage takes).
            writeText(
                "sky/moon/main.frag.slang-stage", "moon.slang\tfragMain\tfragment\tDIFFUSE_MAP GLOW_MAP MATERIAL\n");
            writeText(
                "sky/moon/main.vert.slang-stage", "moon.slang\tvertMain\tvertex\tNORMAL PARTICLE TEXCOORD VERTEX\n");
            writeText("comp/fogofwar/update.comp.slang-stage", "update.slang\tcompMain\tcompute\t-\n");
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
        // The options as the game makes them, with two settings constants.
        vsg::ref_ptr<const vsg::Options> slangOptions() const
        {
            auto options = vsg::Options::create(*mOptions);
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

    TEST(ShaderDefines, the_twin_is_chosen_by_the_set_the_stage_takes)
    {
        SlangTwinFolder folder;
        folder.write("sky/moon/main.frag.p1a2b3c4d.spv", sPermutationWords);
        folder.writeText("sky/moon/main.frag.permutations", "main.frag.p1a2b3c4d.spv\tDIFFUSE_MAP GLOW_MAP MATERIAL\n");
        // The settings-driven defines the moon does not take do not stop the match.
        const auto module = vsgUtil::slangTwin(
            "sky/moon/main.frag", { "COVER", "DIFFUSE_MAP", "GI", "GLOW_MAP", "MATERIAL", "PBR" }, folder.options());
        ASSERT_TRUE(module);
        EXPECT_EQ(module->code, sPermutationWords);
    }

    TEST(ShaderDefines, an_unlisted_set_gets_no_twin_even_where_a_single_file_exists)
    {
        // The single "<stage>.spv" of task 0023 must never be drawn for a set it was not built for.
        SlangTwinFolder folder;
        folder.write("sky/moon/main.frag.p1a2b3c4d.spv", sPermutationWords);
        folder.writeText("sky/moon/main.frag.permutations", "main.frag.p1a2b3c4d.spv\tDIFFUSE_MAP GLOW_MAP MATERIAL\n");
        folder.write("sky/moon/main.frag.spv", sSingleFileWords);
        EXPECT_FALSE(vsgUtil::slangTwin("sky/moon/main.frag", { "DIFFUSE_MAP", "MATERIAL" }, folder.options()));
        // ... and a stage with no manifest at all gets none either.
        SlangTwinFolder bare;
        bare.write("sky/moon/main.frag.spv", sSingleFileWords);
        EXPECT_FALSE(
            vsgUtil::slangTwin("sky/moon/main.frag", { "DIFFUSE_MAP", "GLOW_MAP", "MATERIAL" }, bare.options()));
    }

    TEST(ShaderDefines, a_listed_file_that_cannot_be_read_gives_no_twin)
    {
        SlangTwinFolder folder;
        folder.writeText("sky/moon/main.frag.permutations", "main.frag.p1a2b3c4d.spv\tDIFFUSE_MAP GLOW_MAP MATERIAL\n");
        for (int attempt = 0; attempt < 2; ++attempt)
            EXPECT_FALSE(
                vsgUtil::slangTwin("sky/moon/main.frag", { "DIFFUSE_MAP", "GLOW_MAP", "MATERIAL" }, folder.options()));
    }

    // Task 0363, stage g: a stage the build wrote no stage file for cannot be asked for a set at all.
    TEST(ShaderDefines, a_stage_without_a_stage_file_is_an_error_not_a_guess)
    {
        SlangTwinFolder folder;
        EXPECT_THROW(vsgUtil::slangTwin("sky/other/main.frag", { "MATERIAL" }, folder.options()), std::runtime_error);
        EXPECT_THROW(
            vsgUtil::slangSelectionSet("sky/other/main.frag", { "MATERIAL" }, folder.options()), std::runtime_error);
    }

    // Task 0072, stage c: the compute passes are read with vsgUtil::readShader and made with no defines.
    const std::string sComputeManifest = "update.comp.pda39a3ee.spv\t\n";

    vsg::ref_ptr<vsg::ShaderStage> computeStage()
    {
        return vsgUtil::readShader("comp/fogofwar/update.comp", nullptr);
    }

    TEST(ShaderDefines, a_compute_stage_takes_a_copy_with_its_twin)
    {
        SlangTwinFolder folder;
        folder.write("comp/fogofwar/update.comp.pda39a3ee.spv", sPermutationWords);
        folder.writeText("comp/fogofwar/update.comp.permutations", sComputeManifest);
        const auto bare = computeStage();
        EXPECT_TRUE(bare->module->code.empty());
        EXPECT_TRUE(bare->module->source.empty());
        const auto stage = vsgUtil::withSlangTwin(bare, "comp/fogofwar/update.comp", folder.slangOptions());
        ASSERT_TRUE(stage);
        ASSERT_TRUE(stage->module);
        EXPECT_EQ(stage->module->code, sPermutationWords);
        EXPECT_TRUE(stage->module->source.empty());
        EXPECT_EQ(stage->stage, VK_SHADER_STAGE_COMPUTE_BIT);
        EXPECT_EQ(stage->entryPointName, "main");
        // The twin takes the settings constants, as a graphics twin does.
        EXPECT_EQ(stage->specializationConstants.count(1000), 1u);
        EXPECT_EQ(stage->specializationConstants.count(1004), 1u);
        // The stage that was read is left as it was.
        EXPECT_NE(stage, bare);
        EXPECT_TRUE(bare->module->code.empty());
        EXPECT_TRUE(bare->specializationConstants.empty());
    }

    TEST(ShaderDefines, a_compute_stage_without_a_built_twin_is_an_error)
    {
        // No manifest at all, then a manifest that lists another set only.
        SlangTwinFolder bare;
        const auto stage = computeStage();
        EXPECT_THROW(
            vsgUtil::withSlangTwin(stage, "comp/fogofwar/update.comp", bare.slangOptions()), std::runtime_error);
        SlangTwinFolder other;
        other.write("comp/fogofwar/update.comp.p1a2b3c4d.spv", sPermutationWords);
        other.writeText("comp/fogofwar/update.comp.permutations", "update.comp.p1a2b3c4d.spv\tCOLOR\n");
        EXPECT_THROW(
            vsgUtil::withSlangTwin(stage, "comp/fogofwar/update.comp", other.slangOptions()), std::runtime_error);
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

    TEST(ShaderDefines, the_stage_set_adds_the_settings_defines_the_stage_takes)
    {
        auto constants = vsgUtil::SettingsConstants::create();
        constants->defines = { "CLOUD_SHADOW", "DEBUG_SHADOW" };
        auto options = vsg::Options::create();
        options->setObject(vsgUtil::settingsConstantsKey, constants);
        const Names imports{ "NORMAL", "CLOUD_SHADOW" };
        EXPECT_EQ(vsgUtil::stageDefines(Names{ "NORMAL", "COLOR" }, imports, options.get()),
            (Names{ "CLOUD_SHADOW", "NORMAL" }));
        // Without the settings object, or with a stage that does not take the settings define, only the requested
        // names it takes.
        EXPECT_EQ(vsgUtil::stageDefines(Names{ "NORMAL" }, imports, nullptr), (Names{ "NORMAL" }));
        EXPECT_EQ(vsgUtil::stageDefines(Names{ "NORMAL" }, Names{ "NORMAL" }, options.get()), (Names{ "NORMAL" }));
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
    // Task 0072 (review of the water port): the shadow system draws at most four maps (View::Shadow,
    // lib/view/cascades.glsl), and the shaders' cascade arrays hold four; `number of shadow maps` takes 1 to 8, so the
    // constant both languages read is clamped to the cascades there are.
    TEST(ShaderDefines, the_shadow_map_count_constant_never_exceeds_the_cascades)
    {
        const Settings::CategorySetting key{ "Shadows", "number of shadow maps" };
        const auto saved = Settings::Manager::mUserSettings.find(key);
        const std::optional<std::string> previous = saved == Settings::Manager::mUserSettings.end()
            ? std::nullopt
            : std::optional<std::string>(saved->second);
        for (const auto& [setting, expected] : { std::pair{ "8", 4 }, std::pair{ "5", 4 }, std::pair{ "3", 3 } })
        {
            SCOPED_TRACE(setting);
            Settings::Manager::mUserSettings[key] = setting;
            Resource::ShaderSettings settings;
            const auto found = settings.constants->constants.find(Resource::settingsConstantBase + 62);
            ASSERT_NE(found, settings.constants->constants.end());
            const auto* value = dynamic_cast<const vsg::intValue*>(found->second.get());
            ASSERT_NE(value, nullptr);
            EXPECT_EQ(value->value(), expected);
            const auto* module = dynamic_cast<const vsg::ShaderModule*>(settings.objects.at("shadow.settings").get());
            ASSERT_NE(module, nullptr);
            EXPECT_NE(module->source.find("const int cShadowMapCount = " + std::to_string(expected) + ";"),
                std::string::npos);
        }
        if (previous)
            Settings::Manager::mUserSettings[key] = *previous;
        else
            Settings::Manager::mUserSettings.erase(key);
    }

    // Task 0072, stage f: the object shader declares its texture coordinate arrays (TEXCOORD, PARTICLE) with the
    // length numUvSets, a specialization constant, so its twins are built once per count and chosen by the pipeline,
    // which knows the count. The selection is path-agnostic, so the sky's folder of the fixture stands in for it.
    // The moon's vertex stage file takes VERTEX, NORMAL, TEXCOORD and PARTICLE (SlangTwinFolder writes it).
    const Names sMoonVertImports{ "NORMAL", "PARTICLE", "TEXCOORD", "VERTEX" };
    const std::string sCountingManifest
        = "main.vert.p00000001.spv\tTEXCOORD UVSETS_2 VERTEX\nmain.vert.p00000002.spv\tVERTEX\n";

    TEST(ShaderDefines, the_texture_coordinate_count_is_a_define_name)
    {
        EXPECT_EQ(vsgUtil::uvSetsDefine(1), "UVSETS_1");
        EXPECT_EQ(vsgUtil::uvSetsDefine(12), "UVSETS_12");
        EXPECT_EQ(
            vsgUtil::canonicalDefines({ "TEXCOORD", vsgUtil::uvSetsDefine(2), "VERTEX" }), "TEXCOORD UVSETS_2 VERTEX");
        EXPECT_TRUE(vsgUtil::declaresUvSetArrays({ "TEXCOORD" }));
        EXPECT_TRUE(vsgUtil::declaresUvSetArrays({ "COLOR", "PARTICLE" }));
        EXPECT_FALSE(vsgUtil::declaresUvSetArrays({ "NORMAL", "VERTEX" }));
        EXPECT_TRUE(vsgUtil::countsUvSets(vsgUtil::parsePermutationManifest(sCountingManifest)));
        EXPECT_FALSE(vsgUtil::countsUvSets(vsgUtil::parsePermutationManifest("main.vert.p00000001.spv\tTEXCOORD\n")));
    }

    TEST(ShaderDefines, a_family_that_counts_texture_coordinates_is_chosen_by_the_pipeline_with_its_count)
    {
        SlangTwinFolder folder;
        folder.write("sky/moon/main.vert.p00000001.spv", sPermutationWords);
        folder.write("sky/moon/main.vert.p00000002.spv", sSingleFileWords);
        folder.writeText("sky/moon/main.vert.permutations", sCountingManifest);
        const Names withArrays{ "GI", "TEXCOORD", "VERTEX" };
        // The stage, made once for the pipelines of every count, leaves a set with the arrays to the pipeline ...
        EXPECT_FALSE(vsgUtil::slangTwin("sky/moon/main.vert", withArrays, folder.options()));
        // ... which takes the file of its count, and the GLSL for any other count.
        const auto two = vsgUtil::slangTwin("sky/moon/main.vert", withArrays, folder.options(), 2u);
        ASSERT_TRUE(two);
        EXPECT_EQ(two->code, sPermutationWords);
        EXPECT_FALSE(vsgUtil::slangTwin("sky/moon/main.vert", withArrays, folder.options(), 1u));
        EXPECT_FALSE(vsgUtil::slangTwin("sky/moon/main.vert", withArrays, folder.options(), 0u));
        // A set without the arrays is the stage's to choose; the pipeline leaves it as the stage chose.
        const auto plain = vsgUtil::slangTwin("sky/moon/main.vert", { "VERTEX" }, folder.options());
        ASSERT_TRUE(plain);
        EXPECT_EQ(plain->code, sSingleFileWords);
        EXPECT_FALSE(vsgUtil::slangTwin("sky/moon/main.vert", { "VERTEX" }, folder.options(), 2u));
    }

    TEST(ShaderDefines, a_family_without_counts_keeps_choosing_its_texture_coordinate_sets_by_stage)
    {
        // The sky's clouds and night: one texture coordinate set, listed without a count (stage b).
        SlangTwinFolder folder;
        folder.write("sky/moon/main.vert.p00000001.spv", sPermutationWords);
        folder.writeText("sky/moon/main.vert.permutations", "main.vert.p00000001.spv\tTEXCOORD VERTEX\n");
        const auto module = vsgUtil::slangTwin("sky/moon/main.vert", { "TEXCOORD", "VERTEX" }, folder.options());
        ASSERT_TRUE(module);
        EXPECT_EQ(module->code, sPermutationWords);
        EXPECT_FALSE(vsgUtil::slangTwin("sky/moon/main.vert", { "TEXCOORD", "VERTEX" }, folder.options(), 1u));
    }

    TEST(ShaderDefines, the_selection_set_is_the_one_the_trace_writes)
    {
        SlangTwinFolder counting;
        counting.writeText("sky/moon/main.vert.permutations", sCountingManifest);
        const Names requested{ "GI", "TEXCOORD", "VERTEX" };
        EXPECT_EQ(vsgUtil::slangSelectionSet("sky/moon/main.vert", requested, counting.options()), std::nullopt);
        EXPECT_EQ(vsgUtil::slangSelectionSet("sky/moon/main.vert", requested, counting.options(), 3u),
            (Names{ "TEXCOORD", "UVSETS_3", "VERTEX" }));
        // A count no file can have is still written, so that a trace shows the set that drew with the GLSL.
        EXPECT_EQ(vsgUtil::slangSelectionSet("sky/moon/main.vert", requested, counting.options(), 0u),
            (Names{ "TEXCOORD", "UVSETS_0", "VERTEX" }));
        EXPECT_EQ(
            vsgUtil::slangSelectionSet("sky/moon/main.vert", { "NORMAL" }, counting.options()), (Names{ "NORMAL" }));
        EXPECT_EQ(vsgUtil::slangSelectionSet("sky/moon/main.vert", { "NORMAL" }, counting.options(), 3u), std::nullopt);
        // A family that does not count, or one with no manifest at all: the stage's set, written once.
        SlangTwinFolder bare;
        EXPECT_EQ(vsgUtil::slangSelectionSet("sky/moon/main.vert", requested, bare.options()),
            (Names{ "TEXCOORD", "VERTEX" }));
        EXPECT_EQ(vsgUtil::slangSelectionSet("sky/moon/main.vert", requested, bare.options(), 3u), std::nullopt);
    }

    TEST(ShaderDefines, a_pipeline_takes_its_twins_only_when_every_stage_without_code_has_one)
    {
        const auto bare = [&](VkShaderStageFlagBits stage) {
            return vsg::ShaderStage::create(stage, "main", vsg::ShaderModule::create());
        };
        const auto twin = vsg::ShaderModule::create(sPermutationWords);
        vsg::ShaderStages stages{ bare(VK_SHADER_STAGE_VERTEX_BIT), bare(VK_SHADER_STAGE_FRAGMENT_BIT) };
        EXPECT_FALSE(vsgUtil::takeSlangTwinsTogether(stages, { twin, nullptr }));
        EXPECT_FALSE(vsgUtil::takeSlangTwinsTogether(stages, { nullptr, twin }));
        for (const auto& stage : stages)
            EXPECT_TRUE(stage->module->code.empty());
        EXPECT_TRUE(vsgUtil::takeSlangTwinsTogether(stages, { twin, twin }));
        EXPECT_EQ(stages[0]->module, twin);
        EXPECT_EQ(stages[1]->module, twin);
        // A stage that is SPIR-V already (a Slang-only shader) needs no twin.
        vsg::ShaderStages withSpirv{ vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", twin),
            bare(VK_SHADER_STAGE_FRAGMENT_BIT) };
        EXPECT_TRUE(vsgUtil::takeSlangTwinsTogether(withSpirv, { nullptr, twin }));
        EXPECT_EQ(withSpirv[1]->module, twin);
        // A list that does not match the stages takes nothing.
        vsg::ShaderStages two{ bare(VK_SHADER_STAGE_VERTEX_BIT), bare(VK_SHADER_STAGE_FRAGMENT_BIT) };
        EXPECT_FALSE(vsgUtil::takeSlangTwinsTogether(two, { twin }));
        EXPECT_TRUE(two[0]->module->code.empty());
    }

    // Task 0072, review of stage f: everything below was written before the implementation, from the review's findings
    // alone (its red run was against the stubs).

    // A stage with no code whose module has `requested` as its compile settings (the requested names), as
    // Pipeline::Graphics makes one (vsgUtil::readShader).
    vsg::ref_ptr<vsg::ShaderStage> glslStageWith(VkShaderStageFlagBits flag, const Names& requested)
    {
        auto hints = vsg::ShaderCompileSettings::create();
        hints->defines = requested;
        auto module = vsg::ShaderModule::create();
        module->hints = hints;
        return vsg::ShaderStage::create(flag, "main", module);
    }

    // A twin's words told apart from the others by its bound (word 3); like sPermutationWords it declares the settings
    // constants 1000 and 1004.
    std::vector<uint32_t> twinWordsWithBound(uint32_t bound)
    {
        std::vector<uint32_t> words = moduleDeclaring({ 1000 }, { 1004 });
        words[3] = bound;
        return words;
    }

    // The code of a module, none for no module: two reads of one file are two modules with the same code.
    std::optional<std::vector<uint32_t>> codeOf(const vsg::ref_ptr<vsg::ShaderModule>& module)
    {
        if (!module)
            return std::nullopt;
        return module->code;
    }

    const std::vector<uint32_t> sVertexTwinWords = twinWordsWithBound(2001);
    const std::vector<uint32_t> sFragmentTwinWords = twinWordsWithBound(2002);

    TEST(ShaderTwinChoice, the_requested_defines_of_a_stage_are_its_module_hints)
    {
        const auto withHints = glslStageWith(VK_SHADER_STAGE_FRAGMENT_BIT, { "A", "B" });
        const std::set<std::string>* defines = vsgUtil::requestedDefinesOf(*withHints);
        ASSERT_NE(defines, nullptr);
        EXPECT_EQ(*defines, (Names{ "A", "B" }));
        const auto bare = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create());
        EXPECT_EQ(vsgUtil::requestedDefinesOf(*bare), nullptr);
        const auto spirv = vsg::ShaderStage::create(
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(sPermutationWords));
        EXPECT_EQ(vsgUtil::requestedDefinesOf(*spirv), nullptr);
    }

    // Task 0072, review of stage f, finding 1 (written before the implementation): once a twin has replaced the GLSL
    // module, the defines are those the stage's choice kept, on the stage and on a copy made after the choice was
    // attached (a pipeline twins its own copy of a cached stage).
    TEST(ShaderTwinChoice, the_requested_defines_survive_the_twin_that_replaces_the_module)
    {
        SlangTwinFolder folder;
        const auto stage = glslStageWith(VK_SHADER_STAGE_FRAGMENT_BIT, { "A", "B" });
        const auto choice = vsgUtil::attachSlangStageChoice(*stage, "sky/moon/main.frag", folder.options());
        ASSERT_TRUE(choice);
        EXPECT_EQ(vsgUtil::slangStageChoiceOf(*stage), choice.get());
        EXPECT_EQ(choice->path, "sky/moon/main.frag");
        ASSERT_TRUE(choice->hints);
        EXPECT_EQ(choice->hints->defines, (Names{ "A", "B" }));

        const auto copy = vsg::ShaderStage::create(*stage);
        EXPECT_EQ(vsgUtil::slangStageChoiceOf(*copy), choice.get());

        stage->module = vsg::ShaderModule::create(sPermutationWords);
        const std::set<std::string>* defines = vsgUtil::requestedDefinesOf(*stage);
        ASSERT_NE(defines, nullptr);
        EXPECT_EQ(*defines, (Names{ "A", "B" }));

        // The copy still shares the module; give it a twin of its own, so only the carried choice can answer.
        copy->module = vsg::ShaderModule::create(sPermutationWords);
        const std::set<std::string>* copied = vsgUtil::requestedDefinesOf(*copy);
        ASSERT_NE(copied, nullptr);
        EXPECT_EQ(*copied, (Names{ "A", "B" }));
    }

    TEST(ShaderTwinChoice, a_stage_that_has_code_gets_no_choice)
    {
        SlangTwinFolder folder;
        const auto stage = vsg::ShaderStage::create(
            VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::ShaderModule::create(sPermutationWords));
        EXPECT_FALSE(vsgUtil::attachSlangStageChoice(*stage, "sky/moon/main.frag", folder.options()));
        EXPECT_EQ(vsgUtil::slangStageChoiceOf(*stage), nullptr);
        EXPECT_EQ(vsgUtil::requestedDefinesOf(*stage), nullptr);
    }

    // Task 0072, review of stage f, finding 5 (written before the implementation): the choice is worked out once per
    // cached stage and is what the existing functions give, whatever the options' switch says (the caller decides
    // when to use it). In a family that counts texture coordinate sets, a stage with the arrays is left to each
    // pipeline, which adds its count.
    TEST(ShaderTwinChoice, a_counting_family_leaves_a_stage_with_the_arrays_to_each_pipeline)
    {
        SlangTwinFolder folder;
        folder.write("sky/moon/main.vert.p00000001.spv", sPermutationWords);
        folder.write("sky/moon/main.vert.p00000002.spv", sSingleFileWords);
        folder.writeText("sky/moon/main.vert.permutations", sCountingManifest);
        const std::string path = "sky/moon/main.vert";
        const Names requested{ "GI", "TEXCOORD", "VERTEX" };
        for (const bool constants : { false, true })
        {
            SCOPED_TRACE(constants ? "options with settings constants" : "options without");
            const auto options = constants ? folder.slangOptions() : folder.options();
            const auto stage = glslStageWith(VK_SHADER_STAGE_VERTEX_BIT, requested);
            const auto choice = vsgUtil::attachSlangStageChoice(*stage, path, options);
            ASSERT_TRUE(choice);
            EXPECT_EQ(choice->defines, vsgUtil::stageDefines(requested, sMoonVertImports, options.get()));
            EXPECT_EQ(choice->defines, (Names{ "TEXCOORD", "VERTEX" }));
            EXPECT_TRUE(choice->byPipeline);
            EXPECT_FALSE(choice->twin);
            const auto two = vsgUtil::slangTwinFor(*choice, 2u, options);
            ASSERT_TRUE(two);
            EXPECT_EQ(two->code, sPermutationWords);
            EXPECT_FALSE(vsgUtil::slangTwinFor(*choice, 1u, options));
            EXPECT_FALSE(vsgUtil::slangTwinFor(*choice, 0u, options));
            for (const uint32_t count : { 0u, 1u, 2u })
            {
                EXPECT_EQ(codeOf(vsgUtil::slangTwinFor(*choice, count, options)),
                    codeOf(vsgUtil::slangTwin(path, requested, options, count)))
                    << count;
            }
        }
    }

    // Task 0072, review of stage f, finding 5 (written before the implementation): in the same family a stage without
    // the arrays chooses its twin once, and every pipeline takes that one whatever its count.
    TEST(ShaderTwinChoice, a_stage_without_the_arrays_chooses_its_twin_once_for_every_count)
    {
        SlangTwinFolder folder;
        folder.write("sky/moon/main.vert.p00000001.spv", sPermutationWords);
        folder.write("sky/moon/main.vert.p00000002.spv", sSingleFileWords);
        folder.writeText("sky/moon/main.vert.permutations", sCountingManifest);
        const std::string path = "sky/moon/main.vert";
        const Names requested{ "VERTEX" };
        for (const bool constants : { false, true })
        {
            SCOPED_TRACE(constants ? "options with settings constants" : "options without");
            const auto options = constants ? folder.slangOptions() : folder.options();
            const auto stage = glslStageWith(VK_SHADER_STAGE_VERTEX_BIT, requested);
            const auto choice = vsgUtil::attachSlangStageChoice(*stage, path, options);
            ASSERT_TRUE(choice);
            EXPECT_EQ(choice->defines, vsgUtil::stageDefines(requested, sMoonVertImports, options.get()));
            EXPECT_FALSE(choice->byPipeline);
            ASSERT_TRUE(choice->twin);
            EXPECT_EQ(choice->twin->code, sSingleFileWords);
            EXPECT_EQ(codeOf(choice->twin), codeOf(vsgUtil::slangTwin(path, requested, options)));
            for (const uint32_t count : { 0u, 1u, 2u })
            {
                const auto twin = vsgUtil::slangTwinFor(*choice, count, options);
                ASSERT_TRUE(twin) << count;
                EXPECT_EQ(twin->code, sSingleFileWords) << count;
            }
        }
    }

    // Task 0072, review of stage f, finding 5 (written before the implementation): a family that names no count is
    // chosen by its stage, a stage with the arrays too (the sky's single-set twins).
    TEST(ShaderTwinChoice, a_family_without_counts_is_chosen_by_its_stage_even_with_the_arrays)
    {
        SlangTwinFolder folder;
        folder.write("sky/moon/main.vert.p00000001.spv", sPermutationWords);
        folder.writeText("sky/moon/main.vert.permutations", "main.vert.p00000001.spv\tTEXCOORD VERTEX\n");
        const std::string path = "sky/moon/main.vert";
        const Names requested{ "TEXCOORD", "VERTEX" };
        for (const bool constants : { false, true })
        {
            SCOPED_TRACE(constants ? "options with settings constants" : "options without");
            const auto options = constants ? folder.slangOptions() : folder.options();
            const auto stage = glslStageWith(VK_SHADER_STAGE_VERTEX_BIT, requested);
            const auto choice = vsgUtil::attachSlangStageChoice(*stage, path, options);
            ASSERT_TRUE(choice);
            EXPECT_EQ(choice->defines, (Names{ "TEXCOORD", "VERTEX" }));
            EXPECT_FALSE(choice->byPipeline);
            ASSERT_TRUE(choice->twin);
            EXPECT_EQ(choice->twin->code, sPermutationWords);
            EXPECT_EQ(codeOf(choice->twin), codeOf(vsgUtil::slangTwin(path, requested, options)));
            const auto twin = vsgUtil::slangTwinFor(*choice, 1u, options);
            ASSERT_TRUE(twin);
            EXPECT_EQ(twin->code, sPermutationWords);
        }
    }

    // Task 0072, review of stage f, finding 8: the object shader's and the terrain's stage files in a folder of their
    // own, with the twins a test lists, read as Pipeline::Graphics and Pipeline::terrain read them (the options' paths
    // and reader-writers). No shared objects, so every read is the file's. Task 0363, stage g: there is no GLSL beside
    // them; a stage file names the defines the stage takes.
    const std::string sDefaultVertStage = "default.vert.slang\tvertMain\tvertex\tNORMAL TEXCOORD VERTEX\n";
    const std::string sDefaultFragStage = "default.frag.slang\tfragMain\tfragment\tDIFFUSE_MAP MATERIAL TEXCOORD\n";
    const std::string sTerrainVertStage = "terrain.vert.slang\tvertMain\tvertex\tMODE_CONSTANTS\n";
    const std::string sTerrainFragStage
        = "terrain.frag.slang\tfragMain\tfragment\tGI_SKY MODE_CONSTANTS WETNESS WETNESS_PUDDLES\n";

    class PipelineTwinFolder
    {
    public:
        PipelineTwinFolder()
            : mDir(std::filesystem::temp_directory_path() / uniqueName())
        {
            std::filesystem::create_directories(mDir / "default");
            std::filesystem::create_directories(mDir / "terrain");
            writeText("default/main.vert.slang-stage", sDefaultVertStage);
            writeText("default/main.frag.slang-stage", sDefaultFragStage);
            writeText("terrain/main.vert.slang-stage", sTerrainVertStage);
            writeText("terrain/main.frag.slang-stage", sTerrainFragStage);
            mOptions = vsg::Options::create();
            mOptions->paths = { vsg::Path(mDir.generic_string()) };
            mOptions->readerWriters = { vsg::spirv::create() };
        }
        ~PipelineTwinFolder()
        {
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        PipelineTwinFolder(const PipelineTwinFolder&) = delete;
        PipelineTwinFolder& operator=(const PipelineTwinFolder&) = delete;

        void writeText(const std::string& file, const std::string& text)
        {
            std::ofstream out(mDir / file, std::ios::binary);
            out << text;
        }
        // Writes a twin of the stage file `stage` (for example "default/main.vert") built for the canonical define
        // list `set` and adds its line to the stage's manifest.
        void list(const std::string& stage, const std::string& hex, const std::string& set,
            const std::vector<uint32_t>& words)
        {
            const std::string file = std::filesystem::path(stage).filename().string() + ".p" + hex + ".spv";
            {
                std::ofstream out((mDir / stage).parent_path() / file, std::ios::binary);
                out.write(reinterpret_cast<const char*>(words.data()),
                    static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
            }
            std::ofstream manifest(mDir / (stage + ".permutations"), std::ios::binary | std::ios::app);
            manifest << file << '\t' << set << '\n';
        }
        // The shader options as the game makes them, with two settings constants.
        vsg::ref_ptr<const vsg::Options> options() const
        {
            auto result = vsg::Options::create(*mOptions);
            auto constants = vsgUtil::SettingsConstants::create();
            constants->constants[1000] = vsg::Value<VkBool32>::create(VK_TRUE);
            constants->constants[1004] = vsg::floatValue::create(0.5f);
            result->setObject(vsgUtil::settingsConstantsKey, constants);
            return result;
        }

    private:
        // Manifests are cached per process by search paths, so no two fixtures may share a folder.
        static std::string uniqueName()
        {
            static std::atomic<unsigned> counter{ 0 };
            return "openeden_pipelinetwin_"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_"
                + std::to_string(counter++);
        }

        std::filesystem::path mDir;
        vsg::ref_ptr<vsg::Options> mOptions;
    };

    const std::vector<uint32_t> sTerrainVertexTwinWords = twinWordsWithBound(2003);
    const std::vector<uint32_t> sTerrainFragmentTwinWords = twinWordsWithBound(2004);

    vsg::ref_ptr<vsg::ShaderStage> stageOf(
        const vsg::ref_ptr<vsg::BindGraphicsPipeline>& bind, VkShaderStageFlagBits flag)
    {
        if (!bind || !bind->pipeline)
            return {};
        for (const auto& stage : bind->pipeline->stages)
        {
            if (stage && stage->stage == flag)
                return stage;
        }
        return {};
    }

    bool isTwin(const vsg::ref_ptr<vsg::ShaderStage>& stage, const std::vector<uint32_t>& words)
    {
        return stage && stage->module && stage->module->code == words;
    }

    // Both stages of the pipeline draw with their twins.
    void expectTwins(const vsg::ref_ptr<vsg::BindGraphicsPipeline>& bind, const std::vector<uint32_t>& vertex,
        const std::vector<uint32_t>& fragment)
    {
        ASSERT_TRUE(bind);
        ASSERT_TRUE(bind->pipeline);
        EXPECT_EQ(bind->pipeline->stages.size(), 2u);
        EXPECT_TRUE(isTwin(stageOf(bind, VK_SHADER_STAGE_VERTEX_BIT), vertex)) << "vertex stage";
        EXPECT_TRUE(isTwin(stageOf(bind, VK_SHADER_STAGE_FRAGMENT_BIT), fragment)) << "fragment stage";
    }

    // A draw of the default shader: vertex set "VERTEX" and fragment set "MATERIAL", and TEXCOORD in both with
    // `texcoord`.
    Pipeline::Options objectKey(bool texcoord, uint32_t numUvSets)
    {
        Pipeline::Options key;
        key.shader.addMode(Pipeline::Mode::VERTEX);
        key.shader.addMode(Pipeline::Mode::MATERIAL);
        if (texcoord)
            key.shader.addMode(Pipeline::Mode::TEXCOORD);
        key.numUvSets = numUvSets;
        return key;
    }

    TEST(ShaderTwinChoice, a_graphics_pipeline_with_one_listed_stage_is_not_made)
    {
        for (const bool vertexListed : { true, false })
        {
            SCOPED_TRACE(vertexListed ? "only the vertex set listed" : "only the fragment set listed");
            PipelineTwinFolder folder;
            if (vertexListed)
            {
                folder.list("default/main.vert", "00000001", "VERTEX", sVertexTwinWords);
            }
            else
            {
                folder.list("default/main.frag", "00000002", "MATERIAL", sFragmentTwinWords);
            }
            Pipeline::Graphics graphics(folder.options());
            // There is no GLSL to draw with: one error, naming the stage and the set that has no twin.
            try
            {
                graphics.create(objectKey(false, 0u));
                ADD_FAILURE() << "the pipeline was made without a twin for one of its stages";
            }
            catch (const std::runtime_error& e)
            {
                const std::string text = e.what();
                const std::string stage = vertexListed ? "default/main.frag" : "default/main.vert";
                EXPECT_NE(text.find(stage), std::string::npos) << text;
                EXPECT_NE(text.find(vertexListed ? "[MATERIAL]" : "[VERTEX]"), std::string::npos) << text;
            }
        }
    }

    // Task 0072, review of stage f, finding 8 (written before the implementation).
    TEST(ShaderTwinChoice, a_graphics_pipeline_with_both_sets_listed_takes_both_twins)
    {
        PipelineTwinFolder folder;
        folder.list("default/main.vert", "00000001", "VERTEX", sVertexTwinWords);
        folder.list("default/main.frag", "00000002", "MATERIAL", sFragmentTwinWords);
        Pipeline::Graphics graphics(folder.options());
        expectTwins(graphics.create(objectKey(false, 0u)), sVertexTwinWords, sFragmentTwinWords);
    }

    // Task 0072, review of stage f, finding 8 (written before the implementation): in a family that counts texture
    // coordinate sets both stages are chosen with the pipeline's count, and a count with no file keeps the GLSL in
    // both.
    TEST(ShaderTwinChoice, a_counting_graphics_family_takes_both_twins_only_for_a_listed_count)
    {
        for (const uint32_t count : { 2u, 1u })
        {
            SCOPED_TRACE(count);
            PipelineTwinFolder folder;
            folder.list("default/main.vert", "00000003", "TEXCOORD UVSETS_2 VERTEX", sVertexTwinWords);
            folder.list("default/main.frag", "00000004", "MATERIAL TEXCOORD UVSETS_2", sFragmentTwinWords);
            Pipeline::Graphics graphics(folder.options());
            if (count == 2u)
            {
                expectTwins(graphics.create(objectKey(true, count)), sVertexTwinWords, sFragmentTwinWords);
            }
            else
            {
                EXPECT_THROW(graphics.create(objectKey(true, count)), std::runtime_error);
            }
        }
    }

    TEST(ShaderTwinChoice, pipelines_of_two_counts_from_one_stage_cache_choose_apart)
    {
        PipelineTwinFolder folder;
        folder.list("default/main.vert", "00000003", "TEXCOORD UVSETS_2 VERTEX", sVertexTwinWords);
        folder.list("default/main.frag", "00000004", "MATERIAL TEXCOORD UVSETS_2", sFragmentTwinWords);
        Pipeline::Graphics graphics(folder.options());
        EXPECT_THROW(graphics.create(objectKey(true, 1u)), std::runtime_error);
        const auto two = graphics.create(objectKey(true, 2u));
        EXPECT_THROW(graphics.create(objectKey(true, 1u)), std::runtime_error);
        expectTwins(two, sVertexTwinWords, sFragmentTwinWords);
    }

    TEST(ShaderTwinChoice, a_twinned_graphics_pipeline_keeps_the_requested_defines_of_its_stages)
    {
        PipelineTwinFolder folder;
        folder.list("default/main.vert", "00000001", "VERTEX", sVertexTwinWords);
        folder.list("default/main.frag", "00000002", "MATERIAL", sFragmentTwinWords);
        Pipeline::Graphics graphics(folder.options());
        const auto twinned = graphics.create(objectKey(false, 0u));
        expectTwins(twinned, sVertexTwinWords, sFragmentTwinWords);
        const auto vertexStage = stageOf(twinned, VK_SHADER_STAGE_VERTEX_BIT);
        const auto fragmentStage = stageOf(twinned, VK_SHADER_STAGE_FRAGMENT_BIT);
        ASSERT_TRUE(vertexStage);
        ASSERT_TRUE(fragmentStage);
        const std::set<std::string>* vertexDefines = vsgUtil::requestedDefinesOf(*vertexStage);
        const std::set<std::string>* fragmentDefines = vsgUtil::requestedDefinesOf(*fragmentStage);
        ASSERT_NE(vertexDefines, nullptr);
        ASSERT_NE(fragmentDefines, nullptr);
        EXPECT_EQ(vertexDefines->count("VERTEX"), 1u);
        EXPECT_EQ(fragmentDefines->count("MATERIAL"), 1u);
    }

    // Task 0072, review of the stage f and g rounds, finding 6 (written before the change): the choice is worked out
    // once per cached stage (Graphics::createShaderStage attaches it) and every pipeline's copy of that stage carries
    // that one; Graphics::create works one out on its own copy only for a stage made without one.
    TEST(ShaderTwinChoice, the_pipelines_made_from_one_cached_stage_share_its_choice)
    {
        PipelineTwinFolder folder;
        folder.list("default/main.vert", "00000001", "VERTEX", sVertexTwinWords);
        folder.list("default/main.frag", "00000002", "MATERIAL", sFragmentTwinWords);
        Pipeline::Graphics graphics(folder.options());
        // Two pipelines of one key and one of another colour mode (a specialization constant, not a stage define):
        // all three are made from the same two cached stages.
        Pipeline::Options otherColour = objectKey(false, 0u);
        otherColour.colorMode = 1;
        const std::vector<vsg::ref_ptr<vsg::BindGraphicsPipeline>> binds = { graphics.create(objectKey(false, 0u)),
            graphics.create(objectKey(false, 0u)), graphics.create(otherColour) };
        for (const auto& bind : binds)
            expectTwins(bind, sVertexTwinWords, sFragmentTwinWords);
        for (const VkShaderStageFlagBits flag : { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT })
        {
            SCOPED_TRACE(static_cast<int>(flag));
            const auto firstStage = stageOf(binds[0], flag);
            ASSERT_TRUE(firstStage);
            const vsgUtil::SlangStageChoice* choice = vsgUtil::slangStageChoiceOf(*firstStage);
            ASSERT_NE(choice, nullptr);
            for (std::size_t i = 1; i < binds.size(); ++i)
            {
                const auto stage = stageOf(binds[i], flag);
                ASSERT_TRUE(stage);
                // Another copy of the stage, carrying the same choice.
                EXPECT_NE(stage.get(), firstStage.get());
                EXPECT_EQ(vsgUtil::slangStageChoiceOf(*stage), choice) << "pipeline " << i;
            }
        }
    }

    TEST(ShaderTwinChoice, the_terrain_with_one_listed_stage_is_not_made)
    {
        for (const bool vertexListed : { true, false })
        {
            SCOPED_TRACE(vertexListed ? "only the vertex stage listed" : "only the fragment stage listed");
            PipelineTwinFolder folder;
            if (vertexListed)
            {
                folder.list("terrain/main.vert", "00000005", "", sTerrainVertexTwinWords);
            }
            else
            {
                folder.list("terrain/main.frag", "00000006", "", sTerrainFragmentTwinWords);
            }
            EXPECT_THROW(Pipeline::terrain(folder.options()), std::runtime_error);
        }
    }

    // Task 0072, review of stage f, finding 8 (written before the implementation).
    TEST(ShaderTwinChoice, the_terrain_with_both_stages_listed_takes_both_twins)
    {
        PipelineTwinFolder folder;
        folder.list("terrain/main.vert", "00000005", "", sTerrainVertexTwinWords);
        folder.list("terrain/main.frag", "00000006", "", sTerrainFragmentTwinWords);
        expectTwins(Pipeline::terrain(folder.options()), sTerrainVertexTwinWords, sTerrainFragmentTwinWords);
    }

    // Task 0363 (stage c): with `shader constants` the terrain asks for the one MODE_CONSTANTS permutation of each
    // stage whatever its settings are, and the stage carries the values (Pipeline::terrainConstants); with the switch
    // off the same listing gives the GLSL (the set the define form needs is not listed).
    TEST(ShaderTwinChoice, the_terrain_with_the_constants_switch_takes_the_constants_permutation_and_its_values)
    {
        PipelineTwinFolder folder;
        folder.list("terrain/main.vert", "00000007", "MODE_CONSTANTS", sTerrainVertexTwinWords);
        folder.list("terrain/main.frag", "00000008", "MODE_CONSTANTS", sTerrainFragmentTwinWords);
        auto options = vsg::Options::create(*folder.options());
        auto settings = vsgUtil::SettingsConstants::create();
        settings->constantDefines = true;
        options->setObject(vsgUtil::settingsConstantsKey, settings);
        const Pipeline::WetnessOptions wetness{ true, false, false };
        const auto bind = Pipeline::terrain(
            options, false, 0, true, wetness, false, Pipeline::CoverOptions{}, 0, false, false, false);
        expectTwins(bind, sTerrainVertexTwinWords, sTerrainFragmentTwinWords);
        const auto fragment = stageOf(bind, VK_SHADER_STAGE_FRAGMENT_BIT);
        ASSERT_TRUE(fragment);
        const auto flag = [&](uint32_t id) {
            const auto it = fragment->specializationConstants.find(id);
            const auto* value = it == fragment->specializationConstants.end()
                ? nullptr
                : dynamic_cast<const vsg::Value<VkBool32>*>(it->second.get());
            return value != nullptr && value->value() != VK_FALSE;
        };
        EXPECT_TRUE(flag(Pipeline::TerrainConstantId::GiSky));
        EXPECT_TRUE(flag(Pipeline::TerrainConstantId::Wetness));
        EXPECT_FALSE(flag(Pipeline::TerrainConstantId::WetnessPuddles));
        EXPECT_FALSE(flag(Pipeline::TerrainConstantId::Cover));

        // Without the switch the define form asks for the set its settings make, which is not listed here.
        EXPECT_THROW(Pipeline::terrain(folder.options(), false, 0, true, wetness, false, Pipeline::CoverOptions{}, 0,
                         false, false, false),
            std::runtime_error);
    }

    // Task 0072, review of stage f, finding 7: the GUI's stages (read with readShader, compiled with no defines) take
    // their twins together through vsgUtil::withSlangTwinsTogether, as copies.
    const std::vector<std::string> sGuiPaths{ "sky/moon/main.vert", "sky/moon/main.frag" };

    // The stages as vsgUtil::readShader makes them: no code, no source.
    vsg::ShaderStages guiStages()
    {
        return { vsgUtil::readShader("sky/moon/main.vert", nullptr),
            vsgUtil::readShader("sky/moon/main.frag", nullptr) };
    }

    void listGuiTwins(SlangTwinFolder& folder, bool vertex, bool fragment)
    {
        if (vertex)
        {
            folder.write("sky/moon/main.vert.p00000001.spv", sVertexTwinWords);
            folder.writeText("sky/moon/main.vert.permutations", "main.vert.p00000001.spv\t\n");
        }
        if (fragment)
        {
            folder.write("sky/moon/main.frag.p00000002.spv", sFragmentTwinWords);
            folder.writeText("sky/moon/main.frag.permutations", "main.frag.p00000002.spv\t\n");
        }
    }

    // The stages are left exactly as they were: the result is made of copies.
    void expectUnchangedGui(const vsg::ShaderStages& stages)
    {
        ASSERT_EQ(stages.size(), 2u);
        for (const auto& stage : stages)
        {
            EXPECT_TRUE(stage->module->source.empty());
            EXPECT_TRUE(stage->module->code.empty());
            EXPECT_TRUE(stage->specializationConstants.empty());
        }
    }

    // Task 0072, review of stage f, finding 7 (written before the implementation).
    TEST(ShaderTwinChoice, gui_stages_with_both_twins_listed_take_them_as_new_copies)
    {
        SlangTwinFolder folder;
        listGuiTwins(folder, true, true);
        const vsg::ShaderStages stages = guiStages();
        const vsg::ShaderStages result = vsgUtil::withSlangTwinsTogether(stages, sGuiPaths, folder.slangOptions());
        ASSERT_EQ(result.size(), 2u);
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            SCOPED_TRACE(i);
            ASSERT_TRUE(result[i]);
            EXPECT_NE(result[i], stages[i]);
            ASSERT_TRUE(result[i]->module);
            EXPECT_TRUE(result[i]->module->source.empty());
            EXPECT_EQ(result[i]->stage, stages[i]->stage);
            EXPECT_EQ(result[i]->entryPointName, "main");
            EXPECT_EQ(result[i]->specializationConstants.count(1000), 1u);
            EXPECT_EQ(result[i]->specializationConstants.count(1004), 1u);
        }
        EXPECT_EQ(result[0]->module->code, sVertexTwinWords);
        EXPECT_EQ(result[1]->module->code, sFragmentTwinWords);
        expectUnchangedGui(stages);
    }

    TEST(ShaderTwinChoice, gui_stages_with_one_twin_listed_are_an_error)
    {
        for (const bool vertex : { true, false })
        {
            SCOPED_TRACE(vertex ? "only the vertex twin" : "only the fragment twin");
            SlangTwinFolder folder;
            listGuiTwins(folder, vertex, !vertex);
            const vsg::ShaderStages stages = guiStages();
            EXPECT_THROW(vsgUtil::withSlangTwinsTogether(stages, sGuiPaths, folder.slangOptions()), std::runtime_error);
            expectUnchangedGui(stages);
        }
    }

    TEST(ShaderTwinChoice, gui_stages_with_paths_that_do_not_fit_are_returned_as_they_are)
    {
        SlangTwinFolder folder;
        listGuiTwins(folder, true, true);
        const vsg::ShaderStages stages = guiStages();
        const std::vector<std::string> fewer{ "sky/moon/main.vert" };
        const std::vector<std::string> more{ "sky/moon/main.vert", "sky/moon/main.frag", "sky/moon/main.frag" };
        EXPECT_EQ(vsgUtil::withSlangTwinsTogether(stages, fewer, folder.slangOptions()), stages);
        EXPECT_EQ(vsgUtil::withSlangTwinsTogether(stages, more, folder.slangOptions()), stages);
        expectUnchangedGui(stages);
    }

    // Task 0072, review of stage f, finding 10 (written before the implementation): the process-wide cache of
    // findSlangPermutation holds no module, so a twin (and through its compiled Vulkan objects the device) goes once
    // nothing else holds it, and a later lookup reads its file again.
    TEST(ShaderTwinChoice, a_found_twin_goes_once_nothing_else_holds_it)
    {
        SlangTwinFolder folder;
        folder.write("sky/moon/main.frag.p1a2b3c4d.spv", sPermutationWords);
        folder.writeText("sky/moon/main.frag.permutations", "main.frag.p1a2b3c4d.spv\tDIFFUSE_MAP GLOW_MAP MATERIAL\n");
        const Names set{ "DIFFUSE_MAP", "GLOW_MAP", "MATERIAL" };
        auto module = vsgUtil::findSlangPermutation("sky/moon/main.frag", set, folder.options());
        ASSERT_TRUE(module);
        vsg::observer_ptr<vsg::ShaderModule> observer(module);
        ASSERT_TRUE(observer.valid());
        module.reset();
        EXPECT_FALSE(observer.valid());
        EXPECT_FALSE(observer.ref_ptr());
        const auto again = vsgUtil::findSlangPermutation("sky/moon/main.frag", set, folder.options());
        ASSERT_TRUE(again);
        EXPECT_EQ(again->code, sPermutationWords);
    }

    // Task 0072, review of stage f, finding 10 (written before the implementation): the same for the twins a stage's
    // choice and a pipeline's count take: once the stage, its choice and the result are gone, the module is too.
    TEST(ShaderTwinChoice, a_twin_a_stage_or_a_pipeline_chose_goes_with_its_last_holder)
    {
        SlangTwinFolder folder;
        folder.write("sky/moon/main.vert.p00000001.spv", sPermutationWords);
        folder.write("sky/moon/main.vert.p00000002.spv", sSingleFileWords);
        folder.writeText("sky/moon/main.vert.permutations", sCountingManifest);
        vsg::observer_ptr<vsg::ShaderModule> byPipeline;
        vsg::observer_ptr<vsg::ShaderModule> byStage;
        {
            const auto stage = glslStageWith(VK_SHADER_STAGE_VERTEX_BIT, { "GI", "TEXCOORD", "VERTEX" });
            const auto choice = vsgUtil::attachSlangStageChoice(*stage, "sky/moon/main.vert", folder.options());
            ASSERT_TRUE(choice);
            const auto twin = vsgUtil::slangTwinFor(*choice, 2u, folder.options());
            ASSERT_TRUE(twin);
            byPipeline = twin;

            const auto plain = glslStageWith(VK_SHADER_STAGE_VERTEX_BIT, { "VERTEX" });
            const auto plainChoice = vsgUtil::attachSlangStageChoice(*plain, "sky/moon/main.vert", folder.options());
            ASSERT_TRUE(plainChoice);
            ASSERT_TRUE(plainChoice->twin);
            byStage = plainChoice->twin;
            ASSERT_TRUE(byPipeline.valid());
            ASSERT_TRUE(byStage.valid());
        }
        EXPECT_FALSE(byPipeline.valid());
        EXPECT_FALSE(byStage.valid());
    }

    // Task 0072, review of stage f (written before the implementation): a pipeline's (vertex, fragment) pair is written
    // to the shader trace with what draws it, since a pipeline takes both twins or neither.
    TEST(ShaderTwinChoice, a_pipeline_trace_line_names_both_sets_and_what_draws_them)
    {
        const Names vertex{ "TEXCOORD", "UVSETS_1", "VERTEX" };
        const Names fragment{ "MATERIAL" };
        EXPECT_EQ(vsgUtil::pipelineTraceLine("default", vertex, fragment, true),
            "default/pipeline\tTEXCOORD UVSETS_1 VERTEX\tMATERIAL\tslang");
        EXPECT_EQ(vsgUtil::pipelineTraceLine("default", vertex, fragment, false),
            "default/pipeline\tTEXCOORD UVSETS_1 VERTEX\tMATERIAL\tglsl");
        EXPECT_EQ(vsgUtil::pipelineTraceLine("default", Names{}, Names{}, false), "default/pipeline\t\t\tglsl");
    }
}
