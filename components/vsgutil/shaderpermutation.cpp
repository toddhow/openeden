#include "shaderpermutation.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
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

        // "<stem>.<vert|frag|comp>.spv" or "<stem>.<vert|frag|comp>.p<4 digits>.spv", the stem being lower-case
        // letters and digits (the shader files' own names: main, gui).
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
            // path of the stage file -> manifest, or nullopt when there is none or it could not be read.
            std::map<std::string, std::optional<PermutationManifest>> mManifests;
        };

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
                fail("\"" + std::string(file) + "\" is not <stem>.<vert|frag|comp>[.p<4 digits>].spv");
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

    bool prefersSlang(const vsg::Options* options)
    {
        bool value = false;
        return options != nullptr && options->getValue(preferSlangKey, value) && value;
    }

    vsg::ref_ptr<vsg::ShaderModule> findSlangPermutation(
        const std::string& path, const std::set<std::string>& defines, const vsg::ref_ptr<const vsg::Options>& options)
    {
        std::optional<std::string> file;
        {
            ManifestCache& cache = manifestCache();
            std::lock_guard lock(cache.mMutex);
            auto it = cache.mManifests.find(path);
            if (it == cache.mManifests.end())
                it = cache.mManifests.emplace(path, loadManifest(path, options)).first;
            if (!it->second)
                return {};
            file = selectPermutation(*it->second, defines);
        }
        if (!file)
            return {};
        const std::size_t slash = path.rfind('/');
        const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
        auto module = vsg::read_cast<vsg::ShaderModule>(dir + *file, options);
        if (!module || module->code.empty())
        {
            vsg::warn("Slang permutation \"", dir, *file, "\" is listed but cannot be read; the GLSL is used");
            return {};
        }
        return module;
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
}
