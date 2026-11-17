// Task 0185 (milestone M-S6): the quest journal. `QuestState::revision`, the pure `journal::build` and the
// `JournalService` with its "skyrimse.journal" bindings over a stub bindings service, on hand-made quests (no game
// data, no plugin files). Written before the code and run red against a stub first.
#include <game_skyrimse/journal.hpp>

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::journal;
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kQ1 = 0x01000400;
    constexpr FormId kQ2 = 0x01000401;
    constexpr FormId kQ3 = 0x01000402;
    constexpr FormId kQ4 = 0x01000403;

    LString plain(const std::string& text)
    {
        LString s;
        s.mPresent = true;
        s.mText = text;
        return s;
    }

    LString localized(std::uint32_t id)
    {
        LString s;
        s.mPresent = true;
        s.mLocalized = true;
        s.mId = id;
        return s;
    }

    struct StageSpec
    {
        std::uint16_t mIndex;
        std::string mText; ///< empty: an entry with no text
        std::uint8_t mEntryFlags = 0;
        std::uint8_t mStageFlags = 0;
    };

    QuestRecord makeQuest(FormId id, const std::string& name, std::uint16_t flags, const std::vector<StageSpec>& stages,
        std::vector<std::pair<std::uint16_t, std::string>> objectives = {})
    {
        QuestRecord q;
        q.mCommon.mId = id;
        q.mCommon.mEditorId = "Q" + std::to_string(id & 0xFFF);
        q.mName = name.empty() ? LString() : plain(name);
        q.mFlags = flags;
        for (const StageSpec& s : stages)
        {
            QuestStage stage;
            stage.mIndex = s.mIndex;
            stage.mFlags = s.mStageFlags;
            QuestLogEntry e;
            e.mFlags = s.mEntryFlags;
            if (!s.mText.empty())
                e.mText = plain(s.mText);
            stage.mEntries.push_back(e);
            q.mStages.push_back(stage);
        }
        for (const auto& [index, text] : objectives)
        {
            QuestObjective o;
            o.mIndex = index;
            o.mText = plain(text);
            q.mObjectives.push_back(o);
        }
        return q;
    }

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    QuestIndex indexOf(std::vector<QuestRecord> quests)
    {
        QuestRecords records;
        records.mQuests = std::move(quests);
        QuestIndex index;
        index.add(std::move(records), FormIdResolver(Content::Header(), "test.esp", order()));
        return index;
    }

    constexpr std::uint16_t kEnabled = QuestFlags::StartGameEnabled;

    struct Env
    {
        QuestIndex mIndex;
        QuestState mState;
        std::map<std::pair<FormId, std::uint16_t>, ObjectiveFlags> mObjectives;
        Inputs inputs()
        {
            Inputs in;
            in.mQuests = &mIndex;
            in.mState = &mState;
            in.mObjective = [this](FormId q, std::uint16_t i) {
                const auto it = mObjectives.find({ q, i });
                return it == mObjectives.end() ? ObjectiveFlags() : it->second;
            };
            return in;
        }
    };

    Env envOf(std::vector<QuestRecord> quests)
    {
        Env e;
        e.mIndex = indexOf(std::move(quests));
        e.mState = QuestState::initial(e.mIndex);
        return e;
    }

    // ---- QuestState::revision ----

    TEST(JournalQuestRevision, startsAtZeroAndEveryMutationOfAKnownQuestIncrements)
    {
        Env e = envOf({ makeQuest(kQ1, "One", kEnabled, { { 10, "a" } }) });
        EXPECT_EQ(e.mState.revision(), 0u);
        std::uint64_t last = e.mState.revision();
        const auto expectBump = [&](const char* what) {
            EXPECT_GT(e.mState.revision(), last) << what;
            last = e.mState.revision();
        };
        e.mState.start(kQ1);
        expectBump("start");
        e.mState.stop(kQ1);
        expectBump("stop");
        e.mState.setStage(e.mIndex, kQ1, 10);
        expectBump("setStage");
        e.mState.setCurrentStage(kQ1, 20);
        expectBump("setCurrentStage");
        e.mState.setAliasFill(kQ1, 1, AliasFill());
        expectBump("setAliasFill");
        e.mState.restoreSnapshot(e.mState.snapshot());
        expectBump("restoreSnapshot");
    }

    TEST(JournalQuestRevision, readsAndMutationsOfUnknownQuestsDoNotChangeIt)
    {
        Env e = envOf({ makeQuest(kQ1, "One", kEnabled, { { 10, "a" } }) });
        const std::uint64_t before = e.mState.revision();
        (void)e.mState.running(kQ1);
        (void)e.mState.stage(kQ1);
        (void)e.mState.snapshot();
        e.mState.start(kQ4);
        e.mState.stop(kQ4);
        e.mState.setStage(e.mIndex, kQ4, 5);
        e.mState.setCurrentStage(kQ4, 5);
        EXPECT_EQ(e.mState.revision(), before);
    }

    // ---- build ----

    TEST(JournalBuild, aRunningNamedQuestWithALogIsListedWithItsCurrentEntryAndHistory)
    {
        Env e = envOf(
            { makeQuest(kQ1, "Unbound", kEnabled, { { 10, "Escape." }, { 20, "Find the gate." }, { 30, "x" } }) });
        e.mState.setStage(e.mIndex, kQ1, 10);
        e.mState.setStage(e.mIndex, kQ1, 20);
        Snapshot s;
        build(e.inputs(), s);
        ASSERT_EQ(s.mRunning.size(), 1u);
        EXPECT_TRUE(s.mCompleted.empty());
        const QuestView& q = s.mRunning[0];
        EXPECT_EQ(q.mQuest, kQ1);
        EXPECT_EQ(q.mName, "Unbound");
        EXPECT_EQ(q.mStatus, Status::Running);
        EXPECT_EQ(q.mEntry, "Find the gate.");
        EXPECT_EQ(q.mLog, (std::vector<std::string>{ "Escape.", "Find the gate." }));
        EXPECT_EQ(s.find(kQ1), &q);
        EXPECT_EQ(s.find(kQ2), nullptr);
    }

    TEST(JournalBuild, theStartUpStageOfAStartEnabledQuestShowsFromTheStart)
    {
        Env e = envOf({ makeQuest(kQ1, "Unbound", kEnabled, { { 10, "Wake up.", 0, StageFlags::StartUp } }) });
        Snapshot s;
        build(e.inputs(), s);
        ASSERT_EQ(s.mRunning.size(), 1u);
        EXPECT_EQ(s.mRunning[0].mEntry, "Wake up.");
    }

    TEST(JournalBuild, aQuestWithNoNameNoLogOrNotRunningIsLeftOut)
    {
        Env e = envOf({ makeQuest(kQ1, "", kEnabled, { { 10, "text" } }), // no name
            makeQuest(kQ2, "Quiet", kEnabled, { { 10, "" } }), // no entry text and no objective
            makeQuest(kQ3, "Idle", 0, { { 10, "text" } }) }); // not running
        e.mState.setCurrentStage(kQ1, 10);
        e.mState.setStage(e.mIndex, kQ2, 10);
        e.mState.stop(kQ3);
        e.mState.setCurrentStage(kQ3, 10);
        Snapshot s;
        build(e.inputs(), s);
        EXPECT_TRUE(s.mRunning.empty());
        EXPECT_TRUE(s.mCompleted.empty());
    }

    TEST(JournalBuild, aDisplayedObjectiveAloneListsTheQuestAndTheFlagsComeFromTheHook)
    {
        Env e = envOf({ makeQuest(
            kQ1, "Obj", kEnabled, { { 10, "" } }, { { 0, "Go north" }, { 1, "Hidden" }, { 2, "Done one" } }) });
        e.mObjectives[{ kQ1, 0 }] = { true, false };
        e.mObjectives[{ kQ1, 2 }] = { true, true };
        Snapshot s;
        build(e.inputs(), s);
        ASSERT_EQ(s.mRunning.size(), 1u);
        const QuestView& q = s.mRunning[0];
        ASSERT_EQ(q.mObjectives.size(), 2u);
        EXPECT_EQ(q.mObjectives[0].mText, "Go north");
        EXPECT_FALSE(q.mObjectives[0].mDone);
        EXPECT_EQ(q.mObjectives[1].mText, "Done one");
        EXPECT_TRUE(q.mObjectives[1].mDone);
        EXPECT_TRUE(q.mEntry.empty());
    }

    TEST(JournalBuild, noObjectiveHookMeansNoObjectives)
    {
        Env e = envOf({ makeQuest(kQ1, "Obj", kEnabled, { { 10, "t", 0, StageFlags::StartUp } }, { { 0, "Go" } }) });
        Inputs in = e.inputs();
        in.mObjective = nullptr;
        Snapshot s;
        build(in, s);
        ASSERT_EQ(s.mRunning.size(), 1u);
        EXPECT_TRUE(s.mRunning[0].mObjectives.empty());
    }

    TEST(JournalBuild, completedAndFailedQuestsAreListedSeparately)
    {
        Env e = envOf({ makeQuest(kQ1, "Done", kEnabled, { { 10, "fin", LogEntryFlags::CompleteQuest } }),
            makeQuest(kQ2, "Lost", kEnabled, { { 10, "fail", LogEntryFlags::FailQuest } }),
            makeQuest(kQ3, "Going", kEnabled, { { 10, "on" } }) });
        e.mState.setStage(e.mIndex, kQ1, 10);
        e.mState.setStage(e.mIndex, kQ2, 10);
        e.mState.setStage(e.mIndex, kQ3, 10);
        Snapshot s;
        build(e.inputs(), s);
        ASSERT_EQ(s.mRunning.size(), 1u);
        EXPECT_EQ(s.mRunning[0].mName, "Going");
        ASSERT_EQ(s.mCompleted.size(), 2u);
        EXPECT_EQ(s.mCompleted[0].mName, "Done");
        EXPECT_EQ(s.mCompleted[0].mStatus, Status::Completed);
        EXPECT_EQ(s.mCompleted[1].mName, "Lost");
        EXPECT_EQ(s.mCompleted[1].mStatus, Status::Failed);
    }

    TEST(JournalBuild, aQuestWithTheCompletedFlagIsCompletedEvenWhenNotRunning)
    {
        Env e = envOf({ makeQuest(kQ1, "Old", QuestFlags::Completed, { { 10, "was" } }) });
        e.mState.setCurrentStage(kQ1, 10);
        e.mState.stop(kQ1);
        Snapshot s;
        build(e.inputs(), s);
        EXPECT_TRUE(s.mRunning.empty());
        ASSERT_EQ(s.mCompleted.size(), 1u);
        EXPECT_EQ(s.mCompleted[0].mStatus, Status::Completed);
    }

    TEST(JournalBuild, listsAreSortedByNameIgnoringCaseThenFormId)
    {
        Env e = envOf({ makeQuest(kQ3, "beta", kEnabled, { { 1, "t", 0, StageFlags::StartUp } }),
            makeQuest(kQ2, "Alpha", kEnabled, { { 1, "t", 0, StageFlags::StartUp } }),
            makeQuest(kQ1, "alpha", kEnabled, { { 1, "t", 0, StageFlags::StartUp } }) });
        Snapshot s;
        build(e.inputs(), s);
        ASSERT_EQ(s.mRunning.size(), 3u);
        EXPECT_EQ(s.mRunning[0].mQuest, kQ1);
        EXPECT_EQ(s.mRunning[1].mQuest, kQ2);
        EXPECT_EQ(s.mRunning[2].mQuest, kQ3);
    }

    TEST(JournalBuild, theEntryFallsBackToTheLastLogTextWhenTheCurrentStageHasNone)
    {
        Env e = envOf({ makeQuest(kQ1, "Fb", kEnabled, { { 10, "first" }, { 20, "" } }) });
        e.mState.setStage(e.mIndex, kQ1, 10);
        e.mState.setStage(e.mIndex, kQ1, 20);
        Snapshot s;
        build(e.inputs(), s);
        ASSERT_EQ(s.mRunning.size(), 1u);
        EXPECT_EQ(s.mRunning[0].mEntry, "first");
        EXPECT_EQ(s.mRunning[0].mLog, (std::vector<std::string>{ "first" }));
    }

    TEST(JournalBuild, localisedTextsAreResolvedByPluginAndKind)
    {
        QuestRecord q = makeQuest(kQ1, "", kEnabled, { { 10, "", 0, StageFlags::StartUp } }, { { 0, "x" } });
        q.mName = localized(7);
        q.mStages[0].mEntries[0].mText = localized(8);
        q.mObjectives[0].mText = localized(9);
        Env e = envOf({ q });
        e.mObjectives[{ kQ1, 0 }] = { true, false };
        std::vector<std::pair<std::uint32_t, int>> asked;
        Inputs in = e.inputs();
        in.mText = [&](const LString& t, std::uint32_t plugin, StringTableKind kind) -> std::string {
            asked.emplace_back(t.mId, static_cast<int>(kind));
            EXPECT_EQ(plugin, 0u);
            return t.mId == 7 ? "Quest Name" : t.mId == 8 ? "Log Text" : "Objective Text";
        };
        Snapshot s;
        build(in, s);
        ASSERT_EQ(s.mRunning.size(), 1u);
        EXPECT_EQ(s.mRunning[0].mName, "Quest Name");
        EXPECT_EQ(s.mRunning[0].mEntry, "Log Text");
        ASSERT_EQ(s.mRunning[0].mObjectives.size(), 1u);
        EXPECT_EQ(s.mRunning[0].mObjectives[0].mText, "Objective Text");
        bool nameAsStrings = false, logAsDl = false, objAsDl = false;
        for (const auto& [id, kind] : asked)
        {
            nameAsStrings |= id == 7 && kind == static_cast<int>(StringTableKind::Strings);
            logAsDl |= id == 8 && kind == static_cast<int>(StringTableKind::DlStrings);
            objAsDl |= id == 9 && kind == static_cast<int>(StringTableKind::DlStrings);
        }
        EXPECT_TRUE(nameAsStrings);
        EXPECT_TRUE(logAsDl);
        EXPECT_TRUE(objAsDl);
    }

    TEST(JournalBuild, aLocalisedTextIsLookedUpInTheOtherTablesWhenTheExpectedOneLacksIt)
    {
        QuestRecord q = makeQuest(kQ1, "Named", kEnabled, { { 10, "", 0, StageFlags::StartUp } });
        q.mStages[0].mEntries[0].mText = localized(8);
        Env e = envOf({ q });
        Inputs in = e.inputs();
        in.mText = [](const LString& t, std::uint32_t, StringTableKind kind) -> std::string {
            return t.mId == 8 && kind == StringTableKind::IlStrings ? "From the other table" : std::string();
        };
        Snapshot s;
        build(in, s);
        ASSERT_EQ(s.mRunning.size(), 1u);
        EXPECT_EQ(s.mRunning[0].mEntry, "From the other table");
    }

    TEST(JournalBuild, aLocalisedTextWithNoResolverIsEmptyAndTheQuestIsLeftOut)
    {
        QuestRecord q = makeQuest(kQ1, "", kEnabled, { { 10, "t", 0, StageFlags::StartUp } });
        q.mName = localized(7);
        Env e = envOf({ q });
        Snapshot s;
        build(e.inputs(), s);
        EXPECT_TRUE(s.mRunning.empty());
    }

    TEST(JournalBuild, capsLimitTheListTheLogTheObjectivesAndTheText)
    {
        std::vector<QuestRecord> quests;
        for (std::uint32_t i = 0; i < kMaxQuestsPerList + 10; ++i)
        {
            char name[16];
            std::snprintf(name, sizeof(name), "q%04u", i);
            quests.push_back(makeQuest(0x01001000 + i, name, kEnabled, { { 1, "t", 0, StageFlags::StartUp } }));
        }
        Env e = envOf(std::move(quests));
        Snapshot s;
        build(e.inputs(), s);
        EXPECT_EQ(s.mRunning.size(), kMaxQuestsPerList);
        EXPECT_EQ(s.mDropped, 10u);

        std::vector<StageSpec> stages;
        std::vector<std::pair<std::uint16_t, std::string>> objectives;
        for (std::uint16_t i = 1; i <= kMaxLogEntries + 5; ++i)
            stages.push_back({ i, std::string(kMaxTextBytes + 50, 'x') });
        for (std::uint16_t i = 0; i < kMaxObjectives + 5; ++i)
            objectives.emplace_back(i, "o");
        Env big = envOf({ makeQuest(kQ1, "Big", kEnabled, stages, objectives) });
        for (std::uint16_t i = 1; i <= kMaxLogEntries + 5; ++i)
            big.mState.setStage(big.mIndex, kQ1, i);
        for (std::uint16_t i = 0; i < kMaxObjectives + 5; ++i)
            big.mObjectives[{ kQ1, i }] = { true, false };
        Snapshot b;
        build(big.inputs(), b);
        ASSERT_EQ(b.mRunning.size(), 1u);
        EXPECT_EQ(b.mRunning[0].mLog.size(), kMaxLogEntries);
        EXPECT_EQ(b.mRunning[0].mObjectives.size(), kMaxObjectives);
        EXPECT_EQ(b.mRunning[0].mEntry.size(), kMaxTextBytes);
        EXPECT_EQ(b.mRunning[0].mLog.back().size(), kMaxTextBytes);
    }

    TEST(JournalBuild, theResultIsTheSameEveryTime)
    {
        Env e = envOf({ makeQuest(kQ1, "A", kEnabled, { { 1, "x", 0, StageFlags::StartUp } }),
            makeQuest(kQ2, "B", kEnabled, { { 1, "y", LogEntryFlags::CompleteQuest } }) });
        e.mState.setStage(e.mIndex, kQ2, 1);
        Snapshot a, b;
        build(e.inputs(), a);
        build(e.inputs(), b);
        ASSERT_EQ(a.mRunning.size(), b.mRunning.size());
        ASSERT_EQ(a.mCompleted.size(), b.mCompleted.size());
        EXPECT_EQ(a.mRunning[0].mName, b.mRunning[0].mName);
        EXPECT_EQ(a.mCompleted[0].mEntry, b.mCompleted[0].mEntry);
    }

    TEST(JournalBuild, missingInputsGiveAnEmptySnapshot)
    {
        Snapshot s;
        build(Inputs(), s);
        EXPECT_TRUE(s.mRunning.empty());
        EXPECT_TRUE(s.mCompleted.empty());
    }

    TEST(JournalTestQuests, movesTheFirstNamedQuestsWithLogTextAndCompletesTheNextOne)
    {
        Env e = envOf({ makeQuest(kQ1, "", 0, { { 10, "no name" } }), // no name: skipped
            makeQuest(kQ2, "Alpha", 0, { { 5, "" }, { 10, "alpha ten" }, { 20, "alpha twenty" } }),
            makeQuest(kQ3, "Beta", 0, { { 7, "beta seven" } }),
            makeQuest(kQ4, "Gamma", 0, { { 3, "gamma end", LogEntryFlags::CompleteQuest } }),
            makeQuest(0x01000404, "Delta", 0, { { 9, "delta" } }) });
        const std::size_t set = applyTestQuests(e.mIndex, e.mState, nullptr, 2);
        EXPECT_EQ(set, 3u);
        EXPECT_EQ(e.mState.stage(kQ2), 10.f); // the lowest stage that has a text
        EXPECT_EQ(e.mState.stage(kQ3), 7.f);
        EXPECT_TRUE(e.mState.running(kQ2));
        EXPECT_TRUE(e.mState.completed(kQ4));
        EXPECT_FALSE(e.mState.running(0x01000404)); // past the count
        EXPECT_EQ(e.mState.stage(kQ1), 0.f);
        Snapshot s;
        build(e.inputs(), s);
        EXPECT_EQ(s.mRunning.size(), 2u);
        EXPECT_EQ(s.mCompleted.size(), 1u);
    }

    TEST(JournalTestQuests, nothingMovesWithoutLogText)
    {
        Env e = envOf({ makeQuest(kQ1, "Plain", 0, { { 1, "" } }) });
        EXPECT_EQ(applyTestQuests(e.mIndex, e.mState, nullptr, 3), 0u);
        EXPECT_EQ(e.mState.revision(), 0u);
    }

    TEST(JournalTags, aliasTagsBecomeTheAliasNameOtherTagsBecomeDotsAndStraysAreDropped)
    {
        EXPECT_EQ(expandTags("plain text"), "plain text");
        EXPECT_EQ(expandTags("I've found a <Alias=Staff>."), "I've found a Staff.");
        EXPECT_EQ(expandTags("See <Alias.ShortName=Jarl> now"), "See Jarl now");
        EXPECT_EQ(expandTags("pay <Global=Gold> septims"), "pay ... septims");
        EXPECT_EQ(expandTags("a < b"), "a  b");
        EXPECT_EQ(expandTags("x > y"), "x  y");
        EXPECT_EQ(expandTags("<Alias=A> and <Alias=B>"), "A and B");
        EXPECT_EQ(expandTags("<unterminated"), "unterminated");
        EXPECT_EQ(expandTags(""), "");
    }

    TEST(JournalBuild, tagsInTheTextsAreExpandedBeforeTheyAreShown)
    {
        Env e = envOf(
            { makeQuest(kQ1, "Tags", kEnabled, { { 10, "Take <Alias=Staff> to <Alias=Jarl>", 0, StageFlags::StartUp } },
                { { 0, "Pay <Global=Gold>" } }) });
        e.mObjectives[{ kQ1, 0 }] = { true, false };
        Snapshot s;
        build(e.inputs(), s);
        ASSERT_EQ(s.mRunning.size(), 1u);
        EXPECT_EQ(s.mRunning[0].mEntry, "Take Staff to Jarl");
        ASSERT_EQ(s.mRunning[0].mObjectives.size(), 1u);
        EXPECT_EQ(s.mRunning[0].mObjectives[0].mText, "Pay ...");
    }

    TEST(JournalIds, areEightLowerCaseHexDigitsAndParseBack)
    {
        EXPECT_EQ(formatQuestId(0x1A2B), "00001a2b");
        EXPECT_EQ(formatQuestId(0xFE000801), "fe000801");
        FormId id = 0;
        EXPECT_TRUE(parseQuestId("fe000801", id));
        EXPECT_EQ(id, 0xFE000801u);
        EXPECT_FALSE(parseQuestId("", id));
        EXPECT_FALSE(parseQuestId("xyz", id));
        EXPECT_FALSE(parseQuestId("123456789", id));
    }

    // ---- the service and its bindings ----

    class StubBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mName = std::string(d.name);
            mVersion = d.version;
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args = {})
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            if (it == mFunctions.end())
                return {};
            EXPECT_EQ(args.size(), it->second.params.size()) << name;
            return it->second.handler->call(args);
        }
        std::string mName;
        openeden::sdk::Version mVersion{ 0, 0 };
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    BindingValue str(std::string_view s)
    {
        BindingValue v;
        v.type = BindingType::String;
        v.text = s;
        return v;
    }
    BindingValue boolean(bool b)
    {
        BindingValue v;
        v.type = BindingType::Bool;
        v.boolean = b;
        return v;
    }

    struct ServiceFixture : testing::Test
    {
        Env mEnv;
        bool mAvailable = true;
        std::uint64_t mObjectiveRevision = 0;
        std::unique_ptr<JournalService> mService;
        StubBindings mBindings;
        std::vector<std::string> mLog;

        void SetUp() override
        {
            mEnv = envOf({ makeQuest(kQ1, "Unbound", kEnabled, { { 10, "Escape." }, { 20, "Gate." } },
                               { { 0, "Run" }, { 1, "Hide" } }),
                makeQuest(kQ2, "Finished", kEnabled, { { 5, "Over.", LogEntryFlags::CompleteQuest } }) });
            mEnv.mState.setStage(mEnv.mIndex, kQ1, 10);
            mEnv.mState.setStage(mEnv.mIndex, kQ1, 20);
            mEnv.mState.setStage(mEnv.mIndex, kQ2, 5);
            mEnv.mObjectives[{ kQ1, 0 }] = { true, true };
            mEnv.mObjectives[{ kQ1, 1 }] = { true, false };
            Hooks hooks;
            hooks.mAccess = [this](Access& out) {
                if (!mAvailable)
                    return false;
                out.mQuests = &mEnv.mIndex;
                out.mState = &mEnv.mState;
                return true;
            };
            hooks.mObjective = [this](FormId q, std::uint16_t i) {
                const auto it = mEnv.mObjectives.find({ q, i });
                return it == mEnv.mObjectives.end() ? ObjectiveFlags() : it->second;
            };
            hooks.mObjectiveRevision = [this] { return mObjectiveRevision; };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<JournalService>(std::move(hooks));
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }

        std::vector<std::string> texts(const std::string& fn, const std::string& id)
        {
            const auto r = mBindings.call(fn, { str(id) });
            std::vector<std::string> out;
            for (std::size_t i = 0; i < r.value.count; ++i)
                out.emplace_back(r.value.elements[i].text);
            return out;
        }
    };

    TEST_F(ServiceFixture, theNamespaceIsDeclaredWithReadFunctionsOnly)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.journal");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        EXPECT_EQ(mBindings.mVersion.minor, 0);
        for (const char* fn :
            { "ready", "revision", "quests", "name", "entry", "status", "log", "objectives", "objective_done" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(fn), 1u) << fn;
            EXPECT_EQ(mBindings.mFunctions[fn].access, BindingAccess::Read) << fn;
            EXPECT_TRUE(mBindings.mFunctions[fn].hasResult) << fn;
        }
    }

    TEST_F(ServiceFixture, notReadyUntilTheAccessIsGivenAndNothingIsListedBefore)
    {
        mAvailable = false;
        EXPECT_FALSE(mBindings.call("ready").value.boolean);
        EXPECT_EQ(mBindings.call("quests", { boolean(false) }).value.count, 0u);
        EXPECT_EQ(mBindings.call("revision").value.integer, 0);
        mAvailable = true;
        EXPECT_TRUE(mBindings.call("ready").value.boolean);
        EXPECT_TRUE(mService->ready());
    }

    TEST_F(ServiceFixture, questsNamesEntriesStatusLogAndObjectivesAreAnswered)
    {
        const auto running = mBindings.call("quests", { boolean(false) });
        ASSERT_EQ(running.value.count, 1u);
        const std::string id = std::string(running.value.elements[0].text);
        EXPECT_EQ(id, formatQuestId(kQ1));
        EXPECT_EQ(mBindings.call("name", { str(id) }).value.text, "Unbound");
        EXPECT_EQ(mBindings.call("entry", { str(id) }).value.text, "Gate.");
        EXPECT_EQ(mBindings.call("status", { str(id) }).value.text, "running");
        EXPECT_EQ(texts("log", id), (std::vector<std::string>{ "Escape.", "Gate." }));
        EXPECT_EQ(texts("objectives", id), (std::vector<std::string>{ "Run", "Hide" }));
        const auto done = mBindings.call("objective_done", { str(id) });
        ASSERT_EQ(done.value.count, 2u);
        EXPECT_EQ(done.value.elements[0].type, BindingType::Bool);
        EXPECT_TRUE(done.value.elements[0].boolean);
        EXPECT_FALSE(done.value.elements[1].boolean);

        const auto completed = mBindings.call("quests", { boolean(true) });
        ASSERT_EQ(completed.value.count, 1u);
        const std::string cid = std::string(completed.value.elements[0].text);
        EXPECT_EQ(mBindings.call("status", { str(cid) }).value.text, "completed");
    }

    TEST_F(ServiceFixture, anUnknownOrBadIdAnswersNothing)
    {
        EXPECT_EQ(mBindings.call("name", { str("deadbeef") }).value.text, "");
        EXPECT_EQ(mBindings.call("name", { str("not hex") }).value.text, "");
        EXPECT_EQ(mBindings.call("log", { str("deadbeef") }).value.count, 0u);
        EXPECT_EQ(mBindings.call("status", { str("deadbeef") }).value.text, "");
    }

    TEST_F(ServiceFixture, theSnapshotIsRebuiltOnlyWhenTheRevisionMoved)
    {
        ASSERT_TRUE(mService->poll());
        (void)mService->snapshot();
        const std::uint64_t builds = mService->builds();
        EXPECT_GE(builds, 1u);
        for (int i = 0; i < 20; ++i)
        {
            (void)mBindings.call("revision");
            (void)mBindings.call("quests", { boolean(false) });
        }
        EXPECT_EQ(mService->builds(), builds);
        const int before = mBindings.call("revision").value.integer;
        mEnv.mState.setStage(mEnv.mIndex, kQ1, 10);
        EXPECT_NE(mBindings.call("revision").value.integer, before);
        (void)mBindings.call("quests", { boolean(false) });
        EXPECT_EQ(mService->builds(), builds + 1);
    }

    TEST_F(ServiceFixture, anObjectiveFlagChangeMovesTheRevisionAndRebuilds)
    {
        ASSERT_TRUE(mService->poll());
        (void)mService->snapshot();
        const std::uint64_t builds = mService->builds();
        const int before = mBindings.call("revision").value.integer;
        mEnv.mObjectives[{ kQ1, 1 }] = { true, true };
        ++mObjectiveRevision;
        EXPECT_NE(mBindings.call("revision").value.integer, before);
        const auto id = formatQuestId(kQ1);
        const auto done = mBindings.call("objective_done", { str(id) });
        ASSERT_EQ(done.value.count, 2u);
        EXPECT_TRUE(done.value.elements[1].boolean);
        EXPECT_EQ(mService->builds(), builds + 1);
    }
}
