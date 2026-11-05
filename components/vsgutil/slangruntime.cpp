#include "slangruntime.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
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
#include <thread>
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
#include <pthread.h>
#include <unistd.h>
#endif

namespace vsgUtil
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        std::int64_t microsecondsNow()
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
        }

        double millisecondsSince(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        // 64-bit FNV-1a, with a seed so that two runs over the same bytes give two independent hashes (a 128-bit
        // cache name). Not a security measure: the cache is the user's own and every entry also carries its key text,
        // and a hit is checked for what the driver would choke on (SlangRuntimeCheck::mCheckHit).
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

        // At most `limit` bytes of the file's start.
        std::string readPrefix(const std::filesystem::path& file, std::size_t limit)
        {
            std::ifstream in(file, std::ios::binary);
            std::string bytes(limit, '\0');
            in.read(bytes.data(), static_cast<std::streamsize>(limit));
            bytes.resize(static_cast<std::size_t>(in.gcount()));
            return bytes;
        }

        constexpr char cacheMagic[] = "OEPC1\n";
        // A refused-set file (review of stage h, round 4, finding 7): the same key text, no words.
        constexpr char refusedMagic[] = "OEPN1\n";
        constexpr std::size_t magicSize = sizeof(cacheMagic) - 1;
        // At most 16 Mi words: far above the biggest shader (0.1 MB), small enough to refuse a damaged file's count.
        constexpr std::size_t maxCacheWords = 16u * 1024u * 1024u;
        // A key text is a few hundred bytes; a file that claims more than this is not ours.
        constexpr std::size_t maxKeyBytes = 1024u * 1024u;
        // A temporary file this old was left by a process that died while writing it.
        constexpr std::chrono::minutes staleTemporaryAge{ 10 };

        // The Slang flags of the build, in the text of the cache key: a change of any of them changes every key.
        // (files/shaders/CMakeLists.txt: -target spirv -profile spirv_1_0 -matrix-layout-column-major
        // -warnings-disable 50011, GLSL compatibility on for "import glsl".)
        constexpr char flagsText[] = "spirv_1_0;column-major;disable-warning-50011;glsl-module;v1";

        unsigned long processId()
        {
#ifdef _WIN32
            return static_cast<unsigned long>(GetCurrentProcessId());
#else
            return static_cast<unsigned long>(getpid());
#endif
        }

        // Names the calling thread for a debugger and for the process's thread list; best effort.
        [[maybe_unused]] void nameCurrentThread(const char* name)
        {
#ifdef _WIN32
            using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
            if (HMODULE kernel = GetModuleHandleW(L"kernel32.dll"))
            {
                const auto set = reinterpret_cast<SetThreadDescriptionFn>(
                    reinterpret_cast<void*>(GetProcAddress(kernel, "SetThreadDescription")));
                if (set != nullptr)
                {
                    std::wstring wide;
                    for (const char* c = name; *c != '\0'; ++c)
                        wide += static_cast<wchar_t>(*c);
                    set(GetCurrentThread(), wide.c_str());
                }
            }
#else
            pthread_setname_np(pthread_self(), name);
#endif
        }

        // Writes the file whole beside its final name and renames it, so a reader never sees half a file. The
        // temporary name carries the process id: two processes that write the same entry do not share a temporary.
        bool writeFileAtomic(const std::filesystem::path& file, const std::string& bytes)
        {
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);
            std::filesystem::path temporary = file;
            temporary
                += ".tmp" + std::to_string(processId()) + "_" + std::to_string(Clock::now().time_since_epoch().count());
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
                // On Windows a rename onto a file another process has open fails. That file is an entry for this
                // same key (the name is a hash of it), so the entry exists: nothing is lost.
                return std::filesystem::exists(file, ec);
            }
            return true;
        }

#ifdef OPENEDEN_SLANG_RUNTIME
        using CreateGlobalSession2 = SlangResult(SLANG_MCALL*)(const SlangGlobalSessionDesc*, slang::IGlobalSession**);
#endif

        class DynamicCompiler final : public SlangRuntimeCompiler
        {
        public:
            DynamicCompiler(const std::filesystem::path& directory, SlangLoadOptions options)
                : mOptions(options)
            {
                // Absolute: LoadLibraryExW with LOAD_WITH_ALTERED_SEARCH_PATH needs a full path, and the cache key
                // names the files by it (review of stage h, finding 5).
                std::error_code ec;
                mDirectory = std::filesystem::absolute(directory, ec);
                if (ec)
                    mDirectory = directory;
            }

            ~DynamicCompiler() override
            {
#ifdef OPENEDEN_SLANG_RUNTIME
                // The library stays loaded until the process ends: Slang may have threads of its own.
                mGlobal = nullptr;
#endif
            }

            // Callable from any thread at any time: it only looks at the directory.
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
                // The files this needs; fewer and it cannot run (and without the optimiser it would run wrongly).
                return found >= requiredFiles ? text : std::string();
            }

            bool prepare(std::string& error) override
            {
#ifdef OPENEDEN_SLANG_RUNTIME
                try
                {
                    return load(error);
                }
                catch (const std::exception& e)
                {
                    error = std::string("the Slang compiler threw: ") + e.what();
                }
                catch (...)
                {
                    error = "the Slang compiler threw";
                }
                return false;
#else
                error = "this build has no Slang headers (OPENEDEN_SLANG_RUNTIME)";
                return false;
#endif
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
            static bool isCompanionFile(const std::string& name)
            {
                return name == "slang-glslang.dll" || name == "slang-glsl-module.dll";
            }
            std::filesystem::path libraryFile() const
            {
                return mDirectory / "slang.dll";
            }
#else
            static constexpr std::size_t requiredFiles = 3;
            static bool isLibraryFile(const std::string& name)
            {
                return name.rfind("libslang-compiler.so.", 0) == 0 || name.rfind("libslang-glslang-", 0) == 0
                    || name.rfind("libslang-glsl-module-", 0) == 0;
            }
            static bool isCompanionFile(const std::string& name)
            {
                return name.rfind("libslang-glslang-", 0) == 0 || name.rfind("libslang-glsl-module-", 0) == 0;
            }
            // The library found by scanning the folder (review of stage h, finding 4): the archive ships
            // libslang-compiler.so.<version> and, as a link to it, libslang-compiler.so, which a copy of the folder
            // may not keep. The longest name is the versioned one.
            std::filesystem::path libraryFile() const
            {
                std::error_code ec;
                std::filesystem::path best;
                for (const auto& entry : std::filesystem::directory_iterator(mDirectory, ec))
                {
                    const std::string name = entry.path().filename().string();
                    if (name.rfind("libslang-compiler.so", 0) == 0
                        && (best.empty() || name.size() > best.filename().string().size()))
                        best = entry.path();
                }
                return best.empty() ? mDirectory / "libslang-compiler.so" : best;
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
                const std::filesystem::path path = libraryFile();
#ifdef _WIN32
                HMODULE library = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
                void* symbol = library ? reinterpret_cast<void*>(GetProcAddress(library, "slang_createGlobalSession2"))
                                       : nullptr;
#else
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
                // what a load by name returns, so both are loaded here by their full path first, and the compiler
                // does not start if either does not load (review of stage h, finding 5: the return values were
                // ignored, and a missing glslang left only the self-test between the game and other code).
                // (IGlobalSession::setDownstreamCompilerPath does not help: measured.)
                std::error_code ec;
                std::size_t companions = 0;
                for (const auto& entry : std::filesystem::directory_iterator(mDirectory, ec))
                {
                    if (!isCompanionFile(entry.path().filename().string()))
                        continue;
#ifdef _WIN32
                    void* companion = LoadLibraryExW(entry.path().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
                    void* companion = dlopen(entry.path().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
                    if (companion != nullptr)
                        ++companions;
                    else if (mOptions.mRequireCompanions)
                    {
                        mLoadError = "cannot load " + entry.path().string();
                        error = mLoadError;
                        return false;
                    }
                }
                if (mOptions.mRequireCompanions && companions < 2)
                {
                    mLoadError = "the Slang library's glslang or glsl module is missing from " + mDirectory.string();
                    error = mLoadError;
                    return false;
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
            SlangLoadOptions mOptions;
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

    std::unique_ptr<SlangRuntimeCompiler> createDynamicSlangCompiler(
        const std::filesystem::path& libraryDirectory, SlangLoadOptions options)
    {
        return std::make_unique<DynamicCompiler>(libraryDirectory, options);
    }

    std::string slangSetId(const std::set<std::string>& defines)
    {
        std::string text;
        for (const std::string& name : defines)
            text += (text.empty() ? "" : " ") + name;
        return hex64(fnv1a(text.data(), text.size(), 0)).substr(0, 8);
    }

    std::string formatSlangCompileEvent(const SlangCompileEvent& event)
    {
        const char* kind = "compiled";
        if (event.mKind == SlangEventKind::CacheHit)
            kind = "cache hit";
        else if (event.mKind == SlangEventKind::Failed)
            kind = "FAILED";
        const char* thread = "other";
        if (event.mThread == SlangThreadKind::Main)
            thread = "main";
        else if (event.mThread == SlangThreadKind::Worker)
            thread = "worker";
        const double milliseconds = static_cast<double>(event.mEndMicroseconds - event.mBeginMicroseconds) / 1000.0;
        char text[512];
        std::snprintf(text, sizeof(text),
            "Slang shader %s: %s set %s (%zu defines) %.1f ms on the %s thread, %s %.1f ms, at %lld..%lld us", kind,
            event.mStageFile.c_str(), slangSetId(event.mDefines).c_str(), event.mDefines.size(), milliseconds, thread,
            event.mThread == SlangThreadKind::Worker ? "queued" : "waited", event.mWaitMilliseconds,
            static_cast<long long>(event.mBeginMicroseconds), static_cast<long long>(event.mEndMicroseconds));
        std::string line = text;
        if (event.mSinceRequestMilliseconds > 0)
        {
            std::snprintf(text, sizeof(text), ", %.1f ms since the request", event.mSinceRequestMilliseconds);
            line += text;
        }
        return line;
    }

    struct SlangRuntime::State
    {
        std::unique_ptr<SlangRuntimeCompiler> mCompiler;
        std::filesystem::path mCacheDirectory;
        std::function<void(bool, const std::string&)> mLog;
        std::function<void(const SlangCompileEvent&)> mEventSink;
        std::thread::id mCreator; // the thread that made the runtime: the game's main thread

        std::mutex mMutex; // everything below except what the compile mutex guards
        Stats mStats;
        bool mDisabled = false;
        std::set<std::string> mFailed; // key texts that failed: not tried again
        std::map<std::string, std::string> mSourceHashes; // shader root -> hash of all its .slang files
        std::string mIdentity;
        bool mIdentityKnown = false;

        std::mutex mCompileMutex; // one Slang global session: one compile at a time
        std::vector<std::pair<SlangRuntimeRequest, std::vector<uint32_t>>> mSelfTests;
        bool mSelfTestsDone = false;
        bool mPrepared = false;

        bool mCacheCleaned = false; // cleanCache ran (after the first compile)

        void warn(const std::string& text)
        {
            std::function<void(bool, const std::string&)> sink;
            {
                std::lock_guard lock(mMutex);
                sink = mLog;
            }
            if (sink)
                sink(true, text);
            else
                vsg::warn(text);
        }

        SlangThreadKind threadKind() const
        {
            return std::this_thread::get_id() == mCreator ? SlangThreadKind::Main : SlangThreadKind::Other;
        }

        // Reports an event to the sink (copied under the mutex, called outside it).
        void report(const SlangCompileEvent& event)
        {
            std::function<void(const SlangCompileEvent&)> sink;
            {
                std::lock_guard lock(mMutex);
                sink = mEventSink;
            }
            if (!sink)
                return;
            try
            {
                sink(event);
            }
            catch (...)
            {
            }
        }

        void disable(const std::string& text)
        {
            {
                std::lock_guard lock(mMutex);
                mDisabled = true;
            }
            warn(text);
        }

        // Starts the compiler and runs the self-tests, once; the caller holds mCompileMutex. False (the runtime is
        // disabled, with one line saying why) when either fails.
        bool startLocked()
        {
            {
                std::lock_guard lock(mMutex);
                if (mDisabled)
                    return false;
            }
            const std::string fallback = "; `slang shaders` uses what the build compiled and the GLSL";
            if (!mPrepared)
            {
                std::string error;
                if (!mCompiler->prepare(error))
                {
                    disable("The Slang compiler cannot start (" + error + ")" + fallback);
                    return false;
                }
                mPrepared = true;
            }
            if (!mSelfTestsDone)
            {
                mSelfTestsDone = true;
                for (const auto& [request, expected] : mSelfTests)
                {
                    std::string error;
                    const auto words = mCompiler->compile(request, error);
                    if (words.empty() || words != expected)
                    {
                        disable(words.empty() ? "The Slang compiler cannot start (" + error + ")" + fallback
                                              : "The Slang compiler does not reproduce the build's output (its "
                                                "library does not match the pinned version or lacks slang-glslang)"
                                    + fallback);
                        return false;
                    }
                }
            }
            return true;
        }
    };

    SlangRuntime::SlangRuntime(std::unique_ptr<SlangRuntimeCompiler> compiler, std::filesystem::path cacheDirectory)
        : mState(std::make_unique<State>())
    {
        mState->mCompiler = std::move(compiler);
        mState->mCacheDirectory = std::move(cacheDirectory);
        mState->mCreator = std::this_thread::get_id();
    }

    SlangRuntime::~SlangRuntime()
    {
        // One line at the end of a run that compiled or read anything: what the runtime did and what it cost.
        const Stats stats = mState->mStats;
        if (stats.mCompiled + stats.mCacheHits + stats.mFailed + stats.mRefusedSets == 0)
            return;
        char text[512];
        std::snprintf(text, sizeof(text),
            "Slang run-time shaders: %zu compiled in %.0f ms (%zu on the main thread, %zu on other threads), %zu read "
            "from the cache in %.1f ms, %zu failed, %zu refused sets remembered; longest wait on the compile lock "
            "%.0f ms (%s thread)",
            stats.mCompiled, stats.mCompileMilliseconds, stats.mCompiledOnMain, stats.mCompiledOnOther,
            stats.mCacheHits, stats.mCacheMilliseconds, stats.mFailed, stats.mRefusedSets,
            stats.mLongestWaitMilliseconds, stats.mLongestWaitWasMain ? "main" : "other");
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

    void SlangRuntime::setEventSink(std::function<void(const SlangCompileEvent&)> sink)
    {
        std::lock_guard lock(mState->mMutex);
        mState->mEventSink = std::move(sink);
    }

    void SlangRuntime::setSelfTest(SlangRuntimeRequest request, std::vector<uint32_t> expected)
    {
        {
            std::lock_guard lock(mState->mCompileMutex);
            mState->mSelfTests.clear();
        }
        addSelfTest(std::move(request), std::move(expected));
    }

    void SlangRuntime::addSelfTest(SlangRuntimeRequest request, std::vector<uint32_t> expected)
    {
        std::lock_guard lock(mState->mCompileMutex);
        mState->mSelfTests.emplace_back(std::move(request), std::move(expected));
        mState->mSelfTestsDone = false;
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

    std::uintmax_t SlangRuntime::maxCacheFileBytes(std::size_t keySize)
    {
        // magic, key size, key, word count, the words at most, hash
        return magicSize + sizeof(std::uint64_t) + keySize + sizeof(std::uint64_t)
            + static_cast<std::uintmax_t>(maxCacheWords) * sizeof(uint32_t) + sizeof(std::uint64_t);
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

        // The compiler identity and the hash of a shader root's sources, each worked out once and outside the state's
        // mutex (review of stage h, finding 3: the first call read every .slang file with the mutex held, so
        // disabled() and stats() waited for it). Two threads may both work it out; the first result is kept.
        std::string identityOf(SlangRuntimeCompiler* compiler)
        {
            return compiler != nullptr ? compiler->cacheIdentity() : std::string();
        }
    }

    std::string SlangRuntime::keyText(const SlangRuntimeRequest& request, const SlangRuntimeCheck& check) const
    {
        State& state = *mState;
        std::string identity;
        bool identityKnown = false;
        {
            std::lock_guard lock(state.mMutex);
            identityKnown = state.mIdentityKnown;
            identity = state.mIdentity;
        }
        if (!identityKnown)
        {
            std::string computed = identityOf(state.mCompiler.get());
            std::lock_guard lock(state.mMutex);
            if (!state.mIdentityKnown)
            {
                state.mIdentity = std::move(computed);
                state.mIdentityKnown = true;
            }
            identity = state.mIdentity;
        }
        const std::string root = request.mShaderRoot.generic_string();
        std::string sources;
        bool sourcesKnown = false;
        {
            std::lock_guard lock(state.mMutex);
            const auto it = state.mSourceHashes.find(root);
            if (it != state.mSourceHashes.end())
            {
                sources = it->second;
                sourcesKnown = true;
            }
        }
        if (!sourcesKnown)
        {
            std::string computed = hashSources(request.mShaderRoot);
            std::lock_guard lock(state.mMutex);
            sources = state.mSourceHashes.emplace(root, std::move(computed)).first->second;
        }
        std::string text = std::string(cacheMagic) + identity + "\n" + request.mStageFile + "\n" + request.mInfo.mSource
            + "\n" + request.mInfo.mEntry + "\n" + request.mInfo.mStage + "\nsources " + sources + "\ncheckversion "
            + std::to_string(slangCacheCheckVersion) + "\ncheck " + check.mKey + "\ndefines";
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
        // difference (a hash collision, a damaged or cut file, another compiler) reads as no entry. The size is
        // checked before the file is read (review of stage h, finding 2).
        std::vector<uint32_t> readCache(const std::filesystem::path& file, const std::string& key)
        {
            std::error_code ec;
            const std::uintmax_t fileSize = std::filesystem::file_size(file, ec);
            if (ec || fileSize > SlangRuntime::maxCacheFileBytes(key.size()))
                return {};
            const std::string bytes = readAll(file);
            const std::size_t header = magicSize;
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
            std::string bytes(cacheMagic, magicSize);
            const std::uint64_t keySize = key.size();
            bytes.append(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
            bytes += key;
            const std::uint64_t count = words.size();
            bytes.append(reinterpret_cast<const char*>(&count), sizeof(count));
            bytes.append(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t));
            const std::uint64_t hash = fnv1a(bytes.data(), bytes.size(), 0);
            bytes.append(reinterpret_cast<const char*>(&hash), sizeof(hash));
            return writeFileAtomic(file, bytes);
        }

        // A refused-set file: the magic, the key size, the key and the reason. True when it is for exactly this key.
        bool readRefusal(const std::filesystem::path& file, const std::string& key)
        {
            std::error_code ec;
            const std::uintmax_t fileSize = std::filesystem::file_size(file, ec);
            if (ec || fileSize > magicSize + sizeof(std::uint64_t) + key.size() + 4096)
                return false;
            const std::string bytes = readAll(file);
            if (bytes.size() < magicSize + sizeof(std::uint64_t) || bytes.compare(0, magicSize, refusedMagic) != 0)
                return false;
            std::uint64_t keySize = 0;
            std::memcpy(&keySize, bytes.data() + magicSize, sizeof(keySize));
            const std::size_t position = magicSize + sizeof(keySize);
            return keySize == key.size() && bytes.size() >= position + keySize
                && bytes.compare(position, keySize, key) == 0;
        }

        bool writeRefusal(const std::filesystem::path& file, const std::string& key, const std::string& reason)
        {
            std::string bytes(refusedMagic, magicSize);
            const std::uint64_t keySize = key.size();
            bytes.append(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
            bytes += key;
            bytes += reason.substr(0, 2048);
            return writeFileAtomic(file, bytes);
        }

        // The compiler identity and the sources hash an entry file was made for, from its first bytes; nothing for a
        // file that is not an entry of either kind.
        std::optional<std::pair<std::string, std::string>> entryOrigin(const std::string& prefix)
        {
            if (prefix.size() < magicSize + sizeof(std::uint64_t)
                || (prefix.compare(0, magicSize, cacheMagic) != 0 && prefix.compare(0, magicSize, refusedMagic) != 0))
                return std::nullopt;
            std::uint64_t keySize = 0;
            std::memcpy(&keySize, prefix.data() + magicSize, sizeof(keySize));
            const std::size_t position = magicSize + sizeof(keySize);
            if (keySize == 0 || keySize > maxKeyBytes || prefix.size() < position + keySize)
                return std::nullopt;
            const std::string key = prefix.substr(position, keySize);
            // The key text: the magic line, the identity, then "sources <hash>" among the lines.
            std::istringstream in(key);
            std::string line;
            std::string identity;
            std::string sources;
            std::getline(in, line); // the magic
            if (!std::getline(in, identity))
                return std::nullopt;
            while (std::getline(in, line))
            {
                if (line.rfind("sources ", 0) == 0)
                {
                    sources = line.substr(8);
                    break;
                }
            }
            if (sources.empty())
                return std::nullopt;
            return std::make_pair(identity, sources);
        }
    }

    SlangRuntime::CleanResult SlangRuntime::cleanCache(
        const std::filesystem::path& shaderRoot, std::uintmax_t maxBytes) const
    {
        State& state = *mState;
        CleanResult result;
        if (state.mCacheDirectory.empty())
            return result;
        // The identity and the sources hash this runtime's keys have.
        SlangRuntimeRequest probe;
        probe.mShaderRoot = shaderRoot;
        const std::string key = keyText(probe, SlangRuntimeCheck{});
        std::istringstream keyLines(key);
        std::string line;
        std::string identity;
        std::string sources;
        std::getline(keyLines, line);
        std::getline(keyLines, identity);
        while (std::getline(keyLines, line))
        {
            if (line.rfind("sources ", 0) == 0)
                sources = line.substr(8);
        }
        if (identity.empty())
            return result; // the compiler cannot be identified: nothing is known to be stale

        struct Kept
        {
            std::filesystem::path mPath;
            std::uintmax_t mSize;
            std::filesystem::file_time_type mTime;
        };
        std::vector<Kept> kept;
        const auto erase = [&result](const std::filesystem::path& path) {
            std::error_code removeError;
            if (std::filesystem::remove(path, removeError) && !removeError)
                ++result.mRemoved;
        };
        std::error_code ec;
        const auto now = std::filesystem::file_time_type::clock::now();
        for (const auto& entry : std::filesystem::directory_iterator(state.mCacheDirectory, ec))
        {
            std::error_code entryError;
            if (!entry.is_regular_file(entryError))
                continue;
            const std::string name = entry.path().filename().string();
            const std::filesystem::path extension = entry.path().extension();
            const auto time = entry.last_write_time(entryError);
            if (name.find(".tmp") != std::string::npos)
            {
                // A temporary file; a recent one may be another process's in the middle of a write.
                if (!entryError && now - time > staleTemporaryAge)
                    erase(entry.path());
                continue;
            }
            if (extension != ".spv" && extension != ".no")
                continue; // not ours
            const std::uintmax_t size = entry.file_size(entryError);
            if (entryError)
                continue;
            const auto origin = entryOrigin(readPrefix(entry.path(), std::min<std::uintmax_t>(size, maxKeyBytes + 64)));
            if (!origin || origin->first != identity || origin->second != sources)
            {
                erase(entry.path());
                continue;
            }
            kept.push_back({ entry.path(), size, time });
        }
        std::uintmax_t total = 0;
        for (const Kept& item : kept)
            total += item.mSize;
        std::sort(kept.begin(), kept.end(), [](const Kept& a, const Kept& b) { return a.mTime < b.mTime; });
        for (const Kept& item : kept)
        {
            if (total <= maxBytes)
                break;
            const std::size_t before = result.mRemoved;
            erase(item.mPath);
            if (result.mRemoved > before)
                total -= item.mSize;
        }
        result.mBytesKept = total;
        {
            std::lock_guard lock(state.mMutex);
            state.mStats.mCleaned += result.mRemoved;
        }
        return result;
    }

    std::vector<uint32_t> SlangRuntime::obtain(const SlangRuntimeRequest& request, const SlangRuntimeCheck& check) const
    {
        State& state = *mState;
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
                state.warn(
                    "The Slang compiler's files are missing; `slang shaders` uses what the build compiled and "
                    "the GLSL");
                return {};
            }
            const std::filesystem::path cacheFile
                = state.mCacheDirectory.empty() ? std::filesystem::path() : state.mCacheDirectory / fileNameOf(key);
            std::filesystem::path refusedFile = cacheFile;
            if (!refusedFile.empty())
                refusedFile.replace_extension(".no");
            const auto describe = [&request]() {
                std::string text = request.mStageFile + " [";
                for (const std::string& name : request.mDefines)
                    text += (text.back() == '[' ? "" : " ") + name;
                return text + "]";
            };
            // A set an earlier run refused is not compiled again (its key has the version of the checks, the
            // compiler and the sources, so a change of any of them tries again).
            if (!refusedFile.empty() && readRefusal(refusedFile, key))
            {
                std::lock_guard lock(state.mMutex);
                state.mFailed.insert(key);
                ++state.mStats.mRefusedSets;
                return {};
            }
            bool entryRefused = false; // the entry of this key failed mCheckHit: it is not read (or logged) again
            const auto readFromCache = [&]() -> std::vector<uint32_t> {
                if (cacheFile.empty() || entryRefused)
                    return {};
                const auto start = Clock::now();
                const std::int64_t beginUs = microsecondsNow();
                auto words = readCache(cacheFile, key);
                if (!words.empty() && check.mCheckHit)
                {
                    // Not trusted just because its own hash is right: the hash is not secret.
                    const std::string refused = check.mCheckHit(words);
                    if (!refused.empty())
                    {
                        entryRefused = true;
                        {
                            std::lock_guard lock(state.mMutex);
                            ++state.mStats.mHitsRejected;
                        }
                        state.warn("The Slang shader cache entry for " + describe() + " was refused (" + refused
                            + "); it is compiled again");
                        return {};
                    }
                }
                if (!words.empty())
                {
                    const double milliseconds = millisecondsSince(start);
                    {
                        std::lock_guard lock(state.mMutex);
                        ++state.mStats.mCacheHits;
                        state.mStats.mCacheMilliseconds += milliseconds;
                    }
                    if (milliseconds > slangCacheHitReportMilliseconds)
                    {
                        SlangCompileEvent event;
                        event.mStageFile = request.mStageFile;
                        event.mDefines = request.mDefines;
                        event.mKind = SlangEventKind::CacheHit;
                        event.mThread = state.threadKind();
                        event.mBeginMicroseconds = beginUs;
                        event.mEndMicroseconds = microsecondsNow();
                        state.report(event);
                    }
                }
                return words;
            };
            if (auto words = readFromCache(); !words.empty())
                return words;

            const bool onMain = std::this_thread::get_id() == state.mCreator;
            const auto waitStart = Clock::now();
            std::lock_guard compileLock(state.mCompileMutex);
            double waited = 0;
            {
                waited = millisecondsSince(waitStart);
                std::lock_guard lock(state.mMutex);
                if (waited > state.mStats.mLongestWaitMilliseconds)
                {
                    state.mStats.mLongestWaitMilliseconds = waited;
                    state.mStats.mLongestWaitWasMain = onMain;
                }
            }
            // Another thread may have compiled it while this one waited.
            if (auto words = readFromCache(); !words.empty())
                return words;
            const std::int64_t compileBeginUs = microsecondsNow();
            const auto report = [&](SlangEventKind kind) {
                SlangCompileEvent event;
                event.mStageFile = request.mStageFile;
                event.mDefines = request.mDefines;
                event.mKind = kind;
                event.mThread = state.threadKind();
                event.mBeginMicroseconds = compileBeginUs;
                event.mEndMicroseconds = microsecondsNow();
                event.mWaitMilliseconds = waited;
                state.report(event);
            };
            const auto fail = [&](const std::string& reason, bool remember) -> std::vector<uint32_t> {
                report(SlangEventKind::Failed);
                {
                    std::lock_guard lock(state.mMutex);
                    state.mFailed.insert(key);
                    ++state.mStats.mFailed;
                }
                state.warn("The Slang compiler failed for " + describe() + ": " + reason + "; the GLSL is used");
                if (remember && !refusedFile.empty())
                    writeRefusal(refusedFile, key, reason);
                return {};
            };
            if (!state.startLocked())
                return {};
            const auto start = Clock::now();
            std::string error;
            std::vector<uint32_t> words = state.mCompiler->compile(request, error);
            if (words.empty())
                return fail(error.empty() ? "no code" : error, false);
            if (words[0] != 0x07230203u)
                return fail("the output is not SPIR-V", false);
            if (check.mCheck)
            {
                const std::string refused = check.mCheck(words);
                if (!refused.empty())
                    return fail(refused, true);
            }
            {
                std::lock_guard lock(state.mMutex);
                ++state.mStats.mCompiled;
                (onMain ? state.mStats.mCompiledOnMain : state.mStats.mCompiledOnOther) += 1;
                state.mStats.mCompileMilliseconds += millisecondsSince(start);
            }
            if (!cacheFile.empty() && !writeCache(cacheFile, key, words))
                state.warn("The Slang shader cache cannot be written to " + cacheFile.parent_path().string());
            report(SlangEventKind::Compiled);
            // The first compile of a run also cleans the cache folder (what the start-up preload did): entries of
            // another compiler or other sources, stale temporary files, and the oldest above the size limit.
            bool clean = false;
            {
                std::lock_guard lock(state.mMutex);
                clean = !state.mCacheCleaned;
                state.mCacheCleaned = true;
            }
            if (clean)
                cleanCache(request.mShaderRoot);
            return words;
        }
        catch (const std::exception& e)
        {
            state.warn(std::string("The Slang runtime compiler failed: ") + e.what() + "; the GLSL is used");
        }
        catch (...)
        {
            state.warn("The Slang runtime compiler failed; the GLSL is used");
        }
        return {};
    }
}
