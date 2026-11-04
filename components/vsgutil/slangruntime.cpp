#include "slangruntime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <vsg/io/Logger.h>

#ifdef OPENEDEN_SLANG_RUNTIME
#include <slang-com-ptr.h>
#include <slang.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace vsgUtil
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double millisecondsSince(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        // 64-bit FNV-1a, with a seed so that two runs over the same bytes give two independent hashes (a 128-bit
        // cache name). Not a security measure: the cache is the user's own and every entry also carries its key text.
        std::uint64_t fnv1a(const void* data, std::size_t size, std::uint64_t seed)
        {
            std::uint64_t hash = 1469598103934665603ull ^ seed;
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string hex64(std::uint64_t value)
        {
            char text[17];
            std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
            return text;
        }

        std::string readAll(const std::filesystem::path& file)
        {
            std::ifstream in(file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        constexpr char cacheMagic[] = "OEPC1\n";
        // At most 16 Mi words: far above the biggest shader (0.1 MB), small enough to refuse a damaged file's count.
        constexpr std::size_t maxCacheWords = 16u * 1024u * 1024u;

        // The Slang flags of the build, in the text of the cache key: a change of any of them changes every key.
        // (files/shaders/CMakeLists.txt: -target spirv -profile spirv_1_0 -matrix-layout-column-major
        // -warnings-disable 50011, GLSL compatibility on for "import glsl".)
        constexpr char flagsText[] = "spirv_1_0;column-major;disable-warning-50011;glsl-module;v1";

#ifdef OPENEDEN_SLANG_RUNTIME
        using CreateGlobalSession2 = SlangResult(SLANG_MCALL*)(const SlangGlobalSessionDesc*, slang::IGlobalSession**);
#endif

        class DynamicCompiler final : public SlangRuntimeCompiler
        {
        public:
            explicit DynamicCompiler(std::filesystem::path directory)
                : mDirectory(std::move(directory))
            {
            }

            ~DynamicCompiler() override
            {
#ifdef OPENEDEN_SLANG_RUNTIME
                // The library stays loaded until the process ends: Slang may have threads of its own.
                mGlobal = nullptr;
#endif
            }

            std::string cacheIdentity() override
            {
                std::error_code ec;
                std::string text = flagsText;
                std::size_t found = 0;
                std::vector<std::filesystem::path> files;
                for (const auto& entry : std::filesystem::directory_iterator(mDirectory, ec))
                {
                    const std::string name = entry.path().filename().string();
                    if (isLibraryFile(name))
                        files.push_back(entry.path());
                }
                std::sort(files.begin(), files.end());
                for (const auto& file : files)
                {
                    const auto size = std::filesystem::file_size(file, ec);
                    const auto time = std::filesystem::last_write_time(file, ec);
                    text += ";" + file.filename().string() + ":" + std::to_string(size) + ":"
                        + std::to_string(time.time_since_epoch().count());
                    ++found;
                }
                // The three files this needs; fewer and it cannot run (and without the optimiser it would run wrongly).
                return found >= requiredFiles ? text : std::string();
            }

            std::vector<uint32_t> compile(const SlangRuntimeRequest& request, std::string& error) override
            {
#ifdef OPENEDEN_SLANG_RUNTIME
                try
                {
                    return compileNow(request, error);
                }
                catch (const std::exception& e)
                {
                    error = std::string("the Slang compiler threw: ") + e.what();
                }
                catch (...)
                {
                    error = "the Slang compiler threw";
                }
                return {};
#else
                (void)request;
                error = "this build has no Slang headers (OPENEDEN_SLANG_RUNTIME)";
                return {};
#endif
            }

        private:
#ifdef _WIN32
            static constexpr std::size_t requiredFiles = 4;
            static bool isLibraryFile(const std::string& name)
            {
                return name == "slang.dll" || name == "slang-compiler.dll" || name == "slang-glslang.dll"
                    || name == "slang-glsl-module.dll";
            }
#else
            static constexpr std::size_t requiredFiles = 3;
            static bool isLibraryFile(const std::string& name)
            {
                return name.rfind("libslang-compiler.so.", 0) == 0 || name.rfind("libslang-glslang-", 0) == 0
                    || name.rfind("libslang-glsl-module-", 0) == 0;
            }
#endif

#ifdef OPENEDEN_SLANG_RUNTIME
            bool load(std::string& error)
            {
                if (mGlobal)
                    return true;
                if (mTried)
                {
                    error = mLoadError;
                    return false;
                }
                mTried = true;
#ifdef _WIN32
                const std::filesystem::path path = mDirectory / "slang.dll";
                HMODULE library = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
                void* symbol = library ? reinterpret_cast<void*>(GetProcAddress(library, "slang_createGlobalSession2"))
                                       : nullptr;
#else
                const std::filesystem::path path = mDirectory / "libslang-compiler.so";
                void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
                void* symbol = library ? dlsym(library, "slang_createGlobalSession2") : nullptr;
#endif
                if (!library || !symbol)
                {
                    mLoadError = "cannot load the Slang library " + path.string();
                    error = mLoadError;
                    return false;
                }
                // Slang loads its glslang (the optimiser and the legaliser of its SPIR-V) and its glsl module later,
                // by name, through the process's default search, which does not include this folder: without glslang
                // the compile still succeeds, with other, unoptimised code (docs/dependencies.md; the self-test of
                // SlangRuntime refuses it), and without the glsl module Slang compiles it from source and writes a
                // cache file, slang-glsl-module.bin, into this folder. A library already loaded under that name is
                // what a load by name returns, so both are loaded here by their full path first.
                // (IGlobalSession::setDownstreamCompilerPath does not help: measured.)
                std::error_code ec;
                for (const auto& entry : std::filesystem::directory_iterator(mDirectory, ec))
                {
                    const std::string name = entry.path().filename().string();
                    if (name == "slang-glslang.dll" || name == "slang-glsl-module.dll"
                        || name.rfind("libslang-glslang-", 0) == 0 || name.rfind("libslang-glsl-module-", 0) == 0)
                    {
#ifdef _WIN32
                        LoadLibraryExW(entry.path().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
                        dlopen(entry.path().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
                    }
                }
                SlangGlobalSessionDesc desc = {};
                desc.enableGLSL = true; // "import glsl" in the twins
                if (SLANG_FAILED(reinterpret_cast<CreateGlobalSession2>(symbol)(&desc, mGlobal.writeRef())) || !mGlobal)
                {
                    mLoadError = "the Slang library did not create a session";
                    error = mLoadError;
                    return false;
                }
                return true;
            }

            std::vector<uint32_t> compileNow(const SlangRuntimeRequest& request, std::string& error)
            {
                if (!load(error))
                    return {};
                std::vector<std::string> names(request.mDefines.begin(), request.mDefines.end());
                std::vector<slang::PreprocessorMacroDesc> macros;
                for (const std::string& name : names)
                    macros.push_back({ name.c_str(), "" });
                slang::TargetDesc target = {};
                target.format = SLANG_SPIRV;
                target.profile = mGlobal->findProfile("spirv_1_0");
                slang::SessionDesc desc = {};
                desc.targets = &target;
                desc.targetCount = 1;
                desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
                const std::string root = request.mShaderRoot.generic_string();
                const char* paths[] = { root.c_str() };
                desc.searchPaths = paths;
                desc.searchPathCount = 1;
                desc.preprocessorMacros = macros.data();
                desc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());
                slang::CompilerOptionEntry warning;
                warning.name = slang::CompilerOptionName::DisableWarning;
                warning.value.kind = slang::CompilerOptionValueKind::String;
                warning.value.stringValue0 = "50011"; // SPIR-V 1.0 is old: the point
                desc.compilerOptionEntries = &warning;
                desc.compilerOptionEntryCount = 1;
                // SkipSPIRVValidation is left at its default (off): the validation slangc does runs here too.
                Slang::ComPtr<slang::ISession> session;
                if (SLANG_FAILED(mGlobal->createSession(desc, session.writeRef())) || !session)
                {
                    error = "the Slang library did not create a compile session";
                    return {};
                }
                const std::string sourcePath = root + "/" + request.mInfo.mSource;
                const std::string text = readAll(sourcePath);
                if (text.empty())
                {
                    error = "cannot read " + sourcePath;
                    return {};
                }
                Slang::ComPtr<slang::IBlob> diagnostics;
                slang::IModule* module = session->loadModuleFromSourceString(
                    "module", sourcePath.c_str(), text.c_str(), diagnostics.writeRef());
                if (!module)
                {
                    error = "Slang did not load " + request.mInfo.mSource + ": " + blobText(diagnostics);
                    return {};
                }
                Slang::ComPtr<slang::IEntryPoint> entry;
                if (SLANG_FAILED(module->findEntryPointByName(request.mInfo.mEntry.c_str(), entry.writeRef()))
                    || !entry)
                {
                    error = "no entry point " + request.mInfo.mEntry + " in " + request.mInfo.mSource;
                    return {};
                }
                slang::IComponentType* parts[] = { module, entry };
                Slang::ComPtr<slang::IComponentType> composed;
                if (SLANG_FAILED(
                        session->createCompositeComponentType(parts, 2, composed.writeRef(), diagnostics.writeRef())))
                {
                    error = "Slang did not compose " + request.mInfo.mEntry + ": " + blobText(diagnostics);
                    return {};
                }
                Slang::ComPtr<slang::IComponentType> linked;
                if (SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef())))
                {
                    error = "Slang did not link " + request.mInfo.mEntry + ": " + blobText(diagnostics);
                    return {};
                }
                Slang::ComPtr<slang::IBlob> code;
                if (SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())) || !code)
                {
                    error = "Slang did not generate code for " + request.mInfo.mEntry + ": " + blobText(diagnostics);
                    return {};
                }
                if (code->getBufferSize() == 0 || code->getBufferSize() % sizeof(uint32_t) != 0)
                {
                    error = "Slang generated no SPIR-V";
                    return {};
                }
                std::vector<uint32_t> words(code->getBufferSize() / sizeof(uint32_t));
                std::memcpy(words.data(), code->getBufferPointer(), code->getBufferSize());
                return words;
            }

            static std::string blobText(const Slang::ComPtr<slang::IBlob>& blob)
            {
                if (!blob || blob->getBufferSize() == 0)
                    return "(no message)";
                return std::string(static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize());
            }

            Slang::ComPtr<slang::IGlobalSession> mGlobal;
            bool mTried = false;
            std::string mLoadError;
#endif
            std::filesystem::path mDirectory;
        };
    }

    SlangStageInfo parseSlangStageInfo(const std::string& text)
    {
        std::string line = text;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (line.find('\n') != std::string::npos)
            throw std::invalid_argument("a .slang-stage file has one line");
        SlangStageInfo info;
        std::istringstream in(line);
        std::string extra;
        if (!std::getline(in, info.mSource, '\t') || !std::getline(in, info.mEntry, '\t')
            || !std::getline(in, info.mStage, '\t') || std::getline(in, extra, '\t'))
            throw std::invalid_argument("a .slang-stage line is <source> <entry> <stage>, separated by tabs");
        if (info.mSource.empty() || info.mEntry.empty() || info.mStage.empty())
            throw std::invalid_argument("a .slang-stage line has an empty field");
        if (info.mSource.find("..") != std::string::npos || info.mSource.front() == '/'
            || info.mSource.find(':') != std::string::npos || info.mSource.find('\\') != std::string::npos)
            throw std::invalid_argument("a .slang-stage source must stay inside the shader folder");
        return info;
    }

    std::unique_ptr<SlangRuntimeCompiler> createDynamicSlangCompiler(const std::filesystem::path& libraryDirectory)
    {
        return std::make_unique<DynamicCompiler>(libraryDirectory);
    }

    struct SlangRuntime::State
    {
        std::unique_ptr<SlangRuntimeCompiler> mCompiler;
        std::filesystem::path mCacheDirectory;
        std::function<void(bool, const std::string&)> mLog;

        std::mutex mMutex; // everything below except what the compile mutex guards
        Stats mStats;
        bool mDisabled = false;
        std::set<std::string> mFailed; // key texts that failed: not tried again
        std::map<std::string, std::string> mSourceHashes; // shader root -> hash of all its .slang files
        std::string mIdentity;
        bool mIdentityKnown = false;

        std::mutex mCompileMutex; // one Slang global session: one compile at a time
        std::optional<std::pair<SlangRuntimeRequest, std::vector<uint32_t>>> mSelfTest;
        bool mSelfTestDone = false;
    };

    SlangRuntime::SlangRuntime(std::unique_ptr<SlangRuntimeCompiler> compiler, std::filesystem::path cacheDirectory)
        : mState(std::make_unique<State>())
    {
        mState->mCompiler = std::move(compiler);
        mState->mCacheDirectory = std::move(cacheDirectory);
    }

    SlangRuntime::~SlangRuntime()
    {
        // One line at the end of a run that compiled or read anything: what the runtime did and what it cost.
        const Stats stats = mState->mStats;
        if (stats.mCompiled + stats.mCacheHits + stats.mFailed == 0)
            return;
        char text[256];
        std::snprintf(text, sizeof(text),
            "Slang run-time shaders: %zu compiled in %.0f ms, %zu read from the cache in %.1f ms, %zu failed",
            stats.mCompiled, stats.mCompileMilliseconds, stats.mCacheHits, stats.mCacheMilliseconds, stats.mFailed);
        try
        {
            if (mState->mLog)
                mState->mLog(false, text);
            else
                vsg::info(text);
        }
        catch (...)
        {
        }
    }

    void SlangRuntime::setLog(std::function<void(bool, const std::string&)> log)
    {
        std::lock_guard lock(mState->mMutex);
        mState->mLog = std::move(log);
    }

    void SlangRuntime::setSelfTest(SlangRuntimeRequest request, std::vector<uint32_t> expected)
    {
        std::lock_guard lock(mState->mCompileMutex);
        mState->mSelfTest = std::make_pair(std::move(request), std::move(expected));
        mState->mSelfTestDone = false;
    }

    bool SlangRuntime::disabled() const
    {
        std::lock_guard lock(mState->mMutex);
        return mState->mDisabled || !mState->mCompiler;
    }

    SlangRuntime::Stats SlangRuntime::stats() const
    {
        std::lock_guard lock(mState->mMutex);
        return mState->mStats;
    }

    namespace
    {
        // A hash over every .slang file under the root (its relative path and bytes, in path order): any of them may be
        // included by any permutation, as the build's dependencies say.
        std::string hashSources(const std::filesystem::path& root)
        {
            std::error_code ec;
            std::vector<std::filesystem::path> files;
            for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
            {
                if (it->is_regular_file(ec) && it->path().extension() == ".slang")
                    files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            std::uint64_t first = 0;
            std::uint64_t second = 0x9e3779b97f4a7c15ull;
            for (const auto& file : files)
            {
                const std::string relative = std::filesystem::relative(file, root, ec).generic_string();
                const std::string content = readAll(file);
                first = fnv1a(relative.data(), relative.size(), first);
                first = fnv1a(content.data(), content.size(), first);
                second = fnv1a(content.data(), content.size(), second);
                second = fnv1a(relative.data(), relative.size(), second);
            }
            return hex64(first) + hex64(second);
        }
    }

    std::string SlangRuntime::keyText(const SlangRuntimeRequest& request, const SlangRuntimeCheck& check) const
    {
        std::string identity;
        std::string sources;
        {
            std::lock_guard lock(mState->mMutex);
            if (!mState->mIdentityKnown)
            {
                mState->mIdentity = mState->mCompiler ? mState->mCompiler->cacheIdentity() : std::string();
                mState->mIdentityKnown = true;
            }
            identity = mState->mIdentity;
            const std::string root = request.mShaderRoot.generic_string();
            auto it = mState->mSourceHashes.find(root);
            if (it == mState->mSourceHashes.end())
                it = mState->mSourceHashes.emplace(root, hashSources(request.mShaderRoot)).first;
            sources = it->second;
        }
        std::string text = std::string(cacheMagic) + identity + "\n" + request.mStageFile + "\n" + request.mInfo.mSource
            + "\n" + request.mInfo.mEntry + "\n" + request.mInfo.mStage + "\nsources " + sources + "\ncheck "
            + check.mKey + "\ndefines";
        for (const std::string& name : request.mDefines)
            text += " " + name;
        return text;
    }

    std::string SlangRuntime::fileNameOf(const std::string& keyText)
    {
        return hex64(fnv1a(keyText.data(), keyText.size(), 0))
            + hex64(fnv1a(keyText.data(), keyText.size(), 0x5bd1e995)) + ".spv";
    }

    namespace
    {
        // The entry: magic, the key text, its length, the word count, the words and a hash of all of it before. Any
        // difference (a hash collision, a damaged or cut file, another compiler) reads as no entry.
        std::vector<uint32_t> readCache(const std::filesystem::path& file, const std::string& key)
        {
            const std::string bytes = readAll(file);
            const std::size_t header = sizeof(cacheMagic) - 1;
            if (bytes.size() < header + 2 * sizeof(std::uint64_t) || bytes.compare(0, header, cacheMagic) != 0)
                return {};
            std::uint64_t keySize = 0;
            std::memcpy(&keySize, bytes.data() + header, sizeof(keySize));
            std::size_t position = header + sizeof(keySize);
            if (keySize != key.size() || bytes.size() < position + keySize + sizeof(std::uint64_t) * 2
                || bytes.compare(position, keySize, key) != 0)
                return {};
            position += keySize;
            std::uint64_t count = 0;
            std::memcpy(&count, bytes.data() + position, sizeof(count));
            position += sizeof(count);
            if (count == 0 || count > maxCacheWords
                || bytes.size() != position + count * sizeof(uint32_t) + sizeof(count))
                return {};
            std::vector<uint32_t> words(static_cast<std::size_t>(count));
            std::memcpy(words.data(), bytes.data() + position, count * sizeof(uint32_t));
            std::uint64_t stored = 0;
            std::memcpy(&stored, bytes.data() + position + count * sizeof(uint32_t), sizeof(stored));
            if (stored != fnv1a(bytes.data(), bytes.size() - sizeof(stored), 0))
                return {};
            if (words[0] != 0x07230203u)
                return {};
            return words;
        }

        bool writeCache(const std::filesystem::path& file, const std::string& key, const std::vector<uint32_t>& words)
        {
            std::string bytes(cacheMagic, sizeof(cacheMagic) - 1);
            const std::uint64_t keySize = key.size();
            bytes.append(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
            bytes += key;
            const std::uint64_t count = words.size();
            bytes.append(reinterpret_cast<const char*>(&count), sizeof(count));
            bytes.append(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t));
            const std::uint64_t hash = fnv1a(bytes.data(), bytes.size(), 0);
            bytes.append(reinterpret_cast<const char*>(&hash), sizeof(hash));
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);
            // Written whole beside the final name and renamed, so a reader never sees half a file.
            std::filesystem::path temporary = file;
            temporary += ".tmp" + std::to_string(Clock::now().time_since_epoch().count());
            {
                std::ofstream out(temporary, std::ios::binary);
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!out)
                {
                    out.close();
                    std::filesystem::remove(temporary, ec);
                    return false;
                }
            }
            std::filesystem::rename(temporary, file, ec);
            if (ec)
            {
                std::filesystem::remove(temporary, ec);
                return false;
            }
            return true;
        }
    }

    std::vector<uint32_t> SlangRuntime::obtain(const SlangRuntimeRequest& request, const SlangRuntimeCheck& check) const
    {
        State& state = *mState;
        const auto log = [&state](const std::string& text) {
            std::function<void(bool, const std::string&)> sink;
            {
                std::lock_guard lock(state.mMutex);
                sink = state.mLog;
            }
            if (sink)
                sink(true, text);
            else
                vsg::warn(text);
        };
        try
        {
            if (disabled())
                return {};
            const std::string key = keyText(request, check);
            bool filesMissing = false;
            {
                std::lock_guard lock(state.mMutex);
                filesMissing = state.mIdentity.empty();
                if (filesMissing)
                    state.mDisabled = true;
                else if (state.mFailed.count(key) != 0)
                    return {};
            }
            if (filesMissing)
            {
                // Said once, here: the next call sees the runtime disabled.
                log("The Slang compiler's files are missing; `slang shaders` uses what the build compiled and the "
                    "GLSL");
                return {};
            }
            const std::filesystem::path cacheFile
                = state.mCacheDirectory.empty() ? std::filesystem::path() : state.mCacheDirectory / fileNameOf(key);
            const auto describe = [&request]() {
                std::string text = request.mStageFile + " [";
                for (const std::string& name : request.mDefines)
                    text += (text.back() == '[' ? "" : " ") + name;
                return text + "]";
            };
            const auto readFromCache = [&]() -> std::vector<uint32_t> {
                if (cacheFile.empty())
                    return {};
                const auto start = Clock::now();
                auto words = readCache(cacheFile, key);
                if (!words.empty())
                {
                    std::lock_guard lock(state.mMutex);
                    ++state.mStats.mCacheHits;
                    state.mStats.mCacheMilliseconds += millisecondsSince(start);
                }
                return words;
            };
            if (auto words = readFromCache(); !words.empty())
                return words;

            std::lock_guard compileLock(state.mCompileMutex);
            // Another thread may have compiled it while this one waited.
            if (auto words = readFromCache(); !words.empty())
                return words;
            {
                std::lock_guard lock(state.mMutex);
                if (state.mDisabled)
                    return {};
            }
            const auto fail = [&](const std::string& reason) -> std::vector<uint32_t> {
                {
                    std::lock_guard lock(state.mMutex);
                    state.mFailed.insert(key);
                    ++state.mStats.mFailed;
                }
                log("The Slang compiler failed for " + describe() + ": " + reason + "; the GLSL is used");
                return {};
            };
            if (state.mSelfTest && !state.mSelfTestDone)
            {
                state.mSelfTestDone = true;
                std::string error;
                const auto words = state.mCompiler->compile(state.mSelfTest->first, error);
                if (words.empty() || words != state.mSelfTest->second)
                {
                    {
                        std::lock_guard lock(state.mMutex);
                        state.mDisabled = true;
                    }
                    log(words.empty() ? "The Slang compiler cannot start (" + error
                                + "); `slang shaders` uses what the build compiled and the GLSL"
                                      : "The Slang compiler does not reproduce the build's output (its library does "
                                        "not match the pinned "
                                        "version or lacks slang-glslang); `slang shaders` uses what the build compiled "
                                        "and the GLSL");
                    return {};
                }
            }
            const auto start = Clock::now();
            std::string error;
            std::vector<uint32_t> words = state.mCompiler->compile(request, error);
            if (words.empty())
                return fail(error.empty() ? "no code" : error);
            if (words[0] != 0x07230203u)
                return fail("the output is not SPIR-V");
            if (check.mCheck)
            {
                const std::string refused = check.mCheck(words);
                if (!refused.empty())
                    return fail(refused);
            }
            {
                std::lock_guard lock(state.mMutex);
                ++state.mStats.mCompiled;
                state.mStats.mCompileMilliseconds += millisecondsSince(start);
            }
            if (!cacheFile.empty() && !writeCache(cacheFile, key, words))
                log("The Slang shader cache cannot be written to " + cacheFile.parent_path().string());
            return words;
        }
        catch (const std::exception& e)
        {
            log(std::string("The Slang runtime compiler failed: ") + e.what() + "; the GLSL is used");
        }
        catch (...)
        {
            log("The Slang runtime compiler failed; the GLSL is used");
        }
        return {};
    }
}
