#include "shaderpermutation.hpp"

#include "slangruntime.hpp"
#include "spirvinterface.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <vsg/core/observer_ptr.h>
#include <vsg/io/FileSystem.h>
#include <vsg/io/Logger.h>
#include <vsg/io/read.h>
#include <vsg/utils/ShaderCompiler.h>

namespace vsgUtil
{
    // Task 0072 (review of stage f, finding 5): the specialization constant ids a twin module declares (by SpecId),
    // kept on the module by the reader below. In a named namespace: a type in an anonymous one is a type of its own in
    // each library (root CLAUDE.md, pitfalls).
    class DeclaredSpecConstants : public vsg::Inherit<vsg::Object, DeclaredSpecConstants>
    {
    public:
        std::set<uint32_t> ids;
    };

    namespace
    {
        // Limits of a ".permutations" text: far above any family's real count, small enough that a damaged file
        // cannot make the reader allocate much.
        constexpr std::size_t maxManifestLines = 4096;
        constexpr std::size_t maxManifestBytes = 64 * 1024;
        constexpr std::size_t maxDefineBytes = 64;

        bool isIdentifier(std::string_view name)
        {
            if (name.empty() || name.size() > maxDefineBytes)
                return false;
            for (std::size_t i = 0; i < name.size(); ++i)
            {
                const char c = name[i];
                const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
                const bool digit = c >= '0' && c <= '9';
                if (!letter && !(digit && i > 0))
                    return false;
            }
            return true;
        }

        // "<stem>.<vert|frag|comp>.spv", "<stem>.<vert|frag|comp>.p<4 digits>.spv" (stage a: by position) or
        // "<stem>.<vert|frag|comp>.p<8 lower-case hex digits>.spv" (stage b: by a hash of the define set, so a file
        // left over from an older list can never be taken for another set), the stem being lower-case letters and
        // digits (the shader files' own names: main, gui).
        bool isPermutationFileName(std::string_view name)
        {
            std::size_t stemEnd = 0;
            while (stemEnd < name.size()
                && ((name[stemEnd] >= 'a' && name[stemEnd] <= 'z') || (name[stemEnd] >= '0' && name[stemEnd] <= '9')))
                ++stemEnd;
            if (stemEnd == 0 || stemEnd > 32)
                return false;
            for (const char* stage : { ".vert", ".frag", ".comp" })
            {
                const std::string_view suffix(stage);
                if (name.substr(stemEnd, suffix.size()) != suffix)
                    continue;
                const std::string_view rest = name.substr(stemEnd + suffix.size());
                if (rest == ".spv")
                    return true;
                if (rest.size() == 10 && rest.substr(0, 2) == ".p" && rest.substr(6) == ".spv")
                {
                    for (char c : rest.substr(2, 4))
                        if (c < '0' || c > '9')
                            return false;
                    return true;
                }
                if (rest.size() == 14 && rest.substr(0, 2) == ".p" && rest.substr(10) == ".spv")
                {
                    for (char c : rest.substr(2, 8))
                        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                            return false;
                    return true;
                }
            }
            return false;
        }

        // A define list is canonical when splitting it at single spaces gives identifiers in strictly increasing byte
        // order (so no duplicates, no empty names, no leading, trailing or double spaces).
        bool isCanonicalList(std::string_view list)
        {
            if (list.empty())
                return true;
            std::string_view previous;
            std::size_t start = 0;
            while (true)
            {
                const std::size_t space = list.find(' ', start);
                const std::string_view name
                    = list.substr(start, space == std::string_view::npos ? std::string_view::npos : space - start);
                if (!isIdentifier(name))
                    return false;
                if (!previous.empty() && !(previous < name))
                    return false;
                previous = name;
                if (space == std::string_view::npos)
                    return true;
                start = space + 1;
            }
        }

        // Task 0072 (review of stage f, finding 10): what the cache knows of one (search paths, stage file, set). It
        // holds no module: a module's compile gives it Vulkan objects that hold the device, and a namespace-scope
        // static must not keep those past the engine's teardown (apps/openmw/mwrender/CLAUDE.md). The shader options'
        // SharedObjects and the pipelines hold the modules; once none does, the module goes and is read again.
        struct ModuleEntry
        {
            // A file is listed for the set and was read once; false: unlisted or unreadable, not tried again.
            bool mListed = false;
            // Stage h: the manifest does not list the set, and it is compiled at run time (or read from the cache).
            bool mRuntime = false;
            // Stage h, review round 4 (finding 3): the run-time path refused or failed this set; the GLSL draws it, and
            // later pipelines skip the file reads and the GLSL hash that reaching the runtime's own memory costs.
            bool mRuntimeFailed = false;
            std::string mFile;
            vsg::observer_ptr<vsg::ShaderModule> mModule;
        };

        struct ManifestCache
        {
            std::mutex mMutex;
            // (the options' search paths, path of the stage file) -> manifest, or nullopt when there is none or it
            // could not be read. Keyed by the paths and not by the options object, whose address can be reused.
            std::map<std::pair<std::string, std::string>, std::optional<PermutationManifest>> mManifests;
            // (search paths, stage file, canonical define set) -> what was found for it: each is looked up, and
            // logged, once.
            std::map<std::tuple<std::string, std::string, std::string>, ModuleEntry> mModules;
        };

        std::string searchPathsKey(const vsg::Options* options)
        {
            std::string key;
            if (options != nullptr)
                for (const vsg::Path& path : options->paths)
                    key += path.string() + '\n';
            return key;
        }

        ManifestCache& manifestCache()
        {
            static ManifestCache cache;
            return cache;
        }

        // Task 0072 (review of stage f, finding 5): the specialization constant ids a twin declares, read from its
        // SPIR-V once when the twin is read and kept on the module, so that addSettingsConstants does not parse the
        // whole module again for every stage of every pipeline.
        constexpr const char* declaredSpecConstantsKey = "openeden.declaredSpecConstants";

        // Reads a listed twin file; null when it cannot be read. Called with the cache's lock held.
        vsg::ref_ptr<vsg::ShaderModule> readTwin(
            const std::string& file, const vsg::ref_ptr<const vsg::Options>& options)
        {
            auto module = vsg::read_cast<vsg::ShaderModule>(file, options);
            if (!module || module->code.empty())
                return {};
            // The options' SharedObjects may give a module read before, which has the ids already.
            if (module->getObject<DeclaredSpecConstants>(declaredSpecConstantsKey) == nullptr)
            {
                try
                {
                    auto declared = DeclaredSpecConstants::create();
                    for (const auto& [id, name] : readSpirvSpecConstantNames(module->code))
                        declared->ids.insert(id);
                    module->setObject(declaredSpecConstantsKey, declared);
                }
                catch (const std::runtime_error&)
                {
                    // Left without: addSettingsConstants tries to read it and gives up, as for any unreadable module.
                }
            }
            return module;
        }

        std::optional<PermutationManifest> loadManifest(
            const std::string& path, const vsg::ref_ptr<const vsg::Options>& options)
        {
            const vsg::Path found = vsg::findFile(path + ".permutations", options.get());
            if (found.empty())
                return std::nullopt;
            std::ifstream in(found.string(), std::ios::binary);
            if (!in)
                return std::nullopt;
            std::string text(maxManifestBytes + 1, '\0');
            in.read(text.data(), static_cast<std::streamsize>(text.size()));
            text.resize(static_cast<std::size_t>(in.gcount()));
            try
            {
                return parsePermutationManifest(text);
            }
            catch (const std::exception& e)
            {
                vsg::warn("Slang permutations of \"", path, "\" are not used: ", e.what());
                return std::nullopt;
            }
        }

        // Task 0072, stage f: the texture coordinate count of an object shader twin, "UVSETS_" and decimal digits.
        constexpr std::string_view uvSetsPrefix = "UVSETS_";

        bool isUvSetsDefine(std::string_view name)
        {
            if (name.size() <= uvSetsPrefix.size() || name.substr(0, uvSetsPrefix.size()) != uvSetsPrefix)
                return false;
            for (const char c : name.substr(uvSetsPrefix.size()))
            {
                if (c < '0' || c > '9')
                    return false;
            }
            return true;
        }

        // True when the manifest of `path` (read once, through the cache findSlangPermutation uses) counts texture
        // coordinate sets; false when there is none.
        bool familyCountsUvSets(const std::string& path, const vsg::ref_ptr<const vsg::Options>& options)
        {
            const std::string paths = searchPathsKey(options.get());
            ManifestCache& cache = manifestCache();
            std::lock_guard lock(cache.mMutex);
            auto it = cache.mManifests.find({ paths, path });
            if (it == cache.mManifests.end())
                it = cache.mManifests.emplace(std::make_pair(paths, path), loadManifest(path, options)).first;
            return it->second && countsUvSets(*it->second);
        }

        struct Trace
        {
            std::mutex mMutex;
            std::set<std::string> mSeen;
            std::string mFile;
            bool mTiming = false;
            Trace()
            {
                if (const char* file = std::getenv("OPENEDEN_SHADER_TRACE"); file != nullptr && file[0] != '\0')
                    mFile = file;
                if (const char* timing = std::getenv("OPENEDEN_SHADER_TRACE_TIMING"))
                    mTiming = std::string_view(timing) == "1";
            }
        };

        Trace& trace()
        {
            static Trace instance;
            return instance;
        }
    }

    std::string canonicalDefines(const std::set<std::string>& defines)
    {
        std::string out;
        for (const std::string& name : defines) // std::set<std::string> is already in byte order
        {
            if (!isIdentifier(name))
                throw std::invalid_argument("not a shader define name: \"" + name + "\"");
            if (!out.empty())
                out += ' ';
            out += name;
        }
        return out;
    }

    PermutationManifest parsePermutationManifest(std::string_view text)
    {
        if (text.size() > maxManifestBytes)
            throw std::runtime_error("permutations file larger than 64 KiB");
        PermutationManifest manifest;
        std::set<std::string> fileNames;
        std::size_t lineNumber = 0;
        std::size_t start = 0;
        while (start < text.size())
        {
            ++lineNumber;
            if (lineNumber > maxManifestLines)
                throw std::runtime_error("permutations file has more than 4096 lines");
            std::size_t end = text.find('\n', start);
            std::string_view line
                = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            start = end == std::string_view::npos ? text.size() : end + 1;
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            const auto fail = [&](const std::string& what) {
                throw std::runtime_error("permutations file, line " + std::to_string(lineNumber) + ": " + what);
            };
            const std::size_t tab = line.find('\t');
            if (tab == std::string_view::npos)
                fail("no tab between the file and the defines");
            const std::string_view file = line.substr(0, tab);
            const std::string_view defines = line.substr(tab + 1);
            if (!isPermutationFileName(file))
                fail("\"" + std::string(file) + "\" is not <stem>.<vert|frag|comp>[.p<4 digits or 8 hex digits>].spv");
            if (!isCanonicalList(defines))
                fail("the define list is not canonical (sorted identifiers, one space between)");
            if (!fileNames.insert(std::string(file)).second)
                fail("the file \"" + std::string(file) + "\" is listed twice");
            if (!manifest.mFiles.emplace(std::string(defines), std::string(file)).second)
                fail("the define list \"" + std::string(defines) + "\" is listed twice");
        }
        return manifest;
    }

    std::optional<std::string> selectPermutation(
        const PermutationManifest& manifest, const std::set<std::string>& defines)
    {
        std::string key;
        try
        {
            key = canonicalDefines(defines);
        }
        catch (const std::invalid_argument&)
        {
            return std::nullopt;
        }
        const auto it = manifest.mFiles.find(key);
        if (it == manifest.mFiles.end())
            return std::nullopt;
        return it->second;
    }

    std::set<std::string> importedDefines(std::string_view source)
    {
        // As vsg::ShaderCompiler::combineSourceAndDefines: leading spaces and tabs, trailing spaces, tabs and line
        // ends are ignored; the line must then start with the pragma.
        const auto trim = [](std::string_view text) {
            const std::size_t begin = text.find_first_not_of(" \t");
            if (begin == std::string_view::npos)
                return std::string_view();
            const std::size_t end = text.find_last_not_of(" \t\r\n");
            return text.substr(begin, end - begin + 1);
        };
        constexpr std::string_view pragma = "#pragma import_defines";
        std::set<std::string> names;
        std::size_t start = 0;
        while (start < source.size())
        {
            std::size_t end = source.find('\n', start);
            if (end == std::string_view::npos)
                end = source.size();
            const std::string_view line = trim(source.substr(start, end - start));
            start = end + 1;
            if (line.substr(0, pragma.size()) != pragma)
                continue;
            const std::size_t open = line.find('(');
            if (open == std::string_view::npos)
                continue;
            const std::size_t close = line.find(')', open);
            if (close == std::string_view::npos)
                continue;
            std::string_view list = line.substr(open + 1, close - open - 1);
            while (true)
            {
                const std::size_t comma = list.find(',');
                const std::string_view name = trim(list.substr(0, comma));
                if (!name.empty())
                    names.emplace(name);
                if (comma == std::string_view::npos)
                    break;
                list.remove_prefix(comma + 1);
            }
        }
        return names;
    }

    std::set<std::string> effectiveDefines(const std::set<std::string>& requested, std::string_view source)
    {
        if (requested.empty())
            return {};
        const std::set<std::string> imported = importedDefines(source);
        std::set<std::string> result;
        for (const std::string& name : requested)
        {
            if (imported.count(name) != 0)
                result.insert(name);
        }
        return result;
    }

    void addSettingsConstants(vsg::ShaderStage& stage, const vsg::Options* options)
    {
        if (options == nullptr || !stage.module || stage.module->code.empty() || !stage.module->source.empty())
            return;
        const auto* settings = options->getObject<SettingsConstants>(settingsConstantsKey);
        if (settings == nullptr || settings->constants.empty())
            return;
        // Task 0072 (review of stages b and c): only the ids the module declares, so that a stage without them is
        // left exactly as it was (with the switch off, a Slang-only shader gets nothing). Review of stage f: a twin
        // read through findSlangPermutation carries its ids; any other module is read here.
        std::set<uint32_t> parsed;
        const std::set<uint32_t>* declared = nullptr;
        if (const auto* known = stage.module->getObject<DeclaredSpecConstants>(declaredSpecConstantsKey))
            declared = &known->ids;
        else
        {
            try
            {
                for (const auto& [id, name] : readSpirvSpecConstantNames(stage.module->code))
                    parsed.insert(id);
            }
            catch (const std::runtime_error&)
            {
                return;
            }
            declared = &parsed;
        }
        for (const auto& [id, value] : settings->constants)
        {
            if (declared->count(id) != 0)
                stage.specializationConstants.emplace(id, value);
        }
    }

    std::set<std::string> includedSettingsDefines(std::string_view source, const std::set<std::string>& settingsDefines)
    {
        std::set<std::string> result;
        for (const std::string& name : settingsDefines)
        {
            // The settings files write exactly "#define <name> 1" on a line of its own.
            const std::string line = "#define " + name + " 1";
            for (std::size_t at = source.find(line); at != std::string_view::npos; at = source.find(line, at + 1))
            {
                const bool startsLine = at == 0 || source[at - 1] == '\n';
                const std::size_t end = at + line.size();
                const bool endsLine = end == source.size() || source[end] == '\n' || source[end] == '\r';
                if (startsLine && endsLine)
                {
                    result.insert(name);
                    break;
                }
            }
        }
        return result;
    }

    std::set<std::string> stageDefines(
        const std::set<std::string>& requested, std::string_view source, const vsg::Options* options)
    {
        std::set<std::string> result = effectiveDefines(requested, source);
        if (options != nullptr)
        {
            if (const auto* settings = options->getObject<SettingsConstants>(settingsConstantsKey))
            {
                const std::set<std::string> fromSettings = includedSettingsDefines(source, settings->defines);
                result.insert(fromSettings.begin(), fromSettings.end());
            }
        }
        return result;
    }

    bool prefersSlang(const vsg::Options* options)
    {
        bool value = false;
        return options != nullptr && options->getValue(preferSlangKey, value) && value;
    }

    namespace
    {
        // A twin module made from SPIR-V words (a run-time compile), with the specialization constant ids it declares
        // kept on it as readTwin does for a file.
        vsg::ref_ptr<vsg::ShaderModule> makeTwinModule(std::vector<uint32_t> words)
        {
            auto module = vsg::ShaderModule::create(std::move(words));
            try
            {
                auto declared = DeclaredSpecConstants::create();
                for (const auto& [id, name] : readSpirvSpecConstantNames(module->code))
                    declared->ids.insert(id);
                module->setObject(declaredSpecConstantsKey, declared);
            }
            catch (const std::runtime_error&)
            {
                // As readTwin: left without; addSettingsConstants gives up on it.
            }
            return module;
        }

        // "<stage file>.slang-stage" through the options' paths; the shader folder is where it was found.
        bool readStageInfo(const std::string& path, const vsg::ref_ptr<const vsg::Options>& options,
            SlangStageInfo& info, std::filesystem::path& root)
        {
            const std::string name = path + ".slang-stage";
            const vsg::Path found = vsg::findFile(name, options.get());
            if (found.empty())
                return false;
            std::ifstream in(found.string(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            try
            {
                info = parseSlangStageInfo(text);
            }
            catch (const std::exception&)
            {
                return false;
            }
            const std::string whole = found.string();
            if (whole.size() <= name.size())
                return false;
            root = std::filesystem::path(whole.substr(0, whole.size() - name.size()));
            return true;
        }

        // A twin compiled at run time for a set the manifest does not list, or null: the GLSL draws it.
        vsg::ref_ptr<vsg::ShaderModule> runtimeTwin(const std::string& path, const std::set<std::string>& defines,
            const vsg::ref_ptr<const vsg::Options>& options, const SlangGlslStage& glsl)
        {
            auto* runtime = options ? options->getObject<SlangRuntime>(slangRuntimeKey) : nullptr;
            if (runtime == nullptr || runtime->disabled())
                return {};
            SlangRuntimeRequest request;
            request.mStageFile = path;
            request.mDefines = defines;
            if (!readStageInfo(path, options, request.mInfo, request.mShaderRoot))
                return {};
            SlangRuntimeCheck check;
            // The cache entry is valid for exactly this GLSL: its text and the names it was compiled with.
            std::uint64_t hash = 1469598103934665603ull;
            const auto mix = [&hash](std::string_view text) {
                for (const char ch : text)
                {
                    hash ^= static_cast<unsigned char>(ch);
                    hash *= 1099511628211ull;
                }
            };
            mix(glsl.mSource);
            if (glsl.mRequested != nullptr)
            {
                for (const std::string& name : *glsl.mRequested)
                    mix(" " + name);
            }
            check.mKey = std::to_string(hash) + ":" + std::to_string(glsl.mSource.size());
            check.mCheck = [&path, &defines, &glsl, &options](const std::vector<uint32_t>& words) {
                return checkTwinAgainstGlsl(path, defines, glsl, words, options);
            };
            // A cache hit was checked in full when it was made; it is read back for the structure only.
            check.mCheckHit = checkSlangTwinStructure;
            auto words = runtime->obtain(request, check);
            if (words.empty())
                return {};
            return makeTwinModule(std::move(words));
        }
    }

    void createSlangRuntime(const vsg::ref_ptr<vsg::Options>& options, const std::filesystem::path& shaderRoot,
        const std::filesystem::path& libraryDirectory, const std::filesystem::path& cacheDirectory,
        std::function<void(bool warning, const std::string&)> log, bool preload,
        std::function<void(const SlangCompileEvent&)> eventSink)
    {
        std::error_code absoluteError;
        std::filesystem::path root = std::filesystem::absolute(shaderRoot, absoluteError);
        if (absoluteError)
            root = shaderRoot;
        auto runtime = SlangRuntime::create(createDynamicSlangCompiler(libraryDirectory), cacheDirectory);
        runtime->setLog(std::move(log));
        if (eventSink)
            runtime->setEventSink(std::move(eventSink));
        // The self-tests: the library must reproduce the build's permutations byte for byte. Read through the
        // manifest like any twin; without them (no permutations built) the runtime has nothing to be checked by, and
        // stays off. The small test is the omwfx vertex stage; the large one is the object shader's fragment stage
        // with the most defines (review of stage h, finding 5: a library without its optimiser compiles a small
        // stage the same bytes and a large one other, smaller code: 79,560 against 89,956 bytes were measured).
        const auto addTest = [&](const std::string& stage, const std::set<std::string>& defines) {
            SlangRuntimeRequest request;
            request.mStageFile = stage;
            request.mDefines = defines;
            request.mShaderRoot = root;
            std::filesystem::path found;
            const auto shipped = findSlangPermutation(stage, defines, options);
            if (!shipped || shipped->code.empty() || !readStageInfo(stage, options, request.mInfo, found))
                return false;
            runtime->addSelfTest(std::move(request), shipped->code);
            return true;
        };
        if (!addTest("post/omwfx.vert", {}))
            return;
        {
            const std::string large = "default/main.frag";
            const auto manifest = loadManifest(large, options);
            std::set<std::string> best;
            if (manifest)
            {
                for (const auto& entry : manifest->mFiles)
                {
                    std::set<std::string> names;
                    std::istringstream in(entry.first == "-" ? std::string() : entry.first);
                    for (std::string name; in >> name;)
                        names.insert(name);
                    if (names.size() > best.size())
                        best = std::move(names);
                }
                if (!best.empty())
                    addTest(large, best);
            }
        }
        options->setObject(slangRuntimeKey, runtime);
        if (preload)
            runtime->startPreload();
    }

    std::string checkSlangTwinStructure(const std::vector<uint32_t>& words)
    {
        try
        {
            const SpirvStructure structure = readSpirvStructure(words);
            if (structure.mVersion != 0x00010000u)
                return "the entry is not SPIR-V 1.0";
            if (structure.mSpecConstantOps != 0)
                return "the entry has an expression over specialization constants (OpSpecConstantOp)";
            return {};
        }
        catch (const std::exception& e)
        {
            return std::string("the entry cannot be read: ") + e.what();
        }
    }

    std::string checkTwinAgainstGlsl(const std::string& path, const std::set<std::string>& set,
        const SlangGlslStage& glsl, const std::vector<uint32_t>& twinWords,
        const vsg::ref_ptr<const vsg::Options>& options)
    {
        const auto endsWith = [&path](std::string_view suffix) {
            return path.size() >= suffix.size()
                && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        VkShaderStageFlagBits kind = VK_SHADER_STAGE_VERTEX_BIT;
        if (endsWith(".frag"))
            kind = VK_SHADER_STAGE_FRAGMENT_BIT;
        else if (endsWith(".comp"))
            kind = VK_SHADER_STAGE_COMPUTE_BIT;
        else if (!endsWith(".vert"))
            return "the stage file \"" + path + "\" is not .vert, .frag or .comp";
        try
        {
            const SpirvInterface slang = readSpirvInterface(twinWords);
            if (slang.mVersion != 0x00010000u)
                return "the twin is not SPIR-V 1.0";
            if (slang.mSpecConstantOps != 0)
                return "the twin has an expression over specialization constants (OpSpecConstantOp)";
            std::map<uint32_t, uint32_t> pinned;
            for (const std::string& name : set)
            {
                if (isUvSetsDefine(name))
                    pinned[0] = static_cast<uint32_t>(std::stoul(name.substr(uvSetsPrefix.size())));
            }
            auto stage = vsg::ShaderStage::create(kind, "main", std::string(glsl.mSource));
            std::vector<std::string> requested;
            if (glsl.mRequested != nullptr)
                requested.assign(glsl.mRequested->begin(), glsl.mRequested->end());
            if (!vsg::ShaderCompiler::create()->compile(stage, requested, options) || stage->module->code.empty())
                return "the GLSL stage does not compile, so there is nothing to compare the twin with";
            const SpirvInterface reference = readSpirvInterface(stage->module->code, pinned);
            const SpirvInterfaceDiff diff = compareSpirvInterfaces(slang, reference);
            for (const std::string& line : diff.mOnlyInFirst)
            {
                // The twin's settings constants exist in the twin only (ids from 1000; the GLSL has them as plain
                // constants).
                const std::size_t id = line.find("id=");
                bool settings = false;
                if (id != std::string::npos
                    && (line.rfind("spec_constant ", 0) == 0 || line.rfind("spec_constant_default ", 0) == 0))
                    settings = std::strtoul(line.c_str() + id + 3, nullptr, 10) >= 1000;
                if (!settings)
                    return "the twin declares what the GLSL does not: " + line;
            }
            return {};
        }
        catch (const std::exception& e)
        {
            return std::string("the twin cannot be read: ") + e.what();
        }
    }

    vsg::ref_ptr<vsg::ShaderModule> findSlangPermutation(const std::string& path, const std::set<std::string>& defines,
        const vsg::ref_ptr<const vsg::Options>& options, const SlangGlslStage* glsl)
    {
        std::string key;
        try
        {
            key = canonicalDefines(defines);
        }
        catch (const std::invalid_argument&)
        {
            return {};
        }
        const std::string paths = searchPathsKey(options.get());
        ManifestCache& cache = manifestCache();
        const auto moduleKey = std::make_tuple(paths, path, key);
        bool compileAtRun = false;
        {
            // Held while a file is read too: a module is read once per (paths, stage, set), and pipelines are made
            // rarely. Not held while a set is compiled at run time (100 to 250 ms).
            std::lock_guard lock(cache.mMutex);
            if (const auto known = cache.mModules.find(moduleKey); known != cache.mModules.end())
            {
                ModuleEntry& entry = known->second;
                if (entry.mRuntime)
                {
                    if (auto module = entry.mModule.ref_ptr())
                        return module;
                    if (entry.mRuntimeFailed)
                        return {};
                    compileAtRun = glsl != nullptr;
                }
                else
                {
                    if (!entry.mListed)
                        return {};
                    if (auto module = entry.mModule.ref_ptr())
                        return module;
                    // Review of stage f, finding 10: nothing held it any more; read the file again.
                    auto module = readTwin(entry.mFile, options);
                    if (!module)
                    {
                        vsg::warn(
                            "Slang permutation \"", entry.mFile, "\" is listed but cannot be read; the GLSL is used");
                        entry.mListed = false;
                        return {};
                    }
                    entry.mModule = module;
                    return module;
                }
            }
            else
            {
                ModuleEntry& entry = cache.mModules[moduleKey];
                auto it = cache.mManifests.find({ paths, path });
                if (it == cache.mManifests.end())
                    it = cache.mManifests.emplace(std::make_pair(paths, path), loadManifest(path, options)).first;
                if (!it->second)
                    return {};
                const std::optional<std::string> file = selectPermutation(*it->second, defines);
                if (!file)
                {
                    // Task 0072, stage b (review): a family is ported but this set was never listed; the GLSL draws it
                    // or, since stage h, the set is compiled at run time.
                    if (options == nullptr || options->getObject<SlangRuntime>(slangRuntimeKey) == nullptr)
                    {
                        vsg::info(
                            "Slang permutations of \"", path, "\" have no file for [", key, "]; the GLSL is used");
                        return {};
                    }
                    // Review round 4, finding 12: a caller with no GLSL stage to check against (a trace) does not make
                    // the set unlisted for the runtime; a later call that has one compiles it.
                    entry.mRuntime = true;
                    if (glsl == nullptr)
                        return {};
                    compileAtRun = true;
                }
                else
                {
                    const std::size_t slash = path.rfind('/');
                    const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
                    auto module = readTwin(dir + *file, options);
                    if (!module)
                    {
                        vsg::warn(
                            "Slang permutation \"", dir, *file, "\" is listed but cannot be read; the GLSL is used");
                        return {};
                    }
                    entry.mListed = true;
                    entry.mFile = dir + *file;
                    entry.mModule = module;
                    return module;
                }
            }
        }
        if (!compileAtRun)
            return {};
        auto module = runtimeTwin(path, defines, options, *glsl);
        std::lock_guard lock(cache.mMutex);
        ModuleEntry& entry = cache.mModules[moduleKey];
        if (!module)
        {
            // Refused, failed or disabled: for the rest of the session (SlangRuntime remembers it as well).
            entry.mRuntimeFailed = true;
            return {};
        }
        // Another thread may have made the module meanwhile: one module per set, shared by every pipeline.
        if (auto existing = entry.mModule.ref_ptr())
            return existing;
        entry.mRuntime = true;
        entry.mModule = module;
        return module;
    }

    std::string uvSetsDefine(uint32_t count)
    {
        return std::string(uvSetsPrefix) + std::to_string(count);
    }

    bool declaresUvSetArrays(const std::set<std::string>& defines)
    {
        return defines.count("TEXCOORD") != 0 || defines.count("PARTICLE") != 0;
    }

    bool countsUvSets(const PermutationManifest& manifest)
    {
        for (const auto& [defines, file] : manifest.mFiles)
        {
            std::istringstream names(defines);
            for (std::string name; names >> name;)
            {
                if (isUvSetsDefine(name))
                    return true;
            }
        }
        return false;
    }

    std::optional<std::set<std::string>> slangSelectionSet(const std::string& path,
        const std::set<std::string>& requested, std::string_view glslSource,
        const vsg::ref_ptr<const vsg::Options>& options, std::optional<uint32_t> numUvSets)
    {
        std::set<std::string> defines = stageDefines(requested, glslSource, options.get());
        const bool byPipeline = declaresUvSetArrays(defines) && familyCountsUvSets(path, options);
        if (!numUvSets)
            return byPipeline ? std::nullopt : std::optional(std::move(defines));
        if (!byPipeline)
            return std::nullopt;
        defines.insert(uvSetsDefine(*numUvSets));
        return defines;
    }

    vsg::ref_ptr<vsg::ShaderModule> slangTwin(const std::string& path, const std::set<std::string>& requested,
        std::string_view glslSource, bool preferSlang, const vsg::ref_ptr<const vsg::Options>& options,
        std::optional<uint32_t> numUvSets)
    {
        if (!preferSlang)
            return {};
        const auto defines = slangSelectionSet(path, requested, glslSource, options, numUvSets);
        if (!defines)
            return {};
        const SlangGlslStage glsl{ glslSource, &requested };
        return findSlangPermutation(path, *defines, options, &glsl);
    }

    bool takeSlangTwinsTogether(vsg::ShaderStages& stages, const std::vector<vsg::ref_ptr<vsg::ShaderModule>>& twins)
    {
        if (stages.size() != twins.size())
            return false;
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            const bool glsl = stages[i] && stages[i]->module && !stages[i]->module->source.empty();
            if (glsl && !twins[i])
                return false;
        }
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            if (twins[i] && stages[i])
                stages[i]->module = twins[i];
        }
        return true;
    }

    vsg::ref_ptr<SlangStageChoice> attachSlangStageChoice(
        vsg::ShaderStage& stage, const std::string& path, const vsg::ref_ptr<const vsg::Options>& options)
    {
        if (!stage.module || stage.module->source.empty())
            return {};
        static const std::set<std::string> noDefines;
        auto choice = SlangStageChoice::create();
        choice->path = path;
        choice->glslHints = stage.module->hints;
        choice->glslSource = stage.module->source;
        const std::set<std::string>& requested = stage.module->hints ? stage.module->hints->defines : noDefines;
        // The same set and rule as slangSelectionSet, worked out once.
        choice->defines = stageDefines(requested, stage.module->source, options.get());
        choice->byPipeline = declaresUvSetArrays(choice->defines) && familyCountsUvSets(path, options);
        if (!choice->byPipeline)
        {
            const SlangGlslStage glsl{ choice->glslSource, &requested };
            choice->twin = findSlangPermutation(path, choice->defines, options, &glsl);
        }
        stage.setObject(slangStageChoiceKey, choice);
        return choice;
    }

    const SlangStageChoice* slangStageChoiceOf(const vsg::ShaderStage& stage)
    {
        return stage.getObject<SlangStageChoice>(slangStageChoiceKey);
    }

    vsg::ref_ptr<vsg::ShaderModule> slangTwinFor(
        const SlangStageChoice& choice, uint32_t numUvSets, const vsg::ref_ptr<const vsg::Options>& options)
    {
        if (!choice.byPipeline)
            return choice.twin;
        std::set<std::string> defines = choice.defines;
        defines.insert(uvSetsDefine(numUvSets));
        static const std::set<std::string> noDefines;
        const SlangGlslStage glsl{ choice.glslSource, choice.glslHints ? &choice.glslHints->defines : &noDefines };
        return findSlangPermutation(choice.path, defines, options, &glsl);
    }

    const std::set<std::string>* glslDefinesOf(const vsg::ShaderStage& stage)
    {
        if (stage.module && stage.module->hints)
            return &stage.module->hints->defines;
        if (const SlangStageChoice* choice = slangStageChoiceOf(stage); choice != nullptr && choice->glslHints)
            return &choice->glslHints->defines;
        return nullptr;
    }

    vsg::ShaderStages withSlangTwinsTogether(const vsg::ShaderStages& stages, const std::vector<std::string>& paths,
        const vsg::ref_ptr<const vsg::Options>& options)
    {
        if (stages.size() != paths.size())
            return stages;
        for (std::size_t i = 0; i < stages.size(); ++i)
            recordShaderUse(paths[i], {}, stages[i].get(), options);
        if (!prefersSlang(options.get()))
            return stages;
        std::vector<vsg::ref_ptr<vsg::ShaderModule>> twins;
        vsg::ShaderStages copies;
        bool anyTwin = false;
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            if (!stages[i] || !stages[i]->module)
                return stages;
            twins.push_back(slangTwin(paths[i], {}, stages[i]->module->source, true, options));
            anyTwin = anyTwin || twins.back();
            copies.push_back(vsg::ShaderStage::create(*stages[i]));
        }
        if (!anyTwin || !takeSlangTwinsTogether(copies, twins))
            return stages;
        for (const auto& copy : copies)
            addSettingsConstants(*copy, options.get());
        return copies;
    }

    vsg::ref_ptr<vsg::ShaderStage> withSlangTwin(const vsg::ref_ptr<vsg::ShaderStage>& stage, const std::string& path,
        const vsg::ref_ptr<const vsg::Options>& options)
    {
        if (!stage || !stage->module)
            return stage;
        recordShaderUse(path, {}, stage.get(), options);
        auto module = slangTwin(path, {}, stage->module->source, prefersSlang(options.get()), options);
        if (!module)
            return stage;
        auto copy = vsg::ShaderStage::create(*stage);
        copy->module = module;
        addSettingsConstants(*copy, options.get());
        return copy;
    }

    std::string traceLine(std::string_view path, const std::set<std::string>& defines)
    {
        return std::string(path) + "\t" + canonicalDefines(defines);
    }

    void recordShaderUse(std::string_view path, const std::set<std::string>& defines, const vsg::ShaderStage* stage,
        const vsg::ref_ptr<const vsg::Options>& options, std::optional<uint32_t> numUvSets)
    {
        Trace& t = trace();
        if (t.mFile.empty())
            return;
        std::string line;
        try
        {
            // Stage b: the set the GLSL compiler really applies, which is what a twin is listed and chosen by; stage
            // f: with the texture coordinate count where the pipeline chooses.
            if (stage != nullptr && stage->module)
            {
                const auto selection
                    = slangSelectionSet(std::string(path), defines, stage->module->source, options, numUvSets);
                if (!selection)
                    return;
                line = traceLine(path, *selection);
            }
            else if (numUvSets)
                return;
            else
                line = traceLine(path, defines);
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        std::lock_guard lock(t.mMutex);
        if (!t.mSeen.insert(line).second)
            return;
        if (t.mTiming && stage != nullptr && stage->module && !stage->module->source.empty())
        {
            // A copy compiled with VSG's glslang, as the pipeline would compile it, to time one permutation.
            auto copy = vsg::ShaderStage::create(*stage);
            copy->module = vsg::ShaderModule::create(stage->module->source);
            const std::vector<std::string> list(defines.begin(), defines.end());
            const auto begin = std::chrono::steady_clock::now();
            const bool ok = vsg::ShaderCompiler::create()->compile(copy, list, options);
            const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            std::ostringstream timing;
            timing.precision(2);
            timing << std::fixed << "\t" << (ok ? ms : -1.0);
            line += timing.str();
        }
        std::ofstream out(t.mFile, std::ios::app | std::ios::binary);
        out << line << '\n';
    }

    bool shaderTraceActive()
    {
        return !trace().mFile.empty();
    }

    std::string pipelineTraceLine(std::string_view family, const std::set<std::string>& vertexSet,
        const std::set<std::string>& fragmentSet, bool slang)
    {
        return std::string(family) + "/pipeline\t" + canonicalDefines(vertexSet) + "\t" + canonicalDefines(fragmentSet)
            + (slang ? "\tslang" : "\tglsl");
    }

    void recordPipelineUse(std::string_view family, const std::set<std::string>& vertexSet,
        const std::set<std::string>& fragmentSet, bool slang)
    {
        Trace& t = trace();
        if (t.mFile.empty())
            return;
        std::string line;
        try
        {
            line = pipelineTraceLine(family, vertexSet, fragmentSet, slang);
        }
        catch (const std::invalid_argument&)
        {
            return;
        }
        std::lock_guard lock(t.mMutex);
        if (!t.mSeen.insert(line).second)
            return;
        std::ofstream out(t.mFile, std::ios::app | std::ios::binary);
        out << line << '\n';
    }
}
