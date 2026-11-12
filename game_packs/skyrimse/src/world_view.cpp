#include "game_skyrimse/world_view.hpp"

#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/actors.hpp"
#include "game_skyrimse/appearance.hpp"
#include "game_skyrimse/dialogue_runtime.hpp"
#include "game_skyrimse/weather.hpp"
#include "game_skyrimse/weather_source.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

namespace game_skyrimse
{
    namespace sdkgame = openeden::sdk::game;

    namespace
    {
        constexpr float kCellSize = 4096.0f; // Skyrim's exterior cell is 4096 units square
        constexpr std::size_t kMaxErrorsKept = 256;

        std::string lowerAscii(std::string_view s)
        {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(),
                [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
            return out;
        }

        Content::Error makeError(const std::string& file, std::uint64_t offset, std::uint32_t type, std::string message)
        {
            Content::Error e;
            e.mFile = file;
            e.mOffset = offset;
            e.mRecordType = type;
            e.mMessage = std::move(message);
            return e;
        }

        sdkgame::BaseKind kindOfModelType(std::uint32_t type)
        {
            switch (type)
            {
                case Content::fourCC("ACTI"):
                    return sdkgame::BaseKind::Activator;
                case Content::fourCC("CONT"):
                    return sdkgame::BaseKind::Container;
                case Content::fourCC("DOOR"):
                    return sdkgame::BaseKind::Door;
                case Content::fourCC("FLOR"):
                    return sdkgame::BaseKind::Flora;
                case Content::fourCC("FURN"):
                    return sdkgame::BaseKind::Furniture;
                case Content::fourCC("LIGH"):
                    return sdkgame::BaseKind::Light;
                case Content::fourCC("MSTT"):
                    return sdkgame::BaseKind::MovableStatic;
                case Content::fourCC("TREE"):
                    return sdkgame::BaseKind::Tree;
                case Content::fourCC("TACT"):
                    return sdkgame::BaseKind::Activator;
                case Content::fourCC("ALCH"):
                case Content::fourCC("AMMO"):
                case Content::fourCC("APPA"):
                case Content::fourCC("BOOK"):
                case Content::fourCC("INGR"):
                case Content::fourCC("KEYM"):
                case Content::fourCC("SCRL"):
                case Content::fourCC("SLGM"):
                    return sdkgame::BaseKind::Item;
                default:
                    return sdkgame::BaseKind::Other;
            }
        }

        // The base file name of a plugin without extension.
        std::string stemOf(const std::filesystem::path& p)
        {
            return p.stem().string();
        }

        bool finite(float v)
        {
            return std::isfinite(v);
        }
    }

    // ---- DataFolderFiles --------------------------------------------------------------------------------------------

    bool DataFolderFiles::read(std::string_view path, std::string& bytes) const
    {
        namespace fs = std::filesystem;
        std::string native(path);
        std::replace(native.begin(), native.end(), '\\', '/');
        // Loose file first (a mod may ship its tables loose; the file system is case-insensitive on Windows only, so
        // try the name as given).
        {
            std::error_code ec;
            const fs::path loose = mData / fs::path(native);
            if (fs::is_regular_file(loose, ec))
            {
                std::ifstream in(loose, std::ios::binary);
                if (in)
                {
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    bytes = ss.str();
                    return true;
                }
            }
        }
        // The plugin's base name is the part before the last '_' of the file name.
        const std::size_t slash = native.find_last_of('/');
        const std::string file = slash == std::string::npos ? native : native.substr(slash + 1);
        const std::size_t underscore = file.find_last_of('_');
        const std::string stem = lowerAscii(underscore == std::string::npos ? file : file.substr(0, underscore));

        std::vector<fs::path> candidates;
        candidates.push_back(mData / "Skyrim - Interface.bsa");
        std::error_code ec;
        std::vector<fs::path> ownArchives;
        for (fs::directory_iterator it(mData, ec), end; !ec && it != end; it.increment(ec))
        {
            const std::string name = lowerAscii(it->path().filename().string());
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bsa") == 0 && name.rfind(stem, 0) == 0
                && name != "skyrim - interface.bsa")
                ownArchives.push_back(it->path());
        }
        std::sort(ownArchives.begin(), ownArchives.end());
        candidates.insert(candidates.end(), ownArchives.begin(), ownArchives.end());

        std::string backslashed(path);
        std::replace(backslashed.begin(), backslashed.end(), '/', '\\');
        for (const fs::path& archive : candidates)
        {
            std::string err;
            if (std::filesystem::exists(archive, ec) && readArchiveEntry(archive, backslashed, bytes, err))
                return true;
        }
        return false;
    }

    bool VfsFileSource::read(std::string_view path, std::string& bytes) const
    {
        std::string unified(path);
        std::replace(unified.begin(), unified.end(), '\\', '/');
        const std::vector<std::uint8_t> data = mVfs.readFile(unified);
        if (data.empty())
            return false;
        bytes.assign(reinterpret_cast<const char*>(data.data()), data.size());
        return true;
    }

    bool VfsFileSource::exists(std::string_view path) const
    {
        std::string unified(path);
        std::replace(unified.begin(), unified.end(), '\\', '/');
        return mVfs.hasFile(unified);
    }

    // ---- construction ---------------------------------------------------------------------------------------------

    /// The dialogue and quest records of a load order (task 0140). Built from the plugin files only, so a worker can
    /// build it while the main thread goes on (the records of the five masters take several seconds).
    struct SkyrimWorldView::DialogueData
    {
        DialogueIndex mDialogue;
        QuestIndex mQuests;
    };

    struct SkyrimWorldView::DialogueState
    {
        std::unique_ptr<DialogueData> mData;
        /// String tables by (plugin index, kind); an empty table is one that could not be read (tried once).
        std::map<std::pair<std::uint32_t, int>, std::unique_ptr<StringTable>> mTables;
        std::unique_ptr<DialogueRuntime> mRuntime;
        std::uint64_t mTablesMissing = 0;
    };

    namespace
    {
        struct PluginRef
        {
            std::filesystem::path mPath;
            std::string mName;
        };
    }

    SkyrimWorldView::SkyrimWorldView() = default;

    SkyrimWorldView::SkyrimWorldView(Options options, const FileSource* files)
        : mOptions(std::move(options))
        , mFiles(files)
    {
        mOptions.mLimits.mSkipReferences = true; // the index never holds a reference
    }

    SkyrimWorldView::~SkyrimWorldView()
    {
        cancelAndJoin();
        if (mDialogueThread.joinable())
            mDialogueThread.join();
    }

    bool SkyrimWorldView::ensureActorIndex()
    {
        if (mActorIndex != nullptr)
            return true;
        {
            auto index = std::make_unique<ActorIndex>();
            const ActorLimits limits;
            for (const PluginData& plugin : mPlugins)
            {
                Content::Error openError;
                const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(plugin.mPath, openError);
                if (file == nullptr)
                {
                    noteError(openError);
                    continue;
                }
                ActorRecords records;
                (void)parseActorRecords(*file, mOrder, limits, records);
                for (ActorNpc& npc : records.mNpcs)
                    npc.mPlugin = static_cast<std::uint32_t>(&plugin - mPlugins.data());
                index->add(records);
                index->addLeveledLists(plugin.mParsed.mLeveledLists);
            }
            mActorIndex = std::move(index);
        }
        return true;
    }

    bool SkyrimWorldView::describeActor(openeden::sdk::game::WorldId base, std::uint32_t variation,
        openeden::sdk::game::ActorAppearance& out, std::string& error)
    {
        out = {};
        if (!awaitIndex())
        {
            error = "the world index is not available";
            return false;
        }
        if (!ensureActorIndex())
        {
            error = "the actor records are not available";
            return false;
        }
        startDialoguePrefetch(); // task 0140: the conversation records are read while the cell loads
        FaceFileProbe probe;
        if (mFiles != nullptr)
        {
            probe.mOrder = &mOrder;
            const FileSource* files = mFiles;
            probe.mExists = [files](const std::string& path) { return files->exists(path); };
        }
        return makeActorAppearance(*mActorIndex, static_cast<FormId>(base), variation, out, error, {}, &probe);
    }

    // ---- conversations (task 0140, M-S4 slice 1) -----------------------------------------------------------------

    std::unique_ptr<SkyrimWorldView::DialogueData> SkyrimWorldView::buildDialogueData(
        const std::vector<std::pair<std::filesystem::path, std::string>>& plugins, const LoadOrder& order)
    {
        auto data = std::make_unique<DialogueData>();
        const DialogueLimits dialogueLimits;
        const QuestLimits questLimits;
        for (const auto& [path, name] : plugins)
        {
            Content::Error openError;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(path, openError);
            if (file == nullptr)
            {
                // Keep the plugin numbering of the index: an empty plugin takes the slot.
                data->mDialogue.add(DialogueRecords(), FormIdResolver(Content::Header(), name, order));
                data->mQuests.add(QuestRecords(), FormIdResolver(Content::Header(), name, order));
                continue;
            }
            DialogueRecords dialogue;
            (void)parseDialogueRecords(*file, order, dialogueLimits, dialogue);
            data->mDialogue.add(std::move(dialogue), FormIdResolver(file->header(), name, order));
            QuestRecords quests;
            (void)parseQuestRecords(*file, order, questLimits, quests);
            data->mQuests.add(std::move(quests), FormIdResolver(file->header(), name, order));
        }
        return data;
    }

    void SkyrimWorldView::startDialoguePrefetch()
    {
        if (mDialogue != nullptr || mDialogueThread.joinable())
            return;
        std::vector<std::pair<std::filesystem::path, std::string>> plugins;
        for (const PluginData& plugin : mPlugins)
            plugins.emplace_back(plugin.mPath, plugin.mName);
        mDialogueThread = std::thread([this, plugins = std::move(plugins), order = mOrder]() {
            std::unique_ptr<DialogueData> data = buildDialogueData(plugins, order);
            mPrefetched = std::move(data);
            mDialogueReady.store(true);
        });
    }

    bool SkyrimWorldView::ensureDialogue()
    {
        if (mDialogue != nullptr)
            return mDialogue->mRuntime != nullptr;
        if (!ensureActorIndex())
            return false;
        auto state = std::make_unique<DialogueState>();
        if (mDialogueThread.joinable())
        {
            if (!mDialogueReady.load())
                return false; // still being read: the caller may ask again; nothing blocks the main thread
            mDialogueThread.join();
            state->mData = std::move(mPrefetched);
        }
        else
        {
            std::vector<std::pair<std::filesystem::path, std::string>> plugins;
            for (const PluginData& plugin : mPlugins)
                plugins.emplace_back(plugin.mPath, plugin.mName);
            state->mData = buildDialogueData(plugins, mOrder);
        }
        DialogueState* raw = state.get();
        raw->mRuntime = std::make_unique<DialogueRuntime>(*mActorIndex, raw->mData->mDialogue, raw->mData->mQuests,
            QuestState::initial(raw->mData->mQuests),
            [this, raw](const LString& text, std::uint32_t plugin, StringTableKind kind) -> std::string {
                if (!text.mPresent)
                    return std::string();
                if (!text.mLocalized)
                    return text.mText;
                if (mFiles == nullptr || plugin >= mPlugins.size())
                    return std::string();
                auto& slot = raw->mTables[{ plugin, static_cast<int>(kind) }];
                if (slot == nullptr)
                {
                    std::string bytes;
                    const std::string path = stringTablePath(stemOf(mPlugins[plugin].mPath), mOptions.mLanguage, kind);
                    auto table = std::make_unique<StringTable>();
                    Content::Error tableError;
                    if (mFiles->read(path, bytes)
                        && StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() }, kind,
                            mPlugins[plugin].mName, *table, tableError))
                        slot = std::move(table);
                    else
                    {
                        ++raw->mTablesMissing;
                        slot = std::make_unique<StringTable>(); // empty: not tried again
                    }
                }
                const std::string* found = slot->find(text.mId);
                return found != nullptr ? *found : std::string();
            });
        mDialogue = std::move(state);
        return true;
    }

    bool SkyrimWorldView::beginConversation(sdkgame::WorldId base, sdkgame::WorldId reference, sdkgame::WorldId cell,
        sdkgame::DialogueTurn& out, std::string& error)
    {
        out = {};
        if (!awaitIndex() || !ensureDialogue())
        {
            error = "the dialogue records are not available";
            return false;
        }
        DialogueSpeaker speaker;
        speaker.mBase = static_cast<FormId>(base);
        speaker.mReference = static_cast<FormId>(reference);
        speaker.mCell = static_cast<FormId>(cell);
        return mDialogue->mRuntime->begin(speaker, out, error);
    }

    bool SkyrimWorldView::chooseTopic(std::uint32_t topicId, sdkgame::DialogueTurn& out, std::string& error)
    {
        out = {};
        if (mDialogue == nullptr || mDialogue->mRuntime == nullptr)
        {
            error = "no conversation is open";
            return false;
        }
        return mDialogue->mRuntime->choose(topicId, out, error);
    }

    void SkyrimWorldView::endConversation()
    {
        if (mDialogue != nullptr && mDialogue->mRuntime != nullptr)
            mDialogue->mRuntime->end();
    }

    bool SkyrimWorldView::ensureWeather()
    {
        if (!awaitIndex())
            return false;
        if (mWeatherTable != nullptr)
            return true;
        auto index = std::make_unique<WeatherIndex>();
        const WeatherLimits limits;
        for (const PluginData& plugin : mPlugins)
        {
            Content::Error openError;
            const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(plugin.mPath, openError);
            if (file == nullptr)
            {
                noteError(openError);
                continue;
            }
            WeatherRecords records;
            (void)parseWeatherRecords(*file, mOrder, limits, records);
            index->add(records);
        }
        mWeatherIndex = std::move(index);
        TextureProbe probe;
        if (mFiles != nullptr)
        {
            const FileSource* files = mFiles;
            probe = [files](const std::string& path) { return files->exists("textures\\" + path); };
        }
        mWeatherProbe = probe;
        mWeatherTable = std::make_unique<WeatherTable>(*mWeatherIndex, probe);
        return true;
    }

    bool SkyrimWorldView::describeWeathers(
        std::vector<openeden::sdk::game::WeatherDescription>& out, std::string& error)
    {
        out.clear();
        if (!ensureWeather())
        {
            error = "the world index is not available";
            return false;
        }
        out = mWeatherTable->weathers();
        if (out.empty())
        {
            error = "the plugins hold no weather";
            return false;
        }
        return true;
    }

    bool SkyrimWorldView::describeTimeOfDay(openeden::sdk::game::TimeOfDayDescription& out)
    {
        return ensureWeather() && game_skyrimse::describeTimeOfDay(*mWeatherIndex, out, mWeatherProbe);
    }

    bool SkyrimWorldView::weatherListForCell(openeden::sdk::game::WorldId cell, openeden::sdk::game::CellWeatherList& out)
    {
        out = {};
        return ensureWeather() && mWeatherTable->listForCell(static_cast<FormId>(cell), out);
    }

    // ---- the worker -------------------------------------------------------------------------------------------------

    void SkyrimWorldView::buildTerrainIndex() const
    {
        auto index = std::make_unique<SkyrimTerrainIndex>();
        for (const PluginData& plugin : mPlugins)
        {
            std::string error;
            if (!index->addPluginFile(plugin.mPath, mOrder, error) && mOptions.mNotice)
                mOptions.mNotice("terrain index: " + error);
        }
        mTerrain = std::move(index);
    }

    const SkyrimTerrainIndex* SkyrimWorldView::terrainIndex() const
    {
        if (!awaitIndex())
            return nullptr;
        if (mTerrainState.load(std::memory_order_acquire) == 1)
        {
            // The worker is on it (it started before the cell index was published): wait, it was going to be needed.
            std::unique_lock<std::mutex> lock(mBuildMutex);
            mBuildCv.wait(lock, [this] { return mTerrainState.load(std::memory_order_acquire) != 1; });
        }
        if (!mTerrain)
            buildTerrainIndex();
        return mTerrain.get();
    }

    bool SkyrimWorldView::awaitIndex() const
    {
        if (!mBuilding.load(std::memory_order_acquire))
            return !mFailed.load(std::memory_order_acquire); // a cancelled or failed build is never read
        if (std::this_thread::get_id() == mWorkerId.load())
            return true; // the worker is the only user while it builds
        const auto started = std::chrono::steady_clock::now();
        bool done;
        {
            std::unique_lock<std::mutex> lock(mBuildMutex);
            done = mBuildCv.wait_for(lock, mOptions.mIndexWait, [this] { return !mBuilding.load(); });
        }
        const auto waited = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        mWaits.fetch_add(1);
        std::uint64_t longest = mLongestWaitMillis.load();
        while (waited > longest && !mLongestWaitMillis.compare_exchange_weak(longest, waited))
        {
        }
        if (done && mFailed.load(std::memory_order_acquire))
            return false;
        if (done)
        {
            // Said once: the first wait, with how long the caller was blocked.
            bool expected = false;
            if (mOptions.mNotice && mWaitReported.compare_exchange_strong(expected, true))
                mOptions.mNotice("a query waited " + std::to_string(waited) + " ms for the world index to finish");
            return true;
        }
        bool expected = false;
        if (mOptions.mNotice && mTimeoutReported.compare_exchange_strong(expected, true))
            mOptions.mNotice("the world index was not ready after " + std::to_string(waited)
                + " ms; queries answer 'nothing' until it is");
        return false;
    }

    bool SkyrimWorldView::startLoad(
        std::vector<std::filesystem::path> paths, std::function<void(const LoadReport&)> onBuilt)
    {
        if (mBuilding.load() || !mPlugins.empty())
            return false;
        if (mWorker.joinable())
            mWorker.join(); // a finished earlier load whose thread is only waiting to be joined
        mCancel.store(false);
        mFailed.store(false);
        mBuilding.store(true, std::memory_order_release);
        mWorker = std::thread([this, pathList = std::move(paths), whenBuilt = std::move(onBuilt)]() {
            mWorkerId.store(std::this_thread::get_id());
            LoadReport report;
            report.mGiven = pathList.size();
            const auto started = std::chrono::steady_clock::now();
            try
            {
                for (const std::filesystem::path& path : pathList)
                {
                    if (mCancel.load())
                    {
                        report.mAborted = true;
                        break;
                    }
                    std::string error;
                    if (addPluginUnchecked(path, error))
                        ++report.mTaken;
                    else
                        report.mProblems.push_back(error);
                }
                if (!report.mAborted)
                    finish();
                report.mElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started);
                if (report.mAborted)
                    mFailed.store(true); // no finish(): the lookups were never built
                if (whenBuilt)
                    whenBuilt(report);
            }
            catch (const std::exception& e)
            {
                // The index may be incomplete: queries answer "nothing" from now on (a session error would need a
                // channel to the lifecycle that the pack does not have from a worker).
                mFailed.store(true);
                if (mOptions.mNotice)
                    mOptions.mNotice(std::string("the world index build failed: ") + e.what());
            }
            catch (...)
            {
                mFailed.store(true);
                if (mOptions.mNotice)
                    mOptions.mNotice("the world index build failed with an unknown error");
            }
            mWorkerId.store(std::thread::id{}); // a finished thread's id may be handed to a new thread
            // Task 0125: the terrain index (LAND and LTEX of every plugin, about 2.5 s on the five masters) is built
            // here, after the cell index is published, so that the first exterior door finds it done instead of
            // building it on the main thread.
            const bool buildTerrain = !mFailed.load() && !mCancel.load();
            if (buildTerrain)
                mTerrainState.store(1, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(mBuildMutex);
                mBuilding.store(false, std::memory_order_release);
            }
            mBuildCv.notify_all();
            if (buildTerrain)
            {
                try
                {
                    buildTerrainIndex();
                    mTerrainFromWorker.store(true, std::memory_order_release);
                }
                catch (...)
                {
                    mTerrain.reset(); // terrainIndex() builds it on demand, as before
                }
                {
                    std::lock_guard<std::mutex> lock(mBuildMutex);
                    mTerrainState.store(2, std::memory_order_release);
                }
                mBuildCv.notify_all();
            }
        });
        return true;
    }

    void SkyrimWorldView::cancelAndJoin()
    {
        if (std::this_thread::get_id() == mWorkerId.load() && mBuilding.load())
            return; // never join oneself
        mCancel.store(true);
        if (mWorker.joinable())
            mWorker.join();
    }

    bool SkyrimWorldView::configure(Options options, const FileSource* files)
    {
        if (mBuilding.load() || !mPlugins.empty())
            return false;
        mOptions = std::move(options);
        mOptions.mLimits.mSkipReferences = true;
        mFiles = files;
        return true;
    }

    void SkyrimWorldView::noteError(Content::Error error)
    {
        ++mStats.mRecordErrors;
        if (mErrors.size() < kMaxErrorsKept)
            mErrors.push_back(std::move(error));
    }

    // ---- indexing ---------------------------------------------------------------------------------------------------

    bool SkyrimWorldView::addPlugin(const std::filesystem::path& path, std::string& error)
    {
        if (!awaitIndex())
        {
            error = path.filename().string() + ": the world index is being built";
            return false;
        }
        return addPluginUnchecked(path, error);
    }

    bool SkyrimWorldView::addPluginUnchecked(const std::filesystem::path& path, std::string& error)
    {
        mFinished = false;
        mOptions.mLimits.mSkipReferences = true;
        Content::Error openError;
        const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(path, openError);
        if (plugin == nullptr)
        {
            ++mStats.mPluginsRefused;
            error = openError.describe();
            return false;
        }
        if (!isSkyrimSeHeader(plugin->header()))
        {
            ++mStats.mPluginsRefused;
            error = plugin->name() + ": this plugin is not a Skyrim Special Edition one";
            return false;
        }
        std::string orderError;
        if (!mOrder.add(plugin->name(), plugin->header().isLight(), orderError))
        {
            ++mStats.mPluginsRefused;
            error = plugin->name() + ": " + orderError;
            return false;
        }
        PluginData data;
        data.mPath = path;
        data.mName = plugin->name();
        (void)parseRecords(*plugin, mOrder, mOptions.mLimits, data.mParsed);
        mPlugins.push_back(std::move(data));
        ++mStats.mPlugins;
        indexPlugin(static_cast<std::uint32_t>(mPlugins.size() - 1));
        return true;
    }

    std::size_t SkyrimWorldView::load(
        const std::vector<std::filesystem::path>& paths, std::vector<std::string>& problems)
    {
        std::size_t taken = 0;
        for (const std::filesystem::path& path : paths)
        {
            std::string error;
            if (addPlugin(path, error))
                ++taken;
            else
                problems.push_back(error);
        }
        finish();
        return taken;
    }

    void SkyrimWorldView::resolveNames(PluginData& plugin, const StringTable* table)
    {
        ParsedPlugin& p = plugin.mParsed;
        const auto resolve = [&](LString& s) {
            if (table != nullptr && s.mLocalized)
                (void)lookupLString(s, *table);
        };
        for (WorldRecord& w : p.mWorlds)
            resolve(w.mName);
        for (CellRecord& c : p.mCells)
            resolve(c.mName);
        for (NpcRecord& n : p.mNpcs)
            resolve(n.mName);
        for (ItemRecord& i : p.mItems)
            resolve(i.mName);
    }

    void SkyrimWorldView::indexPlugin(std::uint32_t pluginIndex)
    {
        PluginData& plugin = mPlugins[pluginIndex];
        ParsedPlugin& p = plugin.mParsed;

        mStats.mUnresolvedFormIds += p.mUnresolvedFormIds;
        for (const Content::Error& e : p.mErrors)
            noteError(e);
        if (p.mWalkError)
            noteError(*p.mWalkError);

        // Names of a localised plugin come from its string tables.
        StringTable table;
        bool haveTable = false;
        if (p.mHeader.isLocalized())
        {
            if (mFiles != nullptr)
            {
                std::string bytes;
                const std::string tablePath
                    = stringTablePath(stemOf(plugin.mPath), mOptions.mLanguage, StringTableKind::Strings);
                if (mFiles->read(tablePath, bytes))
                {
                    Content::Error tableError;
                    haveTable = StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() },
                        StringTableKind::Strings, plugin.mName, table, tableError);
                    if (!haveTable)
                        noteError(tableError);
                }
            }
            if (!haveTable)
                ++mStats.mStringTablesMissing;
        }
        resolveNames(plugin, haveTable ? &table : nullptr);

        for (std::uint32_t i = 0; i < p.mWorlds.size(); ++i)
        {
            const WorldRecord& w = p.mWorlds[i];
            if (w.mCommon.mId == kNullFormId || w.mCommon.mId == kUnresolvedFormId)
                continue;
            WorldEntry& entry = mWorlds[w.mCommon.mId];
            entry.mInfo.id = w.mCommon.mId;
            if (!w.mCommon.mEditorId.empty())
                entry.mInfo.editorId = w.mCommon.mEditorId;
            if (!w.mName.mText.empty())
                entry.mInfo.name = w.mName.mText;
        }
        for (std::uint32_t i = 0; i < p.mCells.size(); ++i)
        {
            const CellRecord& c = p.mCells[i];
            if (c.mCommon.mId == kNullFormId || c.mCommon.mId == kUnresolvedFormId)
                continue;
            mCells[c.mCommon.mId].mSources.push_back({ pluginIndex, i });
        }
        for (const auto& [ref, cell] : p.mTeleportRefCells)
        {
            if (ref == kNullFormId || ref == kUnresolvedFormId || cell == kNullFormId || cell == kUnresolvedFormId)
                continue;
            mRefCells[ref] = cell; // later plugins win
            ++mStats.mTeleportDoorsIndexed;
        }
        for (std::uint32_t i = 0; i < p.mStatics.size(); ++i)
            mBases[p.mStatics[i].mCommon.mId] = { pluginIndex, BaseTable::Static, i };
        for (std::uint32_t i = 0; i < p.mModels.size(); ++i)
            mBases[p.mModels[i].mCommon.mId] = { pluginIndex, BaseTable::Model, i };
        for (std::uint32_t i = 0; i < p.mNpcs.size(); ++i)
            mBases[p.mNpcs[i].mCommon.mId] = { pluginIndex, BaseTable::Npc, i };
        for (std::uint32_t i = 0; i < p.mItems.size(); ++i)
            mBases[p.mItems[i].mCommon.mId] = { pluginIndex, BaseTable::Item, i };
        for (std::uint32_t i = 0; i < p.mLeveledLists.size(); ++i)
            mBases[p.mLeveledLists[i].mCommon.mId] = { pluginIndex, BaseTable::Leveled, i };
    }

    void SkyrimWorldView::finish()
    {
        mCellOrder.clear();
        mWorldOrder.clear();
        mCellByEditorId.clear();
        mCellByGrid.clear();
        mWorldByEditorId.clear();

        for (auto& [id, world] : mWorlds)
        {
            mWorldOrder.push_back(id);
            if (!world.mInfo.editorId.empty())
                mWorldByEditorId[lowerAscii(world.mInfo.editorId)] = id;
            world.mPersistentCell = kNullFormId;
        }

        // The winner's fields, with earlier plugins filling what the winner lacks.
        for (auto it = mCells.begin(); it != mCells.end();)
        {
            CellEntry& entry = it->second;
            const CellRecord& winner
                = mPlugins[entry.mSources.back().mPlugin].mParsed.mCells[entry.mSources.back().mIndex];
            if (winner.mCommon.deleted())
            {
                it = mCells.erase(it);
                continue;
            }
            sdkgame::WorldCellInfo info;
            info.id = it->first;
            bool haveFlags = false;
            for (auto s = entry.mSources.rbegin(); s != entry.mSources.rend(); ++s)
            {
                const CellRecord& c = mPlugins[s->mPlugin].mParsed.mCells[s->mIndex];
                if (info.editorId.empty())
                    info.editorId = c.mCommon.mEditorId;
                if (info.name.empty())
                    info.name = c.mName.mText;
                if (!info.hasGrid && c.mGrid)
                {
                    info.hasGrid = true;
                    info.gridX = c.mGrid->x;
                    info.gridY = c.mGrid->y;
                }
                if (info.world == sdkgame::kNoWorldId && c.mWorld != kNullFormId)
                    info.world = c.mWorld;
                if (!haveFlags)
                {
                    info.interior = c.interior() || c.mInteriorBlock >= 0;
                    entry.mWorldPersistent = c.mWorldPersistentCell;
                    haveFlags = true;
                }
            }
            if (info.interior)
                info.world = sdkgame::kNoWorldId;
            entry.mInfo = std::move(info);
            for (auto s = entry.mSources.rbegin(); s != entry.mSources.rend(); ++s)
            {
                const CellRecord& c = mPlugins[s->mPlugin].mParsed.mCells[s->mIndex];
                if (c.mLighting)
                {
                    entry.mLighting = c.mLighting;
                    break;
                }
            }
            if (entry.mWorldPersistent && entry.mInfo.world != sdkgame::kNoWorldId)
            {
                const auto w = mWorlds.find(entry.mInfo.world);
                if (w != mWorlds.end())
                    w->second.mPersistentCell = it->first;
            }
            ++it;
        }

        for (const auto& [id, entry] : mCells)
        {
            mCellOrder.push_back(id);
            if (!entry.mInfo.editorId.empty())
                mCellByEditorId[lowerAscii(entry.mInfo.editorId)] = id;
            if (!entry.mInfo.interior && entry.mInfo.hasGrid)
                mCellByGrid[{ entry.mInfo.world, entry.mInfo.gridX, entry.mInfo.gridY }] = id;
        }
        std::sort(mCellOrder.begin(), mCellOrder.end(), [this](FormId a, FormId b) {
            const sdkgame::WorldCellInfo& x = mCells.at(a).mInfo;
            const sdkgame::WorldCellInfo& y = mCells.at(b).mInfo;
            if (x.interior != y.interior)
                return x.interior; // interior cells first
            if (x.interior)
            {
                const std::string ex = lowerAscii(x.editorId);
                const std::string ey = lowerAscii(y.editorId);
                if (ex != ey)
                    return ex < ey;
                return a < b;
            }
            if (x.world != y.world)
                return x.world < y.world;
            if (x.hasGrid != y.hasGrid)
                return !x.hasGrid; // a world's persistent cell (no grid) before its squares
            if (x.gridY != y.gridY)
                return x.gridY < y.gridY;
            if (x.gridX != y.gridX)
                return x.gridX < y.gridX;
            return a < b;
        });

        mStats.mCells = mCells.size();
        mStats.mWorlds = mWorlds.size();
        mStats.mBaseForms = mBases.size();
        std::uint64_t inIndex = 0;
        for (const PluginData& plugin : mPlugins)
            for (const CellRecord& c : plugin.mParsed.mCells)
                inIndex += c.refCount();
        mStats.mReferencesInIndex = inIndex;
        mFinished = true;
    }

    // ---- queries ----------------------------------------------------------------------------------------------------

    const SkyrimWorldView::Stats& SkyrimWorldView::stats() const
    {
        static const Stats sEmpty;
        return awaitIndex() ? mStats : sEmpty;
    }

    const std::vector<Content::Error>& SkyrimWorldView::errors() const
    {
        static const std::vector<Content::Error> sEmpty;
        return awaitIndex() ? mErrors : sEmpty;
    }

    const RefCellIndex& SkyrimWorldView::teleportDoorCells() const
    {
        static const RefCellIndex sEmpty;
        return awaitIndex() ? mRefCells : sEmpty;
    }

    std::size_t SkyrimWorldView::cellCount() const
    {
        return awaitIndex() ? mCellOrder.size() : 0;
    }

    std::size_t SkyrimWorldView::worldCount() const
    {
        return awaitIndex() ? mWorldOrder.size() : 0;
    }

    std::size_t SkyrimWorldView::listCells(
        std::size_t first, std::size_t maxCount, std::vector<sdkgame::WorldCellInfo>& out) const
    {
        std::size_t appended = 0;
        if (!awaitIndex())
            return 0;
        for (std::size_t i = first; i < mCellOrder.size() && appended < maxCount; ++i, ++appended)
            out.push_back(mCells.at(mCellOrder[i]).mInfo);
        return appended;
    }

    std::size_t SkyrimWorldView::listWorlds(std::vector<sdkgame::WorldInfo>& out) const
    {
        if (!awaitIndex())
            return 0;
        for (const FormId id : mWorldOrder)
            out.push_back(mWorlds.at(id).mInfo);
        return mWorldOrder.size();
    }

    std::optional<sdkgame::WorldCellInfo> SkyrimWorldView::findCellByEditorId(std::string_view editorId) const
    {
        if (!awaitIndex())
            return std::nullopt;
        const auto it = mCellByEditorId.find(lowerAscii(editorId));
        if (it == mCellByEditorId.end())
            return std::nullopt;
        return mCells.at(it->second).mInfo;
    }

    std::optional<sdkgame::WorldCellInfo> SkyrimWorldView::findCellByGrid(
        sdkgame::WorldId world, std::int32_t x, std::int32_t y) const
    {
        if (!awaitIndex())
            return std::nullopt;
        const auto it = mCellByGrid.find({ world, x, y });
        if (it == mCellByGrid.end())
            return std::nullopt;
        return mCells.at(it->second).mInfo;
    }

    std::optional<sdkgame::WorldInfo> SkyrimWorldView::findWorldByEditorId(std::string_view editorId) const
    {
        if (!awaitIndex())
            return std::nullopt;
        const auto it = mWorldByEditorId.find(lowerAscii(editorId));
        if (it == mWorldByEditorId.end())
            return std::nullopt;
        return mWorlds.at(it->second).mInfo;
    }

    // ---- cells ------------------------------------------------------------------------------------------------------

    bool SkyrimWorldView::readCellRefs(
        const CellSource& source, const CellEntry&, std::vector<PlacedRef>& out, std::string& error)
    {
        const PluginData& plugin = mPlugins[source.mPlugin];
        const CellRecord& cell = plugin.mParsed.mCells[source.mIndex];
        if (!cell.mHasChildren)
            return true; // a cell without a children group has no references in this plugin

        // The file is read again from disk: header record, the cell record, the cell-children group.
        std::error_code ec;
        const std::uint64_t fileSize = std::filesystem::file_size(plugin.mPath, ec);
        if (ec)
        {
            error = plugin.mName + ": cannot read the file: " + ec.message();
            return false;
        }
        const std::uint64_t headerEnd = plugin.mParsed.mHeader.mFirstRecordOffset;
        const auto fits
            = [&](std::uint64_t offset, std::uint64_t size) { return offset <= fileSize && size <= fileSize - offset; };
        if (!fits(0, headerEnd) || !fits(cell.mCommon.mOffset, cell.mRecordSize)
            || !fits(cell.mChildrenOffset, cell.mChildrenSize))
        {
            error = plugin.mName
                + ": a cell's place in the file lies outside it (the file changed since it was indexed?)";
            return false;
        }
        const std::uint64_t total = headerEnd + cell.mRecordSize + cell.mChildrenSize;
        if (total > mOptions.mMaxCellBytes)
        {
            error = plugin.mName + ": a cell needs " + std::to_string(total) + " bytes, over the limit of "
                + std::to_string(mOptions.mMaxCellBytes);
            return false;
        }
        std::string bytes;
        bytes.resize(static_cast<std::size_t>(total));
        std::ifstream in(plugin.mPath, std::ios::binary);
        if (!in)
        {
            error = plugin.mName + ": cannot open the file";
            return false;
        }
        const auto readAt = [&](std::uint64_t offset, std::uint64_t size, std::size_t dest) {
            in.clear();
            in.seekg(static_cast<std::streamoff>(offset));
            in.read(bytes.data() + dest, static_cast<std::streamsize>(size));
            return in.gcount() == static_cast<std::streamsize>(size);
        };
        if (!readAt(0, headerEnd, 0) || !readAt(cell.mCommon.mOffset, cell.mRecordSize, headerEnd)
            || !readAt(cell.mChildrenOffset, cell.mChildrenSize, headerEnd + cell.mRecordSize))
        {
            error = plugin.mName + ": a read was cut short";
            return false;
        }
        mStats.mBytesRead += total;
        ++mStats.mCellReads;

        std::unique_ptr<Content::Plugin> mini;
        Content::Error openError;
        mini = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes, std::ios::binary), plugin.mName, openError);
        if (mini == nullptr)
        {
            error = openError.describe();
            return false;
        }
        ParseLimits limits = mOptions.mLimits;
        limits.mSkipReferences = false;
        ParsedPlugin parsed;
        (void)parseRecords(*mini, mOrder, limits, parsed);
        for (const Content::Error& e : parsed.mErrors)
            noteError(e);
        if (parsed.mWalkError)
            noteError(*parsed.mWalkError);
        if (parsed.mCells.empty())
        {
            error = plugin.mName + ": the cell record could not be read again";
            return false;
        }
        CellRecord& c = parsed.mCells.front();
        for (auto* list : { &c.mPersistent, &c.mTemporary, &c.mVisibleDistant, &c.mOther })
            for (PlacedRef& r : *list)
                out.push_back(std::move(r));
        return true;
    }

    const LightData* SkyrimWorldView::lightOfBase(FormId base) const
    {
        if (base == kNullFormId || base == kUnresolvedFormId)
            return nullptr;
        const auto it = mBases.find(base);
        if (it == mBases.end() || it->second.mTable != BaseTable::Model)
            return nullptr;
        const ModelRecord& m = mPlugins[it->second.mPlugin].mParsed.mModels[it->second.mIndex];
        return m.mLight ? &*m.mLight : nullptr;
    }

    sdkgame::PlacedObject SkyrimWorldView::makeObject(const PlacedRef& ref, bool& baseMissing) const
    {
        sdkgame::PlacedObject o;
        o.id = ref.mCommon.mId;
        o.position = { ref.mPosition.x, ref.mPosition.y, ref.mPosition.z };
        o.rotation = { ref.mRotation.x, ref.mRotation.y, ref.mRotation.z };
        o.scale = finite(ref.mScale) && ref.mScale > 0.0f ? ref.mScale : 1.0f;
        o.persistent = ref.persistent() || ref.mGroup == RefGroup::Persistent;
        o.initiallyDisabled = ref.initiallyDisabled();
        baseMissing = false;
        if (ref.mBase == kNullFormId || ref.mBase == kUnresolvedFormId)
        {
            baseMissing = true;
            return o;
        }
        o.base = ref.mBase;
        const auto it = mBases.find(ref.mBase);
        if (it == mBases.end())
        {
            baseMissing = true;
            return o;
        }
        const ParsedPlugin& p = mPlugins[it->second.mPlugin].mParsed;
        const auto bounds = [&](const std::optional<std::array<std::int16_t, 6>>& b) {
            if (!b)
                return;
            o.hasBounds = true;
            o.boundsMin = { static_cast<float>((*b)[0]), static_cast<float>((*b)[1]), static_cast<float>((*b)[2]) };
            o.boundsMax = { static_cast<float>((*b)[3]), static_cast<float>((*b)[4]), static_cast<float>((*b)[5]) };
        };
        switch (it->second.mTable)
        {
            case BaseTable::Static:
            {
                const StaticRecord& s = p.mStatics[it->second.mIndex];
                o.baseKind = sdkgame::BaseKind::Static;
                o.baseEditorId = s.mCommon.mEditorId;
                o.modelPath = s.mModel;
                bounds(s.mBounds);
                break;
            }
            case BaseTable::Model:
            {
                const ModelRecord& m = p.mModels[it->second.mIndex];
                o.baseKind = kindOfModelType(m.mType);
                o.baseEditorId = m.mCommon.mEditorId;
                o.modelPath = m.mModel;
                bounds(m.mBounds);
                break;
            }
            case BaseTable::Npc:
                o.baseKind = sdkgame::BaseKind::Actor;
                o.baseEditorId = p.mNpcs[it->second.mIndex].mCommon.mEditorId;
                break;
            case BaseTable::Item:
            {
                const ItemRecord& i = p.mItems[it->second.mIndex];
                o.baseKind = sdkgame::BaseKind::Item;
                o.baseEditorId = i.mCommon.mEditorId;
                o.modelPath = i.mModel;
                break;
            }
            case BaseTable::Leveled:
                o.baseKind = sdkgame::BaseKind::LeveledList;
                o.baseEditorId = p.mLeveledLists[it->second.mIndex].mCommon.mEditorId;
                break;
        }
        return o;
    }

    std::shared_ptr<const SkyrimWorldView::Materialised> SkyrimWorldView::materialise(FormId cell, std::string& error)
    {
        for (const auto& [id, content] : mCache)
        {
            if (id == cell)
            {
                ++mStats.mCacheHits;
                return content;
            }
        }
        const auto entryIt = mCells.find(cell);
        if (entryIt == mCells.end())
        {
            error = "there is no such cell";
            return nullptr;
        }
        const CellEntry& entry = entryIt->second;

        // Merge the placements of every plugin that touches the cell, in load order.
        std::map<FormId, PlacedRef> merged;
        for (const CellSource& source : entry.mSources)
        {
            std::vector<PlacedRef> refs;
            if (!readCellRefs(source, entry, refs, error))
                return nullptr;
            mStats.mReferencesDecoded += refs.size();
            for (PlacedRef& r : refs)
            {
                const FormId id = r.mCommon.mId;
                if (id == kNullFormId || id == kUnresolvedFormId)
                    continue;
                if (r.mCommon.deleted())
                    merged.erase(id);
                else
                    merged[id] = std::move(r);
            }
        }

        auto result = std::make_shared<Materialised>();
        for (auto& [id, ref] : merged)
        {
            if (result->mObjects.size() >= mOptions.mMaxObjectsPerCell)
            {
                ++mStats.mObjectsDropped;
                continue;
            }
            bool missing = false;
            result->mObjects.push_back(makeObject(ref, missing));
            PlacementComponents components = makeComponents(ref, mRefCells, mStats.mComponents, NeutralLimits{});
            if (const LightData* light = lightOfBase(ref.mBase))
                components.light = makeLightComponent(*light, mStats.mComponents, NeutralLimits{});
            if (!components.empty())
                result->mComponents.emplace(id, std::move(components));
            if (missing)
                ++result->mUnresolvedBases;
        }

        mCache.emplace_back(cell, result);
        while (mCache.size() > std::max<std::size_t>(mOptions.mMaxCachedCells, 1))
            mCache.pop_front();
        return result;
    }

    const PlacementComponents* SkyrimWorldView::find(sdkgame::WorldId cell, sdkgame::WorldId placement) const
    {
        if (!awaitIndex() || cell == kNullFormId || cell != mLastCell)
            return nullptr;
        const auto it = mLastComponents.find(placement);
        return it != mLastComponents.end() ? &it->second : nullptr;
    }

    std::optional<sdkgame::CellLighting> SkyrimWorldView::findCellLighting(sdkgame::WorldId cell) const
    {
        if (!awaitIndex() || cell == kNullFormId || cell != mLastCell)
            return std::nullopt;
        return mLastLighting;
    }

    bool SkyrimWorldView::loadCell(sdkgame::WorldId cell, sdkgame::WorldCellContent& out, std::string& error)
    {
        out = sdkgame::WorldCellContent{};
        error.clear();
        if (!awaitIndex())
        {
            error = "the world index is not ready";
            return false;
        }
        mLastCell = kNullFormId;
        mLastComponents.clear();
        mLastLighting.reset();
        try
        {
            const auto entryIt = mCells.find(cell);
            if (entryIt == mCells.end())
            {
                error = "there is no cell with this id";
                return false;
            }
            const CellEntry& entry = entryIt->second;
            const std::shared_ptr<const Materialised> own = materialise(cell, error);
            if (own == nullptr)
                return false;

            sdkgame::WorldCellContent content;
            content.cell = entry.mInfo;
            content.objects = own->mObjects;
            content.unresolvedBases = own->mUnresolvedBases;
            std::map<FormId, PlacementComponents> components = own->mComponents;

            // The world's persistent placements that stand in this exterior square.
            if (!entry.mInfo.interior && entry.mInfo.hasGrid && !entry.mWorldPersistent)
            {
                const auto world = mWorlds.find(entry.mInfo.world);
                if (world != mWorlds.end() && world->second.mPersistentCell != kNullFormId
                    && world->second.mPersistentCell != cell)
                {
                    std::string persistentError;
                    const std::shared_ptr<const Materialised> persistent
                        = materialise(world->second.mPersistentCell, persistentError);
                    if (persistent == nullptr)
                    {
                        error = persistentError;
                        return false;
                    }
                    for (const sdkgame::PlacedObject& o : persistent->mObjects)
                    {
                        if (!finite(o.position.x) || !finite(o.position.y))
                            continue;
                        if (static_cast<std::int32_t>(std::floor(o.position.x / kCellSize)) == entry.mInfo.gridX
                            && static_cast<std::int32_t>(std::floor(o.position.y / kCellSize)) == entry.mInfo.gridY)
                        {
                            content.objects.push_back(o);
                            if (const auto c = persistent->mComponents.find(o.id); c != persistent->mComponents.end())
                                components.emplace(o.id, c->second);
                            if (o.baseKind == sdkgame::BaseKind::Unknown)
                                ++content.unresolvedBases;
                        }
                    }
                    std::sort(content.objects.begin(), content.objects.end(),
                        [](const sdkgame::PlacedObject& a, const sdkgame::PlacedObject& b) { return a.id < b.id; });
                }
            }
            content.cell.objectCount = static_cast<std::uint32_t>(content.objects.size());
            out = std::move(content);
            mLastCell = cell;
            mLastComponents = std::move(components);
            if (entry.mLighting && entry.mInfo.interior)
                mLastLighting = makeCellLighting(*entry.mLighting);
            return true;
        }
        catch (const std::exception& e)
        {
            out = sdkgame::WorldCellContent{};
            error = std::string("the cell could not be loaded: ") + e.what();
            return false;
        }
    }
}
