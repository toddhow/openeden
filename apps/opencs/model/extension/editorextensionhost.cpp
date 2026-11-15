#include "editorextensionhost.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>

#include <components/debug/debuglog.hpp>
#include <components/extension/extension_loader.hpp>
#include <components/extension/physics_service.hpp>
#include <components/gamesession/gamesession.hpp>
#include <components/worldmodel/store.hpp>

#include "../physics/studioworld.hpp"

namespace
{
    using openeden::extension::DiscoveredExtension;
    using openeden::extension::ExtensionLoadError;

    Debug::Level toDebugLevel(openeden::sdk::LogLevel level)
    {
        switch (level)
        {
            case openeden::sdk::LogLevel::Trace:
                return Debug::Debug;
            case openeden::sdk::LogLevel::Debug:
                return Debug::Verbose;
            case openeden::sdk::LogLevel::Info:
                return Debug::Info;
            case openeden::sdk::LogLevel::Warning:
                return Debug::Warning;
            case openeden::sdk::LogLevel::Error:
            case openeden::sdk::LogLevel::Fatal:
                return Debug::Error;
        }
        return Debug::Info;
    }

    /// An extension's own log lines go to the editor's log (and so to the Output Log panel). formattedMessage already
    /// has the [extensionId] prefix. The editor does not send its lines to extensions, so there is no loop to guard.
    class EditorLogSink final : public openeden::sdk::LogSink
    {
    public:
        void log(std::string_view, openeden::sdk::LogLevel level, std::string_view formattedMessage) override
        {
            Log(toDebugLevel(level)) << formattedMessage;
        }
    };
}

CSMExtension::EditorExtensionHost::EditorExtensionHost(const Config& config)
    : mLifecycle(mHost)
{
    namespace extension = openeden::extension;

    mHost.settingsService().useEngineSettings();

    // The session's rules, shared with the game (task 0173, components/gamesession). A built-in game pack (Morrowind)
    // is satisfied without loading anything: the editor works on its structs directly.
    mGamePackId = config.mGamePackId;
    mGamePackBuiltIn = !mGamePackId.empty()
        && std::find(config.mBuiltInGamePacks.begin(), config.mBuiltInGamePacks.end(), mGamePackId)
            != config.mBuiltInGamePacks.end();
    const GameSession::Rules rules{ config.mEnabled, mGamePackId };
    const auto isEnabled = [&](const std::string& id) { return GameSession::isEnabled(rules, id); };

    std::vector<ExtensionLoadError> errors;
    const std::vector<DiscoveredExtension> discovered
        = extension::discoverExtensionDirectories(config.mSearchDirs, &errors);

    // A folder whose manifest could not be read: always listed, so that the user can see what is wrong.
    for (const ExtensionLoadError& error : errors)
    {
        ExtensionInfo info;
        info.mId = error.extensionId;
        info.mEnabled = isEnabled(error.extensionId);
        // Only a broken extension that was asked for counts as a failure (as in the game); others are just listed.
        info.mState = info.mEnabled ? ExtensionInfo::State::Failed : ExtensionInfo::State::Installed;
        info.mMessage = error.stage + ": " + error.message;
        mInfos.push_back(std::move(info));
        Log(Debug::Error) << "Extension '" << error.extensionId << "' failed at " << error.stage << ": "
                          << error.message;
    }

    std::set<std::string> seen;
    const std::vector<GameSession::ItemVerdict> verdicts = GameSession::classify(discovered, rules);
    for (std::size_t index = 0; index < discovered.size(); ++index)
    {
        const DiscoveredExtension& item = discovered[index];
        const std::string& id = item.manifest.id;
        const GameSession::ItemVerdict& verdict = verdicts[index];
        seen.insert(id);
        if (verdict.mVerdict == GameSession::Verdict::Duplicate)
        {
            Log(Debug::Warning) << "Extension '" << id << "' in " << item.directory
                                << " ignored: found earlier in another extension directory";
            continue;
        }

        ExtensionInfo info;
        info.mId = id;
        info.mName = item.manifest.name;
        info.mVersion = item.manifest.extensionVersion;
        info.mDirectory = item.directory;
        info.mEditor = item.manifest.editor;
        info.mEnabled = isEnabled(id);
        info.mGamePack = !mGamePackId.empty() && id == mGamePackId;

        if (verdict.mVerdict == GameSession::Verdict::NotEnabled)
        {
            Log(Debug::Info) << "Extension '" << id << "' is installed but not enabled";
            mInfos.push_back(std::move(info));
            continue;
        }

        // One game pack per session, chosen by game= (task 0084), as in the game
        if (verdict.mVerdict == GameSession::Verdict::RefusedGamePack)
        {
            Log(Debug::Error) << "Game pack '" << id << "' not loaded: " << verdict.mReason;
            info.mState = ExtensionInfo::State::Failed;
            info.mMessage = "game pack not loaded, " + verdict.mReason;
            mInfos.push_back(std::move(info));
            continue;
        }

        // A built-in game pack is not started, even when a folder with its id is installed
        if (info.mGamePack && mGamePackBuiltIn)
        {
            Log(Debug::Info) << "Game pack '" << id << "' is built in; the installed copy is not loaded";
            mInfos.push_back(std::move(info));
            continue;
        }

        Log(Debug::Info) << "Loading extension '" << id << "' from " << item.directory;
        extension::ExtensionLoadResult result = extension::loadExtensionLibrary(item, mHost.getHostInfo());
        if (!result.success)
        {
            Log(Debug::Error) << "Extension '" << id << "' failed at " << result.error.stage << ": "
                              << result.error.message;
            info.mState = ExtensionInfo::State::Failed;
            info.mMessage = result.error.stage + ": " + result.error.message;
            mInfos.push_back(std::move(info));
            continue;
        }

        // Loaded; start() decides between Active and Failed. Until then it counts as Installed.
        mLifecycle.addExtension(std::move(result.extension.manifest), std::move(result.extension.instance),
            std::move(result.extension.library));
        mAnyLoaded = true;
        if (info.mGamePack)
            mGamePackLoaded = true;
        mInfos.push_back(std::move(info));
    }

    // The chosen game pack (task 0173): not installed is a failure with the game's text. Loaded, it gets the neutral
    // world store its world source goes into (decision 0016), and is told that something reads sources.
    if (!mGamePackId.empty() && !mGamePackBuiltIn && seen.find(mGamePackId) == seen.end())
    {
        Log(Debug::Error) << "Game pack '" << mGamePackId << "' (game=) is not installed";
        ExtensionInfo info;
        info.mId = mGamePackId;
        info.mEnabled = true;
        info.mGamePack = true;
        info.mState = ExtensionInfo::State::Failed;
        info.mMessage = "the game pack chosen by game= is not installed";
        mInfos.push_back(std::move(info));
    }
    if (mGamePackLoaded)
    {
        mNeutralWorldStore = std::make_unique<NeutralWorld::NeutralWorldStore>();
        mStoreListener = std::make_unique<GameSession::StoreListener>(*mNeutralWorldStore);
        mHost.worldSources().setListener(mStoreListener.get());
        mHost.worldSources().setConsumerEnabled(true);
    }

    for (const std::string& id : config.mEnabled)
    {
        if (seen.find(id) != seen.end() || id == mGamePackId)
            continue;
        const bool brokenFolder = std::any_of(
            errors.begin(), errors.end(), [&](const ExtensionLoadError& error) { return error.extensionId == id; });
        if (brokenFolder)
            continue; // already listed with its reason
        Log(Debug::Warning) << "Extension '" << id << "' is enabled but not installed";
        ExtensionInfo info;
        info.mId = id;
        info.mEnabled = true;
        info.mState = ExtensionInfo::State::Failed;
        info.mMessage = "enabled, but no extension with this ID is installed";
        mInfos.push_back(std::move(info));
    }

    // The game pack's row first (the Extension Manager shows it at the top), the others in the loader's order
    std::stable_partition(mInfos.begin(), mInfos.end(), [](const ExtensionInfo& info) { return info.mGamePack; });
}

CSMExtension::EditorExtensionHost::~EditorExtensionHost()
{
    stop();
}

void CSMExtension::EditorExtensionHost::attachPhysicsWorld(JPH::PhysicsSystem& world, float unitsPerMeter)
{
    if (mRunning)
        throw std::logic_error("EditorExtensionHost::attachPhysicsWorld: extensions are already running");
    mHost.attachPhysicsWorld(world, unitsPerMeter);
    openeden::extension::PhysicsServiceImpl& queries = *mHost.physicsService();
    // The same names and values as the game's host (apps/openmw/extensionhost.cpp) for the layers the editor has.
    queries.publishQueryMask("world", CSMPhysics::StudioWorld::sLayerWorld);
    queries.publishQueryMask("dynamic_world", CSMPhysics::StudioWorld::sLayerDynamicWorld);
}

openeden::sdk::PhysicsQueryService* CSMExtension::EditorExtensionHost::getPhysicsQueryService()
{
    return mHost.getPhysicsQueryService();
}

void CSMExtension::EditorExtensionHost::beforeStep(float dt)
{
    if (!mRunning)
        return;
    mLifecycle.runPhase(openeden::sdk::Phase::PreSimulation, dt);
}

void CSMExtension::EditorExtensionHost::afterStep(float dt)
{
    if (!mRunning)
        return;
    mLifecycle.runPhase(openeden::sdk::Phase::PostSimulation, dt);
    mLifecycle.runPhase(openeden::sdk::Phase::Ui, dt);
}

void CSMExtension::EditorExtensionHost::start()
{
    if (!mAnyLoaded || mRunning)
        return;
    mHost.loggingService().addSink(std::make_shared<EditorLogSink>());
    mRunning = true;
    mLifecycle.initialize();

    const auto& failures = mLifecycle.getFailures();
    for (ExtensionInfo& info : mInfos)
    {
        if (!info.mEnabled || info.mState == ExtensionInfo::State::Failed)
            continue;
        if (mLifecycle.isExtensionActive(info.mId))
        {
            info.mState = ExtensionInfo::State::Active;
            Log(Debug::Info) << "Extension '" << info.mId << "' started";
            continue;
        }
        info.mState = ExtensionInfo::State::Failed;
        const auto failure = std::find_if(
            failures.begin(), failures.end(), [&](const auto& item) { return item.extensionId == info.mId; });
        info.mMessage = failure != failures.end() ? failure->stage + ": " + failure->reason : "not started";
        Log(Debug::Error) << "Extension '" << info.mId << "' is disabled: " << info.mMessage;
    }
}

void CSMExtension::EditorExtensionHost::stop()
{
    if (!mAnyLoaded)
        return;
    mAnyLoaded = false;
    mRunning = false;
    Log(Debug::Info) << "Stopping extensions";
    mLifecycle.shutdown();
    mHost.vfsService().setEngineVfs(nullptr);
}

std::vector<CSMModule::ModuleSource> CSMExtension::EditorExtensionHost::editorModuleSources() const
{
    std::vector<CSMModule::ModuleSource> sources;
    for (const std::string& id : mLifecycle.getLoadOrder())
    {
        const auto info = std::find_if(mInfos.begin(), mInfos.end(), [&](const ExtensionInfo& item) {
            return item.mId == id && item.mState == ExtensionInfo::State::Active && item.mEditor.has_value();
        });
        if (info != mInfos.end())
            sources.push_back(
                { info->mId, info->mDirectory / info->mEditor->library, CSMModule::ModuleSource::Origin::Extension });
    }
    return sources;
}

std::vector<CSMExtension::ExtensionInfo> CSMExtension::EditorExtensionHost::extensions() const
{
    return mInfos;
}

std::string CSMExtension::EditorExtensionHost::summary() const
{
    const auto active = std::count_if(mInfos.begin(), mInfos.end(),
        [](const ExtensionInfo& info) { return info.mState == ExtensionInfo::State::Active; });
    const auto failed = std::count_if(mInfos.begin(), mInfos.end(),
        [](const ExtensionInfo& info) { return info.mState == ExtensionInfo::State::Failed; });
    std::string text = std::to_string(active) + " of " + std::to_string(mInfos.size()) + " extensions active";
    if (failed > 0)
        text += ", " + std::to_string(failed) + " failed";
    return text;
}

std::string CSMExtension::EditorExtensionHost::gamePackName() const
{
    if (mGamePackId.empty())
        return {};
    if (mGamePackBuiltIn && mGamePackId == GameSession::sMorrowindId)
        return "Morrowind";
    for (const ExtensionInfo& info : mInfos)
    {
        if (info.mGamePack && !info.mName.empty())
            return info.mName;
    }
    return mGamePackId;
}

CSMExtension::EditorExtensionHost::GamePackState CSMExtension::EditorExtensionHost::gamePackState() const
{
    if (mGamePackId.empty())
        return GamePackState::None;
    if (mGamePackBuiltIn)
        return GamePackState::BuiltIn;
    for (const ExtensionInfo& info : mInfos)
    {
        if (!info.mGamePack)
            continue;
        if (info.mState == ExtensionInfo::State::Failed)
            return GamePackState::Failed;
        if (info.mState == ExtensionInfo::State::Active)
            return GamePackState::Active;
    }
    return mGamePackLoaded ? GamePackState::Loaded : GamePackState::Failed;
}

std::string CSMExtension::EditorExtensionHost::gamePackFailure() const
{
    if (gamePackState() != GamePackState::Failed)
        return {};
    for (const ExtensionInfo& info : mInfos)
    {
        // "<id>: <reason>", as the game reports its load failures (apps/openmw/extensionhost.cpp)
        if (info.mGamePack && info.mState == ExtensionInfo::State::Failed)
            return info.mId + ": " + info.mMessage;
    }
    return mGamePackId + ": the game pack did not load";
}

void CSMExtension::EditorExtensionHost::setSessionContent(
    std::vector<openeden::extension::SessionContentEntry> entries, const VFS::Manager* vfs)
{
    if (mNeutralWorldStore == nullptr || mRunning)
        return;
    mHost.vfsService().setEngineVfs(vfs);
    // As the game does (apps/openmw/extensionhost.cpp, setSessionContent): only what a registered content loader
    // accepts reaches the pack. The loaders register in start(), so the filter is applied when the pack asks.
    mHost.worldSources().setSessionContent(std::move(entries));
    mHost.worldSources().setContentFilter(
        [this](const openeden::sdk::ContentHeader& header) { return mHost.content().findLoader(header) != nullptr; });
}

NeutralWorld::NeutralWorldStore* CSMExtension::EditorExtensionHost::neutralWorldStore()
{
    return mNeutralWorldStore.get();
}
