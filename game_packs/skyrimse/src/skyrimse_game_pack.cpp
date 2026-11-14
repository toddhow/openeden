#include "game_skyrimse/skyrimse_game_pack.hpp"
#include "game_skyrimse/speech.hpp"

#include "game_skyrimse/actor_mechanics.hpp"
#include "game_skyrimse/neutral_world.hpp"
#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/world_view.hpp"

#include <openeden/extension.hpp>
#include <openeden/game/content.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/game/world_view.hpp>
#include <openeden/host.hpp>
#include <openeden/logging.hpp>

#include <openeden/settings.hpp>
#include <openeden/vfs.hpp>

#include <components/files/conversion.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace game_skyrimse
{
    namespace
    {
        // Owns the content loader it registers (the host keeps a reference until stop()), the world view and the
        // neutral world source over it (the source is declared after the view it refers to).
        class SkyrimGamePack final : public openeden::sdk::Extension
        {
        public:
            void registerServices(openeden::sdk::Registrar&) override {}

            bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }

            // The null game rules (design doc game-packs.md 5.5): the pack identifies Skyrim SE content, then refuses
            // the session. A game pack's failure in start() is a session error that names it and refuses saving, so
            // the player sees the message at the main menu; the editor and tools never start the pack and run.
            void start(openeden::sdk::Host& host) override
            {
                openeden::sdk::ContentService* const content = host.getContentService();
                if (content == nullptr)
                    throw std::runtime_error("the content service is missing");
                content->registerLoader(mLoader);
                // The read-only world view (openeden/game/world_view.hpp). It starts empty: the plugins that fill it
                // come from the game's content list, which no host hands to a pack yet (task 0086 stage log); a host
                // that knows the load order calls addPlugin / load on it.
                if (openeden::sdk::game::WorldViewService* const worldViews = host.getWorldViewService())
                {
                    worldViews->registerWorldView(mWorldView);
                    mWorldViews = worldViews;
                }
                // The neutral world source (task 0103, stage N3a-1): when the host reads sources (the engine with
                // [Game] neutral world), the pack hands it the source over its world view and the session goes on,
                // with no rules; the world view is still empty until a host loads plugins into it (the plugin list
                // reaches no pack yet). A host that does not read sources gets the old behaviour below.
                openeden::sdk::LoggingService* const logging = host.getLoggingService();
                openeden::sdk::Logger* const logger = logging != nullptr ? logging->getLogger(kPackId) : nullptr;
                openeden::sdk::game::WorldSourceService* const sources = host.getWorldSourceService();
                if (sources != nullptr && sources->consumerEnabled())
                {
                    // Task 0107 (stage N3a-2b): the view is filled from the session's content list before the source
                    // is registered over it. The list, the VFS for the string tables and the language setting all come
                    // from the host; none of this runs when the host does not read sources.
                    const auto startedAt = std::chrono::steady_clock::now();
                    loadSessionContent(host, *sources, logger);
                    std::string error;
                    if (!sources->registerSource(mCellSource, error))
                        throw std::runtime_error("the world source was refused: " + error);
                    mWorldSources = sources;
                    const auto startMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - startedAt);
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "pack start() work on the main thread: " + std::to_string(startMillis.count()) + " ms");
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "Skyrim SE world source registered; the rules are not implemented, so only the world is "
                            "available");
                    return;
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Warning, kRulesNotImplementedMessage);
                throw std::runtime_error(std::string(kRulesNotImplementedMessage));
            }

            void stop() override
            {
                // The index worker uses the logger and the file source: end it before anything is withdrawn.
                mWorldView.cancelAndJoin();
                withdrawWorldSource();
                withdrawWorldView();
            }

            ~SkyrimGamePack() override
            {
                mWorldView.cancelAndJoin();
                withdrawWorldSource();
                withdrawWorldView();
            }

        private:
            // Starts building the world view from the plugins of the session that this pack's content loader accepts,
            // on a worker thread (task 0109, decision 2): start() returns at once and the view's queries wait for the
            // worker if they come first (SkyrimWorldView, threading note). Plugins the view refuses are named once and
            // left out. Thread: this function runs on the main thread; the completion function runs on the worker.
            void loadSessionContent(openeden::sdk::Host& host, openeden::sdk::game::WorldSourceService& sources,
                openeden::sdk::Logger* logger)
            {
                auto say = [logger](openeden::sdk::LogLevel level, const std::string& text) {
                    if (logger != nullptr)
                        logger->log(level, text);
                };
                SkyrimWorldView::Options options;
                // The view tells us about waits and timeouts from whichever thread is affected; the logger has its own
                // lock (components/extension/logging_service.cpp). The logger outlives the worker (stop() joins first).
                options.mNotice = [logger](const std::string& text) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, text);
                };
                if (const openeden::sdk::SettingsService* settings = host.getSettingsService())
                {
                    const std::string wanted
                        = settings->getString(kPackId, kStringTableLanguageKey, kDefaultStringTableLanguage);
                    bool usedDefault = false;
                    options.mLanguage = sanitiseStringTableLanguage(wanted, usedDefault);
                    if (usedDefault)
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kStringTableLanguageKey)
                                + "' is not a language name (2 to 32 ASCII letters); using " + options.mLanguage);
                }
                if (const openeden::sdk::SettingsService* settings = host.getSettingsService())
                {
                    // Task 0135 stage S3: the test rule of the actor hooks, off unless the setting names an editor id.
                    const std::string walker = settings->getString(kPackId, kActorTestWalkKey, "");
                    if (!mCellSource.actorTable().setTestWalk(walker))
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kActorTestWalkKey) + "' is too long; the test walk is off");
                    else if (!walker.empty())
                        say(openeden::sdk::LogLevel::Info, "actor test walk: actors named " + walker + " pace");
                    // Stage S4: the go-to test rule.
                    const std::string goer = settings->getString(kPackId, kActorTestGotoKey, "");
                    if (!mCellSource.actorTable().setTestGoto(goer))
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kActorTestGotoKey)
                                + "' is not <editor id>,<x>,<y>,<z>; the test go-to is off");
                    else if (!goer.empty())
                        say(openeden::sdk::LogLevel::Info, "actor test goto: " + goer);
                    // Task 0154: the level an actor that scales with the player scales to (a new game's player is 1).
                    const int standInLevel = settings->getInt(kPackId, kPlayerLevelKey, kStandInPlayerLevel);
                    if (standInLevel >= 1 && standInLevel <= kMaxActorLevel)
                        mWorldView.setStandInPlayerLevel(static_cast<std::uint16_t>(standInLevel));
                    else
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kPlayerLevelKey) + "' is not 1 to 65535; using "
                                + std::to_string(kStandInPlayerLevel));
                    // Task 0155: the stand-in player of the speech checks.
                    const int speech = settings->getInt(kPackId, kPlayerSpeechKey, kStandInSkill);
                    if (speech >= 0 && speech <= 100)
                        mWorldView.setStandInPlayerSpeech(static_cast<std::uint8_t>(speech));
                    else
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kPlayerSpeechKey) + "' is not 0 to 100; using "
                                + std::to_string(kStandInSkill));
                    const int gold = settings->getInt(kPackId, kPlayerGoldKey, static_cast<int>(kStandInGold));
                    if (gold >= 0 && gold <= 100000000)
                        mWorldView.setStandInPlayerGold(static_cast<float>(gold));
                    else
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kPlayerGoldKey) + "' is not 0 to 100000000; using "
                                + std::to_string(static_cast<int>(kStandInGold)));
                    mWorldView.setStandInPlayerFemale(settings->getBool(kPackId, kPlayerFemaleKey, false));
                    // Stage S5: AI packages, off unless the setting says so (needs the world view, which is the
                    // packages' source).
                    if (settings->getBool(kPackId, kActorPackagesKey, false))
                    {
                        mCellSource.actorTable().enablePackages(mWorldView, PackageBrain::Options().mSeed, &mWorldView,
                            settings->getBool(kPackId, kActorSleepSnapKey, true));
                        say(openeden::sdk::LogLevel::Info, "actor packages: NPCs run their AI packages");
                        // Task 0160: GetIsCurrentPackage and GetOffersServicesNow in a conversation ask the brain
                        mWorldView.setCurrentPackageLookup([this](FormId reference) -> std::optional<FormId> {
                            const PackageBrain* brain = mCellSource.actorTable().packageBrain();
                            if (brain == nullptr)
                                return std::nullopt;
                            const auto view = brain->view(reference);
                            if (!view || view->mPhase == PackageBrain::Phase::Waiting)
                                return std::nullopt; // not evaluated yet: not known
                            return view->mActive != nullptr ? view->mActive->mCommon.mId : kNullFormId;
                        });
                    }
                }
                if (const openeden::sdk::VfsService* vfs = host.getVfsService())
                {
                    mFileSource = std::make_unique<VfsFileSource>(*vfs);
                }
                if (!mWorldView.configure(std::move(options), mFileSource.get()))
                    return;

                std::vector<openeden::sdk::game::SessionContentFile> files;
                const std::size_t given = sources.sessionContent(files);
                if (given == 0)
                    return; // an empty view, as before this stage
                std::vector<std::filesystem::path> paths;
                paths.reserve(files.size());
                for (const openeden::sdk::game::SessionContentFile& file : files)
                    paths.push_back(Files::pathFromUnicodeString(file.path));
                // Runs on the worker, after finish() and before the index is published to the main thread.
                const bool started = mWorldView.startLoad(
                    std::move(paths), [this, say, logger](const SkyrimWorldView::LoadReport& report) {
                        if (report.mAborted)
                            return; // stop() cancelled the load; nothing to tell
                        for (const std::string& problem : report.mProblems)
                            say(openeden::sdk::LogLevel::Warning, "plugin left out: " + problem);
                        const SkyrimWorldView::Stats& stats = mWorldView.stats();
                        say(openeden::sdk::LogLevel::Info,
                            "world view built from the session: " + std::to_string(report.mTaken) + " of "
                                + std::to_string(report.mGiven) + " plugins, " + std::to_string(stats.mCells)
                                + " cells, " + std::to_string(stats.mWorlds) + " worlds, "
                                + std::to_string(stats.mBaseForms) + " base forms, "
                                + std::to_string(stats.mStringTablesMissing) + " string tables missing, language "
                                + mWorldView.language() + ", " + std::to_string(report.mElapsed.count())
                                + " ms on a worker thread");
                        probeCell(logger);
                    });
                if (!started)
                    say(openeden::sdk::LogLevel::Warning, "the world view could not start building");
            }

            // A test aid: OPENEDEN_SKYRIMSE_PROBE_CELL names an editor id; the log says whether the cell is found and
            // how many objects it has. It never changes behaviour. Runs on the index worker (the one thread allowed to
            // use the view while it builds), so it does not delay the main thread.
            void probeCell(openeden::sdk::Logger* logger)
            {
                const char* const wanted = std::getenv("OPENEDEN_SKYRIMSE_PROBE_CELL");
                if (wanted == nullptr || *wanted == '\0' || logger == nullptr)
                    return;
                const openeden::sdk::game::WorldId id = mCellSource.findCellByEditorId(wanted);
                if (id == openeden::sdk::game::kNoWorldId)
                {
                    logger->log(openeden::sdk::LogLevel::Info, "probe cell found: no");
                    return;
                }
                openeden::sdk::game::WorldCell cell;
                std::string error;
                if (!mCellSource.buildCell(id, cell, error))
                {
                    logger->log(openeden::sdk::LogLevel::Info, "probe cell found: yes, but it did not build");
                    return;
                }
                logger->log(openeden::sdk::LogLevel::Info,
                    "probe cell found: yes, " + std::to_string(cell.objects.size()) + " objects, "
                        + std::to_string(cell.droppedObjects) + " dropped");
            }

            void withdrawWorldSource()
            {
                if (mWorldSources != nullptr)
                    mWorldSources->withdrawSource(mCellSource);
                mWorldSources = nullptr;
            }

            void withdrawWorldView()
            {
                if (mWorldViews != nullptr)
                    mWorldViews->withdrawWorldView(mWorldView);
                mWorldViews = nullptr;
            }

            SkyrimContentLoader mLoader;
            std::unique_ptr<VfsFileSource> mFileSource; // declared before the view that may refer to it
            SkyrimWorldView mWorldView;
            SkyrimCellSource mCellSource{ mWorldView };
            openeden::sdk::game::WorldViewService* mWorldViews = nullptr;
            openeden::sdk::game::WorldSourceService* mWorldSources = nullptr;
        };
    }

    std::unique_ptr<openeden::sdk::Extension> createSkyrimGamePack()
    {
        return std::make_unique<SkyrimGamePack>();
    }
}

#ifndef OPENEDEN_SKYRIMSE_NO_ENTRY
static const openeden::sdk::ExtensionDescriptor sDescriptor{ openeden::sdk::kSdkVersion, game_skyrimse::kPackId,
    &game_skyrimse::createSkyrimGamePack };

extern "C" OPENEDEN_EXTENSION_EXPORT const openeden::sdk::ExtensionDescriptor* openeden_extension_entry(
    const openeden::sdk::HostInfo* /*host*/)
{
    return &sDescriptor;
}
#endif
