// Task 0072, stage j (1): recorded interfaces. SlangPermutations compares each built Slang twin with the interface of
// the GLSL stage it replaces, compiled live. Deleting a GLSL file would delete that check, so the GLSL's interfaces are
// recorded once, per stage and define set, in "<stage>.interface" beside the sources (files/shaders):
//
//     # comments
//     defines <names a set of this stage may contain: imported defines plus settings defines>
//     set <canonical define set, or ->
//     <one SpirvInterface line per line>
//     end
//
// The records are GOLDEN DATA: they change only in a pull request that says why, reviewed by toddhow. They are made
// by `SlangInterfaceRecords.write_the_records_when_asked` (set OPENEDEN_RECORD_INTERFACES=1 and run that test in a
// build that has slangc) and are proven against the live GLSL by `records_equal_the_live_glsl_interfaces`, which stays
// as long as the GLSL does. `every_permutation_has_the_recorded_interface` is SlangPermutations' check read from the
// records, with the same `.interface-exceptions` files; it is what remains when a GLSL file is deleted.
#include <gtest/gtest.h>

#include <components/resource/shadersettings.hpp>
#include <components/vsgutil/shaderpermutation.hpp>
#include <components/vsgutil/spirvinterface.hpp>

#include <vsg/all.h>
#include <vsg/io/glsl.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    [[maybe_unused]] std::vector<uint32_t> readWords(const std::filesystem::path& path)
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

    [[maybe_unused]] std::string settingsFileOf(std::string_view name)
    {
        return name == "DEBUG_CHUNKS" ? "terrain.settings" : "shadow.settings";
    }

    [[maybe_unused]] vsg::ref_ptr<vsg::Options> glslOptions(const std::set<std::string>& settingsDefines = {})
    {
        auto options = vsg::Options::create();
        options->paths = { OPENEDEN_SHADER_SOURCE_DIR };
        vsg::ref_ptr<Resource::ShaderSettings> settings{ new Resource::ShaderSettings };
        for (const std::string& name : settingsDefines)
        {
            auto& object = settings->objects[settingsFileOf(name)];
            const auto* module = dynamic_cast<const vsg::ShaderModule*>(object.get());
            std::string text = module ? module->source : std::string();
            if (!text.empty() && text.back() != '\n')
                text += '\n';
            object = vsg::ShaderModule::create(text + "#define " + name + " 1\n");
        }
        options->readerWriters = { settings, vsg::glsl::create() };
        return options;
    }

    [[maybe_unused]] std::string glslSource(const std::string& file, const std::set<std::string>& settingsDefines = {})
    {
        auto stage = vsg::read_cast<vsg::ShaderStage>(file, glslOptions(settingsDefines));
        return stage && stage->module ? stage->module->source : std::string();
    }

    [[maybe_unused]] std::vector<uint32_t> compileGlsl(const std::string& file, const std::string& canonicalDefines)
    {
        std::vector<std::string> defines;
        std::set<std::string> settingsDefines;
        std::istringstream names(canonicalDefines);
        const auto settingsNames = Resource::settingsDefineNames();
        for (std::string name; names >> name;)
        {
            if (std::find(settingsNames.begin(), settingsNames.end(), name) != settingsNames.end())
                settingsDefines.insert(name);
            else
                defines.push_back(name);
        }
        auto options = glslOptions(settingsDefines);
        auto stage = vsg::read_cast<vsg::ShaderStage>(file, options);
        if (!stage)
            return {};
        vsg::ShaderStages stages{ stage };
        if (!vsg::ShaderCompiler::create()->compile(stages, defines, options))
            return {};
        return stage->module->code;
    }

    [[maybe_unused]] std::optional<uint32_t> uvSetsCount(const std::string& name)
    {
        for (uint32_t count = 0; count <= 64; ++count)
        {
            if (name == vsgUtil::uvSetsDefine(count))
                return count;
        }
        return std::nullopt;
    }

    // The pinned constants of a set (its texture coordinate count is the specialization constant 0 of the GLSL).
    [[maybe_unused]] std::map<uint32_t, uint32_t> pinnedConstants(const std::string& defines)
    {
        std::map<uint32_t, uint32_t> pinned;
        std::istringstream names(defines == "-" ? std::string() : defines);
        for (std::string name; names >> name;)
        {
            if (const auto count = uvSetsCount(name))
                pinned[0] = *count;
        }
        return pinned;
    }

    struct Record
    {
        std::set<std::string> mDefines; // the names a set of this stage may contain
        std::map<std::string, std::set<std::string>> mInterface; // canonical set -> interface lines
    };

    [[maybe_unused]] std::optional<Record> readRecord(const std::string& stageFile)
    {
        std::ifstream in(
            std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / (stageFile + ".interface"), std::ios::binary);
        if (!in)
            return std::nullopt;
        Record record;
        std::string current;
        bool inSet = false;
        for (std::string line; std::getline(in, line);)
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (inSet)
            {
                if (line == "end")
                    inSet = false;
                else
                    record.mInterface[current].insert(line);
                continue;
            }
            if (line.empty() || line[0] == '#')
                continue;
            if (line.rfind("defines", 0) == 0)
            {
                std::istringstream names(line.substr(7));
                for (std::string name; names >> name;)
                    record.mDefines.insert(name);
            }
            else if (line.rfind("set ", 0) == 0)
            {
                current = line.substr(4);
                inSet = true;
                record.mInterface[current];
            }
            else
            {
                ADD_FAILURE() << stageFile << ".interface: unknown line: " << line;
            }
        }
        return record;
    }

    [[maybe_unused]] std::set<std::string> possibleDefines(const std::string& stageFile)
    {
        std::set<std::string> possible = vsgUtil::importedDefines(glslSource(stageFile));
        std::set<std::string> allSettingsDefines;
        for (const std::string_view name : Resource::settingsDefineNames())
            allSettingsDefines.emplace(name);
        for (const std::string& name :
            vsgUtil::includedSettingsDefines(glslSource(stageFile, allSettingsDefines), allSettingsDefines))
            possible.insert(name);
        return possible;
    }

    struct Manifest
    {
        std::string mStageFile;
        std::filesystem::path mDirectory;
        vsgUtil::PermutationManifest mFiles;
    };

    [[maybe_unused]] std::vector<Manifest> builtManifests()
    {
        std::vector<Manifest> result;
        const std::filesystem::path built(OPENEDEN_SHADER_BUILD_DIR);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(built))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".permutations")
                continue;
            std::filesystem::path relative = std::filesystem::relative(entry.path(), built);
            relative.replace_extension();
            std::ifstream in(entry.path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            result.push_back(
                { relative.generic_string(), entry.path().parent_path(), vsgUtil::parsePermutationManifest(text) });
        }
        std::sort(result.begin(), result.end(),
            [](const Manifest& a, const Manifest& b) { return a.mStageFile < b.mStageFile; });
        return result;
    }

    // The same exceptions SlangPermutations reads (copied: that test's helpers are private to its file).
    struct Exceptions
    {
        std::map<std::pair<std::string, std::string>, bool> mLines;
        bool accept(const std::string& defines, const std::string& line)
        {
            for (const std::string& key : { defines, std::string() })
            {
                const auto it = mLines.find({ key, line });
                if (it != mLines.end())
                {
                    it->second = true;
                    return true;
                }
            }
            return false;
        }
    };

    [[maybe_unused]] Exceptions readExceptions(const std::string& stageFile)
    {
        Exceptions exceptions;
        std::ifstream in(std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / (stageFile + ".interface-exceptions"),
            std::ios::binary);
        for (std::string line; std::getline(in, line);)
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            std::string defines;
            if (line[0] == '[')
            {
                const std::size_t close = line.find("] ");
                if (close == std::string::npos)
                {
                    ADD_FAILURE() << stageFile << ".interface-exceptions: no \"] \" after the define set: " << line;
                    continue;
                }
                defines = line.substr(1, close - 1);
                line = line.substr(close + 2);
            }
            exceptions.mLines.emplace(std::make_pair(defines, line), false);
        }
        return exceptions;
    }

    [[maybe_unused]] bool isSettingsConstant(const std::string& line)
    {
        for (const auto& entry : Resource::settingsConstantTable())
        {
            const std::string id = "id=" + std::to_string(entry.mId) + " ";
            const char* type = entry.mType == 'b' ? "bool" : entry.mType == 'i' ? "i32" : "f32";
            if (line == "spec_constant " + id + type || line.rfind("spec_constant_default " + id, 0) == 0)
                return true;
        }
        return false;
    }

    TEST(SlangInterfaceRecords, write_the_records_when_asked)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc";
#else
        if (!std::getenv("OPENEDEN_RECORD_INTERFACES"))
            GTEST_SKIP() << "set OPENEDEN_RECORD_INTERFACES=1 to write the records into files/shaders";
        for (const Manifest& manifest : builtManifests())
        {
            std::ostringstream out;
            out << "# Recorded interface of the GLSL stage " << manifest.mStageFile
                << " per define set (task 0072, stage j). GOLDEN DATA: changes only in a pull request that says why,\n"
                << "# reviewed by toddhow. Made by SlangInterfaceRecords.write_the_records_when_asked.\n";
            out << "defines";
            for (const std::string& name : possibleDefines(manifest.mStageFile))
                out << ' ' << name;
            out << '\n';
            std::map<std::string, std::set<std::string>> byDefines;
            for (const auto& [defines, file] : manifest.mFiles.mFiles)
            {
                const auto words = compileGlsl(manifest.mStageFile, defines);
                ASSERT_FALSE(words.empty()) << manifest.mStageFile << " [" << defines << "] did not compile";
                byDefines[defines] = vsgUtil::readSpirvInterface(words, pinnedConstants(defines)).mLines;
            }
            for (const auto& [defines, lines] : byDefines)
            {
                out << "set " << defines << '\n';
                for (const std::string& line : lines)
                    out << line << '\n';
                out << "end\n";
            }
            std::ofstream file(std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / (manifest.mStageFile + ".interface"),
                std::ios::binary | std::ios::trunc);
            file << out.str();
        }
#endif
    }

    // Task 0072, stage j (the GPU cost of the object shader): writes the SPIR-V glslang makes of every listed set of
    // the stages under OPENEDEN_DUMP_GLSL=<folder>, as <folder>/<stage>/<twin's file name>, so that it can be compared
    // with the twin's file of the same name (instruction counts, a disassembly). A development aid; skipped without the
    // variable.
    TEST(SlangInterfaceRecords, dump_the_glsl_spirv_when_asked)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc";
#else
        const char* folder = std::getenv("OPENEDEN_DUMP_GLSL");
        if (!folder)
        {
            GTEST_SKIP() << "set OPENEDEN_DUMP_GLSL=<folder> to write the GLSL stages' SPIR-V";
        }
        for (const Manifest& manifest : builtManifests())
        {
            const std::filesystem::path directory = std::filesystem::path(folder) / manifest.mStageFile;
            std::filesystem::create_directories(directory.parent_path());
            for (const auto& [defines, file] : manifest.mFiles.mFiles)
            {
                const auto words = compileGlsl(manifest.mStageFile, defines);
                ASSERT_FALSE(words.empty()) << manifest.mStageFile << " [" << defines << "] did not compile";
                std::filesystem::create_directories(directory);
                std::ofstream out(directory / file, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(words.data()),
                    static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
            }
        }
#endif
    }

    TEST(SlangInterfaceRecords, records_equal_the_live_glsl_interfaces)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc";
#else
        std::size_t checked = 0;
        for (const Manifest& manifest : builtManifests())
        {
            SCOPED_TRACE(manifest.mStageFile);
            const auto record = readRecord(manifest.mStageFile);
            ASSERT_TRUE(record) << manifest.mStageFile << ".interface is missing";
            EXPECT_EQ(record->mDefines, possibleDefines(manifest.mStageFile));
            std::set<std::string> listed;
            for (const auto& [defines, file] : manifest.mFiles.mFiles)
            {
                SCOPED_TRACE("[" + defines + "]");
                listed.insert(defines);
                const auto words = compileGlsl(manifest.mStageFile, defines);
                if (words.empty())
                {
                    ADD_FAILURE() << "the GLSL stage did not compile";
                    continue;
                }
                const auto it = record->mInterface.find(defines);
                if (it == record->mInterface.end())
                {
                    ADD_FAILURE() << "no record for this set";
                    continue;
                }
                EXPECT_EQ(it->second, vsgUtil::readSpirvInterface(words, pinnedConstants(defines)).mLines);
                ++checked;
            }
            for (const auto& [defines, lines] : record->mInterface)
                EXPECT_EQ(listed.count(defines), 1u) << "the record has a set the build does not list: " << defines;
        }
        EXPECT_GT(checked, 0u);
#endif
    }

    TEST(SlangInterfaceRecords, every_permutation_has_the_recorded_interface)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc";
#else
        std::size_t checked = 0;
        for (const Manifest& manifest : builtManifests())
        {
            const auto record = readRecord(manifest.mStageFile);
            ASSERT_TRUE(record) << manifest.mStageFile << ".interface is missing";
            Exceptions exceptions = readExceptions(manifest.mStageFile);
            for (const auto& [defines, file] : manifest.mFiles.mFiles)
            {
                SCOPED_TRACE(manifest.mStageFile + " [" + defines + "] " + file);
                std::istringstream names(defines == "-" ? std::string() : defines);
                std::set<std::string> setNames;
                std::optional<uint32_t> uvSets;
                for (std::string name; names >> name;)
                {
                    setNames.insert(name);
                    if (const auto count = uvSetsCount(name))
                    {
                        EXPECT_FALSE(uvSets) << "two texture coordinate counts";
                        uvSets = count;
                        continue;
                    }
                    EXPECT_EQ(record->mDefines.count(name), 1u) << name << " is neither imported nor a settings define";
                }
                if (uvSets)
                {
                    EXPECT_TRUE(vsgUtil::declaresUvSetArrays(setNames)) << "a count without the texture coordinates";
                }
                const auto recorded = record->mInterface.find(defines);
                if (recorded == record->mInterface.end())
                {
                    ADD_FAILURE() << "no record for this set";
                    continue;
                }
                const auto slangWords = readWords(manifest.mDirectory / file);
                if (slangWords.empty())
                {
                    ADD_FAILURE() << file << " was not built";
                    continue;
                }
                const auto slang = vsgUtil::readSpirvInterface(slangWords);
                vsgUtil::SpirvInterface glsl;
                glsl.mLines = recorded->second;
                EXPECT_EQ(slang.mVersion, 0x00010000u);
                const auto diff = vsgUtil::compareSpirvInterfaces(slang, glsl);
                for (const std::string& line : diff.mOnlyInSecond)
                {
                    if (!exceptions.accept(defines, "glsl-only: " + line))
                        ADD_FAILURE() << "the GLSL declares, Slang does not: " << line;
                }
                for (const std::string& line : diff.mOnlyInFirst)
                {
                    if (!isSettingsConstant(line) && !exceptions.accept(defines, "slang-only: " + line))
                        ADD_FAILURE() << "Slang declares, the GLSL does not: " << line;
                }
                EXPECT_EQ(slang.mSpecConstantOps, 0u) << "OpSpecConstantOp in the twin";
                ++checked;
            }
            for (const auto& [key, used] : exceptions.mLines)
            {
                EXPECT_TRUE(used) << manifest.mStageFile
                                  << ".interface-exceptions lists a difference that does not occur: [" << key.first
                                  << "] " << key.second;
            }
        }
        EXPECT_GT(checked, 0u);
#endif
    }

    // Task 0072, stage j: the light grid (comp/light/assign.comp) and its Slang twin run on the GPU over the same
    // lights, and the clusters' light lists are read back and compared. A development check with a real device (it
    // skips without one): the twin must assign the lights the GLSL assigns.
    struct GpuLightGrid
    {
        VkInstance mInstance = VK_NULL_HANDLE;
        VkPhysicalDevice mPhysical = VK_NULL_HANDLE;
        VkDevice mDevice = VK_NULL_HANDLE;
        VkQueue mQueue = VK_NULL_HANDLE;
        uint32_t mFamily = 0;
        VkPhysicalDeviceMemoryProperties mMemory{};

        struct Buffer
        {
            VkBuffer mBuffer = VK_NULL_HANDLE;
            VkDeviceMemory mMemory = VK_NULL_HANDLE;
            void* mMapped = nullptr;
            VkDeviceSize mSize = 0;
        };

        GpuLightGrid() = default;
        GpuLightGrid(const GpuLightGrid&) = delete;
        GpuLightGrid& operator=(const GpuLightGrid&) = delete;

        ~GpuLightGrid()
        {
            if (mDevice != VK_NULL_HANDLE)
            {
                vkDestroyDevice(mDevice, nullptr);
            }
            if (mInstance != VK_NULL_HANDLE)
            {
                vkDestroyInstance(mInstance, nullptr);
            }
        }

        bool create()
        {
            VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
            app.apiVersion = VK_API_VERSION_1_1;
            VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
            instanceInfo.pApplicationInfo = &app;
            if (vkCreateInstance(&instanceInfo, nullptr, &mInstance) != VK_SUCCESS)
            {
                return false;
            }
            uint32_t count = 0;
            vkEnumeratePhysicalDevices(mInstance, &count, nullptr);
            std::vector<VkPhysicalDevice> devices(count);
            vkEnumeratePhysicalDevices(mInstance, &count, devices.data());
            for (const VkPhysicalDevice device : devices)
            {
                uint32_t families = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(device, &families, nullptr);
                std::vector<VkQueueFamilyProperties> properties(families);
                vkGetPhysicalDeviceQueueFamilyProperties(device, &families, properties.data());
                for (uint32_t i = 0; i < families; ++i)
                {
                    if ((properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 && mPhysical == VK_NULL_HANDLE)
                    {
                        mPhysical = device;
                        mFamily = i;
                    }
                }
                VkPhysicalDeviceProperties deviceProperties;
                vkGetPhysicalDeviceProperties(device, &deviceProperties);
                if (mPhysical == device && deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    break;
                }
            }
            if (mPhysical == VK_NULL_HANDLE)
            {
                return false;
            }
            vkGetPhysicalDeviceMemoryProperties(mPhysical, &mMemory);
            const float priority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queueInfo.queueFamilyIndex = mFamily;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;
            VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
            deviceInfo.queueCreateInfoCount = 1;
            deviceInfo.pQueueCreateInfos = &queueInfo;
            if (vkCreateDevice(mPhysical, &deviceInfo, nullptr, &mDevice) != VK_SUCCESS)
            {
                return false;
            }
            vkGetDeviceQueue(mDevice, mFamily, 0, &mQueue);
            return true;
        }

        Buffer makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
        {
            Buffer result;
            result.mSize = size;
            VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            info.size = size;
            info.usage = usage;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(mDevice, &info, nullptr, &result.mBuffer);
            VkMemoryRequirements requirements;
            vkGetBufferMemoryRequirements(mDevice, result.mBuffer, &requirements);
            const VkMemoryPropertyFlags wanted
                = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            uint32_t type = 0;
            for (uint32_t i = 0; i < mMemory.memoryTypeCount; ++i)
            {
                if ((requirements.memoryTypeBits & (1u << i)) != 0
                    && (mMemory.memoryTypes[i].propertyFlags & wanted) == wanted)
                {
                    type = i;
                    break;
                }
            }
            VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocate.allocationSize = requirements.size;
            allocate.memoryTypeIndex = type;
            vkAllocateMemory(mDevice, &allocate, nullptr, &result.mMemory);
            vkBindBufferMemory(mDevice, result.mBuffer, result.mMemory, 0);
            vkMapMemory(mDevice, result.mMemory, 0, size, 0, &result.mMapped);
            std::memset(result.mMapped, 0, static_cast<std::size_t>(size));
            return result;
        }

        void destroy(Buffer& buffer)
        {
            vkUnmapMemory(mDevice, buffer.mMemory);
            vkDestroyBuffer(mDevice, buffer.mBuffer, nullptr);
            vkFreeMemory(mDevice, buffer.mMemory, nullptr);
        }
    };

    // The clusters' lists after one assignment: the sorted light indices of each cluster (the first 50 of an
    // overflowing cluster) and the counts the shader wrote (which may exceed 50).
    struct GridResult
    {
        std::vector<uint32_t> mCounts;
        std::vector<std::vector<uint32_t>> mLists;
        // The entries after the 50th of an overflowing cluster, as the shader wrote them (its local array has 50).
        std::vector<std::vector<uint32_t>> mTails;
        uint32_t mIndexCount = 0;
    };

    VkDescriptorSetLayoutBinding layoutBinding(uint32_t index, VkDescriptorType type)
    {
        VkDescriptorSetLayoutBinding result{};
        result.binding = index;
        result.descriptorType = type;
        result.descriptorCount = 1;
        result.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        return result;
    }

    bool runLightGrid(GpuLightGrid& gpu, const std::vector<uint32_t>& spirv, const std::vector<float>& scene,
        const std::vector<float>& lights, GridResult& result, std::string& error)
    {
        constexpr uint32_t gridX = 16;
        constexpr uint32_t gridY = 8;
        constexpr uint32_t gridZ = 24;
        constexpr uint32_t clusters = gridX * gridY * gridZ;
        const uint32_t lightCount = static_cast<uint32_t>(lights.size() / 8);
        VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
        moduleInfo.pCode = spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(gpu.mDevice, &moduleInfo, nullptr, &module) != VK_SUCCESS)
        {
            error = "no shader module";
            return false;
        }
        auto sceneBuffer = gpu.makeBuffer(1024, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        auto sizeBuffer = gpu.makeBuffer(16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        auto lightBuffer
            = gpu.makeBuffer(std::max<std::size_t>(lights.size() * 4, 64), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const VkDeviceSize listBytes = static_cast<VkDeviceSize>(clusters) * (lightCount + 64) * 4;
        auto indexBuffer = gpu.makeBuffer(listBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto gridBuffer = gpu.makeBuffer(clusters * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto countBuffer = gpu.makeBuffer(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(sceneBuffer.mMapped, scene.data(), scene.size() * 4);
        const uint32_t size[4] = { gridX, gridY, gridZ, 0 };
        std::memcpy(sizeBuffer.mMapped, size, sizeof(size));
        std::memcpy(lightBuffer.mMapped, lights.data(), lights.size() * 4);
        std::memset(indexBuffer.mMapped, 0xFF, static_cast<std::size_t>(listBytes));

        // set 0: scene 0, lights 2, indices 3, grid 4, grid size 5; set 3: the index count at binding 1.
        const std::vector<VkDescriptorSetLayoutBinding> set0 = { layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
            layoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), layoutBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            layoutBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), layoutBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) };
        const std::vector<VkDescriptorSetLayoutBinding> none;
        const std::vector<VkDescriptorSetLayoutBinding> set3 = { layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) };
        std::array<VkDescriptorSetLayout, 4> layouts{};
        const std::array<const std::vector<VkDescriptorSetLayoutBinding>*, 4> all = { &set0, &none, &none, &set3 };
        for (std::size_t i = 0; i < 4; ++i)
        {
            VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            info.bindingCount = static_cast<uint32_t>(all[i]->size());
            info.pBindings = all[i]->data();
            vkCreateDescriptorSetLayout(gpu.mDevice, &info, nullptr, &layouts[i]);
        }
        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = 4;
        layoutInfo.pSetLayouts = layouts.data();
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        vkCreatePipelineLayout(gpu.mDevice, &layoutInfo, nullptr, &pipelineLayout);
        VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(gpu.mDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        {
            error = "no pipeline";
            return false;
        }
        const VkDescriptorPoolSize poolSizes[2]
            = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 } };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets = 4;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        vkCreateDescriptorPool(gpu.mDevice, &poolInfo, nullptr, &pool);
        std::array<VkDescriptorSet, 4> sets{};
        VkDescriptorSetAllocateInfo allocate{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocate.descriptorPool = pool;
        allocate.descriptorSetCount = 4;
        allocate.pSetLayouts = layouts.data();
        vkAllocateDescriptorSets(gpu.mDevice, &allocate, sets.data());
        const VkDescriptorBufferInfo infos[6]
            = { { sceneBuffer.mBuffer, 0, VK_WHOLE_SIZE }, { lightBuffer.mBuffer, 0, VK_WHOLE_SIZE },
                  { indexBuffer.mBuffer, 0, VK_WHOLE_SIZE }, { gridBuffer.mBuffer, 0, VK_WHOLE_SIZE },
                  { sizeBuffer.mBuffer, 0, VK_WHOLE_SIZE }, { countBuffer.mBuffer, 0, VK_WHOLE_SIZE } };
        const auto write
            = [&](VkDescriptorSet set, uint32_t index, VkDescriptorType type, const VkDescriptorBufferInfo& info) {
                  VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                  w.dstSet = set;
                  w.dstBinding = index;
                  w.descriptorCount = 1;
                  w.descriptorType = type;
                  w.pBufferInfo = &info;
                  vkUpdateDescriptorSets(gpu.mDevice, 1, &w, 0, nullptr);
              };
        write(sets[0], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, infos[0]);
        write(sets[0], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, infos[1]);
        write(sets[0], 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, infos[2]);
        write(sets[0], 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, infos[3]);
        write(sets[0], 5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, infos[4]);
        write(sets[3], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, infos[5]);

        VkCommandPoolCreateInfo commandPoolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        commandPoolInfo.queueFamilyIndex = gpu.mFamily;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        vkCreateCommandPool(gpu.mDevice, &commandPoolInfo, nullptr, &commandPool);
        VkCommandBufferAllocateInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandInfo.commandPool = commandPool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(gpu.mDevice, &commandInfo, &commands);
        VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(commands, &begin);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(
            commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 4, sets.data(), 0, nullptr);
        vkCmdDispatch(commands, gridX / 8, gridY / 4, gridZ / 4);
        vkEndCommandBuffer(commands);
        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands;
        vkQueueSubmit(gpu.mQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(gpu.mQueue);

        result.mCounts.assign(clusters, 0);
        result.mLists.assign(clusters, {});
        result.mTails.assign(clusters, {});
        result.mIndexCount = *static_cast<const uint32_t*>(countBuffer.mMapped);
        const auto* grid = static_cast<const uint32_t*>(gridBuffer.mMapped);
        const auto* indices = static_cast<const uint32_t*>(indexBuffer.mMapped);
        for (uint32_t i = 0; i < clusters; ++i)
        {
            const uint32_t offset = grid[i * 2];
            const uint32_t count = grid[i * 2 + 1];
            result.mCounts[i] = count;
            for (uint32_t k = 0; k < std::min<uint32_t>(count, 50); ++k)
            {
                result.mLists[i].push_back(indices[offset + k]);
            }
            std::sort(result.mLists[i].begin(), result.mLists[i].end());
            for (uint32_t k = 50; k < count && offset + k < listBytes / 4; ++k)
            {
                result.mTails[i].push_back(indices[offset + k]);
            }
        }

        vkDestroyCommandPool(gpu.mDevice, commandPool, nullptr);
        vkDestroyDescriptorPool(gpu.mDevice, pool, nullptr);
        vkDestroyPipeline(gpu.mDevice, pipeline, nullptr);
        vkDestroyPipelineLayout(gpu.mDevice, pipelineLayout, nullptr);
        for (const VkDescriptorSetLayout layout : layouts)
        {
            vkDestroyDescriptorSetLayout(gpu.mDevice, layout, nullptr);
        }
        vkDestroyShaderModule(gpu.mDevice, module, nullptr);
        for (auto* buffer : { &sceneBuffer, &sizeBuffer, &lightBuffer, &indexBuffer, &gridBuffer, &countBuffer })
        {
            gpu.destroy(*buffer);
        }
        return true;
    }

    TEST(SlangLightGridGpu, the_twin_assigns_the_lights_the_glsl_assigns)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc";
#else
        GpuLightGrid gpu;
        if (!gpu.create())
        {
            GTEST_SKIP() << "no Vulkan device with a compute queue";
        }
        const auto glslWords = compileGlsl("comp/light/assign.comp", "-");
        ASSERT_FALSE(glslWords.empty());
        std::vector<uint32_t> slangWords;
        for (const Manifest& manifest : builtManifests())
        {
            if (manifest.mStageFile == "comp/light/assign.comp")
            {
                for (const auto& [defines, file] : manifest.mFiles.mFiles)
                {
                    slangWords = readWords(manifest.mDirectory / file);
                }
            }
        }
        ASSERT_FALSE(slangWords.empty());

        // The scene block (std140, lib/view/data.glsl): projection at byte 64, zNear at 596 and zFar at 600.
        const vsg::dmat4 projection = vsg::perspective(vsg::radians(60.0), 16.0 / 9.0, 1.0, 7168.0);
        std::vector<float> scene(256, 0.0f);
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                scene[16 + c * 4 + r] = static_cast<float>(projection[c][r]);
            }
        }
        scene[149] = 1.0f;
        scene[150] = 7168.0f;

        uint32_t seed = 12345;
        const auto random = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
        };
        struct Scenario
        {
            const char* mName;
            uint32_t mRandom;
            uint32_t mBlob;
        };
        const Scenario scenarios[] = { { "scattered lights", 400, 0 }, { "dense blob (overflow)", 100, 70 } };
        for (const Scenario& scenario : scenarios)
        {
            SCOPED_TRACE(scenario.mName);
            std::vector<float> lights;
            const auto add = [&lights](float x, float y, float z, float radius) {
                lights.insert(lights.end(), { 1.0f, 0.9f, 0.7f, radius, x, y, z, 0.0f });
            };
            for (uint32_t i = 0; i < scenario.mRandom; ++i)
            {
                add((random() - 0.5f) * 3000.0f, (random() - 0.5f) * 1500.0f, -(2.0f + random() * 4000.0f),
                    40.0f + random() * 300.0f);
            }
            for (uint32_t i = 0; i < scenario.mBlob; ++i)
            {
                add(100.0f + random() * 10.0f, 50.0f + random() * 10.0f, -600.0f - random() * 10.0f, 90.0f);
            }
            lights[7] = static_cast<float>(lights.size() / 8);
            GridResult glsl;
            GridResult slang;
            std::string error;
            ASSERT_TRUE(runLightGrid(gpu, glslWords, scene, lights, glsl, error)) << error;
            ASSERT_TRUE(runLightGrid(gpu, slangWords, scene, lights, slang, error)) << error;
            std::size_t different = 0;
            std::size_t tailsDiffer = 0;
            std::size_t tailsOutOfRange = 0;
            std::size_t withLights = 0;
            std::size_t overflowing = 0;
            std::size_t totalGlsl = 0;
            std::size_t totalSlang = 0;
            for (std::size_t i = 0; i < glsl.mCounts.size(); ++i)
            {
                withLights += glsl.mCounts[i] != 0 ? 1 : 0;
                overflowing += glsl.mCounts[i] > 50 ? 1 : 0;
                totalGlsl += glsl.mCounts[i];
                totalSlang += slang.mCounts[i];
                tailsDiffer += glsl.mTails[i] != slang.mTails[i] ? 1 : 0;
                for (const uint32_t entry : glsl.mTails[i])
                {
                    tailsOutOfRange += entry >= lights.size() / 8 ? 1 : 0;
                }
                if (glsl.mCounts[i] != slang.mCounts[i] || glsl.mLists[i] != slang.mLists[i])
                {
                    ++different;
                }
            }
            std::cout << "[light grid] " << scenario.mName << ": " << glsl.mCounts.size() << " clusters, " << withLights
                      << " with lights, " << overflowing << " over 50, light entries GLSL " << totalGlsl << " Slang "
                      << totalSlang << ", clusters that differ " << different << ", index counts GLSL "
                      << glsl.mIndexCount << " Slang " << slang.mIndexCount << std::endl;
            std::map<uint32_t, uint32_t> histogram;
            for (const uint32_t count : glsl.mCounts)
            {
                ++histogram[count];
            }
            std::cout << "[light grid]   lights " << lights.size() / 8 << ", clusters by light count:";
            for (const auto& [count, clustersWith] : histogram)
            {
                std::cout << " " << count << "x" << clustersWith;
            }
            std::cout << std::endl;
            for (const uint32_t index : glsl.mLists[0])
            {
                std::cout << "[light grid]   cluster 0 holds light " << index << " z " << lights[index * 8 + 6]
                          << " radius " << lights[index * 8 + 3] << std::endl;
            }
            std::cout << "[light grid]   past the 50th entry (written from a 50 element local array, undefined): "
                      << tailsDiffer
                      << " clusters differ, GLSL tail entries that are no light index: " << tailsOutOfRange
                      << std::endl;
            EXPECT_GT(withLights, 0u);
            EXPECT_EQ(different, 0u);
            EXPECT_EQ(glsl.mIndexCount, slang.mIndexCount);
        }
#endif
    }
}
