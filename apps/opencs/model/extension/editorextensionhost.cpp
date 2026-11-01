#include "editorextensionhost.hpp"

#include <algorithm>
#include <set>

#include <components/debug/debuglog.hpp>
#include <components/extension/extension_loader.hpp>

namespace
{
    using openeden::extension::DiscoveredExtension;
    using openeden::extension::ExtensionLoadError;
}

CSMExtension::EditorExtensionHost::EditorExtensionHost(const Config& config)
    : mLifecycle(mHost)
{
    namespace extension = openeden::extension;

    mHost.settingsService().useEngineSettings();

    const auto isEnabled = [&](const std::string& id) {
        return std::find(config.mEnabled.begin(), config.mEnabled.end(), id) != config.mEnabled.end();
    };

    std::vector<ExtensionLoadError> errors;
    const std::vector<DiscoveredExtension> discovered
        = extension::discoverExtensionDirectories(config.mSearchDirs, &errors);

    // A folder whose manifest could not be read: always listed, so that the user can see what is wrong.
    for (const ExtensionLoadError& error : errors)
    {
        ExtensionInfo info;
        info.mId = error.extensionId;
        info.mState = ExtensionInfo::State::Failed;
        info.mEnabled = isEnabled(error.extensionId);
        info.mMessage = error.stage + ": " + error.message;
        mInfos.push_back(std::move(info));
        Log(Debug::Error) << "Extension '" << error.extensionId << "' failed at " << error.stage << ": "
                          << error.message;
    }

    std::set<std::string> seen;
    for (const DiscoveredExtension& item : discovered)
    {
        const std::string& id = item.manifest.id;
        if (!seen.insert(id).second)
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
        info.mEnabled = isEnabled(id);

        if (!info.mEnabled)
        {
            Log(Debug::Info) << "Extension '" << id << "' is installed but not enabled";
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
        mInfos.push_back(std::move(info));
    }

    for (const std::string& id : config.mEnabled)
    {
        if (seen.find(id) != seen.end())
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
}

CSMExtension::EditorExtensionHost::~EditorExtensionHost()
{
    stop();
}

void CSMExtension::EditorExtensionHost::start()
{
    if (!mAnyLoaded || mRunning)
        return;
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
        const auto failure = std::find_if(failures.begin(), failures.end(),
            [&](const auto& item) { return item.extensionId == info.mId; });
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
}

std::vector<CSMExtension::ExtensionInfo> CSMExtension::EditorExtensionHost::extensions() const
{
    return mInfos;
}

std::string CSMExtension::EditorExtensionHost::summary() const
{
    const auto active = std::count_if(
        mInfos.begin(), mInfos.end(), [](const ExtensionInfo& info) { return info.mState == ExtensionInfo::State::Active; });
    const auto failed = std::count_if(
        mInfos.begin(), mInfos.end(), [](const ExtensionInfo& info) { return info.mState == ExtensionInfo::State::Failed; });
    std::string text = std::to_string(active) + " of " + std::to_string(mInfos.size()) + " extensions active";
    if (failed > 0)
        text += ", " + std::to_string(failed) + " failed";
    return text;
}
