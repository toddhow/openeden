#include "shaderpermutation.hpp"

#include "spirvinterface.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include <vsg/io/FileSystem.h>
#include <vsg/io/Logger.h>
#include <vsg/io/read.h>
#include <vsg/utils/ShaderCompiler.h>

namespace vsgUtil
{
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

        struct ManifestCache
        {
            std::mutex mMutex;
            // (the options' search paths, path of the stage file) -> manifest, or nullopt when there is none or it
            // could not be read. Keyed by the paths and not by the options object, whose address can be reused.
            std::map<std::pair<std::string, std::string>, std::optional<PermutationManifest>> mManifests;
            // (search paths, stage file, canonical define set) -> the module read for it, or null when the set is not
            // listed or its file cannot be read: each is looked up, and logged, once.
            std::map<std::tuple<std::string, std::string, std::string>, vsg::ref_ptr<vsg::ShaderModule>> mModules;
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
        // left exactly as it was (with the switch off, a Slang-only shader gets nothing).
        std::map<uint32_t, std::string> declared;
        try
        {
            declared = readSpirvInterface(stage.module->code).mSpecConstantNames;
        }
        catch (const std::runtime_error&)
        {
            return;
        }
        for (const auto& [id, value] : settings->constants)
        {
            if (declared.count(id) != 0)
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

    vsg::ref_ptr<vsg::ShaderModule> findSlangPermutation(
        const std::string& path, const std::set<std::string>& defines, const vsg::ref_ptr<const vsg::Options>& options)
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
        // Held while a file is read too: a module is read once per (paths, stage, set), and pipelines are made rarely.
        std::lock_guard lock(cache.mMutex);
        const auto moduleKey = std::make_tuple(paths, path, key);
        if (const auto known = cache.mModules.find(moduleKey); known != cache.mModules.end())
            return known->second;
        vsg::ref_ptr<vsg::ShaderModule>& result = cache.mModules[moduleKey];
        auto it = cache.mManifests.find({ paths, path });
        if (it == cache.mManifests.end())
            it = cache.mManifests.emplace(std::make_pair(paths, path), loadManifest(path, options)).first;
        if (!it->second)
            return {};
        const std::optional<std::string> file = selectPermutation(*it->second, defines);
        if (!file)
        {
            // Task 0072, stage b (review): a family is ported but this set was never listed; the GLSL draws it.
            vsg::info("Slang permutations of \"", path, "\" have no file for [", key, "]; the GLSL is used");
            return {};
        }
        const std::size_t slash = path.rfind('/');
        const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
        auto module = vsg::read_cast<vsg::ShaderModule>(dir + *file, options);
        if (!module || module->code.empty())
        {
            vsg::warn("Slang permutation \"", dir, *file, "\" is listed but cannot be read; the GLSL is used");
            return {};
        }
        result = module;
        return module;
    }

    vsg::ref_ptr<vsg::ShaderModule> slangTwin(const std::string& path, const std::set<std::string>& requested,
        std::string_view glslSource, bool preferSlang, const vsg::ref_ptr<const vsg::Options>& options)
    {
        if (!preferSlang)
            return {};
        return findSlangPermutation(path, stageDefines(requested, glslSource, options.get()), options);
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
        const vsg::ref_ptr<const vsg::Options>& options)
    {
        Trace& t = trace();
        if (t.mFile.empty())
            return;
        std::string line;
        try
        {
            // Stage b: the set the GLSL compiler really applies, which is what a twin is listed and chosen by.
            line = traceLine(path,
                stage != nullptr && stage->module ? stageDefines(defines, stage->module->source, options.get())
                                                  : defines);
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
}
