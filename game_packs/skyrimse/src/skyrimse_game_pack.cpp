#include "game_skyrimse/skyrimse_game_pack.hpp"
#include "game_skyrimse/speech.hpp"

#include "game_skyrimse/appearance.hpp"
#include "game_skyrimse/actor_mechanics.hpp"
#include "game_skyrimse/barter_service.hpp"
#include "game_skyrimse/character_service.hpp"
#include "game_skyrimse/services.hpp"
#include "game_skyrimse/crime_service.hpp"
#include "game_skyrimse/pickup_service.hpp"
#include "game_skyrimse/inventory_service.hpp"
#include "game_skyrimse/player_body.hpp"
#include "game_skyrimse/crafting_service.hpp"
#include "game_skyrimse/harvest_service.hpp"
#include "game_skyrimse/mining_service.hpp"
#include "game_skyrimse/journal.hpp"
#include "game_skyrimse/load_order_stamp.hpp"
#include "game_skyrimse/map.hpp"
#include "game_skyrimse/lockbooks_service.hpp"
#include "game_skyrimse/rest.hpp"
#include "game_skyrimse/travel.hpp"
#include "game_skyrimse/menus.hpp"
#include "game_skyrimse/presentation.hpp"
#include "game_skyrimse/neutral_world.hpp"
#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/papyrus_host.hpp"
#include "game_skyrimse/placed_refs.hpp"
#include "game_skyrimse/progression_service.hpp"
#include "game_skyrimse/story_events.hpp"
#include "game_skyrimse/world_view.hpp"

#include <openeden/extension.hpp>
#include <openeden/game/content.hpp>
#include <openeden/game/player_place.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/game/world_view.hpp>
#include <openeden/host.hpp>
#include <openeden/logging.hpp>

#include <openeden/settings.hpp>
#include <openeden/vfs.hpp>

#include <components/files/conversion.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace game_skyrimse
{
    namespace
    {
        // Task 0176 (milestone M-S6): what the Papyrus host reads from the pack. Scripts come through the session's
        // VFS (scripts/<name>.pex, usually in "Skyrim - Misc.bsa"); a form's class and position from the quest index
        // and the placed reference table the dialogue worker built.
        std::string hexOf(FormId id) // task 0198: log lines
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%08X", id);
            return buffer;
        }

        class PackPapyrusData final : public PapyrusGameData
        {
        public:
            PackPapyrusData(const FileSource* files, SkyrimWorldView& view)
                : mFiles(files)
                , mView(view)
            {
            }
            void setQuests(const QuestIndex* quests) { mQuests = quests; }
            void setLocations(const LocationIndex* locations) { mLocations = locations; } // task 0217

            bool readPex(const std::string& lowerName, std::vector<std::uint8_t>& bytes) override
            {
                if (mFiles == nullptr)
                    return false;
                std::string text;
                if (!mFiles->read("scripts/" + lowerName + ".pex", text))
                    return false;
                bytes.assign(reinterpret_cast<const std::uint8_t*>(text.data()),
                    reinterpret_cast<const std::uint8_t*>(text.data()) + text.size());
                return true;
            }
            std::string formClass(FormId form) const override
            {
                if (mQuests != nullptr && mQuests->quest(form) != nullptr)
                    return "Quest";
                if (mLocations != nullptr && mLocations->location(form) != nullptr)
                    return "Location"; // task 0217
                if (const PlacedRefIndex* refs = mView.placedRefs())
                    if (const PlacedEntry* entry = refs->find(form))
                        return (entry->mFlags & PlacedFlags::IsActor) != 0 ? "Actor" : "ObjectReference";
                return {};
            }
            bool referencePosition(FormId reference, double& x, double& y, double& z) const override
            {
                const PlacedRefIndex* refs = mView.placedRefs();
                const PlacedEntry* entry = refs != nullptr ? refs->find(reference) : nullptr;
                if (entry == nullptr)
                    return false;
                x = entry->mX;
                y = entry->mY;
                z = entry->mZ;
                return true;
            }
            FormId baseOf(FormId reference) const override
            {
                const PlacedRefIndex* refs = mView.placedRefs();
                const PlacedEntry* entry = refs != nullptr ? refs->find(reference) : nullptr;
                return entry != nullptr ? entry->mBase : 0;
            }
            bool hasKeyword(FormId form, FormId keyword) const override { return mView.formHasKeyword(form, keyword); }
            // Task 0217: the location of a placed reference (its cell's) and of the player
            FormId locationOf(FormId reference) const override
            {
                if (reference == kPapyrusPlayerReference)
                    return mView.playerLocation();
                const PlacedRefIndex* refs = mView.placedRefs();
                const PlacedEntry* entry = refs != nullptr ? refs->find(reference) : nullptr;
                return entry != nullptr ? mView.locationOfCell(entry->mCell) : kNullFormId;
            }
            // Task 0198
            FormId linkedRef(FormId reference, FormId keyword) const override
            {
                FormId out = 0;
                return mView.linkedRef(reference, keyword, out) ? out : 0;
            }
            bool initiallyDead(FormId actor) const override
            {
                const PlacedRefIndex* refs = mView.placedRefs();
                const PlacedEntry* entry = refs != nullptr ? refs->find(actor) : nullptr;
                return entry != nullptr && (entry->mFlags & PlacedFlags::Dead) != 0;
            }
            FormId formFromFile(std::string_view lowerFile, std::uint32_t local) const override
            {
                return mView.formFromFile(lowerFile, local);
            }
            bool globalValue(FormId global, float& value) const override { return mView.globalValue(global, value); }
            bool isTopicInfo(FormId form) const override { return mView.isScriptedInfo(form); }
            bool isScene(FormId form) const override { return mView.isScene(form); }
            FormId questOfInfo(FormId info) const override { return mView.questOfInfo(info); }
            bool initiallyEnabled(FormId reference) const override
            {
                const PlacedRefIndex* refs = mView.placedRefs();
                const PlacedEntry* entry = refs != nullptr ? refs->find(reference) : nullptr;
                return entry == nullptr || (entry->mFlags & PlacedFlags::Disabled) == 0;
            }

        private:
            const FileSource* mFiles;
            SkyrimWorldView& mView;
            const QuestIndex* mQuests = nullptr;
            const LocationIndex* mLocations = nullptr;
        };

        // Task 0194: the scene runner's environment over the pack's own parts: the quest state and alias filler, the
        // condition evaluator and dialogue runtime of the view, the cell source (speech queue, holding actors, packages)
        // and the Papyrus host (fragments). Main thread only.
        class SceneGlue final : public SceneEnvironment, public SceneControl
        {
        public:
            SceneGlue(const SkyrimWorldView::PapyrusAccess& access, PapyrusHost& host, SkyrimCellSource* cells,
                std::function<void(const std::string&)> log)
                : mAccess(access)
                , mHost(host)
                , mCells(cells)
                , mLog(std::move(log))
                , mRunner(*access.mScenes, *this)
            {
                if (mAccess.mSetSceneLookup)
                    mAccess.mSetSceneLookup(
                        [this](FormId scene, std::uint32_t action) { return mRunner.actionComplete(scene, action); });
            }
            ~SceneGlue() override
            {
                if (mAccess.mSetSceneLookup)
                    mAccess.mSetSceneLookup(nullptr);
            }

            SceneRunner& runner() { return mRunner; }
            const SceneRunner& runner() const { return mRunner; }

            // SceneControl
            bool startScene(FormId scene, bool force) override { return mRunner.start(scene, force); }
            bool stopScene(FormId scene) override { return mRunner.stop(scene); }
            [[nodiscard]] bool scenePlaying(FormId scene) const override { return mRunner.isPlaying(scene); }
            void questStarted(FormId quest) override { (void)mRunner.questStarted(quest); }
            void questStopped(FormId quest) override { mRunner.questStopped(quest); }

            // SceneEnvironment
            [[nodiscard]] bool questRunning(FormId quest) const override { return mAccess.mState->running(quest); }
            [[nodiscard]] FormId aliasReference(FormId quest, std::uint32_t alias) const override
            {
                AliasFill fill = mAccess.mState->aliasFill(quest, alias);
                if ((!fill.mKnown || fill.mReference == kNullFormId) && mAccess.mAliases != nullptr
                    && mAccess.mState->running(quest))
                {
                    (void)mAccess.mAliases->fillAlias(quest, alias, *mAccess.mState);
                    fill = mAccess.mState->aliasFill(quest, alias);
                }
                return fill.mKnown ? fill.mReference : kNullFormId;
            }
            bool evaluate(const std::vector<survey::Ctda>& list, FormId scene, FormId quest) override
            {
                return mAccess.mEvaluate && mAccess.mEvaluate(list, scene, quest);
            }
            bool resolveLine(FormId, FormId topic, FormId speaker, SceneLine& out) override
            {
                return mAccess.mLine && mAccess.mLine(topic, speaker, speaker, out);
            }
            bool speak(FormId speaker, const SceneLine& line) override
            {
                return mCells != nullptr && mCells->sceneSpeak(speaker, line.mText, line.mVoice);
            }
            void hold(FormId actor, bool on) override
            {
                if (mCells != nullptr)
                    mCells->sceneHold(actor, on);
            }
            bool setPackage(FormId actor, FormId package) override
            {
                const PackageRecord* record = mAccess.mPackage ? mAccess.mPackage(package) : nullptr;
                mPackageChecks[actor] = 0;
                return record != nullptr && mCells != nullptr && mCells->sceneForcePackage(actor, record);
            }
            void clearPackage(FormId actor) override
            {
                if (mCells != nullptr)
                    (void)mCells->sceneForcePackage(actor, nullptr);
                mPackageChecks.erase(actor);
            }
            // The brain evaluates a forced package at its next update, so a few answers are not trusted: the actor may
            // still look done with the package it had before.
            bool packageFinished(FormId actor) override
            {
                return mCells != nullptr && ++mPackageChecks[actor] > kTrustAfterChecks && mCells->scenePackageDone(actor);
            }
            void runFragment(FormId scene, const std::string& script, const std::string& function) override
            {
                const SceneRecord* record = mAccess.mScenes->scene(scene);
                if (record != nullptr && record->mVmad != nullptr)
                    (void)mHost.runSceneFragment(scene, *record->mVmad, script, function);
            }
            void stopQuest(FormId quest) override { mAccess.mState->stop(quest); }
            void log(const std::string& line) override { mLog(line); }
            [[nodiscard]] bool actorLoaded(FormId actor) const override
            {
                return mCells != nullptr && mCells->sceneActorLoaded(actor);
            }

        private:
            static constexpr int kTrustAfterChecks = 3;
            SkyrimWorldView::PapyrusAccess mAccess;
            PapyrusHost& mHost;
            SkyrimCellSource* mCells;
            std::function<void(const std::string&)> mLog;
            std::map<FormId, int> mPackageChecks;
            SceneRunner mRunner;
        };

        // Task 0176: runs the Papyrus host in the pack's frame phase. It waits (without blocking) until the quest data
        // the dialogue worker reads is ready, then attaches the scripts of the start-game-enabled quests, and queues
        // the cells the engine built before that. Everything it logs starts with "PAPYRUS" so a scenario can pick the
        // lines out; lines carry script time, never a frame number or a wall clock, so two runs read the same.
        class PapyrusDriver
        {
        public:
            PapyrusDriver(SkyrimWorldView& view, const FileSource* files, openeden::sdk::Logger* logger,
                openeden::sdk::game::WorldSourceService* sources, openeden::sdk::game::WorldCellSource& cells)
                : mView(view)
                , mData(files, view)
                , mHost(mData)
                , mLogger(logger)
                , mSources(sources)
                , mCells(cells)
            {
                // Task 0181: a script's Enable and Disable reach the engine through the host service; a cell built
                // later comes out with the override (SkyrimCellSource::setEnableOverrideLookup).
                mHost.setEnableChanged([this](FormId reference, bool enabled) { pushEnabled(reference, enabled); });
                mHost.setLog([this](bool error, const std::string& line) {
                    // the host's own lines say "Papyrus: ..."; the runtime's have no prefix
                    const std::string text = line.rfind("Papyrus: ", 0) == 0 ? line.substr(9) : line;
                    say(error ? openeden::sdk::LogLevel::Error : openeden::sdk::LogLevel::Info, "PAPYRUS " + text);
                });
                mHost.setQuestChanged(
                    [this](FormId quest, std::uint32_t stage) { mView.setPackageQuestStage(quest, stage); });
                mHost.setLocationKeywordData(&mView.keywordData()); // task 0217
            }

            void frame(float dt)
            {
                if (!mStarted && !tryStart())
                    return;
                flushPending();
                applyPendingScenes(); // task 0211
                applyPendingStory(); // task 0217
                processPlace();
                raiseTestEvent();
                drainStory();
                mHost.tick(dt);
                if (mScenes != nullptr)
                    mScenes->runner().update(dt); // task 0194
                raiseAmbient(dt);
                if (mHost.scriptTime() >= mNextSummary)
                {
                    mNextSummary += kSummarySeconds;
                    summary();
                }
            }

            void cellBuilt(openeden::sdk::game::WorldId cell)
            {
                mPendingCells.push_back(cell);
                if (mPendingCells.size() > kMaxPending)
                    mPendingCells.pop_front();
            }

            void activated(openeden::sdk::game::WorldId reference) { mPendingActivations.push_back(reference); }

            // Task 0181: a save made while the host ran. Empty before the host started.
            [[nodiscard]] std::vector<std::uint8_t> saveState() const { return mHost.saveState(); }

            // A saved state was read. When the host runs already it is restored at once (false: refused, the load
            // is refused); otherwise it is held and used at the host's start in place of startGame().
            bool restore(std::vector<std::uint8_t> bytes, std::string& error)
            {
                if (!mStarted)
                {
                    mPendingRestore = std::move(bytes);
                    return true;
                }
                return applyRestore(bytes, error);
            }

            void summary()
            {
                const PapyrusHostCounters& c = mHost.counters();
                std::string events;
                for (const auto& [name, count] : c.mEvents)
                    events += (events.empty() ? "" : ",") + name + "=" + std::to_string(count);
                std::string natives;
                std::uint64_t answered = 0;
                for (const auto& [name, count] : c.mNatives)
                {
                    natives += (natives.empty() ? "" : ",") + name + "=" + std::to_string(count);
                    answered += count;
                }
                std::string tolerant;
                const std::vector<std::string> names = mHost.tolerantNatives();
                for (const std::string& name : names)
                    tolerant += (tolerant.empty() ? "" : ",") + name;
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS summary at script time " + std::to_string(static_cast<long long>(mHost.scriptTime()))
                        + " s: scripts loaded " + std::to_string(c.mScriptsLoaded) + ", missing "
                        + std::to_string(c.mScriptsMissing) + ", refused " + std::to_string(c.mScriptsRefused)
                        + "; instances " + std::to_string(c.mInstancesAttached) + ", properties set "
                        + std::to_string(c.mPropertiesSet) + " (rejected " + std::to_string(c.mPropertiesRejected)
                        + ", skipped " + std::to_string(c.mPropertiesSkipped) + "); stage sets "
                        + std::to_string(c.mStageSets) + ", fragments " + std::to_string(c.mFragmentsRun)
                        + ", dialogue fragments " + std::to_string(c.mInfoFragmentsRun) + ", story events "
                        + std::to_string(c.mStoryEvents) + " (quests started " + std::to_string(c.mStoryQuestsStarted)
                        + "), aliases filled " + std::to_string(c.mAliasesFilled) + ", updates " + std::to_string(c.mUpdatesFired) + ", script errors "
                        + std::to_string(c.mScriptErrors) + "; native calls answered " + std::to_string(answered)
                        + ", tolerant natives " + std::to_string(names.size()) + ", tolerant calls "
                        + std::to_string(mHost.tolerantCalls()));
                say(openeden::sdk::LogLevel::Info, "PAPYRUS events " + events);
                say(openeden::sdk::LogLevel::Info, "PAPYRUS natives " + natives);
                say(openeden::sdk::LogLevel::Info, "PAPYRUS tolerant " + tolerant);
                std::string ranking; // task 0198: the calls behind the names, most first
                for (const auto& [name, calls] : mHost.tolerantRanking())
                    ranking += (ranking.empty() ? "" : ",") + name + "=" + std::to_string(calls);
                say(openeden::sdk::LogLevel::Info, "PAPYRUS tolerantcalls " + ranking);
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS item events " + std::to_string(c.mItemEvents) + ", combat events "
                        + std::to_string(c.mCombatEvents) + ", notifications "
                        + std::to_string(mHost.notificationCount()));
            }

            [[nodiscard]] PapyrusHost& host() { return mHost; }

        private:
            static constexpr double kSummarySeconds = 10.0;
            static constexpr std::size_t kMaxPending = 4096;

            void say(openeden::sdk::LogLevel level, const std::string& line)
            {
                if (mLogger != nullptr)
                    mLogger->log(level, line);
            }

            bool tryStart()
            {
                SkyrimWorldView::PapyrusAccess access;
                if (!mView.papyrusAccess(access))
                    return false;
                mData.setQuests(access.mQuests);
                mData.setLocations(access.mLocations); // task 0217
                mHost.setQuests(access.mQuests, access.mState, access.mVmads);
                // Task 0187: aliases fill when a quest starts, the story manager answers events, and the script
                // fragments of a dialogue line the player picks run here
                mHost.setAliasFiller(access.mAliases);
                mHost.setStoryEventSink(access.mStory);
                if (mLogStoryFills) // task 0217: the aliases of every quest the story manager started, with their names
                    mHost.setStoryStarted([this, access](FormId quest, const StoryEvent& event) {
                        const QuestRecord* record = access.mQuests->quest(quest);
                        if (record == nullptr)
                            return;
                        std::string line = "STORY started " + hexId(quest) + " " + record->mCommon.mEditorId + " by "
                            + event.mType + ":";
                        for (const QuestAlias& a : record->mAliases)
                        {
                            const AliasFill fill = access.mState->aliasFill(quest, a.mId);
                            if (!fill.mKnown || fill.mReference == kNullFormId)
                                continue;
                            const std::string name
                                = access.mAliasName ? access.mAliasName(quest, a.mName) : std::string();
                            line += " " + a.mName + "=" + hexId(fill.mReference)
                                + (name.empty() ? "" : "(" + name + ")");
                        }
                        say(openeden::sdk::LogLevel::Info, line);
                    });
                mView.setInfoFragmentSink([this](const InfoFragmentCall& call) {
                    if (call.mVmad != nullptr)
                        (void)mHost.runInfoFragments(
                            call.mInfo, *call.mVmad, call.mSpeakerReference, call.mSpeakerBase);
                });
                // Task 0194: the scene runner, over the pack's own cell source
                if (access.mScenes != nullptr)
                {
                    mScenes = std::make_unique<SceneGlue>(access, mHost, dynamic_cast<SkyrimCellSource*>(&mCells),
                        [this](const std::string& line) { say(openeden::sdk::LogLevel::Info, line); });
                    mHost.setSceneControl(mScenes.get());
                }
                mStarted = true;
                const auto began = std::chrono::steady_clock::now();
                if (mPendingRestore)
                {
                    std::string error;
                    std::vector<std::uint8_t> bytes = std::move(*mPendingRestore);
                    mPendingRestore.reset();
                    if (applyRestore(bytes, error))
                        return true; // the state replaces startGame()
                }
                mHost.startGame();
                const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - began);
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS started: " + std::to_string(access.mQuests->questCount()) + " quests, "
                        + std::to_string(mHost.instanceCount()) + " script instances attached");
                // a wall clock reading: its own prefix, so that a comparison of two runs can leave it out
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUSTIME startGame took " + std::to_string(millis.count()) + " ms on the main thread");
                return true;
            }

            // The cells the engine built while it loaded a save raise OnCellLoad and OnLoad again, as in the game (task
            // 0181 stage slice 2b, toddhow 2026-11-15): their references are attached already (the restore rebuilt the
            // ledger, so `attachReference` adds nothing and OnInit does not run again), and only the events are sent.
            bool applyRestore(const std::vector<std::uint8_t>& bytes, std::string& error)
            {
                if (mScenes != nullptr)
                    (void)mScenes->runner().stopAll(); // task 0211: the scenes of the game being replaced end
                const PapyrusRestoreOutcome outcome = mHost.restoreSavedState(bytes);
                if (!outcome.mOk)
                {
                    error = outcome.mError;
                    say(openeden::sdk::LogLevel::Error, "PAPYRUSLOAD the saved Papyrus state was refused: " + error);
                    return false;
                }
                mNextSummary = (std::floor(mHost.scriptTime() / kSummarySeconds) + 1.0) * kSummarySeconds;
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUSLOAD restored: " + std::to_string(mHost.instanceCount()) + " script instances, script time "
                        + std::to_string(static_cast<long long>(mHost.scriptTime())) + " s");
                // the world was rebuilt from the pack, without the scripts' overrides: tell it again
                for (const auto& [reference, enabled] : mHost.enableOverrides())
                    pushEnabled(reference, enabled);
                return true;
            }

            static std::string hexId(FormId id)
            {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%08X", id);
                return buffer;
            }

            // Task 0187: a built cell is logged. Task 0211: the story manager's "change location" event is raised from the
            // place the core reports (`playerPlace`, an interior or an exterior grid square), not from a built cell: the
            // store builds an exterior cell once, so a visit does not build it again.
            void raiseLocationChange(openeden::sdk::game::WorldId cell)
            {
                FormId location = kNullFormId;
                bool interior = false;
                const bool known = mView.cellLocation(static_cast<FormId>(cell), location, interior);
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS cell " + hexId(static_cast<FormId>(cell)) + " location " + hexId(location)
                        + (known ? (interior ? " interior" : " exterior") : " unknown"));
            }

        public:
            // Task 0211: the core tells where the player stands when it changes. Kept until frame() (never worked out inside
            // the core's physics step).
            void playerPlace(const openeden::sdk::game::PlayerPlace& place) { mPendingPlace = place; }

            [[nodiscard]] QuestAliasFiller::PlayerSite playerSite() const { return mSite; }

            void processPlace()
            {
                if (!mPendingPlace)
                    return;
                const openeden::sdk::game::PlayerPlace place = *mPendingPlace;
                mPendingPlace.reset();
                FormId location = kNullFormId;
                mSite = QuestAliasFiller::PlayerSite();
                mSite.mKnown = true;
                mSite.mInterior = place.interior;
                if (place.interior)
                {
                    mSite.mCell = static_cast<FormId>(place.cell);
                    location = mView.locationOfCell(mSite.mCell);
                    mSite.mLocation = location;
                }
                else
                {
                    mSite.mWorld = static_cast<FormId>(place.world);
                    mSite.mX = (static_cast<double>(place.gridX) + 0.5) * QuestAliasFiller::kCellSize;
                    mSite.mY = (static_cast<double>(place.gridY) + 0.5) * QuestAliasFiller::kCellSize;
                    location = mView.locationOfGridSquare(mSite.mWorld, place.gridX, place.gridY);
                }
                const std::optional<LocationChange::Change> change = mLocationChange.arrive(location);
                if (!change)
                    return;
                StoryEvent event;
                event.mType = "CLOC";
                event.mLocation1 = change->mFrom;
                event.mLocation2 = change->mTo;
                const std::size_t started = mHost.raiseStoryEvent(event);
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS story event CLOC to location " + hexId(change->mTo) + " started " + std::to_string(started)
                        + " quests");
            }

            // Task 0211: a story event raised later in frame(): a death is reported inside the combat update, and starting
            // a quest runs scripts, which must not happen there. Bounded: the oldest is dropped.
            void queueStoryEvent(StoryEvent event)
            {
                if (mPendingStory.size() >= kMaxPendingStory)
                    mPendingStory.pop_front();
                mPendingStory.push_back(std::move(event));
            }

            void queueKill(FormId victim, FormId killer)
            {
                FormId location = kNullFormId;
                if (const PlacedRefIndex* refs = mView.placedRefs())
                    if (const PlacedEntry* entry = refs->find(victim))
                        location = mView.locationOfCell(entry->mCell);
                if (location == kNullFormId)
                    location = mLocationChange.current(); // the victim's cell has none: the place the player is in
                queueStoryEvent(makeKillEvent(victim, killer, location));
            }

            void drainStory()
            {
                for (int taken = 0; taken < kStoryPerFrame && !mPendingStory.empty(); ++taken)
                {
                    const StoryEvent event = std::move(mPendingStory.front());
                    mPendingStory.pop_front();
                    const std::size_t started = mHost.raiseStoryEvent(event);
                    say(openeden::sdk::LogLevel::Info,
                        "PAPYRUS story event " + event.mType + " R1 " + hexId(event.mRef1) + " R2 " + hexId(event.mRef2)
                            + " L1 " + hexId(event.mLocation1) + " started " + std::to_string(started) + " quests");
                }
            }

            // A developer aid, [extension.skyrimse] story test event = TYPE[:K1=hex][:L1=hex][:L2=hex][:R1=hex][:R2=hex]
            // [:V1=n][:V2=n]: one event, raised `delay` script seconds after the host started.
            void setTestEvent(const std::string& text, double delay)
            {
                if (text.empty())
                    return;
                std::size_t at = text.find(':');
                StoryEvent event;
                event.mType = text.substr(0, at);
                while (at != std::string::npos)
                {
                    const std::size_t next = text.find(':', at + 1);
                    const std::string part = text.substr(at + 1, next == std::string::npos ? next : next - at - 1);
                    at = next;
                    const std::size_t eq = part.find('=');
                    if (eq == std::string::npos)
                        continue;
                    const std::string key = part.substr(0, eq);
                    const unsigned long value = std::strtoul(part.c_str() + eq + 1, nullptr, 16);
                    const FormId id = static_cast<FormId>(value);
                    if (key == "K1")
                        event.mKeyword = id;
                    else if (key == "L1")
                        event.mLocation1 = id;
                    else if (key == "L2")
                        event.mLocation2 = id;
                    else if (key == "R1")
                        event.mRef1 = id;
                    else if (key == "R2")
                        event.mRef2 = id;
                    else if (key == "V1")
                        event.mValue1 = static_cast<std::int32_t>(std::strtol(part.c_str() + eq + 1, nullptr, 10));
                    else if (key == "V2")
                        event.mValue2 = static_cast<std::int32_t>(std::strtol(part.c_str() + eq + 1, nullptr, 10));
                }
                mTestEvent = std::move(event);
                mTestEventDelay = std::isfinite(delay) && delay >= 0.0 ? delay : 0.0;
            }

            void setTestQuests(const std::string& text, double delay)
            {
                std::size_t at = 0;
                while (at < text.size())
                {
                    const std::size_t comma = text.find(',', at);
                    const std::string part = text.substr(at, comma == std::string::npos ? comma : comma - at);
                    if (!part.empty())
                    {
                        // <hex form id>[:<stage>]
                        const std::size_t colon = part.find(':');
                        mTestQuests.push_back(static_cast<FormId>(std::strtoul(part.c_str(), nullptr, 16)));
                        mTestStages.push_back(colon == std::string::npos
                                ? -1
                                : static_cast<std::int32_t>(std::strtol(part.c_str() + colon + 1, nullptr, 10)));
                    }
                    at = comma == std::string::npos ? text.size() : comma + 1;
                }
                if (!mTestQuests.empty())
                    mTestEventDelay = std::isfinite(delay) && delay >= 0.0 ? delay : 0.0;
            }

            void raiseTestEvent()
            {
                if (!mTestQuests.empty() && mHost.scriptTime() >= mTestEventDelay)
                {
                    const std::vector<FormId> quests = std::move(mTestQuests);
                    const std::vector<std::int32_t> stages = std::move(mTestStages);
                    mTestQuests.clear();
                    mTestStages.clear();
                    for (std::size_t i = 0; i < quests.size(); ++i)
                        say(openeden::sdk::LogLevel::Info,
                            "PAPYRUS story test quest " + hexId(quests[i])
                                + (mHost.startQuestForTest(quests[i], stages[i]) ? " started" : " refused"));
                }
                if (!mTestEvent || mHost.scriptTime() < mTestEventDelay)
                    return;
                const StoryEvent event = *mTestEvent;
                mTestEvent.reset();
                const std::size_t started = mHost.raiseStoryEvent(event);
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS story test event " + event.mType + " started " + std::to_string(started) + " quests");
            }

            // Task 0217: what a save keeps of the story manager and the data scripts stored on locations. A loaded
            // state waits until the story manager exists (the dialogue data is built on a worker) and is applied in a
            // frame.
            [[nodiscard]] StoryState storyState() const { return mView.storyState(); }
            void restoreStory(StoryState state) { mLoadedStory = std::move(state); }
            void setLogStoryFills(bool on) { mLogStoryFills = on; }
            void applyPendingStory()
            {
                if (!mLoadedStory || !mView.restoreStoryState(*mLoadedStory))
                    return;
                say(openeden::sdk::LogLevel::Info,
                    "STORY restored: event " + std::to_string(mLoadedStory->mManager.mEventCounter) + ", "
                        + std::to_string(mLoadedStory->mManager.mCycle.size()) + " rotations, "
                        + std::to_string(mLoadedStory->mRan.size()) + " ambient quests, "
                        + std::to_string(mLoadedStory->mKeywordData.size()) + " keyword data");
                mLoadedStory.reset();
            }

            // Task 0211: what a save keeps of the scenes that play, and the load's side of it. A saved state waits for the
            // host's own restore (the chunks are read in any order) and is applied in the next frame.
            [[nodiscard]] std::vector<SceneSaveEntry> sceneState() const
            {
                return mScenes != nullptr ? mScenes->runner().saveState() : std::vector<SceneSaveEntry>();
            }
            void restoreScenes(std::vector<SceneSaveEntry> entries) { mPendingScenes = std::move(entries); }
            void applyPendingScenes()
            {
                if (!mPendingScenes || mScenes == nullptr || mPendingRestore)
                    return;
                const std::vector<SceneSaveEntry> entries = std::move(*mPendingScenes);
                mPendingScenes.reset();
                const SceneRestoreResult result = mScenes->runner().restoreState(entries);
                say(openeden::sdk::LogLevel::Info,
                    "SCENE restored " + std::to_string(result.mResumed) + " scenes, dropped "
                        + std::to_string(result.mDropped));
            }

        private:
            // Task 0194: the story manager's ambient dialogue event (ADIA). The engine raises it now and then for two
            // actors that stand together; ours: from `kAmbientFirstSeconds` after the actors of a cell exist, every
            // `kAmbientSeconds` of script time, over the actors the core told the pack about, pairs of the same cell in a
            // seeded order, until one pair starts a quest (at most `kAmbientPairs` events a time). RECALLED-VERIFY, a
            // decision for toddhow: the timing and the pairing.
            void raiseAmbient(float dt)
            {
                SkyrimCellSource* cells = dynamic_cast<SkyrimCellSource*>(&mCells);
                const PlacedRefIndex* refs = mView.placedRefs();
                if (!mAmbientOn || cells == nullptr || refs == nullptr || mScenes == nullptr)
                    return;
                mAmbientClock += dt;
                if (mAmbientClock < mAmbientNext)
                    return;
                std::vector<FormId> actors;
                for (const openeden::sdk::game::WorldId id : cells->sceneActors())
                {
                    const PlacedEntry* entry = refs->find(static_cast<FormId>(id));
                    if (entry != nullptr && (entry->mFlags & PlacedFlags::Dead) == 0
                        && (entry->mFlags & PlacedFlags::Disabled) == 0 && mHost.referenceEnabled(entry->mId))
                        actors.push_back(entry->mId);
                }
                if (actors.size() < 2)
                    return; // nobody to talk: asked again next frame
                mAmbientNext = mAmbientClock + kAmbientSeconds;
                std::vector<std::pair<FormId, FormId>> pairs;
                for (const FormId a : actors)
                    for (const FormId b : actors)
                    {
                        const PlacedEntry* ea = refs->find(a);
                        const PlacedEntry* eb = refs->find(b);
                        if (a != b && ea->mCell == eb->mCell)
                            pairs.emplace_back(a, b);
                    }
                std::uint64_t state = (++mAmbientEvents) * 0x9E3779B97F4A7C15ull;
                for (std::size_t i = pairs.size(); i > 1; --i)
                {
                    state += 0x9E3779B97F4A7C15ull;
                    std::uint64_t z = (state ^ (state >> 30)) * 0xBF58476D1CE4E5B9ull;
                    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                    std::swap(pairs[i - 1], pairs[static_cast<std::size_t>((z ^ (z >> 31)) % i)]);
                }
                for (std::size_t i = 0; i < pairs.size() && i < kAmbientPairs; ++i)
                {
                    StoryEvent event;
                    event.mType = "ADIA";
                    event.mRef1 = pairs[i].first;
                    event.mRef2 = pairs[i].second;
                    if (mHost.raiseStoryEvent(event) != 0)
                    {
                        say(openeden::sdk::LogLevel::Info,
                            "SCENE ambient dialogue event started a quest after " + std::to_string(i + 1)
                                + " pairs");
                        break;
                    }
                }
            }

            void pushEnabled(FormId reference, bool enabled)
            {
                if (mSources == nullptr)
                    return;
                FormId cell = kNullFormId;
                if (const PlacedRefIndex* refs = mView.placedRefs())
                    if (const PlacedEntry* entry = refs->find(reference))
                        cell = entry->mCell;
                const bool queued = mSources->setObjectEnabled(mCells, cell, reference, enabled);
                say(openeden::sdk::LogLevel::Info,
                    std::string("PAPYRUS ") + (enabled ? "enable" : "disable") + " of " + hexId(reference) + " in cell "
                        + hexId(cell) + (queued ? " sent to the world" : " not sent"));
            }

            void flushPending()
            {
                while (!mPendingCells.empty())
                {
                    const openeden::sdk::game::WorldId cell = mPendingCells.front();
                    mPendingCells.pop_front();
                    const std::vector<SkyrimWorldView::ReferenceScripts> scripts = mView.scriptsOfCell(cell);
                    std::vector<FormId> references;
                    for (const SkyrimWorldView::ReferenceScripts& s : scripts)
                    {
                        mHost.attachReference(s.mReference, s.mBase, s.mActor, s.mScripts);
                        references.push_back(s.mReference);
                    }
                    mHost.onCellLoaded(cell, references);
                    raiseLocationChange(cell);
                    say(openeden::sdk::LogLevel::Info,
                        std::string("PAPYRUS") + " cell " + std::to_string(cell) + " loaded: "
                            + std::to_string(references.size()) + " references with scripts");
                }
                for (const openeden::sdk::game::WorldId reference : mPendingActivations)
                    mHost.onActivate(reference, 0x14);
                mPendingActivations.clear();
            }

            SkyrimWorldView& mView;
            PackPapyrusData mData;
            PapyrusHost mHost;
            openeden::sdk::Logger* mLogger;
            static constexpr double kAmbientFirstSeconds = 5.0;
            static constexpr double kAmbientSeconds = 30.0;
            static constexpr std::size_t kAmbientPairs = 40;
            bool mAmbientOn = true;
            double mAmbientClock = 0.0;
            double mAmbientNext = kAmbientFirstSeconds;
            std::uint64_t mAmbientEvents = 0;
            bool mStarted = false;
            std::unique_ptr<SceneGlue> mScenes; ///< task 0194 (declared after the host it reaches)
            std::optional<std::vector<std::uint8_t>> mPendingRestore;
            openeden::sdk::game::WorldSourceService* mSources;
            openeden::sdk::game::WorldCellSource& mCells;
            double mNextSummary = kSummarySeconds;
            LocationChange mLocationChange; ///< the last location a CLOC event named (tasks 0187, 0211)
            static constexpr std::size_t kMaxPendingStory = 64;
            static constexpr int kStoryPerFrame = 4;
            std::optional<openeden::sdk::game::PlayerPlace> mPendingPlace;
            QuestAliasFiller::PlayerSite mSite;
            std::deque<StoryEvent> mPendingStory;
            std::optional<StoryEvent> mTestEvent;
            std::vector<FormId> mTestQuests;
            std::vector<std::int32_t> mTestStages;
            double mTestEventDelay = 0.0;
            std::optional<std::vector<SceneSaveEntry>> mPendingScenes;
            std::optional<StoryState> mLoadedStory; // task 0217
            bool mLogStoryFills = false;
            std::deque<openeden::sdk::game::WorldId> mPendingCells;
            std::vector<openeden::sdk::game::WorldId> mPendingActivations;
        };

        // Task 0181: the Papyrus state as a save chunk. Written only while the host runs (a byte says whether there
        // is a state); read before or after the host started (the driver holds it until it can use it). Registered only
        // when [extension.skyrimse] papyrus is on, so a save made without it has no such chunk.
        class PapyrusSaveChunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::uint32_t kMaxBytes = 256u * 1024u * 1024u;

            explicit PapyrusSaveChunk(std::unique_ptr<PapyrusDriver>& driver)
                : mDriver(driver)
            {
            }
            void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }

            std::string_view chunkName() const override { return "papyrus"; }
            std::uint32_t chunkVersion() const override { return kVersion; }

            void write(openeden::sdk::ChunkWriter& writer) override
            {
                const std::vector<std::uint8_t> bytes = mDriver != nullptr ? mDriver->saveState()
                                                                           : std::vector<std::uint8_t>();
                writer.writeValue(static_cast<std::uint8_t>(bytes.empty() ? 0 : 1));
                if (bytes.empty())
                    return;
                writer.writeValue(static_cast<std::uint32_t>(bytes.size()));
                writer.write(bytes.data(), bytes.size());
            }

            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override
            {
                if (version != kVersion)
                    return refuse("the Papyrus chunk has version " + std::to_string(version));
                std::uint8_t present = 0;
                if (!reader.readValue(present) || present > 1)
                    return refuse("the Papyrus chunk is damaged");
                if (present == 0)
                    return reader.remaining() == 0 ? true : refuse("the Papyrus chunk is damaged");
                std::uint32_t size = 0;
                if (!reader.readValue(size) || size > kMaxBytes || size != reader.remaining())
                    return refuse("the Papyrus chunk is damaged");
                std::vector<std::uint8_t> bytes(size);
                if (size != 0 && !reader.read(bytes.data(), size))
                    return refuse("the Papyrus chunk is damaged");
                if (mDriver == nullptr)
                    return true; // the host is gone: nothing to resume
                std::string error;
                if (!mDriver->restore(std::move(bytes), error))
                    return refuse("the Papyrus state cannot be resumed: " + error);
                return true;
            }

        private:
            bool refuse(const std::string& why)
            {
                if (mSink)
                    mSink(why);
                return false;
            }
            std::unique_ptr<PapyrusDriver>& mDriver;
            std::function<void(const std::string&)> mSink;
        };

        // Task 0211: the scenes that play at a save, a chunk of their own (the Papyrus chunk refuses any other version and
        // size, so adding to it would refuse every old save). Written whenever the driver exists (a zero count when no
        // scene plays); a load hands the entries to the driver, which applies them after the host's own restore.
        class SceneSaveChunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::uint32_t kMaxBytes = 4u * 1024u * 1024u;

            explicit SceneSaveChunk(std::unique_ptr<PapyrusDriver>& driver)
                : mDriver(driver)
            {
            }
            void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }

            std::string_view chunkName() const override { return "scenes"; }
            std::uint32_t chunkVersion() const override { return kVersion; }

            void write(openeden::sdk::ChunkWriter& writer) override
            {
                const std::vector<std::uint8_t> bytes = encodeSceneState(mDriver != nullptr ? mDriver->sceneState()
                                                                                         : std::vector<SceneSaveEntry>());
                writer.writeValue(static_cast<std::uint32_t>(bytes.size()));
                writer.write(bytes.data(), bytes.size());
            }

            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override
            {
                if (version != kVersion)
                    return refuse("the scenes chunk has version " + std::to_string(version));
                std::uint32_t size = 0;
                if (!reader.readValue(size) || size > kMaxBytes || size != reader.remaining())
                    return refuse("the scenes chunk is damaged");
                std::vector<std::uint8_t> bytes(size);
                if (size != 0 && !reader.read(bytes.data(), size))
                    return refuse("the scenes chunk is damaged");
                std::vector<SceneSaveEntry> entries;
                std::string error;
                if (!decodeSceneState(bytes, entries, error))
                    return refuse("the scenes chunk is damaged: " + error);
                if (mDriver != nullptr)
                    mDriver->restoreScenes(std::move(entries));
                return true;
            }

        private:
            bool refuse(const std::string& why)
            {
                if (mSink)
                    mSink(why);
                return false;
            }
            std::unique_ptr<PapyrusDriver>& mDriver;
            std::function<void(const std::string&)> mSink;
        };

        // Task 0217: the story manager's state and the data scripts stored on locations, a chunk of their own (the
        // Papyrus chunk is strict about its version and size). Written whenever the driver exists; a load hands the
        // state to the driver, which applies it once the story manager exists.
        class StorySaveChunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            static constexpr std::uint32_t kVersion = 1;
            static constexpr std::uint32_t kMaxBytes = 16u * 1024u * 1024u;

            explicit StorySaveChunk(std::unique_ptr<PapyrusDriver>& driver)
                : mDriver(driver)
            {
            }
            void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }

            std::string_view chunkName() const override { return "story"; }
            std::uint32_t chunkVersion() const override { return kVersion; }

            void write(openeden::sdk::ChunkWriter& writer) override
            {
                const std::vector<std::uint8_t> bytes
                    = encodeStoryState(mDriver != nullptr ? mDriver->storyState() : StoryState());
                writer.writeValue(static_cast<std::uint32_t>(bytes.size()));
                writer.write(bytes.data(), bytes.size());
            }

            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override
            {
                if (version != kVersion)
                    return refuse("the story chunk has version " + std::to_string(version));
                std::uint32_t size = 0;
                if (!reader.readValue(size) || size > kMaxBytes || size != reader.remaining())
                    return refuse("the story chunk is damaged");
                std::vector<std::uint8_t> bytes(size);
                if (size != 0 && !reader.read(bytes.data(), size))
                    return refuse("the story chunk is damaged");
                StoryState state;
                std::string error;
                if (!decodeStoryState(bytes, state, error))
                    return refuse("the story chunk is damaged: " + error);
                if (mDriver != nullptr)
                    mDriver->restoreStory(std::move(state));
                return true;
            }

        private:
            bool refuse(const std::string& why)
            {
                if (mSink)
                    mSink(why);
                return false;
            }
            std::unique_ptr<PapyrusDriver>& mDriver;
            std::function<void(const std::string&)> mSink;
        };

        // Owns the content loader it registers (the host keeps a reference until stop()), the world view and the
        // neutral world source over it (the source is declared after the view it refers to).
        class SkyrimGamePack final : public openeden::sdk::Extension
        {
        public:
            // Task 0206: the player's progression for the other parts of the pack (crafting, lockpicking, books
            // ...): `skillUsed(slot, amount)` trains a skill, `actorValue(index, out)` reads one. Null when
            // `[extension.skyrimse] player progression` is off. Owned by the pack; main thread.
            [[nodiscard]] progression::ProgressionService* progressionService() const { return mProgression.get(); }

            // Task 0176: the Papyrus host runs in the game phase, on the main thread; with the setting off the driver
            // does not exist and the callback does nothing.
            void registerServices(openeden::sdk::Registrar& registrar) override
            {
                registrar.registerPhaseCallback(openeden::sdk::Phase::GameSimulation, [this](float dt) {
                    if (mPapyrus != nullptr)
                        mPapyrus->frame(dt);
                    syncFollowerAlias();
                    if (mProgression != nullptr)
                        pollProgression(); // task 0200
                    if (mInventory != nullptr)
                    {
                        (void)mInventory->poll(); // starts and adopts the item table reader; never waits
                        if (mBarter != nullptr)
                            mBarter->poll(); // task 0189: a pending barter session asks again
                        if (mServices != nullptr)
                            mServices->poll(); // task 0219: a pending training or rent session asks again
                        if (mCrafting != nullptr)
                            mCrafting->poll(); // task 0199: reads the crafting records, registers made items
                        if (mLockBooks != nullptr)
                        {
                            (void)mLockBooks->poll(); // task 0203: the book index is read on a worker
                            syncLearnedSpell();
                        }
                        syncEquipment();
                    }
                    if (mWorldView.playerBodyOn())
                        syncPlayerBody(); // task 0222
                    if (mCharacter != nullptr)
                        pollCharacter(); // task 0225
                });
            }

            // Task 0196: the game's own recruit path. A script (DialogueFollowerScript) forces the follower into the
            // DialogueFollower quest's alias 0 and clears it to dismiss; the pack follows the alias.
            void syncFollowerAlias()
            {
                if (mPapyrus == nullptr)
                    return;
                const std::uint32_t now = mPapyrus->host().forcedAlias(kFollowerQuestId, kFollowerAliasId);
                const FollowerAliasChange change = followerAliasChange(mFollowerAliasSeen, now);
                if (change.mDismiss == 0 && change.mRecruit == 0)
                    return;
                mFollowerAliasSeen = now;
                if (change.mDismiss != 0)
                    (void)mCellSource.actorTable().dismiss(change.mDismiss);
                if (change.mRecruit != 0)
                    (void)mCellSource.actorTable().recruit(change.mRecruit);
            }

            bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }

            // The null game rules (design doc game-packs.md 5.5): the pack identifies Skyrim SE content, then refuses
            // the session. A game pack's failure in start() is a session error that names it and refuses saving, so
            // the player sees the message at the main menu. Eden Studio starts the pack when game=skyrimse (task
            // 0173) and reads sources, so it takes the path below.
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
                    registerLoadOrderStamp(host, *sources, logger);
                    // Task 0176: the Papyrus host, off unless [extension.skyrimse] papyrus = true.
                    if (const openeden::sdk::SettingsService* settings = host.getSettingsService())
                    {
                        if (settings->getBool(kPackId, kPapyrusKey, false))
                        {
                            mPapyrus = std::make_unique<PapyrusDriver>(
                                mWorldView, mFileSource.get(), logger, sources, mCellSource);
                            PapyrusDriver* driver = mPapyrus.get();
                            mCellSource.setEnableOverrideLookup(
                                [driver](openeden::sdk::game::WorldId reference) -> std::optional<bool> {
                                    return driver->host().enableOverride(static_cast<FormId>(reference));
                                });
                            if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                            {
                                mPapyrusChunk = std::make_unique<PapyrusSaveChunk>(mPapyrus);
                                mPapyrusChunk->setRefusalSink([logger](const std::string& reason) {
                                    if (logger != nullptr)
                                        logger->log(openeden::sdk::LogLevel::Error, "the load is refused: " + reason);
                                });
                                if (!chunks->registerHandler(*mPapyrusChunk) && logger != nullptr)
                                    logger->log(openeden::sdk::LogLevel::Warning, "the Papyrus chunk was not registered");
                                mSceneChunk = std::make_unique<SceneSaveChunk>(mPapyrus);
                                mSceneChunk->setRefusalSink([logger](const std::string& reason) {
                                    if (logger != nullptr)
                                        logger->log(openeden::sdk::LogLevel::Error, "the load is refused: " + reason);
                                });
                                if (!chunks->registerHandler(*mSceneChunk) && logger != nullptr)
                                    logger->log(openeden::sdk::LogLevel::Warning, "the scenes chunk was not registered");
                                mStoryChunk = std::make_unique<StorySaveChunk>(mPapyrus);
                                mStoryChunk->setRefusalSink([logger](const std::string& reason) {
                                    if (logger != nullptr)
                                        logger->log(openeden::sdk::LogLevel::Error, "the load is refused: " + reason);
                                });
                                if (!chunks->registerHandler(*mStoryChunk) && logger != nullptr)
                                    logger->log(openeden::sdk::LogLevel::Warning, "the story chunk was not registered");
                            }
                            // Task 0211: where the player stands, and a developer aid for one story event
                            mCellSource.setPlayerPlaceSink(
                                [driver](const openeden::sdk::game::PlayerPlace& place) { driver->playerPlace(place); });
                            mWorldView.setPlayerSite([driver] { return driver->playerSite(); });
                            driver->setTestEvent(settings->getString(kPackId, "story test event", ""),
                                static_cast<double>(settings->getFloat(kPackId, "story test delay", 8.f)));
                            driver->setTestQuests(settings->getString(kPackId, "story test quest", ""),
                                static_cast<double>(settings->getFloat(kPackId, "story test delay", 8.f)));
                            // Task 0217: a developer's trace of every node and quest the story manager's walk tries
                            if (settings->getBool(kPackId, "story trace", false))
                            {
                                driver->setLogStoryFills(true);
                                mWorldView.setStoryTrace([logger](const std::string& line) {
                                    if (logger != nullptr)
                                        logger->log(openeden::sdk::LogLevel::Info, "STORY " + line);
                                });
                            }
                            mCellSource.setScriptSinks(
                                [driver](openeden::sdk::game::WorldId cell) { driver->cellBuilt(cell); },
                                [driver](openeden::sdk::game::WorldId reference) { driver->activated(reference); });
                            if (logger != nullptr)
                                logger->log(openeden::sdk::LogLevel::Info, "PAPYRUS the Papyrus host is on");
                        }
                    }
                    // Task 0180: the combat HUD's script package `core.skyrimse.combat` (read-only; a script only sees
                    // numbers once combat runs, `active()`), declared only for a host that reads sources.
                    if (openeden::sdk::BindingsService* bindings = host.getBindingsService())
                    {
                        const openeden::sdk::DeclareResult declared = mHudBindings.declareTo(*bindings);
                        if (!declared.ok && logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Warning,
                                "the combat HUD package was refused: " + declared.message);
                    }
                    // Task 0182: the inventory state and its script bindings, off unless [extension.skyrimse] inventory.
                    startInventory(host, logger);
                    // Task 0200: the player's skills and levels (the namespace skyrimse.progression).
                    startProgression(host, logger);
                    // Task 0189: the barter session (the namespace skyrimse.barter); needs the inventory.
                    startBarter(host, logger);
                    // Task 0219: training, rooms to rent and gifts (skyrimse.services); needs the inventory.
                    startServices(host, logger);
                    // Task 0204: crime, bounty and guards (the namespace skyrimse.crime).
                    startCrime(host, logger);
                    // Task 0203: lock picking and books (the namespace skyrimse.lockbooks); needs the inventory.
                    startLockBooks(host, logger);
                    // Task 0208: picking up placed items and pickpocketing (needs the inventory).
                    startPickup(host, logger);
                    wirePapyrusWorld(host, logger);
                    // Task 0199: alchemy, smithing and enchanting (namespace skyrimse.crafting); needs the inventory.
                    startCrafting(host, logger);
                    // Task 0185: the quest journal's bindings, off unless [extension.skyrimse] journal.
                    startJournal(host, logger);
                    // Task 0201: the compass and world map's bindings, off unless [extension.skyrimse] map.
                    startMap(host, logger);
                    // Task 0218: fast travel from the world map (the namespace skyrimse.travel), off unless
                    // [extension.skyrimse] fast travel (and map).
                    startTravel(host, logger);
                    // Task 0210: waiting and sleeping (the namespace skyrimse.rest), off unless
                    // [extension.skyrimse] rest.
                    startRest(host, logger);
                    // Task 0221: harvesting flora (the namespace skyrimse.harvest), off unless
                    // [extension.skyrimse] harvest; needs the inventory.
                    startHarvest(host, logger);
                    // Task 0224: mining ore veins and chopping wood (the namespace skyrimse.mining), under the same
                    // setting.
                    startMining(host, logger);
                    // Task 0207: the loading screen tips of the menu script, off unless [extension.skyrimse] menus.
                    startMenus(host, logger);
                    // Task 0215: tells the scripts that the session has its own menus or music (Morrowind's music
                    // script gives way), off unless [extension.skyrimse] menus or music.
                    startPresentation(host, logger);
                    // Task 0225: the character creation (the namespace skyrimse.character), off unless
                    // [extension.skyrimse] character creation (which needs menus and player body).
                    startCharacter(host, logger);
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
                mCellSource.setCrime(nullptr); // task 0204: before the crime service goes
                mCellSource.setTakenLookup({}); // task 0208: before the pickup service goes
                mCellSource.setHarvestedLookup({}); // task 0221: before the harvest service goes
                mCellSource.setPlacementNameLookup({}); // task 0224: before the mining service goes
                unwirePapyrusWorld();
                if (mPapyrus != nullptr)
                {
                    mPapyrus->summary();
                    mCellSource.setScriptSinks({}, {});
                    mCellSource.setEnableOverrideLookup({});
                    mPapyrus.reset();
                }
                // The index worker uses the logger and the file source: end it before anything is withdrawn.
                mWorldView.cancelAndJoin();
                withdrawWorldSource();
                withdrawWorldView();
            }

            ~SkyrimGamePack() override
            {
                mCellSource.setCrime(nullptr);
                unwirePapyrusWorld();
                mPapyrus.reset();
                mWorldView.cancelAndJoin();
                withdrawWorldSource();
                withdrawWorldView();
            }

        private:
            // Task 0198: the Papyrus host's reach into the inventory and combat (see papyrus_world.hpp). Needs the host;
            // the inventory natives need the inventory (setting `inventory`), the combat events need combat to run.
            void wirePapyrusWorld(openeden::sdk::Host& sdkHost, openeden::sdk::Logger* logger)
            {
                if (mPapyrus == nullptr)
                    return;
                PapyrusHost& host = mPapyrus->host();
                PackWorldEnv env;
                if (mInventory != nullptr)
                {
                    env.model = [this]() -> inventory::InventoryModel* {
                        return mInventory != nullptr ? &mInventory->model() : nullptr;
                    };
                    env.ensureOwner = [this](std::uint64_t owner, FormId reference) {
                        if (mInventory == nullptr)
                            return false;
                        const PlacedRefIndex* refs = mWorldView.placedRefs();
                        const PlacedEntry* entry = refs != nullptr ? refs->find(reference) : nullptr;
                        if (entry == nullptr || entry->mBase == 0)
                            return false;
                        return mInventory->open(
                            owner, entry->mBase, (entry->mFlags & PlacedFlags::IsActor) != 0 ? "actor" : "container");
                    };
                }
                env.actorValue = [this](FormId actor, std::string_view name, double& current, double& maximum) {
                    if (actor == kPapyrusPlayerReference)
                    {
                        // Task 0206: the player's skills are the progression's (with the maximum left 0 so that a
                        // script's ModActorValue is not clamped back to it)
                        float skill = 0.f;
                        if (mProgression != nullptr && progression::skillActorValue(*mProgression, name, skill))
                        {
                            current = skill;
                            maximum = 0.0;
                            return true;
                        }
                        const HudSnapshot& hud = mCellSource.hud().snapshot();
                        if (name == "health")
                            current = hud.mPlayerHealth, maximum = hud.mPlayerHealthMax;
                        else if (name == "magicka")
                            current = hud.mPlayerMagicka, maximum = hud.mPlayerMagickaMax;
                        else if (name == "stamina")
                            current = hud.mPlayerStamina, maximum = hud.mPlayerStaminaMax;
                        else
                            return false;
                        return maximum > 0.0;
                    }
                    const openeden::sdk::game::ActorHandle handle = mCellSource.actorTable().handleOf(actor);
                    SkyrimCombat::FighterView view;
                    if (!handle.valid()
                        || !mCellSource.combat().fighterView(openeden::sdk::game::CombatSubject::of(handle), view))
                        return false;
                    if (name == "health")
                        current = view.mHealth, maximum = view.mHealthMax;
                    else if (name == "magicka")
                        current = view.mMagicka, maximum = view.mMagickaMax;
                    else if (name == "stamina")
                        current = view.mStamina, maximum = view.mStaminaMax;
                    else
                        return false;
                    return true;
                };
                env.setHealth = [this](FormId actor, double health) {
                    if (actor == kPapyrusPlayerReference)
                        return false; // the engine owns the player's health: nothing here writes it
                    if (health <= 0.0)
                        return mCellSource.scriptBridge().queueKill(actor);
                    const openeden::sdk::game::ActorHandle handle = mCellSource.actorTable().handleOf(actor);
                    return handle.valid()
                        && mCellSource.combat().setFighterHealth(
                            openeden::sdk::game::CombatSubject::of(handle), static_cast<float>(health));
                };
                env.isDead = [this](FormId actor) {
                    const openeden::sdk::game::CombatSubject subject = actor == kPapyrusPlayerReference
                        ? openeden::sdk::game::CombatSubject::ofPlayer()
                        : openeden::sdk::game::CombatSubject::of(mCellSource.actorTable().handleOf(actor));
                    SkyrimCombat::FighterView view;
                    return subject.valid() && mCellSource.combat().fighterView(subject, view) && view.mDead;
                };
                env.queueKill = [this](FormId actor) { return mCellSource.scriptBridge().queueKill(actor); };
                env.isItem = [this](FormId form) { return mInventory != nullptr && mInventory->table().item(form) != nullptr; };
                mPapyrusHooks = std::make_unique<PackWorldHooks>(std::move(env));
                host.setWorldHooks(mPapyrusHooks.get());
                if (mInventory != nullptr)
                    mInventory->model().setChangeSink(
                        [&host, logger](std::uint64_t from, std::uint64_t to, FormId item, std::int32_t count) {
                            if (logger != nullptr)
                                logger->log(openeden::sdk::LogLevel::Info,
                                    "PAPYRUS item " + hexOf(item) + " x" + std::to_string(count) + " from "
                                        + hexOf(PackWorldHooks::referenceOf(from)) + " to "
                                        + hexOf(PackWorldHooks::referenceOf(to)));
                            host.onItemMoved(PackWorldHooks::referenceOf(from), PackWorldHooks::referenceOf(to), item, count);
                        });
                mCellSource.scriptBridge().setSinks(
                    [&host, logger](FormId victim, FormId attacker, bool blocked, bool bash) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info,
                                "PAPYRUS hit on " + hexOf(victim) + " by " + hexOf(attacker) + (blocked ? " (blocked)" : ""));
                        host.onActorHit(victim, attacker, blocked, bash);
                    },
                    [this, &host, logger](FormId victim, FormId killer) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info,
                                "PAPYRUS death of " + hexOf(victim) + " killer " + hexOf(killer));
                        host.onActorDied(victim, killer);
                        if (mPapyrus != nullptr)
                            mPapyrus->queueKill(victim, killer); // task 0211: the story manager's KILL, raised in frame()
                    },
                    [this](FormId actor, openeden::sdk::game::CombatRequestWriter& writer) {
                        const openeden::sdk::game::ActorHandle handle = mCellSource.actorTable().handleOf(actor);
                        if (!handle.valid())
                            return;
                        (void)writer.reportDeath(handle);
                        (void)mCellSource.combat().setFighterHealth(
                            openeden::sdk::game::CombatSubject::of(handle), 0.f);
                    });
                if (openeden::sdk::BindingsService* const bindings = sdkHost.getBindingsService())
                {
                    mPapyrusBindings = std::make_unique<PapyrusNotificationBindings>(host);
                    const openeden::sdk::DeclareResult declared = mPapyrusBindings->declareTo(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the Papyrus notification package was refused: " + declared.message);
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info,
                        std::string("PAPYRUS the world natives are wired (inventory ")
                            + (mInventory != nullptr ? "on" : "off") + ")");
            }

            void unwirePapyrusWorld()
            {
                if (mPapyrus != nullptr)
                {
                    mPapyrus->host().setWorldHooks(nullptr);
                    mPapyrus->host().setFastTravelSink({}); // task 0223
                }
                if (mInventory != nullptr)
                    mInventory->model().setChangeSink({});
                mCellSource.scriptBridge().setSinks({}, {}, {});
                mPapyrusBindings.reset();
                mPapyrusHooks.reset();
            }

            // Task 0185: the quest journal (the namespace skyrimse.journal over the dialogue runtime's quest state). It
            // needs no polling: the first script read asks for the quest data and a read rebuilds what changed. The
            // objective flags are the Papyrus host's (Quest.SetObjectiveDisplayed and SetObjectiveCompleted); without
            // the Papyrus host (the setting off) no objective is shown.
            void startJournal(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "journal", false))
                    return;
                const int testQuests = settings->getInt(kPackId, "journal test quests", 0);
                journal::Hooks hooks;
                hooks.mAccess = [this, logger, testQuests, testQuestsApplied = false](journal::Access& out) mutable {
                    SkyrimWorldView::PapyrusAccess access;
                    if (!mWorldView.papyrusAccess(access))
                        return false;
                    out.mQuests = access.mQuests;
                    out.mState = access.mState;
                    out.mText = access.mText;
                    out.mAliasName = access.mAliasName; // task 0217
                    if (testQuests > 0 && !testQuestsApplied)
                    {
                        // a developer aid: a fresh session has no quest with a log text (the start-up stages have none)
                        testQuestsApplied = true;
                        std::vector<FormId> moved;
                        const std::size_t set = journal::applyTestQuests(*access.mQuests, *access.mState, access.mText,
                            static_cast<std::size_t>(testQuests), &moved);
                        if (logger != nullptr)
                        {
                            logger->log(openeden::sdk::LogLevel::Info,
                                "JOURNAL test quests: " + std::to_string(set) + " quests moved to a stage with a log text");
                            for (const FormId id : moved)
                            {
                                const QuestRecord* q = access.mQuests->quest(id);
                                std::string objectives;
                                if (q != nullptr)
                                    for (const QuestObjective& o : q->mObjectives)
                                        objectives += " " + std::to_string(o.mIndex);
                                logger->log(openeden::sdk::LogLevel::Info,
                                    "JOURNAL test quest " + std::to_string(id) + " objectives:" + objectives);
                            }
                        }
                    }
                    return true;
                };
                hooks.mObjective = [this](FormId quest, std::uint16_t index) {
                    if (mPapyrus == nullptr)
                        return journal::ObjectiveFlags();
                    const PapyrusHost::ObjectiveState s = mPapyrus->host().objectiveState(quest, index);
                    return journal::ObjectiveFlags{ s.mDisplayed, s.mCompleted };
                };
                hooks.mObjectiveRevision
                    = [this] { return mPapyrus != nullptr ? mPapyrus->host().objectiveRevision() : std::uint64_t(0); };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mJournal = std::make_unique<journal::JournalService>(std::move(hooks));
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mJournal->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the journal bindings were refused: " + declared.message);
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "JOURNAL the journal is on");
            }

            // Task 0201: the map markers, the discovered state (a save chunk), the quest targets and the namespace
            // skyrimse.map. The markers are read by the dialogue worker (one more walk of each plugin), so the flag
            // must be set before the worker starts.
            void startMap(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "map", false))
                    return;
                mWorldView.setWantMapMarkers(true);
                map::Hooks hooks;
                hooks.mAccess = [this](map::Access& out) {
                    SkyrimWorldView::MapAccess markers;
                    SkyrimWorldView::PapyrusAccess quests;
                    if (!mWorldView.mapAccess(markers) || !mWorldView.papyrusAccess(quests))
                        return false;
                    out.mMarkers = markers.mMarkers;
                    out.mPlaced = markers.mPlaced;
                    out.mCellWorld = markers.mCellWorld;
                    out.mInteriorWorld = markers.mInteriorWorld; // task 0223
                    out.mQuests = quests.mQuests;
                    out.mState = quests.mState;
                    out.mText = quests.mText;
                    out.mParentOf = [this](FormId world) {
                        const SkyrimTerrainIndex* terrain = mWorldView.terrainIndex();
                        openeden::sdk::game::WorldSpaceInfo info;
                        return terrain != nullptr && terrain->worldInfo(world, info) ? static_cast<FormId>(info.parent)
                                                                                    : kNullFormId;
                    };
                    out.mLandBounds = [this](FormId world, std::int32_t& minX, std::int32_t& minY, std::int32_t& width,
                                          std::int32_t& height) {
                        const SkyrimTerrainIndex* terrain = mWorldView.terrainIndex();
                        std::int32_t maxX = 0, maxY = 0;
                        openeden::sdk::game::WorldId landWorld = 0;
                        if (terrain == nullptr || !terrain->landExtent(world, landWorld, minX, minY, maxX, maxY))
                            return false;
                        width = maxX - minX + 1;
                        height = maxY - minY + 1;
                        return true;
                    };
                    out.mLandRow = [this](FormId world, std::int32_t row, std::vector<std::int32_t>& pixels) {
                        const SkyrimTerrainIndex* terrain = mWorldView.terrainIndex();
                        std::int32_t minX = 0, minY = 0, maxX = 0, maxY = 0;
                        openeden::sdk::game::WorldId landWorld = 0;
                        if (terrain == nullptr || !terrain->landExtent(world, landWorld, minX, minY, maxX, maxY)
                            || row < 0 || row > maxY - minY)
                            return false;
                        return terrain->mapRow(landWorld, minY + row, minX,
                            std::min<std::int32_t>(maxX - minX + 1, static_cast<std::int32_t>(map::kPageSize)),
                            pixels);
                    };
                    return true;
                };
                hooks.mObjective = [this](FormId quest, std::uint16_t index) {
                    if (mPapyrus == nullptr)
                        return journal::ObjectiveFlags();
                    const PapyrusHost::ObjectiveState s = mPapyrus->host().objectiveState(quest, index);
                    return journal::ObjectiveFlags{ s.mDisplayed, s.mCompleted };
                };
                hooks.mObjectiveRevision
                    = [this] { return mPapyrus != nullptr ? mPapyrus->host().objectiveRevision() : std::uint64_t(0); };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mMap = std::make_unique<map::MapService>(std::move(hooks));
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mMap->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the map bindings were refused: " + declared.message);
                }
                mMapChunk = std::make_unique<map::MapChunk>(mMap->discovery());
                mMapChunk->setRefusalSink([logger](const std::string& reason) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Error, "the map chunk is refused: " + reason);
                });
                mMapChunk->setRestoredSink([logger](const std::string& what) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, "MAP " + what);
                });
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                    if (!chunks->registerHandler(*mMapChunk) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the map chunk was not registered");
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "MAP the map and compass are on");
            }

            // Task 0218: fast travel. The rules are `travel.*`; this wires them to the map service (markers, discovered
            // state), the combat (who may travel) and the follower table (how many come along; the engine carries them
            // with the player's teleport).
            void startTravel(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "fast travel", false))
                    return;
                if (mMap == nullptr)
                {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "fast travel needs the pack setting map");
                    return;
                }
                travel::Hooks hooks;
                hooks.mReady = [this] { return mCellSource.combat().dataReady(); };
                hooks.mInCombat = [this] { return mCellSource.combat().playerInCombat(); };
                hooks.mEnemiesNear = [this] { return mCellSource.combat().enemiesNear(); };
                hooks.mFollowerCount = [this] {
                    openeden::sdk::game::WorldId ids[16];
                    return mCellSource.actorTable().followers(ids, 16);
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mTravel = std::make_unique<travel::TravelService>(*mMap, std::move(hooks));
                // Task 0223: the carriage drivers' Game.FastTravel (the Papyrus host, when it runs) leaves a request that the
                // global travel script reads
                if (mPapyrus != nullptr)
                    mPapyrus->host().setFastTravelSink(
                        [this](FormId destination) { return mTravel != nullptr && mTravel->queueRequest(destination); });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mTravel->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the travel bindings were refused: " + declared.message);
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "TRAVEL fast travel from the world map is on");
            }

            // Task 0207: the LSCR records (read by the dialogue worker, so the flag must be set before the worker
            // starts) and the menuSafe namespace skyrimse.menus that gives the menu script the next loading tip.
            void startMenus(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "menus", false))
                    return;
                mWorldView.setWantLoadingScreens(true);
                menus::Hooks hooks;
                hooks.mAccess = [this](menus::Access& out) { return mWorldView.loadingAccess(out); };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mMenus = std::make_unique<menus::MenusService>(std::move(hooks));
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mMenus->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the menus bindings were refused: " + declared.message);
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "MENUS the menus and loading tips are on");
            }

            // Task 0225: the character creation. The body follows the working choice at once (the world is paused while
            // the window is open, so nothing here may wait for a game phase), the saved choice is a chunk, and the race's
            // abilities reach the combat rules and its starting skills the progression.
            void startCharacter(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, character::kSettingKey, false))
                    return;
                if (!settings->getBool(kPackId, "menus", false) || !settings->getBool(kPackId, kPlayerBodyKey, false))
                {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the setting 'character creation' needs 'menus' and 'player body': it is off");
                    return;
                }
                character::Hooks hooks;
                hooks.mAccess = [this](character::Access& out) { return mWorldView.characterAccess(out); };
                hooks.mApplyBody = [this](const character::Choice& choice) {
                    PlayerBodyRequest request;
                    character::applyChoice(choice, request);
                    mWorldView.setPlayerLook(choice.mRace, choice.mFemale, std::move(request.mHeadChoice));
                };
                hooks.mRace = [this, logger](const character::RaceOption& race, bool fresh) {
                    std::vector<FormId> standIn; // the abilities the stand-in player record already has
                    if (mCharacter != nullptr && mCharacter->creator() != nullptr)
                        for (const character::RaceOption& option : mCharacter->creator()->races())
                            if (option.mId == kPlayerRace)
                                standIn = option.mAbilities;
                    mCellSource.combat().setPlayerRaceAbilities(race.mAbilities, std::move(standIn));
                    if (fresh)
                    {
                        mPendingBoosts = race.mBoosts;
                        mHaveBoosts = !mPendingBoosts.empty();
                    }
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "CHARACTER race " + race.mName + ": " + std::to_string(race.mAbilities.size())
                                + " abilities, starting skills " + character::describeBoosts(race.mBoosts)
                                + (fresh ? " (new character)" : " (loaded)"));
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mCharacterLog = hooks.mLog;
                mCharacter = std::make_unique<character::CharacterService>(std::move(hooks));
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mCharacter->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the character bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                {
                    if (!chunks->registerHandler(mCharacter->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the character chunk was not registered");
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "CHARACTER the character creation is on");
            }

            // Once a frame: builds the service when the records are read and gives a new character the starting skills
            // of its race as soon as the progression exists (a loaded character already has them in its own save).
            void pollCharacter()
            {
                (void)mCharacter->poll();
                if (!mHaveBoosts || mProgression == nullptr || mProgression->progression() == nullptr)
                    return;
                std::vector<std::pair<std::size_t, std::uint8_t>> boosts;
                for (const RaceSkillBoost& b : mPendingBoosts)
                    if (b.mActorValue >= progression::kFirstSkillValue)
                        boosts.emplace_back(b.mActorValue - progression::kFirstSkillValue, b.mBoost);
                const std::size_t changed = mProgression->progression()->applyRacialBoosts(boosts);
                mHaveBoosts = false;
                mPendingBoosts.clear();
                if (mCharacterLog)
                    mCharacterLog("CHARACTER starting skills applied to " + std::to_string(changed) + " skills");
            }

            // Task 0215: the namespace skyrimse.presentation, declared when menus or music is on.
            void startPresentation(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr)
                    return;
                presentation::Flags flags;
                flags.mMenus = settings->getBool(kPackId, "menus", false);
                flags.mMusic = settings->getBool(kPackId, "music", false);
                if (!flags.declared())
                    return;
                mPresentation = std::make_unique<presentation::PresentationService>(flags);
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mPresentation->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the presentation bindings were refused: " + declared.message);
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info,
                        std::string("PRESENTATION menus ") + (flags.mMenus ? "on" : "off") + ", music "
                            + (flags.mMusic ? "on" : "off") + ": Morrowind's music script gives way");
            }

            // Task 0210: waiting and sleeping. The rules are `rest.*`; this wires them to the combat (who may rest, who
            // is restored), the world view (is that furniture a bed, who owns it), the progression (Rested multiplies
            // skill experience) and the save chunk `rest`.
            void startRest(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "rest", false))
                    return;
                const bool ignoreOwner = settings->getBool(kPackId, "rest test ignore owner", false);
                rest::Hooks hooks;
                hooks.mReady = [this] { return mCellSource.combat().dataReady(); };
                hooks.mInCombat = [this] { return mCellSource.combat().playerInCombat(); };
                hooks.mEnemiesNear = [this] { return mCellSource.combat().enemiesNear(); };
                hooks.mBed = [this, ignoreOwner](std::uint64_t placement, std::uint64_t base) {
                    if (!mWorldView.furnitureIsBed(static_cast<FormId>(base)))
                        return rest::BedState::NotABed;
                    bool owned = false;
                    const auto owner = mWorldView.ownerOfPlacement(static_cast<FormId>(placement));
                    // task 0219: a rented room's bed is the player's while the rental lasts
                    const bool rented = mServices != nullptr && mServices->isRented(static_cast<FormId>(placement));
                    if (owner && owner->owner != openeden::sdk::game::kNoWorldId && !ignoreOwner && !rented)
                    {
                        const FormId who = static_cast<FormId>(owner->owner);
                        owned = !(who == crime::kPlayerNpcId || who == kPlayerFactionId);
                    }
                    return rest::bedStateOf(true, owned);
                };
                hooks.mApply = [this](double seconds, int, bool) {
                    mCellSource.combat().requestRest(static_cast<float>(seconds));
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mRest = std::make_unique<rest::RestService>(std::move(hooks));
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mRest->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the rest bindings were refused: " + declared.message);
                }
                if (mProgression != nullptr)
                    mProgression->setGainMultiplier([this] {
                        return mRest != nullptr ? mRest->rested().skillGain(mRest->now()) : 1.f;
                    });
                mRestChunk = std::make_unique<rest::RestChunk>(mRest->rested());
                mRestChunk->setRefusalSink([logger](const std::string& reason) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Error, "the rest chunk is refused: " + reason);
                });
                mRestChunk->setRestoredSink([logger](const std::string& what) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, "REST " + what);
                });
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                    if (!chunks->registerHandler(*mRestChunk) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the rest chunk was not registered");
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "REST waiting and sleeping are on");
            }

            // Task 0221: harvesting flora. The rules are `harvest.*`; this wires them to the world view (is that base a
            // plant, what does it give), the inventory (the item goes to the player's list; a leveled ingredient is
            // resolved with a seed of the placement and the try), the world source (a harvested plant is disabled
            // until it grows back) and the save chunk `harvest`.
            void startHarvest(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "harvest", false) || mInventory == nullptr)
                    return;
                harvest::Hooks hooks;
                hooks.mPlant = [this](FormId base, harvest::PlantInfo& out) {
                    HarvestData data;
                    std::string name;
                    if (!mWorldView.plantOf(base, data, name))
                        return false;
                    out.mIngredient = data.mIngredient;
                    out.mChances = data.mChances;
                    out.mActivateText = data.mActivateText.mPresent ? data.mActivateText.mText : std::string();
                    out.mName = std::move(name);
                    out.mHide = true;
                    return true;
                };
                hooks.mPick = [this](FormId ingredient, std::uint64_t seed, FormId& item, std::int32_t& count) {
                    std::vector<InventoryEntry> entries(1);
                    entries[0].mItem = ingredient;
                    entries[0].mCount = 1;
                    const std::vector<inventory::Stack> stacks = inventory::resolveContents(
                        entries, mInventory->table(), seed, harvest::kListLevel);
                    if (stacks.empty())
                        return false;
                    item = stacks.front().mItem;
                    count = stacks.front().mCount;
                    return true;
                };
                hooks.mAddToPlayer = [this](FormId item, std::int32_t count) {
                    return mInventory->model().add(inventory::kPlayerOwner, item, count)
                        == inventory::TransferStatus::Ok;
                };
                hooks.mItemName = [this](FormId item) { return mWorldView.displayNameOf(item); };
                const auto cellOf = [this](FormId placement) {
                    FormId cell = kNullFormId;
                    if (const PlacedRefIndex* refs = mWorldView.placedRefs())
                        if (const PlacedEntry* entry = refs->find(placement))
                            cell = entry->mCell;
                    return cell;
                };
                hooks.mHide = [this, cellOf](FormId placement) {
                    return mWorldSources != nullptr
                        && mWorldSources->setObjectEnabled(mCellSource, cellOf(placement), placement, false);
                };
                hooks.mShow = [this, cellOf](FormId placement) {
                    return mWorldSources != nullptr
                        && mWorldSources->setObjectEnabled(mCellSource, cellOf(placement), placement, true);
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mHarvest = std::make_unique<harvest::HarvestService>(std::move(hooks));
                mWorldView.setHarvestPrompts(true);
                mCellSource.setHarvestedLookup([this](openeden::sdk::game::WorldId placement) {
                    return mHarvest != nullptr
                        && mHarvest->book().hidden(static_cast<FormId>(placement), mHarvest->now());
                });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mHarvest->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the harvest bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                {
                    mHarvest->chunk().setRefusalSink([logger](const std::string& why) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Warning, "HARVEST " + why);
                    });
                    mHarvest->chunk().setRestoredSink([logger](std::size_t plants) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info,
                                "HARVEST restored " + std::to_string(plants) + " harvested plants");
                    });
                    if (!chunks->registerHandler(mHarvest->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the harvest chunk was not registered");
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "HARVEST harvesting flora is on");
            }

            // Task 0224: mining ore veins and chopping wood. Under `harvest` (and needs the inventory): what the base's
            // script says (MineOreScript, ResourceFurnitureScript), the player's tool, the inventory, the vein book
            // and the save chunk `mining`. The engine sends the activation of an Activator (and, as ever, of a
            // Furniture); nothing is hidden: a depleted vein only reads as depleted.
            void startMining(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "harvest", false) || mInventory == nullptr)
                    return;
                mining::Hooks hooks;
                hooks.mResource = [this](FormId base, mining::ResourceInfo& out, std::string& name, std::string& verb) {
                    return mWorldView.resourceOf(base, out, name, verb);
                };
                hooks.mToolCount = [this](FormId list) {
                    std::vector<FormId> entries;
                    if (!mWorldView.formListEntries(list, entries))
                        return -1; // not read yet
                    int total = 0;
                    for (const FormId item : entries)
                        total += mInventory->model().count(inventory::kPlayerOwner, item);
                    return total;
                };
                hooks.mWarm = [this] {
                    std::vector<FormId> ignored;
                    (void)mWorldView.formListEntries(1, ignored); // starts the dialogue worker that reads the lists
                };
                hooks.mPick = [this](FormId leveled, std::uint64_t seed, FormId& item, std::int32_t& count) {
                    std::vector<InventoryEntry> entries(1);
                    entries[0].mItem = leveled;
                    entries[0].mCount = 1;
                    const std::vector<inventory::Stack> stacks
                        = inventory::resolveContents(entries, mInventory->table(), seed, mining::kListLevel);
                    if (stacks.empty())
                        return false;
                    item = stacks.front().mItem;
                    count = stacks.front().mCount;
                    return true;
                };
                hooks.mAddToPlayer = [this](FormId item, std::int32_t count) {
                    return mInventory->model().add(inventory::kPlayerOwner, item, count)
                        == inventory::TransferStatus::Ok;
                };
                hooks.mItemName = [this](FormId item) { return mWorldView.displayNameOf(item); };
                hooks.mMessage = [this](FormId message) { return mWorldView.messageTextOf(message); };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mMining = std::make_unique<mining::MiningService>(std::move(hooks));
                mCellSource.setPlacementNameLookup(
                    [this](openeden::sdk::game::WorldId placement, openeden::sdk::game::WorldId base) {
                        return mMining != nullptr
                            ? mMining->nameOf(static_cast<FormId>(placement), static_cast<FormId>(base))
                            : std::string();
                    });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mMining->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the mining bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                {
                    mMining->chunk().setRefusalSink([logger](const std::string& why) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Warning, "MINING " + why);
                    });
                    mMining->chunk().setRestoredSink([logger](std::size_t veins) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info,
                                "MINING restored " + std::to_string(veins) + " veins");
                    });
                    if (!chunks->registerHandler(mMining->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the mining chunk was not registered");
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "MINING mining and woodcutting are on");
            }

            // Task 0182: the inventory service (item table, model, save chunk) and the namespace skyrimse.inventory.
            void startInventory(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "inventory", false))
                    return;
                inventory::Hooks hooks;
                hooks.mInputs = [this](inventory::Inputs& out) {
                    return mWorldView.inventoryInputs(out.mPaths, out.mOrder, out.mLeveled);
                };
                hooks.mName = [this](FormId id) { return mWorldView.displayNameOf(id); };
                hooks.mNpcContents = [this](FormId base, std::vector<InventoryEntry>& out) {
                    return mWorldView.npcInventory(base, out);
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                // Task 0184: the armour's slots, the potion's health and the heal itself.
                hooks.mEquipInfo = [this](FormId item, inventory::EquipInfo& out) {
                    ArmourPiece piece;
                    // task 0199: tempered or enchanted armour wears in the slots of its base
                    if (!mWorldView.armourPieceById(mCrafting != nullptr ? mCrafting->baseOf(item) : item, piece))
                        return false;
                    out = { inventory::EquipKind::Armor, piece.mSlotMask };
                    return true;
                };
                hooks.mHealthOf = [this](FormId effect, float magnitude, std::uint32_t duration) {
                    return mWorldView.healthOfEffect(effect, magnitude, duration);
                };
                hooks.mTaken = [this](std::uint64_t owner, FormId item, std::int32_t count) {
                    onContainerTaken(owner, item, count); // task 0204
                };
                hooks.mApplyHealth = [this](float amount) {
                    // the combat rules must run (the engine's health is written through them)
                    return mCellSource.combat().updates() > 0 && mCellSource.combat().queuePlayerHeal(amount);
                };
                const int gold = settings->getInt(kPackId, kPlayerGoldKey, static_cast<int>(kStandInGold));
                mInventory = std::make_unique<inventory::InventoryService>(
                    std::move(hooks), gold >= 0 ? static_cast<float>(gold) : kStandInGold, kStandInPlayerLevel);
                // Task 0184 (a test aid, empty by default): items the player starts with, as hexadecimal form ids.
                const std::string startItems = settings->getString(kPackId, "inventory test items", "");
                std::vector<inventory::Stack> extra;
                if (!startItems.empty())
                {
                    if (!inventory::parseStackList(startItems, extra))
                    {
                        extra.clear();
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Warning,
                                "the setting 'inventory test items' is not a list of hexadecimal ids with counts; the "
                                "player starts with no extra items");
                    }
                    else
                    {
                        std::vector<inventory::Stack> stacks = mInventory->model().stacks(inventory::kPlayerOwner);
                        stacks.insert(stacks.end(), extra.begin(), extra.end());
                        mInventory->model().setPlayer(std::move(stacks));
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info,
                                "INV the player starts with " + std::to_string(extra.size()) + " extra stacks");
                    }
                }
                // Task 0184: the speech and bribe checks read the inventory's gold, and a key in it opens a lock.
                mWorldView.setPlayerGoldSource([this]() -> float {
                    return mInventory != nullptr
                        ? static_cast<float>(mInventory->model().count(inventory::kPlayerOwner, inventory::kGoldItem))
                        : 0.f;
                });
                mCellSource.setPlayerItemLookup([this](FormId item) {
                    return mInventory != nullptr && mInventory->model().count(inventory::kPlayerOwner, item) > 0;
                });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mInventory->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the inventory bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                {
                    mInventory->chunk().setRefusalSink([logger](const std::string& reason) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Error, "the save is refused: " + reason);
                    });
                    mInventory->chunk().setRestoredSink([logger](const std::string& what) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info, "INV chunk " + what);
                    });
                    if (!chunks->registerHandler(mInventory->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the inventory chunk was not registered");
                    mInventory->equipmentChunk().setRefusalSink([logger](const std::string& reason) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Error, "the save is refused: " + reason);
                    });
                    if (!chunks->registerHandler(mInventory->equipmentChunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the equipment chunk was not registered");
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "INV the inventory is on");
            }

            // Task 0200: the player's progression: the skills a fight, a trade or a craft trains, the level, the
            // perks and their window's bindings; off unless [extension.skyrimse] player progression. The story event
            // LEVL is raised for every level reached.
            void startProgression(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "player progression", false))
                    return;
                progression::ServiceHooks hooks;
                hooks.mInputs = [this](progression::Inputs& out) {
                    return mWorldView.progressionInputs(out.mSkills, out.mPerks, out.mSettings);
                };
                hooks.mLevelUp = [this, logger](std::uint16_t level) {
                    if (mPapyrus == nullptr)
                        return;
                    StoryEvent event;
                    event.mType = "LEVL";
                    event.mValue1 = level;
                    const std::size_t started = mPapyrus->host().raiseStoryEvent(event);
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "PROGRESSION story event LEVL for level " + std::to_string(level) + " started "
                                + std::to_string(started) + " quests");
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                const int startLevel = settings->getInt(kPackId, kPlayerLevelKey, kStandInPlayerLevel);
                hooks.mStartLevel = static_cast<std::uint16_t>(std::clamp(startLevel, 1, 65535));
                mProgression = std::make_unique<progression::ProgressionService>(std::move(hooks));
                // a developer aid: every use of a skill counts this many times (default 1), so that a short run
                // shows levels
                float boost = settings->getFloat(kPackId, "progression test amount", 1.f);
                if (!std::isfinite(boost) || boost < 0.f || boost > 10000.f)
                    boost = 1.f;
                mProgressionBoost = boost;
                mCellSource.combat().setSkillUseHook(
                    [this, boost](std::size_t slot, float amount) { mProgression->skillUsed(slot, amount * boost); });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mProgression->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the progression bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                {
                    if (!chunks->registerHandler(mProgression->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the progression chunk was not registered");
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "PROGRESSION the player progression is on");
            }

            // Once a frame: builds the progression when the records are read, hands it to the combat rules and the
            // speech checks when it changed.
            void pollProgression()
            {
                if (!mProgression->poll())
                    return;
                progression::PlayerProgression* p = mProgression->progression();
                if (!mProgressionLinked)
                {
                    mProgressionLinked = true;
                    mCellSource.combat().setPlayerProgression(p);
                }
                if (p->revision() != mProgressionPushed)
                {
                    mProgressionPushed = p->revision();
                    std::array<std::uint8_t, progression::kSkillCount> skills{};
                    for (std::size_t s = 0; s < progression::kSkillCount; ++s)
                        skills[s] = p->skill(s);
                    mWorldView.setProgressionPlayer(p->level(), skills);
                }
            }

            // Task 0204: crime. Off unless [extension.skyrimse] crime. Theft needs the inventory (the window hook),
            // guards need the combat AI (they walk and fight through it).
            void startCrime(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "crime", false))
                    return;
                crime::ServiceHooks hooks;
                hooks.mActors = [this](std::vector<crime::CrimeActor>& out) { mCellSource.combat().crimeActors(out); };
                hooks.mActor = [this](openeden::sdk::game::ActorHandle h, crime::CrimeActor& out) {
                    return mCellSource.combat().crimeActor(h, out);
                };
                hooks.mCrimeData = [this](FormId f) { return mWorldView.crimeDataOf(f); };
                hooks.mFactionName = [this](FormId f) { return mWorldView.factionNameOf(f); };
                hooks.mItemValue = [this](FormId item) -> std::int32_t {
                    if (mInventory == nullptr)
                        return 0;
                    const inventory::ItemStat* stat = mInventory->table().item(item);
                    return stat != nullptr ? stat->mValue : 0;
                };
                hooks.mGold = [this]() -> std::int32_t {
                    return mInventory != nullptr
                        ? mInventory->model().count(inventory::kPlayerOwner, inventory::kGoldItem)
                        : 0;
                };
                hooks.mTakeGold = [this](std::int32_t amount) {
                    return mInventory != nullptr
                        && mInventory->model().consume(inventory::kPlayerOwner, inventory::kGoldItem, amount)
                        == inventory::TransferStatus::Ok;
                };
                hooks.mTakeItem = [this](FormId item, std::int32_t count) -> std::int32_t {
                    if (mInventory == nullptr)
                        return 0;
                    const std::int32_t take = std::min(count, mInventory->model().count(inventory::kPlayerOwner, item));
                    if (take <= 0
                        || mInventory->model().consume(inventory::kPlayerOwner, item, take)
                            != inventory::TransferStatus::Ok)
                        return 0;
                    return take;
                };
                hooks.mItemCount = [this](FormId item) -> std::int32_t {
                    return mInventory != nullptr ? mInventory->model().count(inventory::kPlayerOwner, item) : 0;
                };
                hooks.mGoTo = [this](openeden::sdk::game::ActorHandle h, double x, double y, double z,
                                  openeden::sdk::game::ActorSpeed speed) {
                    mCellSource.combat().crimeGoTo(h, x, y, z, speed);
                };
                hooks.mHold = [this](openeden::sdk::game::ActorHandle h) { mCellSource.combat().crimeHold(h); };
                hooks.mRelease = [this](openeden::sdk::game::ActorHandle h) { mCellSource.combat().crimeRelease(h); };
                hooks.mAttack = [this](openeden::sdk::game::ActorHandle h) { mCellSource.combat().crimeAttack(h); };
                hooks.mJail = [this](FormId faction, crime::JailDestination& out) {
                    return mWorldView.jailDestination(faction, out);
                };
                // Task 0213: trespass reads the CELL records (owner, public flag) and the owner of a bed placement.
                hooks.mCellAccess = [this](const std::string& editorId, crime::CellAccess& out) {
                    return mWorldView.cellAccessByName(editorId, out);
                };
                hooks.mPlacementOwner = [this](FormId placement) {
                    if (mServices != nullptr && mServices->isRented(placement))
                        return crime::kPlayerNpcId; // task 0219: a rented room's bed is the player's
                    const auto owner = mWorldView.ownerOfPlacement(placement);
                    return owner && owner->owner != openeden::sdk::game::kNoWorldId
                        ? static_cast<FormId>(owner->owner)
                        : kNullFormId;
                };
                hooks.mLog = [this, logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mCrime = std::make_unique<crime::CrimeService>(std::move(hooks));
                mCellSource.combat().setCrimeTestGuard(settings->getString(kPackId, "crime test guard", ""));
                mCellSource.setCrime(&mCrime->system());
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mCrime->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the crime bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                {
                    mCrime->chunk().setRefusalSink([logger](const std::string& why) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Warning, "CRIME " + why);
                    });
                    if (!chunks->registerHandler(mCrime->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the crime chunk was not registered");
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "CRIME crime rules are on");
            }

            // Task 0208: picking up placed items and pickpocketing. On with the inventory (no setting of its own); a
            // theft or a caught try is a crime only when the crime rules are on.
            void startPickup(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                if (mInventory == nullptr)
                    return;
                pickup::Hooks hooks;
                hooks.mItem = [this](FormId item, std::int32_t& value, float& weight) {
                    const inventory::ItemStat* stat = mInventory->table().item(item);
                    if (stat == nullptr)
                        return false;
                    value = stat->mValue;
                    weight = stat->mWeight;
                    return true;
                };
                hooks.mCount = [this](FormId placement) { return mWorldView.itemCountOfPlacement(placement); };
                hooks.mAddToPlayer = [this](FormId item, std::int32_t count) {
                    return mInventory->model().add(inventory::kPlayerOwner, item, count)
                        == inventory::TransferStatus::Ok;
                };
                hooks.mDisable = [this](FormId placement) {
                    if (mWorldSources == nullptr)
                        return false;
                    FormId cell = kNullFormId;
                    if (const PlacedRefIndex* refs = mWorldView.placedRefs())
                        if (const PlacedEntry* entry = refs->find(placement))
                            cell = entry->mCell;
                    return mWorldSources->setObjectEnabled(mCellSource, cell, placement, false);
                };
                hooks.mActivated = [this](FormId placement) { mCellSource.notifyActivated(placement); };
                hooks.mTaken = [this](FormId placement, FormId item, std::int32_t count) {
                    onContainerTaken(placement, item, count);
                };
                hooks.mHeldBy = [this](std::uint64_t owner, FormId item) {
                    return mInventory->model().count(owner, item);
                };
                hooks.mMoveToPlayer = [this](std::uint64_t owner, FormId item, std::int32_t count) {
                    return mInventory->model().transfer(owner, inventory::kPlayerOwner, item, count)
                        == inventory::TransferStatus::Ok;
                };
                hooks.mSkill = [this]() {
                    float skill = 0.f;
                    constexpr std::uint32_t kPickpocketValue
                        = progression::kFirstSkillValue + progression::Slot::Pickpocket;
                    if (mProgression != nullptr && mProgression->actorValue(kPickpocketValue, skill))
                        return skill;
                    return pickup::kStandInPickpocket;
                };
                hooks.mTrain = [this](float amount) {
                    if (mProgression != nullptr)
                        mProgression->skillUsed(progression::Slot::Pickpocket, amount);
                };
                hooks.mCaught = [this](std::uint64_t owner, FormId, std::int32_t) {
                    if (mCrime != nullptr)
                        mCrime->system().onPickpocketCaught(static_cast<std::uint32_t>(owner));
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mPickup = std::make_unique<pickup::PickupService>(std::move(hooks));
                mCellSource.setTakenLookup([this](openeden::sdk::game::WorldId placement) {
                    return mPickup != nullptr && mPickup->book().taken(static_cast<FormId>(placement));
                });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mPickup->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the pickup bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                {
                    mPickup->chunk().setRefusalSink([logger](const std::string& why) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Warning, "PICKUP " + why);
                    });
                    if (!chunks->registerHandler(mPickup->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the pickups chunk was not registered");
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "PICKUP picking up items and pickpocketing are on");
            }

            // Task 0204: the player took an item out of a container: theft when somebody else owns the placement.
            void onContainerTaken(std::uint64_t placement, FormId item, std::int32_t count)
            {
                if (mCrime == nullptr)
                    return;
                const auto owner = mWorldView.ownerOfPlacement(static_cast<FormId>(placement));
                if (!owner || owner->owner == openeden::sdk::game::kNoWorldId)
                    return;
                const FormId who = static_cast<FormId>(owner->owner);
                // Things of the player: the Player NPC_ (form id 7) and the player faction.
                if (who == crime::kPlayerNpcId || who == kPlayerFactionId)
                    return;
                mCrime->theft(item, count);
            }

            // Task 0189: the barter service over the inventory (on whenever the inventory is).
            void startBarter(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                if (mInventory == nullptr)
                    return;
                barter::ServiceHooks hooks;
                hooks.mVendorOf = [this](FormId npc, barter::VendorInfo& out) { return mWorldView.vendorOf(npc, out); };
                hooks.mSpeech = [this]() {
                    float speech = 0.f; // task 0200: the real skill with the progression on
                    constexpr std::uint32_t kSpeechValue = progression::kFirstSkillValue + progression::Slot::Speechcraft;
                    if (mProgression != nullptr && mProgression->actorValue(kSpeechValue, speech))
                        return speech;
                    return static_cast<float>(mWorldView.standInPlayerSpeech());
                };
                hooks.mTraded = [this](float gold) {
                    if (mProgression != nullptr)
                        mProgression->skillUsed(progression::Slot::Speechcraft, gold);
                };
                hooks.mStolenHeld = [this](FormId item) { // task 0204
                    return mCrime != nullptr ? mCrime->stolenHeld(item) : 0;
                };
                hooks.mStolenSold = [this](FormId item, std::int32_t count) {
                    if (mCrime != nullptr)
                        (void)mCrime->ledger().forgetStolen(item, count);
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mBarter = std::make_unique<barter::BarterService>(*mInventory, std::move(hooks));
                // a merchant offers its services (the barter line's GetOffersServicesNow condition), see dialogue.hpp
                mWorldView.setVendorServicesLookup([this](FormId base) {
                    barter::VendorInfo info;
                    return mWorldView.vendorOf(base, info) == barter::VendorLookup::Ready;
                });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mBarter->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the barter bindings were refused: " + declared.message);
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "BARTER the barter service is on");
            }

            // Task 0219: the dialogue services that are not barter: training, renting a room and gifts. Off unless
            // [extension.skyrimse] services (and the inventory, which holds the gold); training also needs
            // `player progression` (it raises the skill through it).
            void startServices(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (mInventory == nullptr || settings == nullptr || !settings->getBool(kPackId, "services", false))
                    return;
                services::Hooks hooks;
                hooks.mTrainerOf = [this](FormId npc, services::TrainerInfo& out) {
                    SkyrimWorldView::ClassTraining data;
                    const barter::VendorLookup found = mWorldView.trainingOf(npc, data);
                    if (found == barter::VendorLookup::Pending)
                        return services::Lookup::Pending;
                    if (found != barter::VendorLookup::Ready
                        || !services::makeTrainer(data.mHasData, data.mSkill, data.mMaxLevel, out))
                        return services::Lookup::None;
                    return services::Lookup::Ready;
                };
                hooks.mRentOf = [this](FormId npc, services::RentOffer& out) {
                    const barter::VendorLookup found = mWorldView.rentBedOf(npc, out.mBed);
                    if (found == barter::VendorLookup::Pending)
                        return services::Lookup::Pending;
                    return found == barter::VendorLookup::Ready ? services::Lookup::Ready : services::Lookup::None;
                };
                hooks.mSkillLevel = [this](std::size_t slot) {
                    float value = 0.f;
                    if (mProgression != nullptr
                        && mProgression->actorValue(
                            progression::kFirstSkillValue + static_cast<std::uint32_t>(slot), value))
                        return static_cast<int>(value);
                    return -1;
                };
                hooks.mPlayerLevel = [this]() -> std::uint16_t {
                    if (mProgression != nullptr && mProgression->progression() != nullptr)
                        return mProgression->progression()->level();
                    return 0;
                };
                hooks.mRaise
                    = [this](std::size_t slot) { return mProgression != nullptr && mProgression->raiseSkill(slot); };
                hooks.mSkillName = [](std::size_t slot) { return std::string(progression::skillName(slot)); };
                hooks.mGold = [this]() -> std::int64_t {
                    return mInventory->model().count(inventory::kPlayerOwner, inventory::kGoldItem);
                };
                hooks.mPay = [this](std::int32_t amount) {
                    return mInventory->model().consume(inventory::kPlayerOwner, inventory::kGoldItem, amount)
                        == inventory::TransferStatus::Ok;
                };
                hooks.mOpenOwner = [this](std::uint64_t placement, FormId npcBase) {
                    return mInventory->open(placement, npcBase, "actor");
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mServices = std::make_unique<services::ServicesService>(std::move(hooks));
                mWorldView.setOpenServices(true);
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mServices->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the services bindings were refused: " + declared.message);
                }
                mServicesChunk = std::make_unique<services::ServicesChunk>(mServices->ledger(), mServices->rentals());
                mServicesChunk->setRefusalSink([logger](const std::string& reason) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Error, "the services chunk is refused: " + reason);
                });
                mServicesChunk->setRestoredSink([logger](const std::string& what) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, "SERVICES " + what);
                });
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                    if (!chunks->registerHandler(*mServicesChunk) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the services chunk was not registered");
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info,
                        "SERVICES training, rooms and gifts are on (training needs the player progression)");
            }

            // Task 0203: lock picking and books, off unless [extension.skyrimse] lockpicking and books (and the
            // inventory).
            void startLockBooks(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (mInventory == nullptr || settings == nullptr
                    || !settings->getBool(kPackId, "lockpicking and books", false))
                    return;
                const std::string language = settings->getString(kPackId, "string table language", "english");
                mBookLanguage = "english";
                if (language.size() >= 2 && language.size() <= 32
                    && std::all_of(
                        language.begin(), language.end(), [](unsigned char c) { return std::isalpha(c) != 0; }))
                    mBookLanguage = language;
                lockbooks::ServiceHooks hooks;
                hooks.mInputs = [this](lockbooks::BookInputs& out) {
                    std::vector<LeveledList> unused;
                    if (!mWorldView.inventoryInputs(out.mPaths, out.mOrder, unused))
                        return false;
                    mBookPaths = out.mPaths;
                    return true;
                };
                hooks.mResolve = [this](const LString& text, std::uint32_t plugin, StringTableKind kind) {
                    return resolveBookString(text, plugin, kind);
                };
                hooks.mName = [this](FormId id) { return mWorldView.displayNameOf(id); };
                hooks.mSkill = [this]() {
                    float skill = 15.f; // a new character's start skill (progression off)
                    if (mProgression != nullptr)
                        (void)mProgression->actorValue(
                            progression::kFirstSkillValue + progression::Slot::Lockpicking, skill);
                    return skill;
                };
                hooks.mSkillUsed = [this](std::size_t slot, float amount) {
                    if (mProgression != nullptr)
                        mProgression->skillUsed(slot, amount);
                };
                hooks.mRaiseSkill
                    = [this](std::size_t slot) { return mProgression != nullptr && mProgression->raiseSkill(slot); };
                hooks.mSkillName = [](std::size_t slot) { return std::string(progression::skillName(slot)); };
                hooks.mSpellName = [this](FormId spell) -> std::string {
                    const SpellRecord* record = mWorldView.spellById(spell);
                    return record != nullptr ? progression::prettyPerkName(record->mCommon.mEditorId) : std::string();
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mLockBooks = std::make_unique<lockbooks::LockBooksService>(*mInventory, std::move(hooks));
                mCellSource.setLockpickLookups([this](std::uint64_t placement) {
                    return mLockBooks != nullptr && mLockBooks->lockPicked(placement);
                });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mLockBooks->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the lockbooks bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                {
                    mLockBooks->chunk().setRefusalSink([logger](const std::string& reason) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Warning, "the lockbooks chunk was refused: " + reason);
                    });
                    if (!chunks->registerHandler(mLockBooks->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the lockbooks chunk was not registered");
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "LOCKBOOKS lock picking and books are on");
            }

            // Task 0203: the text of a localised book string, from the plugin's own string table (parsed once).
            std::string resolveBookString(const LString& text, std::uint32_t plugin, StringTableKind kind)
            {
                if (!text.mPresent)
                    return {};
                if (!text.mLocalized)
                    return text.mText;
                if (mFileSource == nullptr || plugin >= mBookPaths.size())
                    return {};
                auto& slot = mBookTables[{ plugin, static_cast<int>(kind) }];
                if (slot == nullptr)
                {
                    auto table = std::make_unique<StringTable>();
                    std::string bytes;
                    Content::Error error;
                    const std::string name = mBookPaths[plugin].filename().string();
                    const std::string path = stringTablePath(mBookPaths[plugin].stem().string(), mBookLanguage, kind);
                    if (mFileSource->read(path, bytes)
                        && StringTable::parse({ reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() }, kind,
                            name, *table, error))
                        slot = std::move(table);
                    else
                        slot = std::make_unique<StringTable>(); // empty: not tried again
                }
                const std::string* found = slot->find(text.mId);
                return found != nullptr ? *found : std::string();
            }

            // Task 0203: the last spell learned from a tome (or restored by a load) becomes the spell the player casts,
            // unless a test setting already named one. The spell's record may not be read yet: asked again next frame.
            void syncLearnedSpell()
            {
                const std::vector<FormId>& spells = mLockBooks->model().spells();
                const FormId last = spells.empty() ? kNullFormId : spells.back();
                if (spells.size() == mSyncedSpells && last == mSyncedLastSpell)
                    return;
                if (mPlayerSpellFromSetting)
                {
                    mSyncedSpells = spells.size();
                    mSyncedLastSpell = last;
                    return;
                }
                if (spells.empty())
                {
                    // a load without any spell: the cast spell goes too
                    mSyncedSpells = 0;
                    mSyncedLastSpell = kNullFormId;
                    (void)mCellSource.combat().setTestPlayerSpell("");
                    return;
                }
                const SpellRecord* record = mWorldView.spellById(last);
                if (record == nullptr || record->mCommon.mEditorId.empty())
                    return;
                mSyncedSpells = spells.size();
                mSyncedLastSpell = last;
                (void)mCellSource.combat().setTestPlayerSpell(record->mCommon.mEditorId);
                mLockBooks->logLine("LOCKBOOKS the player casts " + record->mCommon.mEditorId);
            }

            // Task 0199: crafting over the inventory (on whenever the setting `crafting` and the inventory are).
            void startCrafting(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (mInventory == nullptr || settings == nullptr || !settings->getBool(kPackId, "crafting", false))
                    return;
                mCraftLogger = logger;
                crafting::CraftingHooks hooks;
                hooks.mInputs = [this](inventory::Inputs& out) {
                    return mWorldView.inventoryInputs(out.mPaths, out.mOrder, out.mLeveled);
                };
                hooks.mName = [this](FormId id) { return mWorldView.displayNameOf(id); };
                hooks.mEffectInfo = [this](FormId effect) {
                    crafting::EffectInfo info;
                    std::uint32_t flags = 0, archetype = 0;
                    float cost = 0.f;
                    std::int32_t primary = -1;
                    constexpr std::int32_t kHealthValue = 24; // the Health actor value (RECALLED-VERIFY, as combat)
                    constexpr std::uint32_t kHostile = 0x1, kDetrimental = 0x4; // MGEF DATA flags (RECALLED-VERIFY)
                    if (!mWorldView.magicEffectData(effect, flags, cost, archetype, primary))
                        return info;
                    info.mFound = true;
                    info.mBaseCost = cost;
                    info.mHostile = (flags & (kHostile | kDetrimental)) != 0;
                    info.mDamageHealth = info.mHostile && archetype == 0 && primary == kHealthValue;
                    return info;
                };
                const int alchemy = settings->getInt(kPackId, "crafting alchemy skill", 15);
                const int smithing = settings->getInt(kPackId, "crafting smithing skill", 15);
                const int enchanting = settings->getInt(kPackId, "crafting enchanting skill", 15);
                hooks.mSkill = [alchemy, smithing, enchanting](std::string_view skill) {
                    const int v = skill == "alchemy" ? alchemy : skill == "smithing" ? smithing : enchanting;
                    return static_cast<float>(std::clamp(v, 0, 100));
                };
                // A skills system may set mCraftingSkillSink; the log line is always written.
                hooks.mSkillUsed = [this, logger](std::string_view skill, float amount) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "CRAFT skill used: " + std::string(skill) + " +" + std::to_string(amount));
                    if (mCraftingSkillSink)
                        mCraftingSkillSink(skill, amount);
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                // Task 0206: a craft trains the player's skill, the formulas read the real one and a recipe's perk
                // conditions are answered from the bought perks (only with the progression on; the progression is
                // destroyed before the crafting service but is never called from its destructor).
                if (mProgression != nullptr)
                    crafting::linkProgression(hooks, *mProgression, mProgressionBoost);
                mCrafting = std::make_unique<crafting::CraftingService>(*mInventory, std::move(hooks),
                    crafting::CraftingSettings(), settings->getBool(kPackId, "crafting ignore conditions", false),
                    settings->getBool(kPackId, "crafting test bench", false));
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mCrafting->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the crafting bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                {
                    mCrafting->chunk().setRefusalSink([logger](const std::string& reason) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Error, "the save is refused: " + reason);
                    });
                    mCrafting->chunk().setRestoredSink([logger](const std::string& what) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info, "CRAFT chunk " + what);
                    });
                    if (!chunks->registerHandler(mCrafting->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the crafting chunk was not registered");
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "CRAFT the crafting service is on");
            }

            // Task 0222: what the player wears and wields reaches the picture of his body whenever it changes. Made
            // gear shows as its base record (as the combat's pieces do); a shield is armour.
            void syncPlayerBody()
            {
                if (mInventory == nullptr)
                    return;
                const std::uint32_t revision = mInventory->model().equipmentRevision();
                if (revision == mBodySynced)
                    return;
                mBodySynced = revision;
                std::vector<FormId> armour;
                FormId weapon = kNullFormId;
                for (const inventory::EquippedItem& worn : mInventory->model().equipped())
                {
                    const FormId base = mCrafting != nullptr ? mCrafting->baseOf(worn.mItem) : worn.mItem;
                    if (worn.mKind == inventory::EquipKind::Weapon)
                        weapon = base;
                    else
                        armour.push_back(base);
                }
                mWorldView.setPlayerEquipment(std::move(armour), weapon);
            }

            // Task 0184: what the player wields and wears reaches the combat rules whenever it changes. A weapon whose
            // record is not read yet (the combat records load on a worker) is looked up again next frame.
            void syncEquipment()
            {
                const std::uint32_t revision = mInventory->model().equipmentRevision();
                if (revision == mEquipSynced && !mEquipPending)
                    return;
                SkyrimCombat::PlayerEquipment equipment;
                bool pending = false;
                for (const inventory::EquippedItem& worn : mInventory->model().equipped())
                {
                    if (worn.mKind == inventory::EquipKind::Weapon)
                    {
                        const WeaponRecord* weapon = nullptr;
                        // task 0199: made (tempered, enchanted) gear reads as its base with the changed numbers
                        const FormId weaponBase = mCrafting != nullptr ? mCrafting->baseOf(worn.mItem) : worn.mItem;
                        if (!mWorldView.lookupWeapon(weaponBase, weapon))
                            pending = true;
                        else if (weapon != nullptr && mCrafting != nullptr
                            && mCrafting->weaponFor(worn.mItem, *weapon, mMadeWeapon))
                            equipment.mWeapon = &mMadeWeapon;
                        else
                            equipment.mWeapon = weapon;
                    }
                    else
                    {
                        ArmourPiece piece;
                        const FormId armourBase = mCrafting != nullptr ? mCrafting->baseOf(worn.mItem) : worn.mItem;
                        if (mWorldView.armourPieceById(armourBase, piece))
                        {
                            if (mCrafting != nullptr)
                                (void)mCrafting->armourFor(worn.mItem, piece); // task 0199
                            equipment.mPieces.push_back(std::move(piece));
                        }
                    }
                }
                if (pending && !mEquipPending && mCraftLogger != nullptr && mCrafting != nullptr)
                    mCraftLogger->log(
                        openeden::sdk::LogLevel::Info, "CRAFT combat equipment: the weapon record is not read yet");
                mEquipPending = pending;
                if (pending)
                    return;
                mEquipSynced = revision;
                // task 0199: what the combat rules read of the made gear
                if (mCraftLogger != nullptr && mCrafting != nullptr)
                    mCraftLogger->log(openeden::sdk::LogLevel::Info,
                        "CRAFT combat equipment: weapon damage "
                            + (equipment.mWeapon != nullptr ? std::to_string(equipment.mWeapon->mDamage)
                                                            : std::string("none"))
                            + ", " + std::to_string(equipment.mPieces.size()) + " armour pieces");
                mCellSource.combat().setPlayerEquipment(std::move(equipment));
            }

            // Task 0177 (neutral saves): the save chunk that ties a save to the load order it was made with.
            void registerLoadOrderStamp(openeden::sdk::Host& host, openeden::sdk::game::WorldSourceService& sources,
                openeden::sdk::Logger* logger)
            {
                std::vector<openeden::sdk::game::SessionContentFile> files;
                sources.sessionContent(files);
                std::vector<std::string> names;
                names.reserve(files.size());
                for (const openeden::sdk::game::SessionContentFile& file : files)
                    names.push_back(Files::pathFromUnicodeString(file.path).filename().string());
                mLoadOrderStamp.setLoadOrder(std::move(names));
                mLoadOrderStamp.setRefusalSink([logger](const std::string& reason) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Error, "the save is refused: " + reason);
                });
                openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService();
                if (chunks == nullptr || !chunks->registerHandler(mLoadOrderStamp))
                {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the load order stamp chunk was not registered");
                }
            }

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
                    // Task 0209 (music): off unless the setting is true; the seed picks tracks (no clock).
                    mCellSource.setMusic(settings->getBool(kPackId, "music", false),
                        static_cast<std::uint64_t>(std::max(0, settings->getInt(kPackId, "music seed", 1))));
                    // Task 0135 stage S3: the test rule of the actor hooks, off unless the setting names an editor id.
                    const std::string walker = settings->getString(kPackId, kActorTestWalkKey, "");
                    if (!mCellSource.actorTable().setTestWalk(walker))
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kActorTestWalkKey) + "' is too long; the test walk is off");
                    else if (!walker.empty())
                        say(openeden::sdk::LogLevel::Info, "actor test walk: actors named " + walker + " pace");
                    // Task 0170 (combat stage S5-2): the test swing, off unless the setting names an editor id.
                    const std::string swinger = settings->getString(kPackId, kCombatTestSwingKey, "");
                    if (!mCellSource.combat().setTestSwing(swinger))
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kCombatTestSwingKey)
                                + "' is too long; the test swing is off");
                    else if (!swinger.empty())
                        say(openeden::sdk::LogLevel::Info, "combat test swing: actors named " + swinger + " swing");
                    // Task 0171 (combat stage S5-3): the melee test rules and the decision trace, all off by default.
                    const std::string hostile = settings->getString(kPackId, kCombatTestHostileKey, "");
                    if (!mCellSource.combat().setTestHostile(hostile))
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kCombatTestHostileKey) + "' is too long; it is off");
                    else if (!hostile.empty())
                        say(openeden::sdk::LogLevel::Info, "combat test hostile: actors named " + hostile + " attack");
                    // Task 0192: the crowd test (many fighters), off unless a count is given.
                    const int crowd = settings->getInt(kPackId, kCombatTestCrowdKey, 0);
                    const int duels = settings->getInt(kPackId, kCombatTestDuelsKey, 0);
                    mCellSource.combat().setTestCrowd(
                        crowd, duels, settings->getFloat(kPackId, kCombatTestCrowdDelayKey, kDefaultCrowdDelay));
                    if (crowd > 0 || duels > 0)
                        say(openeden::sdk::LogLevel::Info,
                            "combat test crowd: " + std::to_string(crowd) + " hostile, " + std::to_string(duels)
                                + " duels");
                    const std::string weapon = settings->getString(kPackId, kCombatTestWeaponKey, "");
                    if (!mCellSource.combat().setTestWeapon(weapon))
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kCombatTestWeaponKey)
                                + "' is too long; fighters are unarmed");
                    else if (!weapon.empty())
                        say(openeden::sdk::LogLevel::Info, "combat test weapon: " + weapon);
                    // Task 0214: fighters use their own melee weapon and shield (the test weapon still overrides).
                    if (settings->getBool(kPackId, kCombatRealGearKey, false))
                    {
                        mCellSource.combat().setRealGear(true);
                        say(openeden::sdk::LogLevel::Info, "combat real gear: on");
                    }
                    const float playerSwing = settings->getFloat(kPackId, kCombatTestPlayerSwingKey, 0.f);
                    mCellSource.combat().setTestPlayerSwing(playerSwing);
                    if (playerSwing > 0.f)
                        say(openeden::sdk::LogLevel::Info,
                            "combat test player swing: every " + std::to_string(playerSwing) + " s");
                    // Task 0172 (stage S5-4): the block, power attack and bash test rules.
                    const std::string blocker = settings->getString(kPackId, kCombatTestBlockKey, "");
                    if (!mCellSource.combat().setTestBlock(blocker))
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kCombatTestBlockKey) + "' is too long; it is off");
                    else if (!blocker.empty())
                        say(openeden::sdk::LogLevel::Info, "combat test block: actors named " + blocker + " block");
                    const int powerEvery = settings->getInt(kPackId, kCombatTestPowerEveryKey, 0);
                    mCellSource.combat().setTestPowerEvery(powerEvery);
                    if (powerEvery > 0)
                        say(openeden::sdk::LogLevel::Info,
                            "combat test power every: " + std::to_string(powerEvery) + " attacks");
                    const int bashEvery = settings->getInt(kPackId, kCombatTestBashEveryKey, 0);
                    mCellSource.combat().setTestBashEvery(bashEvery);
                    if (bashEvery > 0)
                        say(openeden::sdk::LogLevel::Info,
                            "combat test bash every: " + std::to_string(bashEvery) + " attacks");
                    // Task 0178 (stage S5-6): the bow test rules, all off by default.
                    const std::string ammo = settings->getString(kPackId, kCombatTestAmmoKey, "");
                    if (!mCellSource.combat().setTestAmmo(ammo))
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kCombatTestAmmoKey) + "' is too long; nobody shoots");
                    else if (!ammo.empty())
                        say(openeden::sdk::LogLevel::Info, "combat test ammo: " + ammo);
                    const float playerBow = settings->getFloat(kPackId, kCombatTestPlayerBowKey, 0.f);
                    mCellSource.combat().setTestPlayerBow(playerBow);
                    if (playerBow > 0.f)
                        say(openeden::sdk::LogLevel::Info,
                            "combat test player bow: holds " + std::to_string(playerBow) + " s");
                    const float bowBonus = settings->getFloat(kPackId, kCombatTestBowSpeedBonusKey, 0.f);
                    mCellSource.combat().setTestBowSpeedBonus(bowBonus);
                    if (bowBonus != 0.f)
                        say(openeden::sdk::LogLevel::Info,
                            "combat test bow speed bonus: " + std::to_string(bowBonus));
                    // Task 0186 (stage S5-8): the spell test rules, all off by default.
                    const std::string playerSpell = settings->getString(kPackId, kCombatTestPlayerSpellKey, "");
                    mPlayerSpellFromSetting = !playerSpell.empty(); // task 0203
                    if (!mCellSource.combat().setTestPlayerSpell(playerSpell))
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kCombatTestPlayerSpellKey) + "' is too long; no spell");
                    else if (!playerSpell.empty())
                        say(openeden::sdk::LogLevel::Info, "combat test player spell: " + playerSpell);
                    mCellSource.combat().setTestPlayerCastEvery(
                        settings->getFloat(kPackId, kCombatTestPlayerCastEveryKey, 0.f));
                    mCellSource.combat().setTestPlayerCastHold(
                        settings->getFloat(kPackId, kCombatTestPlayerCastHoldKey, kDefaultCastHold));
                    mCellSource.combat().setTestCastEvery(
                        settings->getFloat(kPackId, kCombatTestCastEveryKey, kDefaultCastPeriod));
                    mCellSource.combat().setTestCastHold(
                        settings->getFloat(kPackId, kCombatTestCastHoldKey, kDefaultCastHold));
                    const std::string casts = settings->getString(kPackId, kCombatTestCastKey, "");
                    if (!mCellSource.combat().setTestCasts(casts))
                        say(openeden::sdk::LogLevel::Warning,
                            "the setting '" + std::string(kCombatTestCastKey) + "' is not a list of actor:spell");
                    else if (!casts.empty())
                        say(openeden::sdk::LogLevel::Info, "combat test cast: " + casts);
                    mCellSource.combat().setTestSpellConditions(
                        settings->getBool(kPackId, kCombatTestSpellConditionsKey, false));
                    // Task 0191: held aimed spells, touch spells and the drawn spell effects (both off by default).
                    mCellSource.combat().setSpellBeams(settings->getBool(kPackId, kCombatSpellBeamsKey, false));
                    mCellSource.combat().setSpellVisuals(settings->getBool(kPackId, kCombatSpellVisualsKey, false));
                    // Task 0179 (stage S5-7): perks. The switch defaults to on; the two lists to none.
                    mCellSource.combat().setUsePerks(settings->getBool(kPackId, kCombatPerksKey, true));
                    mCellSource.combat().setUseSounds(settings->getBool(kPackId, kCombatSoundsKey, true));
                    const std::string playerPerks = settings->getString(kPackId, kCombatTestPlayerPerksKey, "");
                    mCellSource.combat().setTestPlayerPerks(playerPerks);
                    if (!playerPerks.empty())
                        say(openeden::sdk::LogLevel::Info, "combat test player perks: " + playerPerks);
                    const std::string npcPerks = settings->getString(kPackId, kCombatTestNpcPerksKey, "");
                    mCellSource.combat().setTestNpcPerks(npcPerks);
                    if (!npcPerks.empty())
                        say(openeden::sdk::LogLevel::Info, "combat test npc perks: " + npcPerks);
                    mCellSource.combat().setWarning(
                        [say](const std::string& line) { say(openeden::sdk::LogLevel::Warning, line); });
                    // Task 0190 (stage S5-9): the combat AI; the test settings above stay as its overrides.
                    if (settings->getBool(kPackId, kCombatAiKey, false))
                    {
                        mCellSource.combat().setCombatAi(true);
                        say(openeden::sdk::LogLevel::Info, "combat ai: on");
                        // Task 0197: relations, followers, guards and sight choose the target.
                        if (settings->getBool(kPackId, kCombatFactionsKey, false))
                        {
                            mCellSource.combat().setCombatFactions(true);
                            say(openeden::sdk::LogLevel::Info, "combat factions: on");
                        }
                        mCellSource.combat().setFactionsDelay(
                            settings->getFloat(kPackId, kCombatFactionsDelayKey, 0.f));
                        mCellSource.combat().setTestAttacker(settings->getString(kPackId, kCombatTestAttackerKey, ""));
                    }
                    if (settings->getBool(kPackId, kCombatTraceKey, false))
                        mCellSource.combat().setTrace(
                            [say](const std::string& line) { say(openeden::sdk::LogLevel::Info, line); });
                    if (settings->getBool(kPackId, kCombatTraceKey, false))
                        mCellSource.combat().setSoundTrace(
                            [say](const std::string& line) { say(openeden::sdk::LogLevel::Info, line); });
                    // Task 0212: creature races animate, collide and fight as creatures (off by default).
                    if (settings->getBool(kPackId, kCreaturesKey, false))
                    {
                        mWorldView.setCreatures(true);
                        say(openeden::sdk::LogLevel::Info, "creatures: on (the wolf)");
                    }
                    // Task 0222: the player has a Skyrim body (off by default; the core also needs [Game] neutral player body).
                    if (settings->getBool(kPackId, kPlayerBodyKey, false))
                    {
                        mWorldView.setPlayerBody(true);
                        say(openeden::sdk::LogLevel::Info, "player body: on");
                    }
                    // Task 0220: an actor's melee weapon and shield are drawn (off by default).
                    if (settings->getBool(kPackId, kHeldGearKey, false))
                    {
                        mWorldView.setHeldGear(true);
                        say(openeden::sdk::LogLevel::Info, "held gear: on");
                    }
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
                        // Task 0196: followers need the package brain.
                        const std::string recruits = settings->getString(kPackId, kFollowerTestRecruitKey, "");
                        if (!mCellSource.actorTable().setTestFollowers(recruits))
                            say(openeden::sdk::LogLevel::Warning,
                                "the setting '" + std::string(kFollowerTestRecruitKey)
                                    + "' is not a short list of editor ids; the test recruit is off");
                        else if (!recruits.empty())
                            say(openeden::sdk::LogLevel::Info, "follower test recruit: " + recruits);
                        mCellSource.actorTable().setTestDismissAfter(
                            static_cast<float>(settings->getInt(kPackId, kFollowerTestDismissKey, 0)));
                        mCellSource.actorTable().setHomeLookup([this](openeden::sdk::game::WorldId reference) {
                            const PlacedRefIndex* placed = mWorldView.placedRefs();
                            const PlacedEntry* entry = placed != nullptr ? placed->find(reference) : nullptr;
                            return entry != nullptr ? static_cast<openeden::sdk::game::WorldId>(entry->mCell)
                                                    : openeden::sdk::game::kNoWorldId;
                        });
                        mFollowerChunk = std::make_unique<FollowerChunk>(
                            [this] { return mCellSource.actorTable().followerEntries(); },
                            [this](const std::vector<FollowerEntry>& entries) {
                                return mCellSource.actorTable().restoreFollowers(entries);
                            });
                        if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                        {
                            if (!chunks->registerHandler(*mFollowerChunk))
                                say(openeden::sdk::LogLevel::Warning, "the followers chunk was not registered");
                        }
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
            CombatHudBindings mHudBindings{ mCellSource.hud() }; // task 0180; refers to the cell source's HUD
            openeden::sdk::game::WorldViewService* mWorldViews = nullptr;
            openeden::sdk::game::WorldSourceService* mWorldSources = nullptr;
            LoadOrderStamp mLoadOrderStamp; // task 0177; registered with the host's save chunks, forgotten before stop
            std::unique_ptr<inventory::InventoryService> mInventory; // task 0182; refers to the world view
            std::unique_ptr<services::ServicesService> mServices; // task 0219; the lambdas refer to the pack
            std::unique_ptr<services::ServicesChunk> mServicesChunk; // task 0219; refers to mServices' ledger and book
            std::unique_ptr<barter::BarterService> mBarter; // task 0189; refers to the inventory and the world view
            std::unique_ptr<crafting::CraftingService> mCrafting; // task 0199; refers to the inventory
            openeden::sdk::Logger* mCraftLogger = nullptr; // task 0199
            WeaponRecord mMadeWeapon; // task 0199: the combat record of the wielded made weapon (combat points at it)
            std::function<void(std::string_view, float)> mCraftingSkillSink; // task 0199: a skills system may set it
            std::unique_ptr<lockbooks::LockBooksService> mLockBooks; // task 0203; refers to the inventory
            std::vector<std::filesystem::path> mBookPaths;
            std::string mBookLanguage = "english";
            std::map<std::pair<std::uint32_t, int>, std::unique_ptr<StringTable>> mBookTables;
            std::size_t mSyncedSpells = 0;
            FormId mSyncedLastSpell = kNullFormId;
            bool mPlayerSpellFromSetting = false;
            std::unique_ptr<journal::JournalService> mJournal; // task 0185; refers to the world view
            std::unique_ptr<crime::CrimeService> mCrime; // task 0204; refers to the cell source and the inventory
            std::unique_ptr<pickup::PickupService> mPickup; // task 0208; refers to the inventory, crime, progression
            std::unique_ptr<progression::ProgressionService> mProgression; // task 0200; refers to the world view
            bool mProgressionLinked = false;
            float mProgressionBoost = 1.f; // task 0206: the test multiplier of `progression test amount`
            std::uint64_t mProgressionPushed = 0;
            std::unique_ptr<map::MapService> mMap; // task 0201; refers to the world view
            std::unique_ptr<map::MapChunk> mMapChunk; // task 0201; refers to mMap's discovery
            std::unique_ptr<travel::TravelService> mTravel; // task 0218; refers to mMap and the combat; declared after mMap
            std::unique_ptr<rest::RestService> mRest; // task 0210; the lambdas refer to the pack, the combat, the view
            std::unique_ptr<rest::RestChunk> mRestChunk; // task 0210; refers to mRest's Rested state
            std::unique_ptr<harvest::HarvestService> mHarvest; // task 0221; refers to the view and the inventory
            std::unique_ptr<mining::MiningService> mMining; // task 0224; refers to the view and the inventory
            std::unique_ptr<menus::MenusService> mMenus; // task 0207; refers to the world view
            std::unique_ptr<presentation::PresentationService> mPresentation; // task 0215; the handlers it declared
            std::unique_ptr<FollowerChunk> mFollowerChunk; // task 0196; refers to the cell source
            std::uint32_t mFollowerAliasSeen = 0; // task 0196: the last fill of the DialogueFollower alias
            std::uint32_t mEquipSynced = 0xFFFFFFFFu; // task 0184
            std::uint32_t mBodySynced = 0xFFFFFFFFu; // task 0222
            std::unique_ptr<character::CharacterService> mCharacter; // task 0225; refers to the world view
            std::vector<RaceSkillBoost> mPendingBoosts; // task 0225: a new character's starting skills, not applied yet
            bool mHaveBoosts = false;
            std::function<void(const std::string&)> mCharacterLog;
            bool mEquipPending = false;
            std::unique_ptr<PapyrusDriver> mPapyrus; // refers to the view and the file source
            std::unique_ptr<PapyrusSaveChunk> mPapyrusChunk; // task 0181; refers to mPapyrus
            std::unique_ptr<SceneSaveChunk> mSceneChunk; // task 0211; refers to mPapyrus
            std::unique_ptr<StorySaveChunk> mStoryChunk; // task 0217; refers to mPapyrus
            std::unique_ptr<PapyrusNotificationBindings> mPapyrusBindings; // task 0198
            std::unique_ptr<PackWorldHooks> mPapyrusHooks; // task 0198; last: the host and the model refer to it
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
