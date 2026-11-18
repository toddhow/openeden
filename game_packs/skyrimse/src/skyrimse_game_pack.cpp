#include "game_skyrimse/skyrimse_game_pack.hpp"
#include "game_skyrimse/speech.hpp"

#include "game_skyrimse/actor_mechanics.hpp"
#include "game_skyrimse/appearance.hpp"
#include "game_skyrimse/barter_service.hpp"
#include "game_skyrimse/character_service.hpp"
#include "game_skyrimse/crafting_service.hpp"
#include "game_skyrimse/crime_service.hpp"
#include "game_skyrimse/diseases.hpp"
#include "game_skyrimse/forcegreet.hpp"
#include "game_skyrimse/harvest_service.hpp"
#include "game_skyrimse/houses.hpp"
#include "game_skyrimse/inventory_service.hpp"
#include "game_skyrimse/journal.hpp"
#include "game_skyrimse/load_order_stamp.hpp"
#include "game_skyrimse/lockbooks_service.hpp"
#include "game_skyrimse/map.hpp"
#include "game_skyrimse/marriage.hpp"
#include "game_skyrimse/menus.hpp"
#include "game_skyrimse/mining_service.hpp"
#include "game_skyrimse/neutral_world.hpp"
#include "game_skyrimse/pack_core.hpp"
#include "game_skyrimse/papyrus_host.hpp"
#include "game_skyrimse/perks.hpp"
#include "game_skyrimse/pickup_service.hpp"
#include "game_skyrimse/placed_refs.hpp"
#include "game_skyrimse/player_body.hpp"
#include "game_skyrimse/presentation.hpp"
#include "game_skyrimse/progression_service.hpp"
#include "game_skyrimse/rest.hpp"
#include "game_skyrimse/services.hpp"
#include "game_skyrimse/shouts_service.hpp"
#include "game_skyrimse/sneak.hpp"
#include "game_skyrimse/stones.hpp"
#include "game_skyrimse/story_events.hpp"
#include "game_skyrimse/travel.hpp"
#include "game_skyrimse/vampirism.hpp" // task 0260
#include "game_skyrimse/werewolf.hpp"
#include "game_skyrimse/werewolf_service.hpp"
#include "game_skyrimse/world_view.hpp"
#include "game_skyrimse/worldbooks.hpp"
#include <game_skyrimse/main_quest.hpp>

#include <functional>
#include <openeden/extension.hpp>
#include <openeden/game/content.hpp>
#include <openeden/game/player_place.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/game/world_view.hpp>
#include <openeden/host.hpp>
#include <openeden/logging.hpp>
#include <set>

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
            // Task 0237: Message.Show
            bool messageInfo(FormId message, PapyrusMessageInfo& out) override
            {
                SkyrimWorldView::MessageData data;
                if (!mView.messageOf(message, data))
                    return false;
                out.mText = std::move(data.mText);
                out.mButtons = std::move(data.mButtons);
                out.mBox = data.mBox;
                return true;
            }
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
                if (mView.isFactionForm(form))
                    return "Faction"; // task 0258: Faction.PlayerPayCrimeGold and its kin
                if (mView.isActorBaseForm(form))
                    return "ActorBase"; // task 0274
                const std::string scripted = mView.scriptClassOf(form); // task 0237: Spell and Message
                if (!scripted.empty())
                    return scripted;
                // Task 0256: a word wall's properties are a word of power and a global variable
                if (const ShoutIndex* shouts = mView.shoutIndex())
                    if (shouts->word(form) != nullptr)
                        return "WordOfPower";
                float globalNow = 0.f;
                return mView.globalValue(form, globalNow) ? "GlobalVariable" : std::string();
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
            // Task 0202
            FormId cellOf(FormId reference) const override
            {
                const PlacedRefIndex* refs = mView.placedRefs();
                const PlacedEntry* entry = refs != nullptr ? refs->find(reference) : nullptr;
                return entry != nullptr ? entry->mCell : 0;
            }
            bool cellIsInterior(FormId cell) const override
            {
                FormId location = 0;
                bool interior = false;
                return mView.cellLocation(cell, location, interior) && interior;
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
            // Task 0276
            FormId locationOfCell(FormId cell) const override { return mView.locationOfCell(cell); }
            bool locationWithin(FormId location, FormId ancestor) const override
            {
                return mView.locationWithin(location, ancestor);
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
            // Task 0239: the factions an alias fill is added to (ALFC)
            bool aliasFactions(FormId quest, std::uint32_t alias, std::vector<FormId>& out) const override
            {
                out.clear();
                const QuestRecord* record = mQuests != nullptr ? mQuests->quest(quest) : nullptr;
                if (record == nullptr)
                    return false;
                for (const QuestAlias& a : record->mAliases)
                    if (a.mId == alias)
                        out = a.mFactions;
                return !out.empty();
            }
            bool isTopicInfo(FormId form) const override { return mView.isScriptedInfo(form); }
            bool isScene(FormId form) const override { return mView.isScene(form); }
            FormId questOfInfo(FormId info) const override { return mView.questOfInfo(info); }
            FormId questOfScene(FormId scene) const override { return mView.questOfScene(scene); }
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
        // condition evaluator and dialogue runtime of the view, the cell source (speech queue, holding actors,
        // packages) and the Papyrus host (fragments). Main thread only.
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
                const bool given = record != nullptr && mCells != nullptr && mCells->sceneForcePackage(actor, record);
                // Task 0264: a ForceGreet package with a topic ends with the actor opening its conversation
                const PackageRecord* shape = record;
                if (record != nullptr && record->mProcedures.empty() && record->mTemplate != kNullFormId
                    && mAccess.mPackage) // the procedures of a package built from a template are the template's
                    if (const PackageRecord* t = mAccess.mPackage(record->mTemplate))
                        shape = t;
                if (given && mForce != nullptr && record->mForceGreetTopic != kNullFormId
                    && std::any_of(shape->mProcedures.begin(), shape->mProcedures.end(),
                        [](const std::string& n) { return n == "ForceGreet" || n == "forcegreet"; }))
                {
                    const FormId base = mAccess.mBaseOf ? mAccess.mBaseOf(actor) : kNullFormId;
                    if (mForce->request(actor, base, record->mForceGreetTopic))
                        mLog("FORCEGREET " + inventory::formatId(actor) + " asked to say "
                            + inventory::formatId(record->mForceGreetTopic));
                }
                return given;
            }
            void clearPackage(FormId actor) override
            {
                if (mCells != nullptr)
                    (void)mCells->sceneForcePackage(actor, nullptr);
                mPackageChecks.erase(actor);
                if (mForce != nullptr)
                    mForce->cancel(actor);
            }
            void setForceGreet(ForceGreet* force) { mForce = force; }
            // The brain evaluates a forced package at its next update, so a few answers are not trusted: the actor may
            // still look done with the package it had before.
            bool packageFinished(FormId actor) override
            {
                if (mForce != nullptr && mForce->stateOf(actor) != ForceGreet::State::Done)
                    return mForce->finished(actor); // task 0264: the conversation is the package
                return mCells != nullptr && ++mPackageChecks[actor] > kTrustAfterChecks
                    && mCells->scenePackageDone(actor);
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
            ForceGreet* mForce = nullptr; ///< task 0264 (owned by the driver)
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
                mHost.setQuestChanged([this](FormId quest, std::uint32_t stage) {
                    mView.setPackageQuestStage(quest, stage);
                    // task 0276: the test aid sets the married-state quest's stage in the next frame: the wedding
                    // quest's stage 500 comes after the story event that started it
                    if (mMarriageOn && quest == marriage::kWeddingQuest && stage == 500 && mMarriageFinStage > 0)
                        mMarriageFinPending = true;
                    // Task 0276: the wedding quest's MoveTo of actors (the lover and the witnesses into the temple)
                    if (mMarriageOn && quest == marriage::kWeddingQuest && !mWeddingRelocation)
                    {
                        mWeddingRelocation = true;
                        mHost.setRelocation(true, mainquest::kRelocateRange);
                    }
                    // Task 0264: the test aid that stands in for the walk to the training yard
                    if (mCompanionsTraining && quest == kCompanionsQuest && stage == 20)
                        mTrainingPending = true;
                });
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
                updateWordWalls(); // task 0256
                raiseTestEvent();
                applyHouses(); // task 0261
                replayMainQuestCellLoad(); // task 0268
                applyMainQuestTestStage();
                applyMainQuest(); // task 0268
                applyCarts(); // task 0274
                applyTraining(); // task 0264
                applyMarriage(); // task 0270
                if (mMarriageFinPending) // task 0276
                {
                    mMarriageFinPending = false;
                    say(openeden::sdk::LogLevel::Info,
                        "MARRIAGE test: the married-state quest goes to stage " + std::to_string(mMarriageFinStage)
                            + (mHost.startQuestForTest(marriage::kFinQuest, mMarriageFinStage) ? "" : " (refused)"));
                }
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

            // Task 0256: the combat update reports where the player is and looks; the trigger volumes of the word walls
            // (and every other volume a script sits on) are tested against it just before the scripts run.
            void playerState(const openeden::sdk::game::SubjectState& state)
            {
                mPlayerState = state;
                mHavePlayerState = true;
            }

            void updateWordWalls()
            {
                PapyrusPlayerView view;
                if (mHavePlayerState)
                {
                    view.mKnown = true;
                    view.mX = mPlayerState.x;
                    view.mY = mPlayerState.y;
                    view.mZ = mPlayerState.z;
                    view.mHeading = mPlayerState.heading;
                    view.mPitch = mPlayerState.pitch;
                    view.mPlace.mKnown = mSite.mKnown;
                    view.mPlace.mInterior = mSite.mInterior;
                    view.mPlace.mCell = mSite.mCell;
                    mView.setPlayerPosition(view.mX, view.mY, view.mZ); // task 0276
                }
                mHost.setPlayerView(view);
                const std::size_t sent = mHost.updateTriggers();
                if (sent != 0)
                    say(openeden::sdk::LogLevel::Info,
                        "PAPYRUS trigger events sent: " + std::to_string(sent) + " (player at "
                            + std::to_string(static_cast<long long>(view.mX)) + " "
                            + std::to_string(static_cast<long long>(view.mY)) + " "
                            + std::to_string(static_cast<long long>(view.mZ)) + ")");
            }

            void cellBuilt(openeden::sdk::game::WorldId cell)
            {
                mPendingCells.push_back(cell);
                if (mPendingCells.size() > kMaxPending)
                    mPendingCells.pop_front();
            }

            void activated(openeden::sdk::game::WorldId reference) { mPendingActivations.push_back(reference); }
            /// Task 0234: a placed book was read (OnRead goes to its scripts at the next game update).
            void read(openeden::sdk::game::WorldId reference)
            {
                if (mPendingReads.size() < kMaxPending)
                    mPendingReads.push_back(reference);
            }

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
                        + "), aliases filled " + std::to_string(c.mAliasesFilled) + ", updates "
                        + std::to_string(c.mUpdatesFired) + ", script errors " + std::to_string(c.mScriptErrors)
                        + "; native calls answered " + std::to_string(answered) + ", tolerant natives "
                        + std::to_string(names.size()) + ", tolerant calls " + std::to_string(mHost.tolerantCalls()));
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
            /// Task 0275: a quest's current stage (0 when the quest state is not there yet).
            [[nodiscard]] float questStage(FormId quest) const
            {
                return mQuestStateForStage != nullptr ? mQuestStateForStage->stage(quest) : 0.f;
            }
            void setMarriageFinStage(std::int32_t stage) { mMarriageFinStage = stage; } // task 0276
            void enableForceGreet() { mForceOn = true; }
            [[nodiscard]] bool forceGreetOn() const { return mForceOn; }
            void enableCompanions() { mCompanionsOn = true; }
            [[nodiscard]] bool companionsOn() const { return mCompanionsOn; }
            [[nodiscard]] ForceGreet& forceGreet() { return mForce; }

            // Task 0261: bought houses. A stage's fragment runs only when its log entry's conditions hold
            // (HousePurchase: the player is in the hold), a quest variable a script holds wins in the conversation's
            // conditions (the steward stops offering what the player owns), and the quest of the offers is started (no
            // script does). `permission` is the test aid: the Whiterun jarls like the player. Task 0268: `main quest
            // start`. The game's own quest MQ101 ("Unbound") is started once when the host runs; its start-up stage,
            // scenes and fragments then do the opening (nothing is hand-written here but the start). Task 0268, a
            // developer aid: `main quest test stage = <stage>[,<script seconds>]` sets that stage of MQ101 once the
            // host's script time reaches the seconds (default 0), so the journal and what follows can be seen without
            // playing the whole opening (nothing before stage 160 shows in the journal: no objective is displayed)
            void setMainQuestTestStage(const std::string& text)
            {
                if (text.empty())
                    return;
                const std::size_t comma = text.find(',');
                mMainQuestTestStage = static_cast<std::int32_t>(std::strtol(text.c_str(), nullptr, 10));
                mMainQuestTestDelay = comma == std::string::npos ? 0.0 : std::strtod(text.c_str() + comma + 1, nullptr);
            }
            void applyMainQuestTestStage()
            {
                if (mMainQuestTestStage <= 0 || !mMainQuestApplied || mHost.scriptTime() < mMainQuestTestDelay)
                    return;
                const std::int32_t stage = mMainQuestTestStage;
                mMainQuestTestStage = 0;
                say(openeden::sdk::LogLevel::Info,
                    "MAINQUEST test stage " + std::to_string(stage) + " "
                        + (mHost.startQuestForTest(mainquest::kUnboundQuest, stage) ? "set" : "refused"));
            }

            [[nodiscard]] bool mainQuestOn() const { return mMainQuest; } // task 0274
            [[nodiscard]] std::string formName(FormId form) const { return mData.nameOf(form); } // task 0274
            void setSceneParkSeconds(float seconds) { mSceneParkSeconds = seconds; } // task 0287
            void enableMainQuest()
            {
                mMainQuest = true;
                mHost.setRelocation(true, mainquest::kRelocateRange);
            }
            // Task 0274 (the cart ride): the carts the cell source moved are pushed to the world, and the horses that
            // carry them are tested against the trigger slabs that set the quest's stages
            void applyCarts()
            {
                SkyrimCellSource* cells = dynamic_cast<SkyrimCellSource*>(&mCells);
                if (!mMainQuest || cells == nullptr)
                    return;
                std::vector<CartRides::CartMove> moves;
                cells->drainCartMoves(moves);
                static const bool hold = std::getenv("OPENEDEN_CART_HOLD") != nullptr; // test aid: carts stay put
                for (const CartRides::CartMove& move : moves)
                {
                    if (hold)
                        break;
                    ObjectMoveRequest request;
                    request.hasPosition = true;
                    request.position[0] = move.mPose.x;
                    request.position[1] = move.mPose.y;
                    request.position[2] = move.mPose.z;
                    request.hasAngles = true;
                    request.angles[2] = static_cast<double>(move.mPose.heading);
                    (void)pushMove(move.mCart, request, true);
                }
                if (!moves.empty() && mHost.scriptTime() >= mNextCartLog)
                {
                    mNextCartLog = mHost.scriptTime() + 10.0; // script seconds, so a trace compares between runs
                    for (const CartRides::CartMove& move : moves)
                        say(openeden::sdk::LogLevel::Info,
                            "CART " + hexId(move.mCart) + " at " + std::to_string(static_cast<long long>(move.mPose.x))
                                + " " + std::to_string(static_cast<long long>(move.mPose.y)) + " "
                                + std::to_string(static_cast<long long>(move.mPose.z)) + " heading "
                                + std::to_string(static_cast<int>(move.mPose.heading * 100.f)) + "/100");
                    for (const SkyrimCellSource::HorsePlace& horse : cells->horsePlaces())
                        say(openeden::sdk::LogLevel::Info,
                            "CART horse " + hexId(horse.mReference)
                                + (horse.mRider != 0 ? " ridden by " + hexId(horse.mRider) : std::string()) + " at "
                                + std::to_string(static_cast<long long>(horse.mX)) + " "
                                + std::to_string(static_cast<long long>(horse.mY)) + " "
                                + std::to_string(static_cast<long long>(horse.mZ)));
                }
                std::vector<PapyrusHost::SubjectPlace> subjects;
                for (const SkyrimCellSource::HorsePlace& horse : cells->horsePlaces())
                    subjects.push_back({ horse.mReference, horse.mX, horse.mY, horse.mZ });
                if (!subjects.empty())
                {
                    const std::size_t sent = mHost.updateSubjectTriggers(subjects);
                    if (sent != 0)
                        say(openeden::sdk::LogLevel::Info,
                            "PAPYRUS CART trigger events sent: " + std::to_string(sent));
                }
            }
            void applyMainQuest()
            {
                if (!mMainQuest || mMainQuestApplied || !mHost.started())
                    return;
                mMainQuestApplied = true;
                const bool started = mHost.startQuestForTest(mainquest::kUnboundQuest);
                say(openeden::sdk::LogLevel::Info,
                    std::string("MAINQUEST MQ101 ") + (started ? "started" : "was not started (running or done)"));
                mMainQuestReplay = started;
            }

            // One frame after the start (the scripts of the quest attach right after it): the cells of the start were
            // loaded before the quest, so the quest's cell-load registrations are told now (stage 12 waits for them)
            void replayMainQuestCellLoad()
            {
                if (!mMainQuestReplay)
                    return;
                mMainQuestReplay = false;
                say(openeden::sdk::LogLevel::Info,
                    "MAINQUEST the starting cells are loaded: "
                        + std::to_string(mHost.replayAliasCellLoad(mainquest::kUnboundQuest))
                        + " cell-load events sent");
            }

            void enableHouses(bool permission, FollowerState* ranks)
            {
                mHouses = true;
                mHousesPermission = permission;
                mFollowers = ranks;
            }

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
                mHouseQuests = access.mQuests; // task 0269
                mQuestStateForStage = access.mState; // task 0275
                // Task 0187: aliases fill when a quest starts, the story manager answers events, and the script
                // fragments of a dialogue line the player picks run here
                mHost.setAliasFiller(access.mAliases);
                mHost.setStoryEventSink(access.mStory);
                if ((mHouses || mMainQuest) && access.mQuestConditions)
                {
                    const QuestIndex* quests = access.mQuests;
                    const auto conditions = access.mQuestConditions;
                    mHost.setStageEntryCheck(
                        [quests, conditions](FormId quest, std::uint32_t stage, std::int32_t entry) {
                            const QuestRecord* record = quests->quest(quest);
                            if (record == nullptr || entry < 0)
                                return true;
                            for (const QuestStage& s : record->mStages)
                                if (s.mIndex == stage)
                                    return static_cast<std::size_t>(entry) >= s.mEntries.size()
                                        || conditions(s.mEntries[static_cast<std::size_t>(entry)].mConditions, quest);
                            return true;
                        });
                }
                if ((mHouses || mMainQuest || mView.aliasPackagesOn()) && access.mSetLiveQuestVariable)
                    access.mSetLiveQuestVariable([this](FormId quest, const std::string& name) -> std::optional<float> {
                        const std::optional<double> live = mHost.anyNumberVariable(quest, name);
                        return live ? std::optional<float>(static_cast<float>(*live)) : std::nullopt;
                    });
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
                    mScenes->runner().setRetryQuestStart(mMainQuest); // task 0268
                    if (mSceneParkSeconds > 0.f) // task 0287
                        mScenes->runner().setParkSeconds(mSceneParkSeconds);
                    if (mForceOn) // task 0264: a ForceGreet package ends with its conversation
                        mScenes->setForceGreet(&mForce);
                }
                if (mForceOn) // tasks 0264 and 0271: the conversation a ForceGreet package asks for
                {
                    SkyrimWorldView::ForcedGreetHooks hooks;
                    hooks.mTopic = [this](FormId reference) { return mForce.topicFor(reference); };
                    hooks.mBegan = [this](FormId reference) {
                        mForce.began(reference);
                        say(openeden::sdk::LogLevel::Info, "FORCEGREET conversation began with " + hexId(reference));
                    };
                    hooks.mEnded = [this](FormId reference) {
                        mForce.ended(reference);
                        say(openeden::sdk::LogLevel::Info, "FORCEGREET conversation ended with " + hexId(reference));
                    };
                    hooks.mPlainWhenNone = [this](FormId reference) { return mForce.fallsBackToPlain(reference); };
                    mView.setForcedGreeting(std::move(hooks));
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
                const auto millis
                    = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began);
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
                pushMoves();
                mHost.resetTriggers(); // task 0256: a load puts the player outside every volume until he is seen inside
                return true;
            }

            static std::string hexId(FormId id)
            {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%08X", id);
                return buffer;
            }

            // Task 0187: a built cell is logged. Task 0211: the story manager's "change location" event is raised from
            // the place the core reports (`playerPlace`, an interior or an exterior grid square), not from a built
            // cell: the store builds an exterior cell once, so a visit does not build it again.
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
            // Task 0211: the core tells where the player stands when it changes. Kept until frame() (never worked out
            // inside the core's physics step).
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
                // task 0271: the place, for the package conditions that run on the player
                mView.setPlayerPlace(place.interior, static_cast<FormId>(place.cell), static_cast<FormId>(place.world));
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

            // Task 0211: a story event raised later in frame(): a death is reported inside the combat update, and
            // starting a quest runs scripts, which must not happen there. Bounded: the oldest is dropped.
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

            // A developer aid, [extension.skyrimse] story test event =
            // TYPE[:K1=hex][:L1=hex][:L2=hex][:R1=hex][:R2=hex]
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

            // Task 0264: `companions test training`. The game walks the player to the training yard (outside) with
            // Vilkas and Vilkas' topic there starts the training quest. Neither actors going through doors nor the
            // exterior walk is built, so the aid starts the quest (C00VilkasTrainingQuest, whose own scripts begin the
            // fight) when C00 reaches stage 20.
            void enableCompanionsTraining() { mCompanionsTraining = true; }
            void applyTraining()
            {
                if (!mTrainingPending || !mHost.started())
                    return;
                // stage 20 is set inside Kodlak's dismiss scene (000BD697); the fight waits for its end
                if (mScenes != nullptr && mScenes->scenePlaying(0x000BD697))
                    return;
                mTrainingPending = false;
                say(openeden::sdk::LogLevel::Info,
                    std::string("COMPANIONS training quest ")
                        + (mHost.startQuestForTest(kCompanionsTrainingQuest) ? "started by the test aid"
                                                                             : "was refused"));
            }

            // Task 0270: the test aid of marriage: the listed NPCs are the player's friends (rank 2), once, when the
            // host runs. The real rank comes from favour and follower quests this build does not run.
            void enableMarriage(std::vector<FormId> friends, FollowerState* ranks)
            {
                mMarriageFriends = std::move(friends);
                mMarriageRanks = ranks;
                mMarriageOn = true;
            }

            void applyMarriage()
            {
                if (!mMarriageOn || mMarriageApplied || mMarriageRanks == nullptr || !mHost.started())
                    return;
                mMarriageApplied = true;
                const PlacedRefIndex* placed = mView.placedRefs();
                if (placed == nullptr)
                    return;
                std::size_t given = 0;
                for (const FormId base : mMarriageFriends)
                {
                    const PlacedRefIndex::Range range = placed->byBase(base);
                    for (const PlacedEntry* const* it = range.mBegin; it != range.mEnd; ++it)
                        if (mMarriageRanks->setRelationshipRank((*it)->mId, kPapyrusPlayerReference, 2))
                            ++given;
                }
                say(openeden::sdk::LogLevel::Info,
                    "MARRIAGE test friends: " + std::to_string(given) + " placements are the player's friends");
            }

            // Task 0261 (once, when the host runs): the offers' quest starts and the test aid gives the permission
            void applyHouses()
            {
                if (!mHouses || mHousesApplied || mFollowers == nullptr || !mHost.started())
                    return;
                mHousesApplied = true;
                say(openeden::sdk::LogLevel::Info,
                    std::string("HOUSES the quest of the house offers ")
                        + (mHost.startQuestForTest(houses::kHousePurchaseQuest) ? "started" : "was refused"));
                // Task 0269: a hold's steward alias puts the steward into JobStewardFaction while its dialogue quest
                // runs (ALFC); Falk Firebeard has the faction no other way, and the offers test GetInFaction
                if (mHouseQuests != nullptr)
                {
                    std::size_t granted = 0;
                    for (const houses::AliasFactionGrant& g : houses::aliasFactionGrants(
                             *mHouseQuests, houses::holdDialogueQuests(), houses::kJobStewardFaction))
                        if (mFollowers->addToFaction(g.mReference, g.mFaction))
                            ++granted;
                    say(openeden::sdk::LogLevel::Info,
                        "HOUSES the stewards of the hold dialogue quests are in JobStewardFaction: "
                            + std::to_string(granted));
                }
                if (!mHousesPermission)
                    return;
                const PlacedRefIndex* placed = mView.placedRefs();
                if (placed == nullptr)
                    return;
                std::size_t given = 0;
                for (const FormId base : houses::testPermissionJarlBases()) // task 0269: every hold's jarls
                {
                    const PlacedRefIndex::Range range = placed->byBase(base);
                    for (const PlacedEntry* const* it = range.mBegin; it != range.mEnd; ++it)
                        if (mFollowers->setRelationshipRank((*it)->mId, kPapyrusPlayerReference, 2))
                            ++given;
                }
                say(openeden::sdk::LogLevel::Info,
                    "HOUSES test permission: " + std::to_string(given) + " jarls are the player's friends");
                // Task 0269: Hjerim's offer also needs the Windhelm murders (MS11) behind the player
                say(openeden::sdk::LogLevel::Info,
                    std::string("HOUSES test permission: MS11 stage 1 ")
                        + (mHost.markStageDoneForTest(houses::kMs11Quest, 1) ? "marked done" : "was refused"));
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

            // Task 0211: what a save keeps of the scenes that play, and the load's side of it. A saved state waits for
            // the host's own restore (the chunks are read in any order) and is applied in the next frame.
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
            // `kAmbientSeconds` of script time, over the actors the core told the pack about, pairs of the same cell in
            // a seeded order, until one pair starts a quest (at most `kAmbientPairs` events a time). RECALLED-VERIFY, a
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
                            "SCENE ambient dialogue event started a quest after " + std::to_string(i + 1) + " pairs");
                        break;
                    }
                }
            }

        public:
            // The pack's id of the cell a reference stands in: where a script created it, else its record's cell.
            FormId cellOfReference(FormId reference) const
            {
                const auto moved = mMovedCells.find(reference); // task 0286: the world moved it into another cell
                if (moved != mMovedCells.end())
                    return moved->second;
                for (const PapyrusHost::CreatedObject& created : mHost.createdObjects())
                    if (created.mReference == reference)
                        return created.mCell;
                if (const PlacedRefIndex* refs = mView.placedRefs())
                    if (const PlacedEntry* entry = refs->find(reference))
                        return entry->mCell;
                return kNullFormId;
            }

            bool pushEnabled(FormId reference, bool enabled, float fadeSeconds = 0.f)
            {
                if (mSources == nullptr)
                    return false;
                // Task 0274: a reference's enable children (XESP) follow it (Skyrim's rule: the second cart, its horse
                // and the riders' markers are enabled by the first horse). Only with the main quest (a reference's
                // children are looked for in the whole table).
                if (mMainQuest && !mEnablingChildren)
                {
                    mEnablingChildren = true;
                    if (const PlacedRefIndex* placed = mView.placedRefs())
                    {
                        std::vector<FormId> frontier{ reference };
                        std::set<FormId> seen{ reference };
                        for (std::size_t depth = 0; depth < 4 && !frontier.empty(); ++depth)
                        {
                            std::vector<FormId> next;
                            for (const PlacedEntry& entry : placed->all())
                            {
                                if (entry.mEnableParent == kNullFormId || entry.mDeleted
                                    || std::find(frontier.begin(), frontier.end(), entry.mEnableParent)
                                        == frontier.end()
                                    || !seen.insert(entry.mId).second)
                                    continue;
                                const bool opposite = (entry.mFlags & PlacedFlags::EnableOpposite) != 0;
                                (void)pushEnabled(entry.mId, opposite ? !enabled : enabled, 0.f);
                                next.push_back(entry.mId);
                            }
                            frontier = std::move(next);
                        }
                    }
                    mEnablingChildren = false;
                }
                const FormId cell = cellOfReference(reference);
                // Task 0202: one ordered queue with the moves and creations (the 1.2 call stays for older hosts: the
                // service's default forwards to it)
                const bool queued = mSources->setObjectEnabledFaded(mCells, cell, reference, enabled, fadeSeconds);
                say(openeden::sdk::LogLevel::Info,
                    std::string("PAPYRUS ") + (enabled ? "enable" : "disable") + " of " + hexId(reference) + " in cell "
                        + hexId(cell) + (fadeSeconds > 0.f ? " with a fade" : "")
                        + (queued ? " sent to the world" : " not sent"));
                return queued;
            }

            // Task 0202: a script moved or turned a reference.
            bool pushMove(FormId reference, const ObjectMoveRequest& request, bool quiet = false)
            {
                if (mSources == nullptr)
                    return false;
                openeden::sdk::game::ObjectMove move;
                move.toOriginal = request.toOriginal;
                if (request.hasPosition)
                {
                    move.hasPosition = true;
                    move.position = { request.position[0], request.position[1], request.position[2] };
                }
                if (request.hasAngles)
                {
                    move.hasRotation = true;
                    move.rotation = eulerToQuat({ static_cast<float>(request.angles[0]),
                        static_cast<float>(request.angles[1]), static_cast<float>(request.angles[2]) });
                }
                const FormId cell = cellOfReference(reference);
                const bool queued = mSources->moveObject(mCells, cell, reference, move);
                if (!quiet)
                    say(openeden::sdk::LogLevel::Info,
                        "PAPYRUS move of " + hexId(reference) + " in cell " + hexId(cell)
                            + (request.toOriginal ? " back to its record" : "")
                            + (queued ? " sent to the world" : " not sent"));
                return queued;
            }

            // Task 0286: a script (or a scene's travel) moved a reference INTO another cell. The world moves it, loaded or
            // not, and it keeps its identity: its reference id, so its inventory, packages and quest aliases follow.
            // `destinationCell` is the pack's id of the cell the target stands in (0: the exterior square that holds
            // the position). `request.from` says where the reference stands now, for a persistent reference whose cell
            // id does not say.
            bool pushMoveToCell(FormId reference, FormId destinationCell, const ObjectMoveRequest& request)
            {
                if (mSources == nullptr || !request.hasPosition)
                    return false;
                // an object that stays in its cell is moved by the plain move (the actor system places actors, and an
                // actor's move inside its cell is the world's too)
                if (destinationCell != 0 && destinationCell == cellOfReference(reference) && !isActor(reference))
                    return false;
                openeden::sdk::game::ObjectMove move;
                move.hasPosition = true;
                move.position = { request.position[0], request.position[1], request.position[2] };
                if (request.hasAngles)
                {
                    move.hasRotation = true;
                    move.rotation = eulerToQuat({ static_cast<float>(request.angles[0]),
                        static_cast<float>(request.angles[1]), static_cast<float>(request.angles[2]) });
                }
                move.hasCell = true;
                move.cell = destinationCell != 0 ? static_cast<openeden::sdk::game::WorldId>(destinationCell)
                                                 : openeden::sdk::game::kNoWorldId;
                if (request.hasFrom)
                {
                    move.hasFrom = true;
                    move.from = { request.from[0], request.from[1], request.from[2] };
                }
                const FormId cell = cellOfReference(reference);
                const bool queued = mSources->moveObject(mCells, cell, reference, move);
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS move of " + hexId(reference) + " from cell " + hexId(cell) + " into cell "
                        + hexId(destinationCell) + (queued ? " sent to the world" : " not sent"));
                if (queued)
                    mMovedCells[reference] = destinationCell;
                return queued;
            }

            // Task 0286: a scene's Travel package leads an actor to a reference in another cell: the world moves the
            // actor to the target's place. The target's place and cell come from its record.
            bool travelMove(openeden::sdk::game::WorldId actor, FormId target)
            {
                const PlacedRefIndex* placed = mView.placedRefs();
                if (placed == nullptr)
                    return false;
                const PlacedEntry* where = placed->find(target);
                if (where == nullptr || where->mDeleted)
                    return false;
                ObjectMoveRequest request;
                request.hasPosition = true;
                request.position[0] = where->mX;
                request.position[1] = where->mY;
                request.position[2] = where->mZ;
                if (const PlacedEntry* self = placed->find(static_cast<FormId>(actor)))
                {
                    request.hasFrom = true;
                    request.from[0] = self->mX;
                    request.from[1] = self->mY;
                    request.from[2] = self->mZ;
                }
                return pushMoveToCell(static_cast<FormId>(actor), where->mCell, request);
            }

            // Task 0202: PlaceAtMe. The base is described by the view, the object is built as the pack builds a placed
            // one
            // and handed to the host service, which mints its id.
            ObjectPlaceResult placeObject(const ObjectPlaceRequest& request)
            {
                ObjectPlaceResult none;
                if (mSources == nullptr)
                    return none;
                openeden::sdk::game::PlacedObject placed;
                if (!mView.describeBase(request.base, placed))
                {
                    say(openeden::sdk::LogLevel::Info,
                        "PAPYRUS PlaceAtMe: " + hexId(request.base) + " is not a base form");
                    return none;
                }
                if (placed.baseKind == openeden::sdk::game::BaseKind::Actor
                    || placed.baseKind == openeden::sdk::game::BaseKind::LeveledList
                    || placed.baseKind == openeden::sdk::game::BaseKind::Unknown)
                {
                    say(openeden::sdk::LogLevel::Info,
                        "PAPYRUS PlaceAtMe: " + hexId(request.base)
                            + " is an actor, a leveled list or unknown: not placed");
                    return none;
                }
                placed.position = { static_cast<float>(request.position[0]), static_cast<float>(request.position[1]),
                    static_cast<float>(request.position[2]) };
                placed.rotation = { static_cast<float>(request.angles[0]), static_cast<float>(request.angles[1]),
                    static_cast<float>(request.angles[2]) };
                placed.scale = request.scale > 0.f ? request.scale : 1.f;
                openeden::sdk::game::WorldObject object;
                std::string problem;
                if (!toNeutral(placed, object, problem))
                {
                    say(openeden::sdk::LogLevel::Info, "PAPYRUS PlaceAtMe: " + hexId(request.base) + ": " + problem);
                    return none;
                }
                const FormId cell = request.nearCell != 0 ? request.nearCell : cellOfReference(request.nearReference);
                const openeden::sdk::game::WorldId id = mSources->placeObject(mCells, cell, object);
                if (id == openeden::sdk::game::kNoWorldId)
                {
                    say(openeden::sdk::LogLevel::Info,
                        "PAPYRUS PlaceAtMe: the world did not take " + hexId(request.base) + " in cell " + hexId(cell));
                    return none;
                }
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS placed " + hexId(request.base) + " as " + hexId(static_cast<FormId>(id)) + " in cell "
                        + hexId(cell));
                return { static_cast<FormId>(id), cell };
            }

            // Task 0268: a script moved an actor, or an object of another cell, to a place near the player: the world
            // cannot move an actor (the actor system places actors) and keeps an object in its cell, so a NEW reference
            // of the same base is made at the place. The host hides the old one and points the aliases at the new.
            // Whether the base of a reference (placed, or created by a script) is an actor.
            bool isActor(FormId reference) const
            {
                FormId base = kNullFormId;
                for (const PapyrusHost::CreatedObject& created : mHost.createdObjects())
                    if (created.mReference == reference)
                        base = created.mBase;
                if (base == kNullFormId)
                    if (const PlacedRefIndex* refs = mView.placedRefs())
                        if (const PlacedEntry* entry = refs->find(reference))
                            base = entry->mBase;
                openeden::sdk::game::PlacedObject placed;
                return base != kNullFormId && mView.describeBase(base, placed)
                    && placed.baseKind == openeden::sdk::game::BaseKind::Actor;
            }

            ObjectPlaceResult relocateObject(
                FormId reference, FormId base, const ObjectMoveRequest& request, FormId destinationCell)
            {
                ObjectPlaceResult none;
                if (mSources == nullptr || !request.hasPosition)
                    return none;
                openeden::sdk::game::PlacedObject placed;
                if (!mView.describeBase(base, placed))
                    return none;
                const bool actor = placed.baseKind == openeden::sdk::game::BaseKind::Actor;
                if (placed.baseKind == openeden::sdk::game::BaseKind::LeveledList
                    || placed.baseKind == openeden::sdk::game::BaseKind::Unknown)
                    return none;
                if (!actor && cellOfReference(reference) == destinationCell)
                    return none; // the world moves it where it is
                placed.position = { static_cast<float>(request.position[0]), static_cast<float>(request.position[1]),
                    static_cast<float>(request.position[2]) };
                placed.rotation = { static_cast<float>(request.angles[0]), static_cast<float>(request.angles[1]),
                    static_cast<float>(request.angles[2]) };
                placed.scale = 1.f;
                openeden::sdk::game::WorldObject object;
                std::string problem;
                if (!toNeutral(placed, object, problem))
                    return none;
                const FormId hint = destinationCell != 0 && mData.cellIsInterior(destinationCell) ? destinationCell : 0;
                const openeden::sdk::game::WorldId id
                    = actor ? mSources->placeActor(mCells, hint, object) : mSources->placeObject(mCells, hint, object);
                say(openeden::sdk::LogLevel::Info,
                    "PAPYRUS relocate " + hexId(reference) + " (" + hexId(base) + (actor ? ", an actor" : "")
                        + ") near the player: "
                        + (id == openeden::sdk::game::kNoWorldId ? "the world did not take it"
                                                                 : "made " + hexId(static_cast<FormId>(id))));
                if (id == openeden::sdk::game::kNoWorldId)
                    return none;
                if (actor)
                {
                    mView.noteCreatedActor(static_cast<FormId>(id), base); // speaks as its base does (scene lines)
                    if (auto* cells = dynamic_cast<SkyrimCellSource*>(&mCells))
                        cells->noteRelocated(reference, static_cast<FormId>(id)); // task 0274: a rider's horse
                }
                ObjectPlaceResult placedResult{ static_cast<FormId>(id), hint };
                placedResult.actor = actor; // task 0276
                return placedResult;
            }

            // After a load the world is rebuilt from the pack without the scripts' moves (a cell that was not loaded
            // when
            // the script ran): tell it again.
            void pushMoves()
            {
                std::map<FormId, ObjectMoveRequest> all;
                for (const auto& [reference, xyz] : mHost.positionOverrides())
                {
                    ObjectMoveRequest& request = all[reference];
                    request.hasPosition = true;
                    for (std::size_t i = 0; i < 3; ++i)
                        request.position[i] = xyz[i];
                }
                for (const auto& [reference, angles] : mHost.angleOverrides())
                {
                    ObjectMoveRequest& request = all[reference];
                    request.hasAngles = true;
                    for (std::size_t i = 0; i < 3; ++i)
                        request.angles[i] = angles[i];
                }
                for (const auto& [reference, request] : all)
                    (void)pushMove(reference, request);
            }

        private:
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
                        if (s.mHasPrimitive) // task 0256
                        {
                            TriggerVolume volume;
                            volume.mType = s.mPrimitive.mType;
                            for (std::size_t i = 0; i < 3; ++i)
                                volume.mBounds[i] = static_cast<double>(s.mPrimitive.mBounds[i]);
                            volume.mPosition[0] = static_cast<double>(s.mPosition.x);
                            volume.mPosition[1] = static_cast<double>(s.mPosition.y);
                            volume.mPosition[2] = static_cast<double>(s.mPosition.z);
                            volume.mCell = s.mCell;
                            volume.mRotationZ = static_cast<double>(s.mRotation.z); // task 0274
                            volume.mAcceptBox = mMainQuest; // task 0274: the cart ride's slabs
                            volume.mInterior = mData.cellIsInterior(s.mCell);
                            if (mMainQuest && volume.mType == kPrimitiveBox)
                                say(openeden::sdk::LogLevel::Info,
                                    std::string("PAPYRUS box trigger ") + hexOf(s.mReference) + " "
                                        + (s.mScripts.empty() ? std::string("-") : s.mScripts.front().mName) + " rot "
                                        + std::to_string(s.mRotation.z) + " at "
                                        + std::to_string(static_cast<int>(s.mPosition.x)) + " "
                                        + std::to_string(static_cast<int>(s.mPosition.y)) + " "
                                        + std::to_string(static_cast<int>(s.mPosition.z)) + " bounds "
                                        + std::to_string(static_cast<int>(volume.mBounds[0])) + " "
                                        + std::to_string(static_cast<int>(volume.mBounds[1])) + " "
                                        + std::to_string(static_cast<int>(volume.mBounds[2])));
                            (void)mHost.addTrigger(s.mReference, volume);
                        }
                    }
                    mHost.onCellLoaded(cell, references);
                    raiseLocationChange(cell);
                    say(openeden::sdk::LogLevel::Info,
                        std::string("PAPYRUS") + " cell " + std::to_string(cell)
                            + " loaded: " + std::to_string(references.size()) + " references with scripts");
                }
                for (const openeden::sdk::game::WorldId reference : mPendingActivations)
                    mHost.onActivate(reference, 0x14);
                mPendingActivations.clear();
                for (const openeden::sdk::game::WorldId reference : mPendingReads)
                    mHost.onRead(static_cast<FormId>(reference));
                mPendingReads.clear();
            }

            SkyrimWorldView& mView;
            PackPapyrusData mData;
            PapyrusHost mHost;
            const QuestState* mQuestStateForStage = nullptr; // task 0275: borrowed (the world view owns it)
            openeden::sdk::Logger* mLogger;
            static constexpr double kAmbientFirstSeconds = 5.0;
            static constexpr double kAmbientSeconds = 30.0;
            static constexpr std::size_t kAmbientPairs = 40;
            bool mAmbientOn = true;
            double mAmbientClock = 0.0;
            double mAmbientNext = kAmbientFirstSeconds;
            std::uint64_t mAmbientEvents = 0;
            bool mStarted = false;
            bool mHouses = false; ///< task 0261
            const QuestIndex* mHouseQuests = nullptr; ///< task 0269
            bool mHousesPermission = false;
            bool mHousesApplied = false;
            bool mMainQuest = false; ///< task 0268
            float mSceneParkSeconds = 0.f; ///< task 0287: 0 keeps the runner's default
            std::map<FormId, FormId> mMovedCells; ///< task 0286: a reference the world moved -> the cell it stands in
            bool mMainQuestApplied = false;
            double mNextCartLog = 0.0; ///< task 0274
            bool mEnablingChildren = false; ///< task 0274: pushEnabled is walking the enable children
            bool mMainQuestReplay = false;
            std::int32_t mMainQuestTestStage = 0;
            double mMainQuestTestDelay = 0.0;
            bool mMarriageOn = false; ///< task 0270
            bool mWeddingRelocation = false; ///< task 0276
            std::int32_t mMarriageFinStage = 0; ///< task 0276: the test aid's stage for the married-state quest
            bool mMarriageFinPending = false;
            bool mMarriageApplied = false;
            std::vector<FormId> mMarriageFriends;
            FollowerState* mMarriageRanks = nullptr;
            FollowerState* mFollowers = nullptr;
            static constexpr FormId kCompanionsQuest = 0x0004B2D9; // C00 "Take Up Arms"
            static constexpr FormId kCompanionsTrainingQuest = 0x000A3EBC; // C00VilkasTrainingQuest
            bool mCompanionsTraining = false;
            bool mTrainingPending = false;
            ForceGreet mForce; ///< task 0264 (before the scene glue that points at it)
            bool mForceOn = false; ///< tasks 0264 and 0271: forced greetings (companions or quest alias packages)
            bool mCompanionsOn = false; ///< task 0264
            std::unique_ptr<SceneGlue> mScenes; ///< task 0194 (declared after the host it reaches)
            std::optional<std::vector<std::uint8_t>> mPendingRestore;
            openeden::sdk::game::WorldSourceService* mSources;
            openeden::sdk::game::WorldCellSource& mCells;
            double mNextSummary = kSummarySeconds;
            LocationChange mLocationChange; ///< the last location a CLOC event named (tasks 0187, 0211)
            static constexpr std::size_t kMaxPendingStory = 64;
            static constexpr int kStoryPerFrame = 4;
            std::optional<openeden::sdk::game::PlayerPlace> mPendingPlace;
            openeden::sdk::game::SubjectState mPlayerState; ///< task 0256
            bool mHavePlayerState = false;
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
            std::vector<openeden::sdk::game::WorldId> mPendingReads; // task 0234
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
                const std::vector<std::uint8_t> bytes
                    = mDriver != nullptr ? mDriver->saveState() : std::vector<std::uint8_t>();
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

        // Task 0211: the scenes that play at a save, a chunk of their own (the Papyrus chunk refuses any other version
        // and size, so adding to it would refuse every old save). Written whenever the driver exists (a zero count when
        // no scene plays); a load hands the entries to the driver, which applies them after the host's own restore.
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
                const std::vector<std::uint8_t> bytes
                    = encodeSceneState(mDriver != nullptr ? mDriver->sceneState() : std::vector<SceneSaveEntry>());
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
        // Task 0260: the vampire quest's script as the vampirism service sees it (PlayerVampireQuest, 0x0EAFD5, whose
        // PlayerVampireQuestScript the Papyrus host attached from the record's VMAD).
        class VampireQuestPort final : public vampirism::ScriptPort
        {
        public:
            explicit VampireQuestPort(PapyrusHost& host)
                : mHost(host)
            {
            }
            bool ready() const override
            {
                return mHost.numberVariable(vampirism::kQuest, vampirism::kQuestScript, "VampireStatus").has_value();
            }
            bool change() override
            {
                const PapyrusHost::CallArgument args[]
                    = { PapyrusHost::CallArgument::form(vampirism::kPlayerReference) };
                return mHost.callFunctionWith(vampirism::kQuest, vampirism::kQuestScript, "VampireChange", args);
            }
            bool feed() override
            {
                return mHost.callFunction(vampirism::kQuest, vampirism::kQuestScript, "VampireFeed");
            }
            bool tick() override
            {
                return mHost.sendEventTo(vampirism::kQuest, vampirism::kQuestScript, "OnUpdateGameTime");
            }
            bool progress(int stage) override
            {
                const PapyrusHost::CallArgument args[] = { PapyrusHost::CallArgument::form(vampirism::kPlayerReference),
                    PapyrusHost::CallArgument::integer(stage) };
                return mHost.callFunctionWith(vampirism::kQuest, vampirism::kQuestScript, "VampireProgression", args);
            }
            int status() const override
            {
                const auto v = mHost.numberVariable(vampirism::kQuest, vampirism::kQuestScript, "VampireStatus");
                return v ? static_cast<int>(*v) : -1;
            }
            bool setStage(int stage, double lastFeedDays) override
            {
                return mHost.setNumberVariable(vampirism::kQuest, vampirism::kQuestScript, "VampireStatus", stage)
                    && mHost.setNumberVariable(
                        vampirism::kQuest, vampirism::kQuestScript, "LastFeedTime", lastFeedDays);
            }
            bool cure() override // task 0279: what the Falion quest's fragment calls
            {
                const PapyrusHost::CallArgument args[]
                    = { PapyrusHost::CallArgument::form(vampirism::kPlayerReference) };
                return mHost.callFunctionWith(vampirism::kQuest, vampirism::kQuestScript, "VampireCure", args);
            }
            void setDays(double days) override
            {
                if (mDaysGlobal == kNullFormId)
                    if (const auto f = mHost.formVariable(vampirism::kQuest, vampirism::kQuestScript, "GameDaysPassed"))
                        mDaysGlobal = *f;
                if (mDaysGlobal != kNullFormId)
                    mHost.setGlobalValue(mDaysGlobal, static_cast<float>(days));
            }

        private:
            PapyrusHost& mHost;
            FormId mDaysGlobal = kNullFormId;
        };

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
                    feedGameClock(); // task 0276
                    syncStones(); // task 0237
                    syncDiseases(); // task 0240
                    syncVampirism(); // task 0260
                    syncEffectNumbers(); // task 0248
                    if (mProgression != nullptr)
                        pollProgression(); // task 0200
                    if (mShouts != nullptr)
                        (void)mShouts->poll(); // task 0205
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
                    if (mWerewolf != nullptr)
                        werewolfFrame(dt); // task 0259
                    if (mWorldView.playerBodyOn())
                        syncPlayerBody(); // task 0222
                    if (mCharacter != nullptr)
                        pollCharacter(); // task 0225
                });
            }

            // Task 0276: the game clock for the Papyrus host's game-time updates and the GameDaysPassed global that the
            // wedding scripts read (GLOB 0x39). Only with `marriage`; the vampirism quest keeps its own hand-written
            // update (its registrations are ignored).
            void feedGameClock()
            {
                if (!mGameClockOn || mPapyrus == nullptr)
                    return;
                if (mRest != nullptr && !(mRest->now() > 0.0))
                    return; // the rest clock is not known yet: the two clocks must not switch under a timer
                const double clock = vampireClockSeconds();
                mPapyrus->host().setGameClock(clock);
                mPapyrus->host().setGlobalValue(kGameDaysPassedGlobal, static_cast<float>(clock / 86400.0));
            }

            // Task 0196: the game's own recruit path. A script (DialogueFollowerScript) forces the follower into the
            // DialogueFollower quest's alias 0 and clears it to dismiss; the pack follows the alias.
            void syncFollowerAlias()
            {
                if (mPapyrus == nullptr)
                    return;
                const std::uint32_t now = mPapyrus->host().forcedAlias(kFollowerQuestId, kFollowerAliasId);
                // Task 0239: "Wait here" and "Follow me" set the WaitingForPlayer actor value; the brain follows it
                for (const openeden::sdk::game::WorldId id : mCellSource.actorTable().followerIds())
                    (void)mCellSource.actorTable().setFollowerWaiting(id, mFollowerState.waiting(id));
                const FollowerAliasChange change = followerAliasChange(mFollowerAliasSeen, now);
                if (change.mDismiss == 0 && change.mRecruit == 0)
                    return;
                mFollowerAliasSeen = now;
                if (change.mDismiss != 0)
                {
                    const bool done = mCellSource.actorTable().dismiss(change.mDismiss);
                    if (mFollowerLogger != nullptr)
                        mFollowerLogger->log(openeden::sdk::LogLevel::Info,
                            "follower dismissed: placement " + std::to_string(change.mDismiss)
                                + (done ? "" : " (no change)"));
                }
                if (change.mRecruit != 0)
                {
                    const bool done = mCellSource.actorTable().recruit(change.mRecruit);
                    if (mFollowerLogger != nullptr)
                        mFollowerLogger->log(openeden::sdk::LogLevel::Info,
                            "follower recruited: placement " + std::to_string(change.mRecruit)
                                + (done ? "" : " (no change)"));
                }
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
                mFollowerLogger = logger;
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
                            // Task 0238: GetGlobalValue in an effect's conditions sees what scripts set
                            mCellSource.combat().setGlobalSource([driver](FormId global, float& value) {
                                return driver->host().globalSetByScript(global, value);
                            });
                            mPapyrus->host().setFollowerState(&mFollowerState); // task 0239
                            mPapyrus->setSceneParkSeconds(settings->getFloat(kPackId, "scene park seconds", 0.f)); // 0287
                            if (settings->getBool(kPackId, kActorCellMovesKey, false)) // task 0286
                            {
                                mPapyrus->host().setCellMoves(true);
                                PapyrusDriver* const mover = mPapyrus.get();
                                mCellSource.actorTable().setTravelMoveHook(
                                    [mover](openeden::sdk::game::WorldId actor, FormId target) {
                                        return mover->travelMove(actor, target);
                                    });
                                if (logger != nullptr)
                                    logger->log(openeden::sdk::LogLevel::Info,
                                        "ACTORMOVES actors and objects move between cells through the world");
                            }
                            if (settings->getBool(kPackId, mainquest::kMainQuestStartKey, false)) // task 0268
                            {
                                mPapyrus->enableMainQuest();
                                mPapyrus->setMainQuestTestStage(
                                    settings->getString(kPackId, "main quest test stage", ""));
                                mCellSource.setNewGameStart(mainquest::kHelgenStart);
                                if (logger != nullptr)
                                    logger->log(openeden::sdk::LogLevel::Info,
                                        std::string("MAINQUEST a new game starts at the cart: ")
                                            + mainquest::kHelgenStart);
                            }
                            if (settings->getBool(kPackId, marriage::kMarriageKey, false)) // task 0270
                            {
                                std::vector<FormId> friends;
                                if (!marriage::parseBaseIdList(
                                        settings->getString(kPackId, marriage::kMarriageTestFriendsKey, ""), friends)
                                    && logger != nullptr)
                                    logger->log(openeden::sdk::LogLevel::Warning,
                                        "the setting 'marriage test friends' is not a list of hexadecimal base ids");
                                mPapyrus->enableMarriage(std::move(friends), &mFollowerState);
                                mPapyrus->host().enableGameTimers(true); // task 0276
                                mPapyrus->host().ignoreGameTimersOf(vampirism::kQuest);
                                mGameClockOn = true;
                                mPapyrus->setMarriageFinStage(static_cast<std::int32_t>(
                                    settings->getInt(kPackId, marriage::kMarriageTestFinStageKey, 0)));
                                // the priest, the lover and the witnesses are moved into the temple (MoveTo of actors
                                // near the player: a new reference there, task 0268's rule), but only once the wedding
                                // quest runs: the start-up quests' own moves stay as they were
                                // the wedding's travel packages and the priest's forced greeting need these two
                                if (logger != nullptr
                                    && !(settings->getBool(kPackId, kQuestAliasPackagesKey, false)
                                        && settings->getBool(kPackId, kActorPackagesKey, false)))
                                    logger->log(openeden::sdk::LogLevel::Warning,
                                        "marriage: the wedding scene needs 'actor packages' and 'quest alias "
                                        "packages' (its travel packages and the priest's forced greeting)");
                            }
                            if (settings->getBool(kPackId, houses::kHousesKey, false)) // task 0261
                            {
                                mHousesOn = true;
                                mPapyrus->enableHouses(
                                    settings->getBool(kPackId, houses::kHousesTestPermissionKey, false),
                                    &mFollowerState);
                                mHousesChunk = std::make_unique<houses::HousesChunk>([this] { return mHouses.data(); },
                                    [this, logger](const std::map<FormId, FormId>& data) {
                                        const bool ok = mHouses.restore(data);
                                        if (ok && logger != nullptr)
                                            logger->log(openeden::sdk::LogLevel::Info,
                                                "HOUSES restored " + std::to_string(data.size()) + " owned cells");
                                        return ok;
                                    });
                                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                                    if (!chunks->registerHandler(*mHousesChunk) && logger != nullptr)
                                        logger->log(
                                            openeden::sdk::LogLevel::Warning, "the houses chunk was not registered");
                                if (logger != nullptr)
                                    logger->log(openeden::sdk::LogLevel::Info, "HOUSES bought houses are on");
                            }
                            {
                                // Task 0278: a stablemaster's lines test JobHostlerFaction, which every hostler holds at rank -1
                                // (as the stewards hold JobStewardFaction: task 0261): rank -1 counts as a member for those.
                                std::set<FormId> minusOne;
                                if (mHousesOn)
                                    minusOne.insert(houses::kJobStewardFaction);
                                if (settings->getBool(kPackId, kRidingKey, false))
                                    minusOne.insert(static_cast<FormId>(kJobHostlerFactionId));
                                if (!minusOne.empty())
                                    mWorldView.setRankMinusOneMembers(std::move(minusOne));
                            }
                            const bool companions = settings->getBool(kPackId, "companions", false); // task 0264
                            if (companions)
                            {
                                driver->enableCompanions();
                                mCellSource.actorTable().setCarryTravellers(true); // task 0277
                                mWorldView.setLiveActorPlace(
                                    [this](FormId reference, double& x, double& y, double& z) {
                                        return mCellSource.actorTable().brainPlace(reference, x, y, z);
                                    });
                                if (settings->getBool(kPackId, "companions test training", false))
                                    driver->enableCompanionsTraining();
                            }
                            // tasks 0264 and 0271: one forced-greeting mechanism for both settings
                            if (companions || settings->getBool(kPackId, kQuestAliasPackagesKey, false))
                            {
                                driver->enableForceGreet();
                                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                                {
                                    const openeden::sdk::DeclareResult declared
                                        = driver->forceGreet().declare(*bindings);
                                    if (!declared.ok && logger != nullptr)
                                        logger->log(openeden::sdk::LogLevel::Warning,
                                            "the forced greeting bindings were refused: " + declared.message);
                                }
                                if (logger != nullptr)
                                    logger->log(openeden::sdk::LogLevel::Info, "FORCEGREET forced greetings are on");
                            }
                            mCellSource.setEnableOverrideLookup(
                                [this, driver](openeden::sdk::game::WorldId reference) -> std::optional<bool> {
                                    if (const std::optional<bool> own
                                        = driver->host().enableOverride(static_cast<FormId>(reference)))
                                        return own;
                                    // Task 0261: the furnishings of a house are enabled by their enable parent (XESP),
                                    // a marker the purchase's script enables: a cell built after follows its parent's
                                    // state
                                    if (!mHousesOn && !driver->mainQuestOn())
                                        return std::nullopt;
                                    const PlacedRefIndex* placed = mWorldView.placedRefs();
                                    const PlacedEntry* entry
                                        = placed != nullptr ? placed->find(static_cast<FormId>(reference)) : nullptr;
                                    if (entry == nullptr || entry->mEnableParent == kNullFormId)
                                        return std::nullopt;
                                    const std::optional<bool> parent
                                        = driver->host().enableOverride(entry->mEnableParent);
                                    if (!parent)
                                        return std::nullopt;
                                    return (entry->mFlags & PlacedFlags::EnableOpposite) != 0 ? !*parent : *parent;
                                });
                            // Task 0202: a cell built after a script moved one of its objects has it already there
                            mCellSource.setMoveOverrideLookup([driver](openeden::sdk::game::WorldId reference)
                                                                  -> std::optional<openeden::sdk::game::ObjectMove> {
                                const FormId ref = static_cast<FormId>(reference);
                                const auto position = driver->host().positionOverride(ref);
                                const auto angles = driver->host().angleOverride(ref);
                                if (!position && !angles)
                                    return std::nullopt;
                                openeden::sdk::game::ObjectMove move;
                                if (position)
                                {
                                    move.hasPosition = true;
                                    move.position = { (*position)[0], (*position)[1], (*position)[2] };
                                }
                                if (angles)
                                {
                                    move.hasRotation = true;
                                    move.rotation = eulerToQuat({ static_cast<float>((*angles)[0]),
                                        static_cast<float>((*angles)[1]), static_cast<float>((*angles)[2]) });
                                }
                                return move;
                            });
                            if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                            {
                                mPapyrusChunk = std::make_unique<PapyrusSaveChunk>(mPapyrus);
                                mPapyrusChunk->setRefusalSink([logger](const std::string& reason) {
                                    if (logger != nullptr)
                                        logger->log(openeden::sdk::LogLevel::Error, "the load is refused: " + reason);
                                });
                                if (!chunks->registerHandler(*mPapyrusChunk) && logger != nullptr)
                                    logger->log(
                                        openeden::sdk::LogLevel::Warning, "the Papyrus chunk was not registered");
                                mSceneChunk = std::make_unique<SceneSaveChunk>(mPapyrus);
                                mSceneChunk->setRefusalSink([logger](const std::string& reason) {
                                    if (logger != nullptr)
                                        logger->log(openeden::sdk::LogLevel::Error, "the load is refused: " + reason);
                                });
                                if (!chunks->registerHandler(*mSceneChunk) && logger != nullptr)
                                    logger->log(
                                        openeden::sdk::LogLevel::Warning, "the scenes chunk was not registered");
                                mStoryChunk = std::make_unique<StorySaveChunk>(mPapyrus);
                                mStoryChunk->setRefusalSink([logger](const std::string& reason) {
                                    if (logger != nullptr)
                                        logger->log(openeden::sdk::LogLevel::Error, "the load is refused: " + reason);
                                });
                                if (!chunks->registerHandler(*mStoryChunk) && logger != nullptr)
                                    logger->log(openeden::sdk::LogLevel::Warning, "the story chunk was not registered");
                            }
                            // Task 0211: where the player stands, and a developer aid for one story event
                            mCellSource.setPlayerPlaceSink([driver](const openeden::sdk::game::PlayerPlace& place) {
                                driver->playerPlace(place);
                            });
                            mWorldView.setPlayerSite([driver] { return driver->playerSite(); });
                            mCellSource.setPlayerStateSink([driver](const openeden::sdk::game::SubjectState& state) {
                                driver->playerState(state);
                            });
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
                    // Task 0182: the inventory state and its script bindings, off unless [extension.skyrimse]
                    // inventory.
                    startInventory(host, logger);
                    // Task 0200: the player's skills and levels (the namespace skyrimse.progression).
                    startProgression(host, logger);
                    // Task 0189: the barter session (the namespace skyrimse.barter); needs the inventory.
                    startBarter(host, logger);
                    // Task 0219: training, rooms to rent and gifts (skyrimse.services); needs the inventory.
                    startServices(host, logger);
                    // Task 0204: crime, bounty and guards (the namespace skyrimse.crime).
                    startCrime(host, logger);
                    // Task 0231: NPCs speak unprompted (hello, chatter, combat lines), off unless
                    // [extension.skyrimse] barks.
                    startBarks(host, logger);
                    // Task 0257: Night Eye changes the picture (the core polls the cell source's image space).
                    if (const openeden::sdk::SettingsService* const settings = host.getSettingsService())
                        mCellSource.combat().setNightEyeView(settings->getBool(kPackId, "night eye", false));
                    // Task 0203: lock picking and books (the namespace skyrimse.lockbooks); needs the inventory.
                    startLockBooks(host, logger);
                    // Task 0208: picking up placed items and pickpocketing (needs the inventory).
                    startPickup(host, logger);
                    // Task 0227: sneaking and detection (the namespace skyrimse.sneak), off unless
                    // [extension.skyrimse] sneak; crime and pickpocketing ask it who has noticed the player.
                    startSneak(host, logger);
                    // Task 0244: the Skyrim-style combat HUD's numbers (the namespace skyrimse.hudstyle).
                    startHudStyle(host, logger);
                    wirePapyrusWorld(host, logger);
                    // Task 0237: standing stones and shrines (the Papyrus host's spells, messages and blessings)
                    startStones(host, logger);
                    // Task 0240: diseases (a creature carries one with neutral combat and creatures), off unless
                    // [extension.skyrimse] diseases
                    startDiseases(host, logger);
                    // Task 0260: vampirism (the game's own PlayerVampireQuest scripts), off unless
                    // [extension.skyrimse] vampirism
                    startVampirism(host, logger);
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
                    // Task 0205: the shouts (the namespace skyrimse.shouts), off unless [extension.skyrimse] shouts.
                    startShouts(host, logger);
                    // Task 0259: the werewolf beast form (the namespace skyrimse.werewolf), off unless
                    // [extension.skyrimse] werewolf; needs the player body.
                    startWerewolf(host, logger);
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
                mCellSource.setDeaths(nullptr); // task 0254: before the keeper goes
                mCellSource.setCrime(nullptr); // task 0204: before the crime service goes
                mCellSource.setTakenLookup({}); // task 0208: before the pickup service goes
                mCellSource.setHarvestedLookup({}); // task 0221: before the harvest service goes
                mCellSource.setPlacementNameLookup({}); // task 0224: before the mining service goes
                mCellSource.combat().setShoutState(nullptr);
                unwirePapyrusWorld();
                if (mPapyrus != nullptr)
                {
                    mPapyrus->summary();
                    mCellSource.setScriptSinks({}, {});
                    mCellSource.setEnableOverrideLookup({});
                    mCellSource.combat().setGlobalSource({}); // task 0238: it refers to the host
                    mPapyrus.reset();
                }
                // The index worker uses the logger and the file source: end it before anything is withdrawn.
                mWorldView.cancelAndJoin();
                withdrawWorldSource();
                withdrawWorldView();
            }

            ~SkyrimGamePack() override
            {
                mCellSource.setDeaths(nullptr);
                mCellSource.setCrime(nullptr);
                unwirePapyrusWorld();
                mCellSource.combat().setGlobalSource({}); // task 0238
                mPapyrus.reset();
                mWorldView.cancelAndJoin();
                withdrawWorldSource();
                withdrawWorldView();
            }

        private:
            // Task 0198: the Papyrus host's reach into the inventory and combat (see papyrus_world.hpp). Needs the
            // host; the inventory natives need the inventory (setting `inventory`), the combat events need combat to
            // run.
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
                // Task 0264: the Companions' training fight asks the combat AI for a fight and a stop
                const auto handleOfActor = [this](FormId actor) { return mCellSource.actorTable().handleOf(actor); };
                env.startCombat = [this, handleOfActor](FormId actor) {
                    const openeden::sdk::game::ActorHandle h = handleOfActor(actor);
                    return h.valid() && mCellSource.combat().scriptStartCombat(h);
                };
                env.stopCombat = [this, handleOfActor](FormId actor) {
                    const openeden::sdk::game::ActorHandle h = handleOfActor(actor);
                    return h.valid() && mCellSource.combat().scriptStopCombat(h);
                };
                env.resetHealth = [this, handleOfActor](FormId actor) {
                    const openeden::sdk::game::ActorHandle h = handleOfActor(actor);
                    return h.valid() && mCellSource.combat().scriptResetHealth(h);
                };
                env.inCombat = [this, handleOfActor](FormId actor) {
                    // task 0282: the player is in combat when he fought lately or a living hostile is engaged with him
                    const openeden::sdk::game::CombatSubject subject = actor == kPapyrusPlayerReference
                        ? openeden::sdk::game::CombatSubject::ofPlayer()
                        : openeden::sdk::game::CombatSubject::of(handleOfActor(actor));
                    return mCellSource.combat().scriptInCombatOf(subject);
                };
                env.isItem = [this](FormId form) {
                    return mInventory != nullptr && mInventory->table().item(form) != nullptr;
                };
                env.goldValue = [this](FormId form) -> std::int32_t { // task 0285
                    const auto* stat = mInventory != nullptr ? mInventory->table().item(form) : nullptr;
                    return stat != nullptr ? stat->mValue : -1;
                };
                // Task 0202: moves, fades and creations reach the engine through the world source service
                env.setObjectEnabled = [this](FormId reference, bool enabled, float fade) {
                    return mPapyrus != nullptr && mPapyrus->pushEnabled(reference, enabled, fade);
                };
                env.moveObject = [this](FormId reference, const ObjectMoveRequest& request) {
                    return mPapyrus != nullptr && mPapyrus->pushMove(reference, request);
                };
                env.moveToCell = [this](FormId reference, FormId cell, const ObjectMoveRequest& request) {
                    return mPapyrus != nullptr && mPapyrus->pushMoveToCell(reference, cell, request);
                };
                env.tetherCart = [this, logger](FormId cart, FormId horse, const double (&at)[3], double heading) {
                    if (mPapyrus == nullptr || !mPapyrus->mainQuestOn())
                        return false;
                    CartPose pose;
                    pose.x = at[0];
                    pose.y = at[1];
                    pose.z = at[2];
                    pose.heading = static_cast<float>(heading); // NaN: the rig takes the line to the horse
                    const bool made = mCellSource.tetherCart(cart, horse, pose);
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "CART " + hexOf(cart) + " tethered to " + hexOf(horse) + (made ? "" : " (refused)"));
                    return made;
                };
                env.setVehicle = [this, logger](FormId rider, FormId vehicle) {
                    if (mPapyrus == nullptr || !mPapyrus->mainQuestOn())
                        return false;
                    const bool done = vehicle != 0 ? mCellSource.addCartRider(rider, vehicle)
                                                   : mCellSource.removeCartRider(rider);
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "CART rider " + hexOf(rider) + (vehicle != 0 ? " on " + hexOf(vehicle) : " gets off")
                                + (done ? "" : " (refused)"));
                    return done;
                };
                // The cart's exit idles: the rider gets off at once (stands beside the cart) and the game's script
                // hears "ExitCartEnd" after kCartExitSeconds (OURS: the exit clips are not played)
                env.playIdle = [this, logger](FormId actor, FormId idle, std::string& event, double& seconds) {
                    if (mPapyrus == nullptr || !mPapyrus->mainQuestOn() || !mCellSource.carts().isRider(actor))
                        return false;
                    std::string name = mPapyrus->formName(idle);
                    for (char& ch : name)
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (name.find("exit") == std::string::npos)
                        return false;
                    (void)mCellSource.removeCartRider(actor);
                    event = "ExitCartEnd";
                    seconds = mainquest::kCartExitSeconds;
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "CART rider " + hexOf(actor) + " leaves the cart (" + name + ")");
                    return true;
                };
                env.referenceLoaded = [this](FormId reference) {
                    // an actor is loaded when the actor system holds it; anything else is taken to be
                    return mPapyrus == nullptr || !mPapyrus->isActor(reference)
                        || mCellSource.sceneActorLoaded(reference);
                };
                env.relocateObject
                    = [this](FormId reference, FormId base, const ObjectMoveRequest& request, FormId cell) {
                          return mPapyrus != nullptr ? mPapyrus->relocateObject(reference, base, request, cell)
                                                     : ObjectPlaceResult{};
                      };
                env.placeObject = [this](const ObjectPlaceRequest& request) {
                    return mPapyrus != nullptr ? mPapyrus->placeObject(request) : ObjectPlaceResult{};
                };
                env.setCellOwner = [this, logger](FormId cell, FormId owner) {
                    if (!mHousesOn || !mHouses.setCellOwner(cell, owner))
                        return false;
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "HOUSES cell " + hexOf(cell) + " is now owned by " + hexOf(owner));
                    return true;
                };
                env.teachWord = [this](FormId word) { return mShouts != nullptr && mShouts->teachWord(word); };
                env.unlockWord = [this](FormId word) { return mShouts != nullptr && mShouts->unlockWord(word); };
                env.isWordUnlocked
                    = [this](FormId word) { return mShouts != nullptr && mShouts->isWordUnlocked(word); };
                // Task 0258: the guards' arrest dialogue fragments act through the crime service
                env.crimeFactionOf
                    = [this](FormId actor) { return mCrime != nullptr ? mCrime->crimeFactionOfReference(actor) : 0; };
                env.payCrimeGold = [this](FormId faction, bool removeStolenItems, bool) {
                    return mCrime != nullptr && mCrime->payByScript(faction, removeStolenItems);
                };
                env.sendToJail
                    = [this](FormId faction, bool, bool) { return mCrime != nullptr && mCrime->jailByScript(faction); };
                env.resistArrest = [this](FormId) { return mCrime != nullptr && mCrime->resistByScript(); };
                env.crimeGold = [this](FormId faction) -> std::int32_t {
                    return mCrime != nullptr ? mCrime->crimeGoldOf(faction) : -1;
                };
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
                            host.onItemMoved(
                                PackWorldHooks::referenceOf(from), PackWorldHooks::referenceOf(to), item, count);
                        });
                mCellSource.scriptBridge().setSinks(
                    [&host, logger](FormId victim, FormId attacker, bool blocked, bool bash) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info,
                                "PAPYRUS hit on " + hexOf(victim) + " by " + hexOf(attacker)
                                    + (blocked ? " (blocked)" : ""));
                        host.onActorHit(victim, attacker, blocked, bash);
                    },
                    [this, &host, logger](FormId victim, FormId killer) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info,
                                "PAPYRUS death of " + hexOf(victim) + " killer " + hexOf(killer));
                        host.onActorDied(victim, killer);
                        if (mPapyrus != nullptr)
                            mPapyrus->queueKill(
                                victim, killer); // task 0211: the story manager's KILL, raised in frame()
                    },
                    [this](FormId actor, openeden::sdk::game::CombatRequestWriter& writer) {
                        const openeden::sdk::game::ActorHandle handle = mCellSource.actorTable().handleOf(actor);
                        if (!handle.valid())
                            return;
                        (void)writer.reportDeath(handle);
                        (void)mCellSource.combat().setFighterHealth(
                            openeden::sdk::game::CombatSubject::of(handle), 0.f);
                    });
                if (mPapyrus != nullptr && mPapyrus->companionsOn()) // task 0264
                {
                    mCellSource.combat().setEssentialBleedout(true);
                    mCellSource.setStandOnEngage(true);
                    mCellSource.combat().setBleedoutSink([&host, logger](openeden::sdk::game::ActorHandle actor) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info, "PAPYRUS bleedout of " + hexOf(actor.reference));
                        host.onActorBleedout(actor.reference);
                    });
                }
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

            // Task 0237: the spells and messages of the stones and shrines. Needs the Papyrus host; the experience
            // effect needs `player progression`, the damage effect `neutral combat`.
            void startStones(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                if (mPapyrus == nullptr)
                    return;
                if (const openeden::sdk::SettingsService* const testSettings = host.getSettingsService())
                {
                    const std::string cast = testSettings->getString(kPackId, "stones test cast", "");
                    if (!cast.empty())
                        mStonesTestCast = static_cast<FormId>(std::strtoul(cast.c_str(), nullptr, 16)); // task 0248
                }
                stones::StonesService::Hooks hooks;
                hooks.mLibrary = [this]() -> const PerkLibrary* {
                    const progression::SkillTable* skills = nullptr;
                    const PerkLibrary* perks = nullptr;
                    progression::Settings settings;
                    return mWorldView.progressionInputs(skills, perks, settings) ? perks : nullptr;
                };
                hooks.mPersistent = [this] {
                    std::vector<FormId> spells = mPapyrus->host().spellsOf(kPapyrusPlayerReference);
                    if (mVampirism != nullptr) // task 0260: the sun damage abilities only in the sun; the race ability
                        return vampirism::filterAbilities(spells, mVampirism->vampire(), mVampireSun);
                    return spells;
                };
                // task 0241: the game clock (a wait or a sleep moves it) once the rest script gave it; else the host's
                hooks.mNow = [this] {
                    return mRest != nullptr && mRest->now() > 0.0 ? mRest->now() : mPapyrus->host().gameSeconds();
                };
                hooks.mApply = [this](std::span<const FormId> spells) {
                    std::vector<FormId> all(spells.begin(), spells.end());
                    all.insert(all.end(), mBeastAbilities.begin(), mBeastAbilities.end()); // task 0259
                    mCellSource.combat().setPlayerExtraAbilities(std::move(all));
                };
                hooks.mAnswer = [this](FormId message, std::int32_t button) {
                    return mPapyrus->host().answerMessage(message, button);
                };
                hooks.mActivate = [this](FormId placement, FormId base) {
                    if (mWorldView.stoneNameOf(base).empty())
                        return false;
                    mPapyrus->host().onActivate(placement, kPapyrusPlayerReference);
                    return true;
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                hooks.mCure = [this] {
                    if (mDiseases != nullptr)
                        (void)mDiseases->cure(); // task 0240: every temple blessing carries Cure Disease
                };
                mStones = std::make_unique<stones::StonesService>(std::move(hooks));
                stones::StonesService* stonesPtr = mStones.get();
                mPapyrus->host().setMessageSink(
                    [stonesPtr](FormId message, const PapyrusMessageInfo& info) { stonesPtr->ask(message, info); });
                if (mProgression != nullptr)
                    mProgression->setSkillUseScale(
                        [stonesPtr](std::size_t slot) { return stonesPtr->skillScale(slot); });
                // a stone or shrine shows its name (and so can be activated with the real key) like an ore vein does
                mCellSource.setPlacementNameLookup(
                    [this](openeden::sdk::game::WorldId placement, openeden::sdk::game::WorldId base) {
                        std::string name = mMining != nullptr
                            ? mMining->nameOf(static_cast<FormId>(placement), static_cast<FormId>(base))
                            : std::string();
                        return name.empty() ? mWorldView.stoneNameOf(static_cast<FormId>(base)) : name;
                    });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mStones->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the messages bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                    if (!chunks->registerHandler(mStones->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the stones chunk was not registered");
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "STONES standing stones and shrines are on");
            }

            // Task 0240: diseases. A blow of a creature that carries one (the race attack spell is of type Disease)
            // infects the player by a seeded roll; the penalties go to the combat rules, a cure potion or a temple
            // shrine takes them away, and the infections are saved. Off unless [extension.skyrimse] diseases.
            const PerkLibrary* diseaseLibrary()
            {
                const progression::SkillTable* skills = nullptr;
                const PerkLibrary* perks = nullptr;
                progression::Settings settings;
                return mWorldView.progressionInputs(skills, perks, settings) ? perks : nullptr;
            }

            void startDiseases(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "diseases", false))
                    return;
                diseases::Settings options;
                const float chance = settings->getFloat(kPackId, "diseases test chance", -1.f);
                if (chance >= 0.f)
                    options.mChance = chance; // a proof aid: 1 infects with every carrier blow
                const int seed = settings->getInt(kPackId, "diseases seed", 0);
                if (seed != 0)
                    options.mSeed = static_cast<std::uint64_t>(static_cast<std::uint32_t>(seed));
                const std::string infect = settings->getString(kPackId, "diseases test infect", "");
                if (!infect.empty())
                    mDiseaseTestInfect = static_cast<FormId>(std::strtoul(infect.c_str(), nullptr, 16));
                diseases::DiseaseService::Hooks hooks;
                hooks.mLibrary = [this]() { return diseaseLibrary(); };
                hooks.mPlayerSpells = [this] { return mCellSource.combat().playerSpells(); };
                hooks.mApply = [this](const PlayerValueModifiers& modifiers) {
                    mCellSource.combat().setPlayerValueModifiers(modifiers);
                };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mDiseases = std::make_unique<diseases::DiseaseService>(std::move(hooks), options);
                diseases::DiseaseService* service = mDiseases.get();
                mCellSource.combat().setPlayerHitHook(
                    [service](const std::vector<FormId>& attackSpells, std::uint32_t attacker) {
                        (void)service->onHit(attackSpells, attacker);
                    });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mDiseases->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the diseases bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                    if (!chunks->registerHandler(mDiseases->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the diseases chunk was not registered");
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info,
                        "DISEASE diseases are on (chance " + std::to_string(options.mChance) + ")");
            }

            void syncDiseases()
            {
                if (mDiseases == nullptr)
                    return;
                if (mDiseaseTestInfect != kNullFormId)
                {
                    // a proof aid (the setting holds a hex form id): infects once the records are read
                    if (mDiseases->infect(mDiseaseTestInfect) || mDiseases->has(mDiseaseTestInfect))
                        mDiseaseTestInfect = kNullFormId;
                }
                mDiseases->poll();
            }

            // Task 0260: vampirism. The game's own PlayerVampireQuestScript runs on the Papyrus host; the service adds
            // the clocks the scripts left to the engine, the sunlight test and the feeding (vampirism.hpp).
            double vampireClockSeconds() const
            {
                return mRest != nullptr && mRest->now() > 0.0 ? mRest->now() : mPapyrus->host().gameSeconds();
            }

            void startVampirism(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "vampirism", false))
                    return;
                if (mPapyrus == nullptr || mStones == nullptr)
                {
                    if (logger != nullptr)
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "vampirism needs the Papyrus host (papyrus = true)");
                    return;
                }
                mVampireLog = logger;
                mVampirePort = std::make_unique<VampireQuestPort>(mPapyrus->host());
                vampirism::VampirismService::Hooks hooks;
                hooks.mDays = [this] { return vampireClockSeconds() / 86400.0; };
                hooks.mSanguinare = [this] { return mDiseases != nullptr && mDiseases->has(vampirism::kSanguinare); };
                hooks.mCureSanguinare = [this] {
                    if (mDiseases != nullptr)
                        (void)mDiseases->cureOne(vampirism::kSanguinare, false);
                };
                hooks.mSafe = [this] { return !mCellSource.combat().playerInCombat(); };
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mVampirism = std::make_unique<vampirism::VampirismService>(*mVampirePort, std::move(hooks));
                if (const int stage = settings->getInt(kPackId, "vampirism test stage", 0); stage > 0)
                    mVampirism->startAs(stage);
                mVampireTestCureHours = settings->getFloat(kPackId, "vampirism test cure", 0.f); // task 0279
                // a sleeper activated by a vampire is fed on, not woken for a talk
                mCellSource.setFeedHook(
                    [this](openeden::sdk::game::WorldId) { return mVampirism != nullptr && mVampirism->feed(); });
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                    if (!chunks->registerHandler(mVampirism->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the vampirism chunk was not registered");
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "VAMPIRE vampirism is on");
            }

            void syncVampirism()
            {
                if (mVampirism == nullptr)
                    return;
                mVampirism->poll();
                // Task 0279: VampireChange's SetRace and VampireCure's way back: the body is drawn with the vampire race
                mWorldView.setVampireLook(mVampirism->vampire());
                if (mVampireTestCureHours > 0.f && mVampirism->vampire() && !mVampireCureScheduled)
                {
                    mVampireCureScheduled = true;
                    mVampirism->scheduleCure(vampireClockSeconds() / 86400.0 + mVampireTestCureHours / 24.0);
                }
                // sunlight: the data's own conditions, re-evaluated when the game minute or the place changes
                bool sun = false;
                effectconditions::Place place;
                const PerkLibrary* library = mVampirism->vampire() ? diseaseLibrary() : nullptr;
                if (library != nullptr && mCellSource.currentPlace(place))
                {
                    const double clock = vampireClockSeconds();
                    const std::int64_t minute = static_cast<std::int64_t>(clock / 60.0);
                    const std::uint64_t placeKey
                        = (place.mInterior ? 1ull : 0ull) | (static_cast<std::uint64_t>(place.mWorld) << 1);
                    if (minute != mVampireSunMinute || placeKey != mVampireSunPlace)
                    {
                        mVampireSunMinute = minute;
                        mVampireSunPlace = placeKey;
                        mVampireSunValue = vampirism::sunlit(*library, vampirism::kSunDamageSpells[0], clock, place,
                            [this](FormId global, float& value) {
                                return mPapyrus->host().globalSetByScript(global, value)
                                    || mWorldView.globalDefault(global, value);
                            });
                    }
                    sun = mVampireSunValue;
                }
                else
                    mVampireSunMinute = -1;
                if (sun != mVampireSun)
                {
                    mVampireSun = sun;
                    mLogVampireSun(sun);
                }
            }

            void mLogVampireSun(bool sun) const
            {
                if (mVampireLog != nullptr)
                    mVampireLog->log(
                        openeden::sdk::LogLevel::Info, sun ? "VAMPIRE sunlight burns" : "VAMPIRE out of the sun");
            }

            void syncStones()
            {
                if (mStones == nullptr || mPapyrus == nullptr)
                    return;
                for (const PapyrusHost::SpellCast& cast : mPapyrus->host().takeCasts())
                    (void)mStones->cast(cast.mSpell, cast.mTarget);
                if (mStonesTestCast != kNullFormId && mStones->cast(mStonesTestCast, 0x14))
                    mStonesTestCast = kNullFormId; // task 0248
                mStones->poll();
                // task 0241: the perks Actor.AddPerk gave the player (the Ritual stone's) reach the combat; the carry
                // weight a stone gives (the Steed's) reaches the inventory
                const std::uint64_t perkRevision = mPapyrus->host().perkRevision();
                if (perkRevision != mPerksSynced)
                {
                    mPerksSynced = perkRevision;
                    mCellSource.combat().setPlayerExtraPerks(mPapyrus->host().perksOf(kPapyrusPlayerReference));
                }
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
                if (mProgression != nullptr)
                    mProgression->setSkillUseScale({});
                mCellSource.setFeedHook({}); // task 0260
                mVampirism.reset();
                mVampirePort.reset();
                mStones.reset();
                mCellSource.combat().setPlayerHitHook({}); // task 0240
                mDiseases.reset();
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
                                "JOURNAL test quests: " + std::to_string(set)
                                    + " quests moved to a stage with a log text");
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
                            std::min<std::int32_t>(maxX - minX + 1, static_cast<std::int32_t>(map::kPageSize)), pixels);
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
                // Task 0223: the carriage drivers' Game.FastTravel (the Papyrus host, when it runs) leaves a request
                // that the global travel script reads
                if (mPapyrus != nullptr)
                    mPapyrus->host().setFastTravelSink([this](FormId destination) {
                        return mTravel != nullptr && mTravel->queueRequest(destination);
                    });
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
            // the window is open, so nothing here may wait for a game phase), the saved choice is a chunk, and the
            // race's abilities reach the combat rules and its starting skills the progression.
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
                hooks.mName = [this](const std::string& name) { mWorldView.setPlayerName(name); }; // task 0228
                hooks.mPowers = [this](const std::vector<FormId>& spells) {
                    std::vector<character::PowerInfo> out;
                    for (const SkyrimCombat::RacePower& power : mCellSource.combat().racePowers(spells))
                    {
                        character::PowerInfo info;
                        info.mId = power.mId;
                        info.mName = power.mName;
                        info.mSupport = static_cast<character::PowerSupport>(power.mSupport);
                        out.push_back(std::move(info));
                    }
                    return out;
                };
                hooks.mPowerSource = [](const character::PowerInfo& power) { // task 0281
                    if (power.mId == werewolf::kBeastPower)
                        return std::string("Lycanthropy");
                    if (power.mName.rfind("Vampire", 0) == 0)
                        return std::string("Vampirism");
                    return std::string("Standing stone");
                };
                hooks.mCastPower = [this](FormId spell) { return mCellSource.combat().castPlayerPower(spell); };
                hooks.mExtraPowerSpells = [this] { // task 0241: the powers of the stones the player holds
                    std::vector<FormId> held = mStones != nullptr ? mStones->active() : std::vector<FormId>();
                    for (const FormId power : werewolfPowers()) // task 0259: Beast Form
                        held.push_back(power);
                    return held;
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
                    if (!chunks->registerHandler(mCharacter->powerChunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the character powers chunk was not registered");
                    if (!chunks->registerHandler(mCharacter->choiceChunk()) && logger != nullptr) // task 0281
                        logger->log(openeden::sdk::LogLevel::Warning, "the power choice chunk was not registered");
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
                    // task 0261: the owner is the bed's own or, when it has none, the one a script gave its cell
                    const FormId who = effectiveOwnerOfPlacement(static_cast<FormId>(placement));
                    // task 0219: a rented room's bed is the player's while the rental lasts
                    const bool rented = mServices != nullptr && mServices->isRented(static_cast<FormId>(placement));
                    if (who != kNullFormId && !ignoreOwner && !rented)
                        owned = !(who == crime::kPlayerNpcId || who == kPlayerFactionId);
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
                    mProgression->setGainMultiplier(
                        [this] { return mRest != nullptr ? mRest->rested().skillGain(mRest->now()) : 1.f; });
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
                    const std::vector<inventory::Stack> stacks
                        = inventory::resolveContents(entries, mInventory->table(), seed, harvest::kListLevel);
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
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the harvest bindings were refused: " + declared.message);
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
                        std::string name = mMining != nullptr
                            ? mMining->nameOf(static_cast<FormId>(placement), static_cast<FormId>(base))
                            : std::string();
                        // task 0237: a standing stone or shrine shows its name too
                        return name.empty() && mStones != nullptr ? mWorldView.stoneNameOf(static_cast<FormId>(base))
                                                                  : name;
                    });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mMining->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the mining bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                {
                    mMining->chunk().setRefusalSink([logger](const std::string& why) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Warning, "MINING " + why);
                    });
                    mMining->chunk().setRestoredSink([logger](std::size_t veins) {
                        if (logger != nullptr)
                            logger->log(
                                openeden::sdk::LogLevel::Info, "MINING restored " + std::to_string(veins) + " veins");
                    });
                    if (!chunks->registerHandler(mMining->chunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the mining chunk was not registered");
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "MINING mining and woodcutting are on");
            }

            // Task 0205: the player's shouts: the state (words, souls, cooldown), its save chunk, the namespace
            // skyrimse.shouts and the test settings. The combat rules cast the shout (combat_shout.cpp).
            // Task 0259: the werewolf beast form. The power Beast Form (SPEL WerewolfChange 0x92C48, an effect of
            // archetype 36) is cast from the character service's power list with the P key; the combat calls
            // beastStart() instead of casting it. The form lasts 150 s (the quest script's own number), is third person
            // with the beast's body and clips, fights with the race's claws, ignores gear and menus, and feeding on a
            // corpse (the activate key) adds time and heals. HAND-WRITTEN: the shipped scripts are not run (they need
            // SetRace, OnRaceSwitchComplete, UnequipAll, ForceThirdPerson and the Companions quest's own scripts).
            void startWerewolf(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, std::string(werewolf::kKey), false))
                    return;
                mWerewolfLog = [logger](openeden::sdk::LogLevel level, const std::string& text) {
                    if (logger != nullptr)
                        logger->log(level, text);
                };
                mWerewolfTestSeconds = settings->getFloat(kPackId, std::string(werewolf::kTestSecondsKey), 0.f);
                mWerewolfGrant = settings->getBool(kPackId, std::string(werewolf::kTestGrantKey), false);
                mWerewolf = std::make_unique<werewolf::WerewolfService>(
                    [this](std::uint64_t placement, FormId base) { return beastFeed(placement, base); });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mWerewolf->declare(*bindings);
                    if (!declared.ok)
                        mWerewolfLog(openeden::sdk::LogLevel::Warning,
                            "the werewolf bindings were refused: " + declared.message);
                }
                mCellSource.combat().setBeastStartHook([this] { beastStart(); });
                // Task 0265: the form, its time, the corpses fed on and the grant are saved in a chunk of their own.
                mBeastChunk = werewolf::makeBeastChunk([this] { return beastSaved(); },
                    [this](const werewolf::BeastSaved& saved) { beastRestore(saved); });
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                    if (!chunks->registerHandler(*mBeastChunk))
                        mWerewolfLog(openeden::sdk::LogLevel::Warning, "the werewolf chunk was not registered");
                mWerewolfLog(openeden::sdk::LogLevel::Info,
                    std::string("WEREWOLF the beast form is on") + (mWerewolfGrant ? " (the power is granted)" : ""));
            }

            /// The powers the player has beyond his race's: with the test grant, Beast Form.
            [[nodiscard]] std::vector<FormId> werewolfPowers() const
            {
                if (mWerewolf != nullptr && mWerewolfGrant)
                    return { werewolf::kBeastPower };
                return {};
            }

            void applyExtraAbilities()
            {
                std::vector<FormId> all = mStones != nullptr ? mStones->active() : std::vector<FormId>();
                all.insert(all.end(), mBeastAbilities.begin(), mBeastAbilities.end());
                mCellSource.combat().setPlayerExtraAbilities(std::move(all));
            }

            void beastStart()
            {
                if (mWerewolf == nullptr || mBeastClock.active() || mBeastEnding.active())
                    return;
                (void)beastBegin(mWerewolfTestSeconds > 0.f ? mWerewolfTestSeconds : werewolf::kStandardSeconds, {});
            }

            /// Starts the form with `seconds` on the clock and `fed` already fed on (a fresh change: none; a load: the
            /// saved ones). False (nothing changed) when the race's numbers are not read yet.
            bool beastBegin(float seconds, const std::set<std::uint64_t>& fed, bool warn = true, bool fresh = true)
            {
                const std::uint32_t level = mProgression != nullptr && mProgression->progression() != nullptr
                    ? mProgression->progression()->level()
                    : 1u;
                werewolf::BeastNumbers numbers;
                if (!mWorldView.beastNumbers(level, numbers))
                {
                    if (warn)
                        mWerewolfLog(openeden::sdk::LogLevel::Warning,
                            "WEREWOLF the race's numbers are not read yet: no change");
                    return false;
                }
                mBeastClock.start(seconds);
                mBeastFed = fed;
                mBeastAbilities = { werewolf::kBaseAbility, werewolf::levelAbility(level) };
                mCellSource.combat().setPlayerBeast(&numbers);
                applyExtraAbilities();
                mWorldView.setBeastForm(true);
                mWerewolf->setState(true, mBeastClock.remaining());
                if (fresh)
                    mCellSource.bodyClips().queue(werewolf::kTransformInClip); // task 0279
                beastGiveHowl(fresh);
                mWerewolfLog(openeden::sdk::LogLevel::Info,
                    "WEREWOLF transformed: " + std::to_string(static_cast<int>(mBeastClock.remaining())) + " s, claws "
                        + std::to_string(numbers.mUnarmedDamage) + " reach " + std::to_string(numbers.mUnarmedReach)
                        + ", level ability " + std::to_string(werewolf::levelAbility(level)));
                return true;
            }

            // Task 0279: PlayerWerewolfChangeScript.StartTracking learns and equips the quest's CurrentHowl
            // (HowlWerewolfFear: Fury, then its two other words) and the end removes it; what the player held is given back.
            // A form restored from a save (`fresh` false) already finds the Howl in the shouts chunk: every word of it is
            // taken as ours, so the end removes them.
            void beastGiveHowl(bool fresh)
            {
                mBeastHowlWords.clear();
                mBeastPrevShout = kNullFormId;
                if (mShouts == nullptr)
                    return;
                shouts::ShoutState& state = mShouts->state();
                for (const FormId word : werewolf::kDefaultHowlWords)
                {
                    if (!fresh)
                        mBeastHowlWords.push_back(word);
                    else if (!state.unlocked(word) && state.forceUnlock(word))
                        mBeastHowlWords.push_back(word);
                }
                if (fresh && state.equipped() != werewolf::kDefaultHowl)
                    mBeastPrevShout = state.equipped();
                state.equip(werewolf::kDefaultHowl);
            }

            void beastTakeHowl()
            {
                if (mShouts == nullptr)
                    return;
                shouts::ShoutState& state = mShouts->state();
                if (state.equipped() == werewolf::kDefaultHowl)
                    state.equip(mBeastPrevShout);
                for (const FormId word : mBeastHowlWords)
                    (void)state.forget(word);
                mBeastHowlWords.clear();
                mBeastPrevShout = kNullFormId;
            }

            void beastEnd(const char* why, bool announce = true)
            {
                mBeastEnding.cancel();
                beastTakeHowl();
                mBeastClock.stop();
                mBeastAbilities.clear();
                mCellSource.combat().setPlayerBeast(nullptr);
                applyExtraAbilities();
                mWorldView.setBeastForm(false);
                mWerewolf->setState(false, 0.f);
                if (announce)
                    mWerewolf->announce("You return to your human form.");
                mWerewolfLog(openeden::sdk::LogLevel::Info, std::string("WEREWOLF back to human (") + why + ")");
            }

            // Task 0265: what a save keeps. While a loaded form still waits for the race's numbers it is kept as it
            // was read, so a save made in that gap does not lose it.
            [[nodiscard]] werewolf::BeastSaved beastSaved() const
            {
                if (mBeastPending.has_value())
                    return *mBeastPending;
                werewolf::BeastSaved saved;
                saved.mGranted = mWerewolfGrant;
                saved.mActive = mBeastClock.active();
                saved.mRemaining = mBeastClock.remaining();
                saved.mFed.assign(mBeastFed.begin(), mBeastFed.end());
                return saved;
            }

            // A load: the grant comes back, a form that is on in this process but not in the save ends quietly, and a
            // form that was on in the save is begun again as soon as the race's numbers can be read (werewolfFrame).
            void beastRestore(const werewolf::BeastSaved& saved)
            {
                if (saved.mGranted)
                    mWerewolfGrant = true;
                if (mBeastClock.active())
                    beastEnd("a load", false);
                mBeastPending.reset();
                mBeastPendingWarned = false;
                if (saved.mActive)
                    mBeastPending = saved;
                mWerewolfLog(openeden::sdk::LogLevel::Info,
                    std::string("WEREWOLF restored: ")
                        + (saved.mActive ? "the beast form, " + std::to_string(static_cast<int>(saved.mRemaining))
                                    + " s left, " + std::to_string(saved.mFed.size()) + " corpses fed on"
                                         : std::string("human"))
                        + (saved.mGranted ? ", Beast Form granted" : ""));
            }

            void beastApplyPending()
            {
                const std::set<std::uint64_t> fed(mBeastPending->mFed.begin(), mBeastPending->mFed.end());
                // a loaded form waits for the race's numbers: said once, tried again every frame
                if (beastBegin(mBeastPending->mRemaining, fed, !mBeastPendingWarned, false))
                {
                    mBeastPending.reset();
                    mBeastPendingWarned = false;
                }
                else
                {
                    mBeastPendingWarned = true;
                }
            }

            void werewolfFrame(float dt)
            {
                mCellSource.bodyClips().tickFrame(); // task 0279
                if (mBeastPending.has_value())
                {
                    beastApplyPending();
                    return;
                }
                if (mBeastEnding.active())
                {
                    // the quest script changes the race back at the clip's TransformToHuman event
                    if (mBeastEnding.tick(dt))
                        beastEnd("the time ran out");
                    return;
                }
                if (!mBeastClock.active())
                    return;
                switch (mBeastClock.tick(dt))
                {
                    case werewolf::BeastClock::Event::Warned:
                        mWerewolf->announce("Your transformation is about to end.");
                        mWerewolfLog(openeden::sdk::LogLevel::Info, "WEREWOLF the form is about to end");
                        break;
                    case werewolf::BeastClock::Event::Ended:
                        // Task 0279: the way back plays ww_bleedoutstart; the body changes at its TransformToHuman
                        mBeastEnding.begin();
                        mCellSource.bodyClips().queue(werewolf::kTransformOutClip);
                        mWerewolf->setState(true, 0.f);
                        mWerewolfLog(openeden::sdk::LogLevel::Info, "WEREWOLF the time ran out: the way back begins");
                        return;
                    case werewolf::BeastClock::Event::None:
                        break;
                }
                mWerewolf->setState(true, mBeastClock.remaining());
            }

            bool beastFeed(std::uint64_t placement, FormId base)
            {
                if (!mBeastClock.active() || !mBeastFed.insert(placement).second)
                    return false; // a corpse is fed on once
                const bool person = mWorldView.baseIsPerson(static_cast<openeden::sdk::game::WorldId>(base));
                (void)mBeastClock.feed(person);
                mCellSource.bodyClips().queue(werewolf::kFeedClip); // task 0279
                const bool healed = mCellSource.combat().queuePlayerHeal(werewolf::kFeedHealth);
                mWerewolf->announce("You feed.");
                mWerewolf->setState(true, mBeastClock.remaining());
                mWerewolfLog(openeden::sdk::LogLevel::Info,
                    std::string("WEREWOLF fed on ") + (person ? "a person" : "an animal") + ", "
                        + std::to_string(static_cast<int>(mBeastClock.remaining())) + " s left"
                        + (healed ? ", healed" : ", not healed (the combat rules are not running)"));
                return true;
            }

            void startShouts(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "shouts", false))
                    return;
                const auto say = [logger](openeden::sdk::LogLevel level, const std::string& text) {
                    if (logger != nullptr)
                        logger->log(level, text);
                };
                shouts::Hooks hooks;
                hooks.mIndex = [this]() -> const ShoutIndex* {
                    return mWorldView.combatReady() ? mWorldView.shoutIndex() : nullptr;
                };
                hooks.mLog = [say](const std::string& line) { say(openeden::sdk::LogLevel::Info, line); };
                // Task 0266: the shout menu shows the words (WOOP FULL) and their translations (TNAM) from the string
                // tables.
                const std::string language = settings->getString(kPackId, "string table language", "english");
                if (language.size() >= 2 && language.size() <= 32
                    && std::all_of(
                        language.begin(), language.end(), [](unsigned char c) { return std::isalpha(c) != 0; }))
                    mBookLanguage = language;
                hooks.mText = [this](const LString& text, FormId owner) { return resolveShoutString(text, owner); };
                mShouts = std::make_unique<shouts::ShoutService>(std::move(hooks));
                shouts::TestSettings test;
                test.mLearn = settings->getString(kPackId, "shouts test learn", "");
                test.mUnlock = settings->getString(kPackId, "shouts test unlock", "");
                test.mSouls = settings->getInt(kPackId, "shouts test souls", 0);
                test.mShout = settings->getString(kPackId, "shouts test shout", "");
                mShouts->setTestSettings(test);
                mShouts->chunk().setRefusalSink([say](const std::string& reason) {
                    say(openeden::sdk::LogLevel::Error, "the shouts chunk is refused: " + reason);
                });
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mShouts->declare(*bindings);
                    if (!declared.ok)
                        say(openeden::sdk::LogLevel::Warning, "the shouts bindings were refused: " + declared.message);
                }
                if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                    if (!chunks->registerHandler(mShouts->chunk()))
                        say(openeden::sdk::LogLevel::Warning, "the shouts chunk was not registered");
                mCellSource.combat().setShoutState(&mShouts->state());
                if (!mCellSource.combat().setTestPlayerShout(test.mShout))
                    say(openeden::sdk::LogLevel::Warning, "the setting 'shouts test shout' is too long; no shout");
                mCellSource.combat().setTestShoutEvery(settings->getFloat(kPackId, "shouts test shout every", 0.f));
                mCellSource.combat().setTestShoutHold(settings->getFloat(kPackId, "shouts test shout hold", 1.f));
                say(openeden::sdk::LogLevel::Info, "SHOUTS the shouts are on");
            }

            // Task 0182: the inventory service (item table, model, save chunk) and the namespace skyrimse.inventory.
            void startInventory(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "inventory", false))
                    return;
                mScrollLogger = logger; // task 0234
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
                hooks.mUsePoison = [this](FormId item) { return usePoisonOnWeapon(item); }; // task 0229
                hooks.mUseScroll = [this](FormId item) { return useScroll(item); }; // task 0234
                hooks.mIsCureEffect = [this](FormId effect) { // task 0240
                    const PerkLibrary* library = mDiseases != nullptr ? diseaseLibrary() : nullptr;
                    return library != nullptr && diseases::isCureDiseaseEffect(*library, effect);
                };
                hooks.mCureDisease = [this] {
                    if (mDiseases != nullptr)
                        (void)mDiseases->cure();
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
                if (settings->getBool(kPackId, marriage::kMarriageKey, false)) // task 0270
                {
                    mWorldView.setPlayerWornSource(
                        [this](FormId item) { return mInventory != nullptr && mInventory->model().isEquipped(item); });
                    mWorldView.setPlayerItemSource([this](FormId item) {
                        return mInventory != nullptr
                            ? static_cast<float>(mInventory->model().count(inventory::kPlayerOwner, item))
                            : 0.f;
                    });
                }
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
                    applyEffectiveSkills(*p);
                }
            }

            // Task 0248: the speech checks of the dialogue see the skill with the blessings' points and percent; the
            // progression keeps its own value.
            void applyEffectiveSkills(const progression::PlayerProgression& p)
            {
                std::array<std::uint8_t, progression::kSkillCount> skills{};
                for (std::size_t s = 0; s < progression::kSkillCount; ++s)
                {
                    float value = 0.f;
                    if (!mProgression->effectiveActorValue(
                            progression::kFirstSkillValue + static_cast<std::uint32_t>(s), value))
                        value = p.skill(s);
                    skills[s] = static_cast<std::uint8_t>(std::clamp(value, 0.f, 255.f));
                }
                mWorldView.setProgressionPlayer(p.level(), skills);
            }

            // Task 0248: what the combat holds of abilities, blessings and diseases that other systems read: skill
            // points and percent for the progression, the carry weight for the inventory.
            void syncEffectNumbers()
            {
                const std::uint64_t skillRevision = mCellSource.combat().skillModRevision();
                if (mProgression != nullptr && skillRevision != mSkillModsSynced)
                {
                    mSkillModsSynced = skillRevision;
                    const SkyrimCombat::SkillModifiers mods = mCellSource.combat().playerSkillModifiers();
                    mProgression->setSkillModifiers(mods.mPoints, mods.mPower);
                    if (progression::PlayerProgression* p = mProgression->progression())
                        applyEffectiveSkills(*p);
                }
                const std::uint64_t abilityRevision = mCellSource.combat().abilityRevision();
                if (mInventory != nullptr && abilityRevision != mAbilitiesSynced)
                {
                    mAbilitiesSynced = abilityRevision;
                    mInventory->setCapacityBonus(mCellSource.combat().playerCarryBonus());
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
                hooks.mGoTo
                    = [this](openeden::sdk::game::ActorHandle h, double x, double y, double z,
                          openeden::sdk::game::ActorSpeed speed) { mCellSource.combat().crimeGoTo(h, x, y, z, speed); };
                hooks.mHold = [this](openeden::sdk::game::ActorHandle h) { mCellSource.combat().crimeHold(h); };
                hooks.mRelease = [this](openeden::sdk::game::ActorHandle h) { mCellSource.combat().crimeRelease(h); };
                hooks.mAttack = [this](openeden::sdk::game::ActorHandle h) { mCellSource.combat().crimeAttack(h); };
                hooks.mJail = [this](FormId faction, crime::JailDestination& out) {
                    return mWorldView.jailDestination(faction, out);
                };
                // Task 0213: trespass reads the CELL records (owner, public flag) and the owner of a bed placement.
                hooks.mCellAccess = [this](const std::string& editorId, crime::CellAccess& out) {
                    if (!mWorldView.cellAccessByName(editorId, out))
                        return false;
                    if (mHousesOn) // task 0261: a bought house is the player's
                        mHouses.applyToAccess(mWorldView.cellIdByEditorId(editorId), out);
                    return true;
                };
                hooks.mPlacementOwner = [this](FormId placement) {
                    if (mServices != nullptr && mServices->isRented(placement))
                        return crime::kPlayerNpcId; // task 0219: a rented room's bed is the player's
                    return effectiveOwnerOfPlacement(placement);
                };
                hooks.mLog = [this, logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                // Task 0258: the arrest conversation runs only with the Papyrus host (its fragments make the choice)
                // and when a forced greeting passes for the guard; else the window offers
                hooks.mDialogueAvailable = [this, logger](const crime::CrimeActor& guard) {
                    if (mPapyrus == nullptr)
                    {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info,
                                "CRIME the arrest conversation needs the Papyrus host ([extension.skyrimse] papyrus): "
                                "the window offers");
                        return false;
                    }
                    std::string why;
                    const bool available = mWorldView.forcedGreetingAvailable(
                        guard.mBase, static_cast<FormId>(guard.mHandle.reference), &why);
                    if (!available && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "CRIME no arrest conversation for " + guard.mName + " (base " + std::to_string(guard.mBase)
                                + "), the window offers:" + why);
                    return available;
                };
                mCrime = std::make_unique<crime::CrimeService>(std::move(hooks));
                {
                    SkyrimWorldView::ArrestDialogueHooks arrest;
                    arrest.mWants = [this](FormId reference) {
                        return mCrime != nullptr && mCrime->system().wantsDialogueWith(reference);
                    };
                    arrest.mBegan = [this](FormId reference) {
                        if (mCrime != nullptr)
                            mCrime->system().dialogueBegan(reference);
                    };
                    arrest.mEnded = [this](FormId reference) {
                        if (mCrime != nullptr)
                            mCrime->system().dialogueEnded(reference);
                    };
                    arrest.mFailed = [this](FormId reference) {
                        if (mCrime != nullptr)
                            mCrime->system().dialogueFailed(reference);
                    };
                    mWorldView.setArrestDialogue(std::move(arrest));
                    mWorldView.setCrimeConditionSource(mCrime->conditionSource());
                }
                mCellSource.combat().setCrimeTestGuard(settings->getString(kPackId, "crime test guard", ""));
                mCellSource.setCrime(&mCrime->system());
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mCrime->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the crime bindings were refused: " + declared.message);
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

            // Task 0244: the restyled combat HUD (the namespace skyrimse.hudstyle). Off unless
            // [extension.skyrimse] hud style; it needs the combat rules ([Game] neutral combat) for its numbers.
            void startHudStyle(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "hud style", false))
                    return;
                hudstyle::StyleHud::Source source;
                source.mFighters
                    = [this](const SkyrimCombat::FighterVisitor& visit) { mCellSource.combat().forEachFighter(visit); };
                source.mLookup = [this](openeden::sdk::game::CombatSubject subject, SkyrimCombat::FighterView& out) {
                    return mCellSource.combat().fighterView(subject, out);
                };
                source.mIsAlly
                    = [this](openeden::sdk::game::WorldId reference) { return mCellSource.isFollower(reference); };
                mStyleHud = std::make_unique<hudstyle::StyleHud>(mCellSource.hud(), std::move(source));
                mCellSource.setStyleHud(mStyleHud.get());
                mStyleBindings = std::make_unique<hudstyle::StyleBindings>(*mStyleHud);
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mStyleBindings->declareTo(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the HUD style bindings were refused: " + declared.message);
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "HUDSTYLE the Skyrim-style combat HUD is on");
            }

            // Task 0227: sneaking and detection. Needs `[Game] neutral combat` (the engine hands the pack the player's
            // stance only through the combat bridge).
            void startSneak(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "sneak", false))
                    return;
                sneak::Hooks hooks;
                hooks.mActors = [this](std::vector<crime::CrimeActor>& out) { mCellSource.combat().crimeActors(out); };
                hooks.mIsFollower = [this](std::uint32_t reference) { return mCellSource.isFollower(reference); };
                hooks.mSkill = [this]() {
                    float skill = 0.f;
                    constexpr std::uint32_t kSneakValue = progression::kFirstSkillValue + progression::Slot::Sneak;
                    if (mProgression != nullptr && mProgression->effectiveActorValue(kSneakValue, skill))
                        return skill;
                    return 15.f; // a new character's Sneak (RECALLED-VERIFY)
                };
                hooks.mArmourWeight = [this]() {
                    float weight = 0.f;
                    if (mInventory == nullptr)
                        return weight;
                    for (const inventory::EquippedItem& worn : mInventory->model().equipped())
                        if (worn.mKind != inventory::EquipKind::Weapon)
                            if (const inventory::ItemStat* stat = mInventory->table().item(worn.mItem))
                                weight += stat->mWeight;
                    return weight;
                };
                hooks.mTrain = [this](float amount) {
                    if (mProgression != nullptr)
                        mProgression->skillUsed(progression::Slot::Sneak, amount);
                };
                hooks.mInvisible = [this] { return mCellSource.combat().playerInvisible(); }; // task 0241
                hooks.mLog = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                mSneak = std::make_unique<sneak::SneakSystem>(std::move(hooks));
                mCellSource.setSneak(mSneak.get());
                mCellSource.combat().setSneakAttackHooks(
                    [this](std::uint32_t victim, std::uint8_t animType, bool ranged) {
                        return mSneak->attackMultiplier(victim, animType, ranged);
                    },
                    [this](std::uint32_t victim, float multiplier) { mSneak->onSneakAttack(victim, multiplier); });
                if (mCrime != nullptr)
                    mCrime->system().setDetectedLookup(
                        [this](std::uint32_t reference) { return mSneak->detected(reference); });
                mSneakBindings = std::make_unique<sneak::SneakBindings>(*mSneak);
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mSneakBindings->declareTo(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the sneak bindings were refused: " + declared.message);
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "SNEAK sneaking and detection are on");
                // Task 0235: an actor that begins to suspect a sneaking player says what it noticed (NOTA).
                if (mBarks != nullptr)
                    mSneak->setAlertHook(
                        [this](std::uint32_t reference, sneak::Alert before, sneak::Alert after, bool sneaking) {
                            if (sneaking && before == sneak::Alert::Hidden && after == sneak::Alert::Caution)
                                mBarks->onEvent(barks::Kind::Notice, static_cast<std::uint64_t>(reference));
                        });
            }

            // Task 0231: barks. Off unless [extension.skyrimse] barks. The rules are barks.* (pure); this wires them to
            // the dialogue runtime (which line), the scene speech path of task 0194 (voice and subtitle) and the cell
            // source (the roster and the combat's moments). Needs the dialogue data (about 25 s after start): until
            // then no line is found and the actors try again.
            void startBarks(openeden::sdk::Host& host, openeden::sdk::Logger* logger)
            {
                const openeden::sdk::SettingsService* settings = host.getSettingsService();
                if (settings == nullptr || !settings->getBool(kPackId, "barks", false))
                    return;
                mBarkSubtitles = settings->getBool(kPackId, "barks subtitles", true);
                mBarkStartAt = std::max(0.f, settings->getFloat(kPackId, "barks test start", 0.f)); // a proof aid
                barks::Hooks hooks;
                hooks.mReady = [this] {
                    if (mBarkLine)
                        return true;
                    SkyrimWorldView::PapyrusAccess access;
                    if (!mWorldView.papyrusAccess(access) || !access.mBark)
                        return false;
                    mBarkLine = access.mBark;
                    return true;
                };
                hooks.mChoose
                    = [this](barks::Kind kind, std::uint64_t reference, std::uint32_t pick, barks::Line& out) {
                          if (!mBarkConfigured)
                              if (const SettingsTable* table = mWorldView.gameSettings())
                              {
                                  mBarkConfigured = true;
                                  barks::Settings configured = barks::fromGameSettings(table);
                                  configured.mStartAt = mBarkStartAt;
                                  mBarks->configure(std::move(configured));
                              }
                          if (!mBarkLine)
                          {
                              SkyrimWorldView::PapyrusAccess access;
                              if (!mWorldView.papyrusAccess(access) || !access.mBark)
                                  return false;
                              mBarkLine = access.mBark;
                          }
                          SceneLine line;
                          if (!mBarkLine(barks::subtypeOf(kind), static_cast<FormId>(reference), pick,
                                  mBarks->speakerIsVictim(), line))
                              return false;
                          out.mText = std::move(line.mText);
                          out.mVoice = std::move(line.mVoice);
                          return true;
                      };
                hooks.mSpeak = [this](std::uint64_t reference, const barks::Line& line) {
                    // Skyrim's unsubtitled barks have a text of one space: no box for those.
                    const bool blank = line.mText.find_first_not_of(" \t\r\n") == std::string::npos;
                    return mCellSource.sceneSpeak(static_cast<openeden::sdk::game::WorldId>(reference),
                        mBarkSubtitles && !blank ? line.mText : std::string(), line.mVoice);
                };
                hooks.mTrace = [logger](const std::string& line) {
                    if (logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Info, line);
                };
                barks::Settings initial = barks::fromGameSettings(nullptr);
                initial.mStartAt = mBarkStartAt;
                mBarks = std::make_unique<barks::BarkSystem>(std::move(initial), std::move(hooks));
                mCellSource.setBarks(mBarks.get());
                // Task 0235: a witness of a crime that counts speaks (the crime rules are off, or not: no hook then).
                if (mCrime != nullptr)
                    mCrime->system().setWitnessHook(
                        [this](crime::Kind crime, openeden::sdk::game::ActorHandle witness) {
                            const barks::Kind kind = barks::kindOfCrime(crime);
                            // Task 0243: the witness that is the victim (a caught pickpocket, an assault)
                            const openeden::sdk::game::ActorHandle victim = mCrime->system().witnessVictim();
                            const bool isVictim = victim.reference != 0 && victim.reference == witness.reference;
                            mBarks->onEvent(kind, static_cast<std::uint64_t>(witness.reference), isVictim);
                        });
                // Task 0235: the player's stance reaches the conditions of the bark lines (a drawn weapon, sneaking)
                // and a drawn weapon or spell near an actor makes it say a hello again.
                mCellSource.setStanceSink([this](const openeden::sdk::game::SubjectState& player) {
                    if (!mBarkStance)
                    {
                        SkyrimWorldView::PapyrusAccess access;
                        if (mWorldView.papyrusAccess(access) && access.mSetStance)
                            mBarkStance = access.mSetStance;
                    }
                    if (mBarkStance)
                        mBarkStance(player.sneaking, player.drawState);
                    mBarks->onPlayerStance(player.drawState == 1 || player.drawState == 2, player.sneaking);
                });
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "BARK barks are on");
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
                hooks.mHeldBy
                    = [this](std::uint64_t owner, FormId item) { return mInventory->model().count(owner, item); };
                hooks.mMoveToPlayer = [this](std::uint64_t owner, FormId item, std::int32_t count) {
                    return mInventory->model().transfer(owner, inventory::kPlayerOwner, item, count)
                        == inventory::TransferStatus::Ok;
                };
                hooks.mMoveFromPlayer = [this](std::uint64_t owner, FormId item, std::int32_t count) {
                    return mInventory->model().transfer(inventory::kPlayerOwner, owner, item, count)
                        == inventory::TransferStatus::Ok; // task 0275: reverse pickpocketing
                };
                hooks.mChanceBonus = [this, logger](std::uint64_t victim) {
                    const float bonus = pickpocketPerkBonus(victim);
                    if (logger != nullptr && bonus != 0.f)
                        logger->log(openeden::sdk::LogLevel::Info,
                            "PICKPOCKET the perks add " + std::to_string(static_cast<int>(bonus)) + " percent");
                    return bonus;
                };
                hooks.mSkill = [this]() {
                    float skill = 0.f;
                    constexpr std::uint32_t kPickpocketValue
                        = progression::kFirstSkillValue + progression::Slot::Pickpocket;
                    if (mProgression != nullptr && mProgression->effectiveActorValue(kPickpocketValue, skill))
                        return skill;
                    return pickup::kStandInPickpocket;
                };
                hooks.mTrain = [this](float amount) {
                    if (mProgression != nullptr)
                        mProgression->skillUsed(progression::Slot::Pickpocket, amount);
                };
                hooks.mAware = [this](std::uint64_t owner) {
                    return mSneak != nullptr && mSneak->detected(static_cast<std::uint32_t>(owner)); // task 0227
                };
                hooks.mCaught = [this](std::uint64_t owner, FormId, std::int32_t) {
                    if (mSneak != nullptr)
                        mSneak->markDetected(static_cast<std::uint32_t>(owner)); // task 0227: the victim noticed
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
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the pickup bindings were refused: " + declared.message);
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

            // Task 0275: the perks the player holds (Actor.AddPerk through the Papyrus host: the Thieves Guild's helper
            // perk of TG00) change a pickpocket chance by entry point 56, "Mod Pickpocket Chance", in percentage points.
            float pickpocketPerkBonus(std::uint64_t victim)
            {
                if (mPapyrus == nullptr)
                    return 0.f;
                const PerkLibrary* library = mWorldView.perkLibrary();
                if (library == nullptr)
                    return 0.f;
                const std::vector<FormId> perks = mPapyrus->host().perksOf(kPapyrusPlayerReference);
                if (perks.empty())
                    return 0.f;
                if (!mPickpocketPerks)
                {
                    mPickpocketPerks = std::make_unique<PerkEngine>(*library);
                    mPickpocketPerks->setPlayerRole(PerkRole::Owner);
                    // GetStage of the perk's own quest (the Thieves Guild's helper holds for stages 10 to 29 of TG00)
                    mPickpocketPerks->setQuestStageSource([this](std::uint32_t quest) {
                        return mPapyrus != nullptr ? mPapyrus->questStage(static_cast<FormId>(quest)) : 0.f;
                    });
                }
                if (!mPickpocketPerks->hasEntryPoint(perks, PerkEntry::ModPickpocketChance))
                    return 0.f;
                PerkParticipant owner;
                owner.mKind = PerkParticipant::Kind::Actor;
                owner.mForm = crime::kPlayerNpcId;
                owner.mPerks = &perks;
                // the second tab of the entry point is the victim (RECALLED-VERIFY: the PERK has two tabs); a victim the
                // pack cannot name is an actor with no data, which a tab with no conditions accepts
                PerkParticipant target;
                target.mKind = PerkParticipant::Kind::Actor;
                if (const PlacedRefIndex* refs = mWorldView.placedRefs())
                    if (const PlacedEntry* entry = refs->find(static_cast<FormId>(victim)))
                        target.mForm = entry->mBase;
                PerkQuery query;
                query.set(PerkRole::Owner, &owner);
                query.set(PerkRole::Target, &target);
                return mPickpocketPerks->apply(PerkEntry::ModPickpocketChance, perks, query, 0.f).mValue;
            }

            // Task 0204: the player took an item out of a container: theft when somebody else owns the placement.
            void onContainerTaken(std::uint64_t placement, FormId item, std::int32_t count)
            {
                if (mCrime == nullptr)
                    return;
                const FormId who = effectiveOwnerOfPlacement(static_cast<FormId>(placement)); // task 0261
                if (who == kNullFormId)
                    return;
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
                    constexpr std::uint32_t kSpeechValue
                        = progression::kFirstSkillValue + progression::Slot::Speechcraft;
                    if (mProgression != nullptr && mProgression->effectiveActorValue(kSpeechValue, speech))
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
                        logger->log(
                            openeden::sdk::LogLevel::Warning, "the barter bindings were refused: " + declared.message);
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
                        (void)mProgression->effectiveActorValue(
                            progression::kFirstSkillValue + progression::Slot::Lockpicking, skill); // task 0248
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
                // Task 0234: books lying in the world are read in place; OnRead goes to the placement's scripts.
                lockbooks::WorldBooksHooks worldHooks;
                worldHooks.mRead = [this](std::uint64_t placement) {
                    if (mPapyrus != nullptr)
                        mPapyrus->read(static_cast<openeden::sdk::game::WorldId>(placement));
                };
                worldHooks.mLog = hooks.mLog;
                mWorldBooks = std::make_unique<lockbooks::WorldBooksService>(*mLockBooks, std::move(worldHooks));
                if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                {
                    const openeden::sdk::DeclareResult declared = mWorldBooks->declare(*bindings);
                    if (!declared.ok && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning,
                            "the worldbooks bindings were refused: " + declared.message);
                }
                if (logger != nullptr)
                    logger->log(openeden::sdk::LogLevel::Info, "LOCKBOOKS lock picking and books are on");
            }

            // Task 0266: the text of a localised string of a word of power (WOOP): the plugin of the record is the load
            // order position in the top byte of its form id (a light plugin is not asked: the editor id stands in).
            std::string resolveShoutString(const LString& text, FormId owner)
            {
                if (mBookPaths.empty())
                {
                    std::vector<std::filesystem::path> paths;
                    LoadOrder order;
                    std::vector<LeveledList> unused;
                    if (!mWorldView.inventoryInputs(paths, order, unused))
                        return {};
                    mBookPaths = std::move(paths);
                }
                return resolveBookString(text, owner >> 24, StringTableKind::Strings);
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
                    // Task 0233: a weapon's poison is saved in a chunk of its own; after a load the wielded weapon's
                    // coating goes to the combat again.
                    mCrafting->poisonChunk().setRefusalSink([logger](const std::string& reason) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Error, "the save is refused: " + reason);
                    });
                    mCrafting->poisonChunk().setRestoredSink([this, logger](std::size_t coatings) {
                        if (logger != nullptr)
                            logger->log(openeden::sdk::LogLevel::Info,
                                "CRAFT poisons restored: " + std::to_string(coatings) + " coated weapons");
                        syncPoison();
                    });
                    if (!chunks->registerHandler(mCrafting->poisonChunk()) && logger != nullptr)
                        logger->log(openeden::sdk::LogLevel::Warning, "the poisons chunk was not registered");
                }
                // Task 0229: a landed blow spends one charge of the poison on the weapon.
                mCellSource.combat().setPoisonSpentHook([this](int remaining) {
                    if (mCrafting == nullptr || mPoisonedWeapon == kNullFormId)
                        return;
                    const int left = mCrafting->spendPoison(mPoisonedWeapon);
                    if (mCraftLogger != nullptr)
                        mCraftLogger->log(openeden::sdk::LogLevel::Info,
                            "CRAFT poison spent on a blow: " + std::to_string(left) + " charges left ("
                                + std::to_string(remaining) + " in combat)");
                    if (left <= 0)
                        mPoisonedWeapon = kNullFormId;
                });
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
                syncPoison(); // task 0229: the poison the wielded weapon carries
            }

            // Task 0229: the weapon item the player wields and whether it is a melee weapon (one- or two-handed;
            // a bow, a crossbow, a staff and bare hands are not). `known` is false while its record is not read.
            FormId wieldedWeapon(bool& melee, bool& known)
            {
                melee = false;
                known = true;
                if (mInventory == nullptr || mCrafting == nullptr)
                    return kNullFormId;
                for (const inventory::EquippedItem& worn : mInventory->model().equipped())
                {
                    if (worn.mKind != inventory::EquipKind::Weapon)
                        continue;
                    const WeaponRecord* record = nullptr;
                    if (!mWorldView.lookupWeapon(mCrafting->baseOf(worn.mItem), record))
                        known = false;
                    else
                        melee = record != nullptr && record->mAnimType >= 1 && record->mAnimType <= 6;
                    return worn.mItem;
                }
                return kNullFormId;
            }

            // Task 0234: the player uses a scroll: the combat rules cast it (no magicka). 1 when the cast was accepted
            // (the inventory uses one up), -1 when the combat refused (it has never run: [Game] neutral combat is
            // off; the id is not a scroll; another waits), 0 when there is nothing to cast with.
            int useScroll(FormId item)
            {
                if (mInventory == nullptr)
                    return 0;
                SkyrimCombat& combat = mCellSource.combat();
                if (!mScrollSinkSet)
                {
                    mScrollSinkSet = true;
                    combat.setScrollOutcome([this](FormId scroll, bool cast) { onScrollOutcome(scroll, cast); });
                }
                const bool accepted = combat.castPlayerScroll(item);
                scrollLog("SCROLL " + inventory::formatId(item) + (accepted ? " cast accepted" : " refused"));
                return accepted ? 1 : -1;
            }

            void scrollLog(const std::string& line)
            {
                if (mScrollLogger != nullptr)
                    mScrollLogger->log(openeden::sdk::LogLevel::Info, line);
            }

            // A scroll whose cast never began is given back; one whose cast began stays used up.
            void onScrollOutcome(FormId scroll, bool cast)
            {
                scrollLog(
                    "SCROLL " + inventory::formatId(scroll) + (cast ? " cast began" : " cast gave up, given back"));
                if (!cast && mInventory != nullptr)
                    (void)mInventory->model().add(inventory::kPlayerOwner, scroll, 1);
            }

            // Task 0229: the player used an item from the inventory window: 0 when it is no poison, 1 when it went on
            // the weapon in hand (the inventory consumes it), -1 when refused.
            int usePoisonOnWeapon(FormId item)
            {
                if (mCrafting == nullptr)
                    return 0;
                bool melee = false;
                bool known = true;
                const FormId weapon = wieldedWeapon(melee, known);
                const crafting::PoisonUse use = mCrafting->usePoison(item, weapon, melee && known);
                if (use == crafting::PoisonUse::NotPoison)
                    return 0;
                if (use != crafting::PoisonUse::Applied)
                    return -1;
                syncPoison();
                return 1;
            }

            // Task 0229: gives the combat rules the poison the wielded melee weapon carries (none when it carries
            // none), so the next landed blows apply it.
            void syncPoison()
            {
                if (mCrafting == nullptr)
                    return;
                bool melee = false;
                bool known = true;
                const FormId weapon = wieldedWeapon(melee, known);
                const crafting::WeaponPoison* coat
                    = weapon != kNullFormId && melee ? mCrafting->coating(weapon) : nullptr;
                mPoisonedWeapon = coat != nullptr ? weapon : kNullFormId;
                if (coat == nullptr)
                    mCellSource.combat().setPlayerPoison(SpellRecord(), 0);
                else
                    mCellSource.combat().setPlayerPoison(crafting::poisonSpell(*coat), coat->mCharges);
            }

            // Task 0177 (neutral saves): the save chunk that ties a save to the load order it was made with.
            void registerLoadOrderStamp(openeden::sdk::Host& host, openeden::sdk::game::WorldSourceService& sources,
                openeden::sdk::Logger* logger)
            {
                std::vector<openeden::sdk::game::SessionContentFile> files;
                (void)sources.sessionContent(files);
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
            // Task 0250: what the combat's summon code asks of the world. The actor is a created object of the actor
            // kind (the ordered object queue of SDK 1.3: the engine builds the actor as it builds a placed one and
            // does not save it), hidden again through the enable queue; following the player is the actor table's.
            SummonHooks makeSummonHooks(openeden::sdk::game::WorldSourceService* sources)
            {
                using openeden::sdk::game::WorldId;
                SummonHooks hooks;
                hooks.resolveBase = [this](SummonFormId associated, std::uint16_t level, std::uint32_t pick) {
                    return static_cast<SummonFormId>(mWorldView.summonBase(associated, level, pick));
                };
                hooks.place
                    = [this, sources](SummonFormId base, double x, double y, double z, float heading) -> WorldId {
                    openeden::sdk::game::PlacedObject placed;
                    if (!mWorldView.describeBase(base, placed)
                        || placed.baseKind != openeden::sdk::game::BaseKind::Actor)
                        return openeden::sdk::game::kNoWorldId;
                    placed.position = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
                    placed.rotation = { 0.f, 0.f, heading };
                    placed.scale = 1.f;
                    openeden::sdk::game::WorldObject object;
                    std::string problem;
                    if (!toNeutral(placed, object, problem))
                        return openeden::sdk::game::kNoWorldId;
                    const WorldId cell = mCellSource.playerInteriorCell();
                    const WorldId id = sources->placeActor(mCellSource, cell, object);
                    if (id != openeden::sdk::game::kNoWorldId)
                        mSummonCells[id] = cell;
                    return id;
                };
                hooks.remove = [this, sources](WorldId reference) {
                    WorldId cell = openeden::sdk::game::kNoWorldId;
                    const auto made = mSummonCells.find(reference);
                    if (made != mSummonCells.end())
                    {
                        cell = made->second;
                        mSummonCells.erase(made);
                    }
                    else if (const PlacedRefIndex* refs = mWorldView.placedRefs())
                        if (const PlacedEntry* entry = refs->find(static_cast<FormId>(reference)))
                            cell = entry->mCell;
                    (void)sources->setObjectEnabled(mCellSource, cell, reference, false);
                };
                hooks.follow = [this](WorldId reference, bool follow) {
                    if (follow)
                        (void)mCellSource.actorTable().recruit(reference);
                    else
                        (void)mCellSource.actorTable().dismiss(reference);
                };
                return hooks;
            }

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
                        say(openeden::sdk::LogLevel::Info, "combat test bow speed bonus: " + std::to_string(bowBonus));
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
                    // Task 0242: target actor, target location (runes) and area delivery (off by default).
                    mCellSource.combat().setSpellDelivery(settings->getBool(kPackId, kCombatSpellDeliveryKey, false));
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
                        say(openeden::sdk::LogLevel::Info,
                            "creatures: on (" + std::to_string(creatureProfilesAll().size()) + " creatures)");
                    }
                    // Task 0254: dead stays dead across a load (a fix of the neutral saves: no setting). Hooks only;
                    // the keeper does nothing until an actor of the ledger appears.
                    {
                        DeathHooks deathHooks;
                        deathHooks.handleOf = [this](openeden::sdk::game::WorldId reference) {
                            return mCellSource.actorTable().handleOf(reference);
                        };
                        deathHooks.hold = [this](openeden::sdk::game::WorldId reference, bool on) {
                            mCellSource.actorTable().onConversation(reference, on);
                        };
                        deathHooks.respawnHours = [this] { return mWorldView.hoursToRespawnCell(); };
                        deathHooks.enable = [this, src = &sources](openeden::sdk::game::WorldId reference) {
                            openeden::sdk::game::WorldId cell = openeden::sdk::game::kNoWorldId;
                            if (const PlacedRefIndex* refs = mWorldView.placedRefs())
                                if (const PlacedEntry* entry = refs->find(static_cast<FormId>(reference)))
                                    cell = entry->mCell;
                            (void)src->setObjectEnabled(mCellSource, cell, reference, true);
                        };
                        mDeathKeeper = std::make_unique<DeathKeeper>(mDeathLedger, std::move(deathHooks));
                        mCellSource.setDeaths(mDeathKeeper.get());
                        if (settings->getBool(kPackId, kCombatTraceKey, false))
                            mDeathKeeper->setTrace(
                                [say](const std::string& line) { say(openeden::sdk::LogLevel::Info, line); });
                        mCellSource.combat().setCorpseRaisedSink(
                            [this](openeden::sdk::game::WorldId corpse) { mDeathKeeper->corpseRaised(corpse); });
                        mDeathsChunk = std::make_unique<DeathsChunk>([this] { return mDeathLedger.data(); },
                            [this](const std::map<FormId, DeathEntry>& saved) {
                                if (!mDeathLedger.restore(saved))
                                    return false;
                                mDeathKeeper->ledgerRestored();
                                return true;
                            });
                        if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                            if (!chunks->registerHandler(*mDeathsChunk))
                                say(openeden::sdk::LogLevel::Warning, "the deaths chunk was not registered");
                    }
                    // Task 0250: Summon Creature and Reanimate (off by default; needs `combat spell delivery`).
                    if (settings->getBool(kPackId, kSummonsKey, false))
                    {
                        mCellSource.combat().setSummons(true);
                        mCellSource.combat().setSummonHooks(makeSummonHooks(&sources));
                        mCellSource.combat().setMaxSummons(static_cast<std::size_t>(std::max(
                            1, settings->getInt(kPackId, kSummonsMaxKey, static_cast<int>(kDefaultMaxSummons)))));
                        mCellSource.combat().setTestKill(settings->getString(kPackId, kSummonsTestKillKey, ""));
                        mCellSource.combat().setTestPlayerMagicka(
                            settings->getFloat(kPackId, kSummonsTestMagickaKey, 0.f));
                        say(openeden::sdk::LogLevel::Info, "summons: on");
                    }
                    // Task 0278: a lethal sword blow of the player may play the game's paired kill move (off by
                    // default; needs the engine's neutral combat, actors and player body).
                    if (settings->getBool(kPackId, killmove::kKillMovesKey, false))
                    {
                        mCellSource.killMoves().setEnabled(true);
                        mCellSource.killMoves().setChance(
                            settings->getFloat(kPackId, killmove::kKillMovesChanceKey, killmove::kDefaultChance));
                        mCellSource.killMoves().setSeed(0x4B494C4C4D4F5645ull);
                        mCellSource.killMoves().setLog(
                            [say](const std::string& line) { say(openeden::sdk::LogLevel::Info, line); });
                        say(openeden::sdk::LogLevel::Info, "kill moves: on");
                    }
                    // Task 0251: the player may ride a horse (off by default; needs creatures and the engine's
                    // neutral actors; the horse the player owns is the test setting for now).
                    if (settings->getBool(kPackId, kRidingKey, false))
                    {
                        mCellSource.riding().setEnabled(true);
                        mCellSource.riding().setTestOwned(settings->getString(kPackId, kHorseTestOwnedKey, ""));
                        // Task 0278: a horse bought through the stablemaster's own dialogue and script
                        // (TIF__00068CF1 puts it in PlayerHorseFaction at rank 1) is the player's.
                        mCellSource.riding().setOwnedLookup([this](openeden::sdk::game::WorldId reference) {
                            const std::optional<float> rank = mFollowerState.factionRank(
                                static_cast<FormId>(reference), static_cast<FormId>(kPlayerHorseFactionId));
                            return rank.has_value() && *rank >= 1.f;
                        });
                        // Task 0267: the game's paired mount and dismount clips.
                        mCellSource.riding().setPairedAnimations(
                            settings->getBool(kPackId, kPairedAnimationsKey, false));
                        mCellSource.riding().setLog(
                            [say](const std::string& line) { say(openeden::sdk::LogLevel::Info, line); });
                        say(openeden::sdk::LogLevel::Info, "riding: on");
                        // Task 0255: the camera package, and the chunk that puts a rider on his feet after a load.
                        mRidingBindings = std::make_unique<RidingBindings>(mCellSource.riding());
                        if (openeden::sdk::BindingsService* const bindings = host.getBindingsService())
                        {
                            const openeden::sdk::DeclareResult declared = mRidingBindings->declareTo(*bindings);
                            if (!declared.ok)
                                say(openeden::sdk::LogLevel::Warning,
                                    "the riding bindings were refused: " + declared.message);
                        }
                        mRidingChunk
                            = std::make_unique<RidingChunk>([this] { return mCellSource.riding().savedState(); },
                                [this](const RidingState& state) { mCellSource.riding().restoreState(state); });
                        if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                            if (!chunks->registerHandler(*mRidingChunk))
                                say(openeden::sdk::LogLevel::Warning, "the riding chunk was not registered");
                    }
                    // Task 0253: dragons (off by default; needs creatures and the combat AI to fight).
                    if (settings->getBool(kPackId, kCreaturesKey, false)
                        && settings->getBool(kPackId, std::string(dragons::kDragonsKey), false))
                    {
                        mWorldView.setDragons(true);
                        dragons::Settings dragonSettings;
                        dragonSettings.mOn = true;
                        dragonSettings.mNavmeshWalk
                            = settings->getBool(kPackId, std::string(dragons::kNavmeshKey), false);
                        dragonSettings.mFlight.mEnabled
                            = settings->getBool(kPackId, std::string(dragons::kFlightKey), true);
                        const float ground = settings->getFloat(kPackId, std::string(dragons::kTestGroundKey), 0.f);
                        const float air = settings->getFloat(kPackId, std::string(dragons::kTestAirKey), 0.f);
                        if (std::isfinite(ground) && ground > 0.f && ground < 3600.f)
                            dragonSettings.mFlight.mGroundSeconds = ground;
                        if (std::isfinite(air) && air > 0.f && air < 3600.f)
                            dragonSettings.mFlight.mAirSeconds = air;
                        const float testHealth = settings->getFloat(kPackId, std::string(dragons::kTestHealthKey), 0.f);
                        if (std::isfinite(testHealth) && testHealth > 0.f && testHealth < 1.0e7f)
                            dragonSettings.mTestHealth = testHealth;
                        const float damageScale
                            = settings->getFloat(kPackId, std::string(dragons::kTestDamageKey), 1.f);
                        if (std::isfinite(damageScale) && damageScale >= 0.f && damageScale <= 100.f)
                            dragonSettings.mTestDamageScale = damageScale;
                        const float killAt = settings->getFloat(kPackId, std::string(dragons::kTestKillKey), 0.f);
                        if (std::isfinite(killAt) && killAt > 0.f && killAt < 1.0e6f)
                            dragonSettings.mTestKillAt = killAt;
                        mCellSource.combat().setDragons(dragonSettings);
                        mCellSource.setTestDragon(settings->getString(kPackId, std::string(dragons::kTestPlaceKey), ""),
                            settings->getString(kPackId, std::string(dragons::kTestReplaceKey), ""));
                        say(openeden::sdk::LogLevel::Info,
                            std::string("dragons: on, flight ") + (dragonSettings.mFlight.mEnabled ? "on" : "off")
                                + ", " + std::to_string(dragonSettings.mFlight.mGroundSeconds) + " s on the ground, "
                                + std::to_string(dragonSettings.mFlight.mAirSeconds) + " s in the air");
                        // Task 0280: a dragon in the air at a save is back in the air after a load.
                        mDragonsChunk = std::make_unique<dragons::DragonsChunk>(
                            [this] { return mCellSource.combat().savedDragons(); },
                            [this](std::vector<dragons::DragonSave> saves) {
                                mCellSource.combat().restoreDragons(std::move(saves));
                            });
                        if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                            if (!chunks->registerHandler(*mDragonsChunk))
                                say(openeden::sdk::LogLevel::Warning, "the dragons chunk was not registered");
                    }
                    // Task 0222: the player has a Skyrim body (off by default; the core also needs [Game] neutral
                    // player body).
                    if (settings->getBool(kPackId, kPlayerBodyKey, false))
                    {
                        mWorldView.setPlayerBody(true);
                        say(openeden::sdk::LogLevel::Info, "player body: on");
                        // Task 0245: the player's first-person arms (needs the player body).
                        if (settings->getBool(kPackId, kFirstPersonBodyKey, false))
                        {
                            mWorldView.setFirstPersonBody(true);
                            say(openeden::sdk::LogLevel::Info, "first person body: on");
                        }
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
                        // Task 0271: quest aliases' packages and the ForceGreet conversation (needs the brain)
                        if (settings->getBool(kPackId, kQuestAliasPackagesKey, false))
                        {
                            mWorldView.setAliasPackages(true);
                            // Task 0274: a package that ends (a patrol at its last marker) runs its OnEnd fragment
                            // (the cart horses' last package sets MQ101 stage 41)
                            mCellSource.actorTable().setPackageEndHook(
                                [this](openeden::sdk::game::WorldId reference, const PackageRecord& package) {
                                    if (mPapyrus == nullptr)
                                        return;
                                    (void)mPapyrus->host().runPackageEnd(package.mCommon.mId,
                                        static_cast<FormId>(reference),
                                        mWorldView.aliasPackageQuest(package.mCommon.mId), package.mEndScript,
                                        package.mEndFunction);
                                });
                            if (const char* trace = std::getenv("OPENEDEN_PACKAGE_TRACE"))
                                mWorldView.setPackageTrace(static_cast<FormId>(std::strtoul(trace, nullptr, 10)),
                                    [say](const std::string& line) { say(openeden::sdk::LogLevel::Info, line); });
                            // the brain's ask joins the one forced-greeting mechanism (task 0264's ForceGreet)
                            mCellSource.actorTable().setForceGreetHook(
                                [this, say](openeden::sdk::game::WorldId reference, FormId base, FormId topic) {
                                    if (mPapyrus != nullptr && mPapyrus->forceGreetOn()
                                        && mPapyrus->forceGreet().requestFromBrain(
                                            static_cast<FormId>(reference), base, topic))
                                        say(openeden::sdk::LogLevel::Info,
                                            "FORCEGREET the actor placed as " + std::to_string(reference) + " (base "
                                                + std::to_string(base) + ") wants to say topic "
                                                + std::to_string(topic));
                                });
                            say(openeden::sdk::LogLevel::Info,
                                "quest alias packages: quests' ALPC packages join the actors' lists, ForceGreet "
                                "packages greet the player");
                        }
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
                        // Task 0239: what the follower scripts changed (factions, globals, ranks), read by the dialogue
                        mFollowerStateChunk
                            = std::make_unique<FollowerStateChunk>([this] { return mFollowerState.data(); },
                                [this](const FollowerStateData& data) { return mFollowerState.restore(data); });
                        mWorldView.setFollowerOverlay(&mFollowerState);
                        if (openeden::sdk::SaveChunkService* const chunks = host.getSaveChunkService())
                        {
                            if (!chunks->registerHandler(*mFollowerChunk))
                                say(openeden::sdk::LogLevel::Warning, "the followers chunk was not registered");
                            if (!chunks->registerHandler(*mFollowerStateChunk))
                                say(openeden::sdk::LogLevel::Warning, "the follower state chunk was not registered");
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
            std::map<openeden::sdk::game::WorldId, openeden::sdk::game::WorldId>
                mSummonCells; ///< task 0250: made actor -> hint
            LoadOrderStamp mLoadOrderStamp; // task 0177; registered with the host's save chunks, forgotten before stop
            std::unique_ptr<inventory::InventoryService> mInventory; // task 0182; refers to the world view
            std::unique_ptr<services::ServicesService> mServices; // task 0219; the lambdas refer to the pack
            std::unique_ptr<services::ServicesChunk> mServicesChunk; // task 0219; refers to mServices' ledger and book
            std::unique_ptr<barter::BarterService> mBarter; // task 0189; refers to the inventory and the world view
            std::unique_ptr<crafting::CraftingService> mCrafting; // task 0199; refers to the inventory
            openeden::sdk::Logger* mCraftLogger = nullptr; // task 0199
            FormId mPoisonedWeapon = kNullFormId; // task 0229: the weapon item whose poison the combat holds
            WeaponRecord mMadeWeapon; // task 0199: the combat record of the wielded made weapon (combat points at it)
            std::function<void(std::string_view, float)> mCraftingSkillSink; // task 0199: a skills system may set it
            std::unique_ptr<lockbooks::LockBooksService> mLockBooks; // task 0203; refers to the inventory
            std::unique_ptr<lockbooks::WorldBooksService> mWorldBooks; // task 0234; refers to the book service
            bool mScrollSinkSet = false; // task 0234
            openeden::sdk::Logger* mScrollLogger = nullptr; // task 0234 (the host's logger outlives the pack)
            std::vector<std::filesystem::path> mBookPaths;
            std::string mBookLanguage = "english";
            std::map<std::pair<std::uint32_t, int>, std::unique_ptr<StringTable>> mBookTables;
            std::size_t mSyncedSpells = 0;
            FormId mSyncedLastSpell = kNullFormId;
            bool mPlayerSpellFromSetting = false;
            std::unique_ptr<journal::JournalService> mJournal; // task 0185; refers to the world view
            std::unique_ptr<crime::CrimeService> mCrime; // task 0204; refers to the cell source and the inventory
            std::unique_ptr<hudstyle::StyleHud> mStyleHud; // task 0244; refers to the cell source's HUD and combat
            std::unique_ptr<hudstyle::StyleBindings> mStyleBindings;
            std::unique_ptr<sneak::SneakSystem> mSneak; // task 0227; refers to the cell source, inventory, progression
            std::unique_ptr<sneak::SneakBindings> mSneakBindings;
            std::unique_ptr<barks::BarkSystem> mBarks; // task 0231; refers to the cell source and the world view
            std::function<bool(std::string_view, FormId, std::uint32_t, bool, SceneLine&)>
                mBarkLine; // from papyrusAccess
            std::function<void(bool, std::uint8_t)> mBarkStance; // task 0235, from papyrusAccess
            bool mBarkSubtitles = true;
            bool mBarkConfigured = false;
            float mBarkStartAt = 0.f;
            std::unique_ptr<PerkEngine> mPickpocketPerks; // task 0275: the perk bonus to a pickpocket chance
            std::unique_ptr<pickup::PickupService> mPickup; // task 0208; refers to the inventory, crime, progression
            std::unique_ptr<progression::ProgressionService> mProgression; // task 0200; refers to the world view
            bool mProgressionLinked = false;
            float mProgressionBoost = 1.f; // task 0206: the test multiplier of `progression test amount`
            std::uint64_t mProgressionPushed = 0;
            std::unique_ptr<map::MapService> mMap; // task 0201; refers to the world view
            std::unique_ptr<shouts::ShoutService> mShouts; // task 0205; refers to the world view
            std::unique_ptr<werewolf::WerewolfService> mWerewolf; // task 0259; its lambdas refer to the pack
            werewolf::BeastClock mBeastClock; // task 0259
            werewolf::BeastEnding mBeastEnding; // task 0279
            std::vector<FormId> mBeastHowlWords; // task 0279: the Howl's words the form gave
            FormId mBeastPrevShout = kNullFormId; // task 0279: the shout he held before the Howl
            std::vector<FormId> mBeastAbilities; // task 0259: AbWerewolf and the level ability while the form lasts
            std::set<std::uint64_t> mBeastFed; // task 0259: the corpses fed on in this form
            std::unique_ptr<openeden::sdk::SaveChunkHandler> mBeastChunk; // task 0265
            bool mBeastPendingWarned = false; // task 0265
            std::optional<werewolf::BeastSaved>
                mBeastPending; // task 0265: a loaded form waiting for the race's numbers
            std::function<void(openeden::sdk::LogLevel, const std::string&)> mWerewolfLog;
            float mWerewolfTestSeconds = 0.f;
            bool mWerewolfGrant = false;
            std::unique_ptr<map::MapChunk> mMapChunk; // task 0201; refers to mMap's discovery
            std::unique_ptr<travel::TravelService>
                mTravel; // task 0218; refers to mMap and the combat; declared after mMap
            bool mGameClockOn = false; // task 0276
            static constexpr FormId kGameDaysPassedGlobal = 0x39; // GLOB GameDaysPassed (Skyrim.esm)
            std::unique_ptr<rest::RestService> mRest; // task 0210; the lambdas refer to the pack, the combat, the view
            std::unique_ptr<RidingBindings> mRidingBindings; // task 0255; refers to the cell source's riding rules
            std::unique_ptr<RidingChunk> mRidingChunk; // task 0255
            std::unique_ptr<dragons::DragonsChunk> mDragonsChunk; // task 0280
            std::unique_ptr<rest::RestChunk> mRestChunk; // task 0210; refers to mRest's Rested state
            std::unique_ptr<harvest::HarvestService> mHarvest; // task 0221; refers to the view and the inventory
            std::unique_ptr<stones::StonesService> mStones; // task 0237; refers to the Papyrus driver
            std::unique_ptr<diseases::DiseaseService> mDiseases; // task 0240; refers to the combat and the view
            FormId mDiseaseTestInfect = kNullFormId; // task 0240: a test aid
            float mVampireTestCureHours = 0.f; // task 0279
            bool mVampireCureScheduled = false; // task 0279
            std::unique_ptr<VampireQuestPort> mVampirePort; // task 0260; outlives mVampirism
            std::unique_ptr<vampirism::VampirismService>
                mVampirism; // task 0260; refers to the host, the diseases, the combat
            bool mVampireSun = false; // task 0260: the sun damage abilities count now
            bool mVampireSunValue = false;
            std::int64_t mVampireSunMinute = -1;
            std::uint64_t mVampireSunPlace = 0;
            openeden::sdk::Logger* mVampireLog = nullptr;
            std::uint64_t mPerksSynced = 0; // task 0241: the host's perk revision the combat last read
            std::uint64_t mSkillModsSynced = 0; // task 0248
            FormId mStonesTestCast = kNullFormId; // task 0248: a proof aid, a hex form id cast once as a blessing
            std::uint64_t mAbilitiesSynced = 0; // task 0241: the combat's ability revision the inventory last read
            std::unique_ptr<mining::MiningService> mMining; // task 0224; refers to the view and the inventory
            std::unique_ptr<menus::MenusService> mMenus; // task 0207; refers to the world view
            std::unique_ptr<presentation::PresentationService> mPresentation; // task 0215; the handlers it declared
            std::unique_ptr<FollowerChunk> mFollowerChunk; // task 0196; refers to the cell source
            DeathLedger mDeathLedger; // task 0254: who died, for the saves
            std::unique_ptr<DeathKeeper> mDeathKeeper; // task 0254; refers to the ledger and the cell source
            std::unique_ptr<DeathsChunk> mDeathsChunk; // task 0254; refers to the ledger and the keeper
            FollowerState mFollowerState; // task 0239: what the follower scripts changed
            houses::Ledger mHouses; // task 0261: who owns a cell now (a script's SetFactionOwner)
            std::unique_ptr<houses::HousesChunk> mHousesChunk;
            bool mHousesOn = false;
            /// task 0261: the owner of a placement as the theft, bed and trespass rules see it
            [[nodiscard]] FormId effectiveOwnerOfPlacement(FormId placement) const
            {
                FormId own = kNullFormId;
                if (const auto owner = mWorldView.ownerOfPlacement(placement))
                    if (owner->owner != openeden::sdk::game::kNoWorldId)
                        own = static_cast<FormId>(owner->owner);
                if (!mHousesOn)
                    return own;
                return mHouses.effectiveOwner(own, mWorldView.cellOfPlacement(placement));
            }
            std::unique_ptr<FollowerStateChunk> mFollowerStateChunk; // task 0239; refers to mFollowerState
            std::uint32_t mFollowerAliasSeen = 0; // task 0196: the last fill of the DialogueFollower alias
            openeden::sdk::Logger* mFollowerLogger = nullptr; // task 0239: where the recruit and dismiss lines go
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
