#include "game_skyrimse/skyrimse_game_pack.hpp"
#include "game_skyrimse/speech.hpp"

#include "game_skyrimse/actor_mechanics.hpp"
#include "game_skyrimse/barter_service.hpp"
#include "game_skyrimse/inventory_service.hpp"
#include "game_skyrimse/journal.hpp"
#include "game_skyrimse/load_order_stamp.hpp"
#include "game_skyrimse/neutral_world.hpp"
#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/papyrus_host.hpp"
#include "game_skyrimse/placed_refs.hpp"
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
#include <cmath>
#include <cstdio>
#include <deque>
#include <cstdlib>
#include <filesystem>
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
        class PackPapyrusData final : public PapyrusGameData
        {
        public:
            PackPapyrusData(const FileSource* files, SkyrimWorldView& view)
                : mFiles(files)
                , mView(view)
            {
            }
            void setQuests(const QuestIndex* quests) { mQuests = quests; }

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
            }

            void frame(float dt)
            {
                if (!mStarted && !tryStart())
                    return;
                flushPending();
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
                mHost.setQuests(access.mQuests, access.mState, access.mVmads);
                // Task 0187: aliases fill when a quest starts, the story manager answers events, and the script
                // fragments of a dialogue line the player picks run here
                mHost.setAliasFiller(access.mAliases);
                mHost.setStoryEventSink(access.mStory);
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

            // Task 0187: the story manager's "change location" event. The pack is not told where the player stands, so
            // it is raised for an INTERIOR cell the engine built (the player enters those by a door or starts in
            // one) whose location differs from the last one raised; exterior cells load in rings and are not raised.
            void raiseLocationChange(openeden::sdk::game::WorldId cell)
            {
                FormId location = kNullFormId;
                bool interior = false;
                const bool known = mView.cellLocation(static_cast<FormId>(cell), location, interior);
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS cell " + hexId(static_cast<FormId>(cell)) + " location " + hexId(location)
                        + (known ? (interior ? " interior" : " exterior") : " unknown"));
                if (!known || !interior || location == kNullFormId || location == mLastLocation)
                    return;
                StoryEvent event;
                event.mType = "CLOC";
                event.mLocation1 = mLastLocation;
                event.mLocation2 = location;
                mLastLocation = location;
                const std::size_t started = mHost.raiseStoryEvent(event);
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS story event CLOC to location " + hexId(location) + " started "
                        + std::to_string(started) + " quests");
            }

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
            FormId mLastLocation = kNullFormId; ///< the last location a CLOC event named (task 0187)
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

        // Owns the content loader it registers (the host keeps a reference until stop()), the world view and the
        // neutral world source over it (the source is declared after the view it refers to).
        class SkyrimGamePack final : public openeden::sdk::Extension
        {
        public:
            // Task 0176: the Papyrus host runs in the game phase, on the main thread; with the setting off the driver
            // does not exist and the callback does nothing.
            void registerServices(openeden::sdk::Registrar& registrar) override
            {
                registrar.registerPhaseCallback(openeden::sdk::Phase::GameSimulation, [this](float dt) {
                    if (mPapyrus != nullptr)
                        mPapyrus->frame(dt);
                    syncFollowerAlias();
                    if (mInventory != nullptr)
                    {
                        (void)mInventory->poll(); // starts and adopts the item table reader; never waits
                        if (mBarter != nullptr)
                            mBarter->poll(); // task 0189: a pending barter session asks again
                        syncEquipment();
                    }
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
                    // Task 0189: the barter session (the namespace skyrimse.barter); needs the inventory.
                    startBarter(host, logger);
                    // Task 0185: the quest journal's bindings, off unless [extension.skyrimse] journal.
                    startJournal(host, logger);
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
                mPapyrus.reset();
                mWorldView.cancelAndJoin();
                withdrawWorldSource();
                withdrawWorldView();
            }

        private:
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
                    if (!mWorldView.armourPieceById(item, piece))
                        return false;
                    out = { inventory::EquipKind::Armor, piece.mSlotMask };
                    return true;
                };
                hooks.mHealthOf = [this](FormId effect, float magnitude, std::uint32_t duration) {
                    return mWorldView.healthOfEffect(effect, magnitude, duration);
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

            // Task 0189: the barter service over the inventory (on whenever the inventory is).
            void startBarter(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                if (mInventory == nullptr)
                    return;
                barter::ServiceHooks hooks;
                hooks.mVendorOf = [this](FormId npc, barter::VendorInfo& out) { return mWorldView.vendorOf(npc, out); };
                hooks.mSpeech = [this]() { return static_cast<float>(mWorldView.standInPlayerSpeech()); };
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
                        if (!mWorldView.lookupWeapon(worn.mItem, weapon))
                            pending = true;
                        else
                            equipment.mWeapon = weapon;
                    }
                    else
                    {
                        ArmourPiece piece;
                        if (mWorldView.armourPieceById(worn.mItem, piece))
                            equipment.mPieces.push_back(std::move(piece));
                    }
                }
                mEquipPending = pending;
                if (pending)
                    return;
                mEquipSynced = revision;
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
                    }
                    if (settings->getBool(kPackId, kCombatTraceKey, false))
                        mCellSource.combat().setTrace(
                            [say](const std::string& line) { say(openeden::sdk::LogLevel::Info, line); });
                    if (settings->getBool(kPackId, kCombatTraceKey, false))
                        mCellSource.combat().setSoundTrace(
                            [say](const std::string& line) { say(openeden::sdk::LogLevel::Info, line); });
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
            std::unique_ptr<barter::BarterService> mBarter; // task 0189; refers to the inventory and the world view
            std::unique_ptr<journal::JournalService> mJournal; // task 0185; refers to the world view
            std::unique_ptr<FollowerChunk> mFollowerChunk; // task 0196; refers to the cell source
            std::uint32_t mFollowerAliasSeen = 0; // task 0196: the last fill of the DialogueFollower alias
            std::uint32_t mEquipSynced = 0xFFFFFFFFu; // task 0184
            bool mEquipPending = false;
            std::unique_ptr<PapyrusDriver> mPapyrus; // refers to the view and the file source
            std::unique_ptr<PapyrusSaveChunk> mPapyrusChunk; // task 0181; last: refers to mPapyrus
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
