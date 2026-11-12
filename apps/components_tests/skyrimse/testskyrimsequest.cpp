// Task 0139, milestone M-S4 slice 2: Skyrim SE quests (QUST), the pure quest state, and the conditions world that answers
// GetStage, GetStageDone, GetQuestRunning, GetQuestCompleted and GetIsAliasRef from them, on generated plugins. No
// game data.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/quest.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;

    // load order: Skyrim.esm = 0, test.esp = 1, other.esp = 2. Ids 0x01xxxxxx are test.esp's own.
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kFaction = kSelf | 0x701;
    constexpr FormId kNpcA = kSelf | 0x200;
    constexpr FormId kNpcB = kSelf | 0x201;
    constexpr FormId kRefA = kSelf | 0x2A0; // a placed reference of kNpcA
    constexpr FormId kRefB = kSelf | 0x2B0;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kQuestA = kSelf | 0x400;
    constexpr FormId kQuestB = kSelf | 0x401;
    constexpr FormId kQuestC = kSelf | 0x402;
    constexpr FormId kQuestMissing = kSelf | 0x4FF;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        EXPECT_TRUE(o.add("other.esp", false, err));
        return o;
    }

    Bytes header(const std::vector<Master>& masters)
    {
        return tes4Header(1.7f, 0, "A", "B", 1, masters, 24, 44);
    }

    std::vector<Master> masters()
    {
        return { { "Skyrim.esm", 0 } };
    }

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes top(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), kGrupTop, records);
    }

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0,
        std::uint32_t runOn = 0, std::uint32_t reference = 0, std::uint8_t flags = 0, std::uint32_t p2 = 0,
        std::uint32_t p3 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>((compare << 5) | flags)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(p2).u32(runOn).u32(reference).u32(p3);
        return sub4("CTDA", b);
    }

    // ---- QUST builders (layouts: see quest.hpp) --------------------------------------------------------------------

    Bytes dnam(std::uint16_t flags, std::uint8_t priority, std::uint32_t type)
    {
        Bytes b;
        b.u16(flags).u8(priority).u8(0).u32(0).u32(type);
        return sub4("DNAM", b);
    }

    struct Entry
    {
        std::uint8_t mFlags = 0;
        Bytes mConditions;
        std::string mText;
        std::uint32_t mNextQuest = 0;
    };

    Bytes stage(std::uint16_t index, std::uint8_t flags, const std::vector<Entry>& entries, std::uint8_t junk = 0xAB)
    {
        Bytes b;
        b.append(sub4("INDX", Bytes().u16(index).u8(flags).u8(junk)));
        for (const Entry& e : entries)
        {
            b.append(sub4("QSDT", Bytes().u8(e.mFlags)));
            b.append(e.mConditions);
            if (!e.mText.empty())
                b.append(sub4("CNAM", Bytes().zstring(e.mText)));
            if (e.mNextQuest != 0)
                b.append(sub4("NAM0", Bytes().u32(e.mNextQuest)));
        }
        return b;
    }

    Bytes objective(std::uint16_t index, std::uint32_t flags, const std::string& text,
        const std::vector<std::pair<std::uint32_t, Bytes>>& targets)
    {
        Bytes b;
        b.append(sub4("QOBJ", Bytes().u16(index)));
        b.append(sub4("FNAM", Bytes().u32(flags)));
        b.append(sub4("NNAM", Bytes().zstring(text)));
        for (const auto& [alias, conditions] : targets)
        {
            b.append(sub4("QSTA", Bytes().u32(alias).u32(1)));
            b.append(conditions);
        }
        return b;
    }

    Bytes alias(const char (&startTag)[5], std::uint32_t id, const std::string& name, std::uint32_t flags,
        const Bytes& fill, const Bytes& conditions = Bytes())
    {
        Bytes b;
        b.append(sub4(startTag, Bytes().u32(id)));
        b.append(sub4("ALID", Bytes().zstring(name)));
        b.append(sub4("FNAM", Bytes().u32(flags)));
        b.append(fill);
        b.append(conditions);
        b.append(sub4("VTCK", Bytes().u32(0)));
        b.append(sub4("ALED", Bytes()));
        return b;
    }

    Bytes refAlias(std::uint32_t id, const std::string& name, const Bytes& fill, std::uint32_t flags = 0,
        const Bytes& conditions = Bytes())
    {
        return alias("ALST", id, name, flags, fill, conditions);
    }

    Bytes locAlias(std::uint32_t id, const std::string& name, const Bytes& fill)
    {
        return alias("ALLS", id, name, 1, fill);
    }

    Bytes fillRef(std::uint32_t ref)
    {
        return sub4("ALFR", Bytes().u32(ref));
    }

    Bytes fillActor(std::uint32_t npc)
    {
        return sub4("ALUA", Bytes().u32(npc));
    }

    struct QuestSpec
    {
        std::uint32_t mId = kQuestA;
        std::string mEditorId = "TestQuest";
        std::string mName = "A Quest";
        std::uint16_t mFlags = QuestFlags::StartGameEnabled;
        std::uint8_t mPriority = 30;
        std::uint32_t mType = 6;
        std::string mEvent;
        std::string mFilter;
        Bytes mHeadConditions; ///< the dialogue conditions: before NEXT
        Bytes mStartConditions; ///< after NEXT
        Bytes mStages;
        Bytes mObjectives;
        std::uint32_t mNextAlias = 1;
        Bytes mAliases;
        bool mDeleted = false;
        bool mNoDnam = false;
        std::size_t mDnamSize = 12;
    };

    Bytes questRecord(const QuestSpec& q)
    {
        Bytes b;
        b.append(edid(q.mEditorId));
        if (!q.mName.empty())
            b.append(sub4("FULL", Bytes().zstring(q.mName)));
        if (!q.mNoDnam)
        {
            Bytes d = dnam(q.mFlags, q.mPriority, q.mType);
            if (q.mDnamSize != 12)
                d = sub4("DNAM", Bytes().u16(q.mFlags).u8(q.mPriority));
            b.append(d);
        }
        if (!q.mEvent.empty())
            b.append(sub4("ENAM", Bytes().text(q.mEvent)));
        if (!q.mFilter.empty())
            b.append(sub4("FLTR", Bytes().zstring(q.mFilter)));
        b.append(q.mHeadConditions);
        b.append(sub4("NEXT", Bytes()));
        b.append(q.mStartConditions);
        b.append(q.mStages);
        b.append(q.mObjectives);
        b.append(sub4("ANAM", Bytes().u32(q.mNextAlias)));
        b.append(q.mAliases);
        return record4("QUST", q.mDeleted ? 0x20u : 0u, q.mId, b);
    }

    Bytes questPlugin(const std::vector<QuestSpec>& quests, const std::vector<Master>& m = masters())
    {
        Bytes bytes = header(m);
        Bytes records;
        for (const QuestSpec& q : quests)
            records.append(questRecord(q));
        bytes.append(top("QUST", records));
        return bytes;
    }

    struct Parsed
    {
        QuestRecords mRecords;
        std::unique_ptr<Content::Plugin> mPlugin;
        bool mOk = false;
    };

    Parsed parse(const Bytes& bytes, const std::string& name = "test.esp", QuestLimits limits = QuestLimits())
    {
        Parsed p;
        Content::Error error;
        p.mOk = parseQuestRecordsBytes(bytes.str(), name, order(), limits, p.mRecords, error);
        p.mPlugin = Content::Plugin::open(std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), name,
            error);
        return p;
    }

    QuestIndex indexOf(const Bytes& bytes)
    {
        Parsed p = parse(bytes);
        QuestIndex index;
        index.add(std::move(p.mRecords), FormIdResolver(p.mPlugin->header(), "test.esp", order()));
        return index;
    }

    QuestSpec simpleQuest(std::uint32_t id, std::uint16_t flags, const Bytes& stages = Bytes(),
        const Bytes& aliases = Bytes())
    {
        QuestSpec q;
        q.mId = id;
        q.mFlags = flags;
        q.mStages = stages;
        q.mAliases = aliases;
        q.mEditorId = "Q" + std::to_string(id & 0xFFF);
        return q;
    }

    // ---- parsing -------------------------------------------------------------------------------------------------

    TEST(SkyrimQuestParse, theHeaderOfAQuestIsReadInFull)
    {
        QuestSpec q;
        q.mFlags = QuestFlags::StartGameEnabled | QuestFlags::RunOnce;
        q.mPriority = 80;
        q.mType = 1;
        q.mEvent = "ADIA";
        q.mFilter = "Main Quest\\";
        q.mNextAlias = 77;
        const Parsed p = parse(questPlugin({ q }));
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mRecords.mQuests.size(), 1u);
        const QuestRecord& r = p.mRecords.mQuests[0];
        EXPECT_EQ(r.mCommon.mId, kQuestA);
        EXPECT_EQ(r.mCommon.mEditorId, "TestQuest");
        EXPECT_EQ(r.mName.mText, "A Quest");
        EXPECT_EQ(r.mFlags, QuestFlags::StartGameEnabled | QuestFlags::RunOnce);
        EXPECT_TRUE(r.startsEnabled());
        EXPECT_EQ(r.mPriority, 80);
        EXPECT_EQ(r.mType, 1u);
        EXPECT_EQ(r.mEvent, "ADIA");
        EXPECT_EQ(r.mFilter, "Main Quest\\");
        EXPECT_EQ(r.mNextAliasId, 77u);
        EXPECT_TRUE(r.mHasDnam);
        EXPECT_EQ(p.mRecords.mCounts.at(Content::fourCC("QUST")).mParsed, 1u);
    }

    TEST(SkyrimQuestParse, theDialogueConditionsAreBeforeNextTheStartConditionsAfterAndNothingElse)
    {
        QuestSpec q;
        q.mHeadConditions.append(ctda(72, 0, 1.0f, kNpcA));
        q.mHeadConditions.append(ctda(71, 0, 1.0f, kFaction));
        q.mStartConditions.append(ctda(74, 0, 1.0f, kSelf | 0x901));
        Entry e;
        e.mConditions.append(ctda(58, 0, 1.0f, kQuestB)); // belongs to the log entry, not to the quest
        q.mStages = stage(10, 0, { e });
        q.mAliases = refAlias(1, "A", Bytes(), 0, ctda(70, 0, 1.0f));
        const Parsed p = parse(questPlugin({ q }));
        ASSERT_EQ(p.mRecords.mQuests.size(), 1u);
        const QuestRecord& r = p.mRecords.mQuests[0];
        ASSERT_EQ(r.mConditions.size(), 2u);
        EXPECT_EQ(r.mConditions[0].mFunction, 72);
        EXPECT_EQ(r.mConditions[1].mFunction, 71);
        EXPECT_TRUE(r.mConditionsOk);
        ASSERT_EQ(r.mStartConditions.size(), 1u);
        EXPECT_EQ(r.mStartConditions[0].mFunction, 74);
        EXPECT_TRUE(r.mStartConditionsOk);
        ASSERT_EQ(r.mStages.size(), 1u);
        ASSERT_EQ(r.mStages[0].mEntries.size(), 1u);
        ASSERT_EQ(r.mStages[0].mEntries[0].mConditions.size(), 1u);
        EXPECT_EQ(r.mStages[0].mEntries[0].mConditions[0].mFunction, 58);
        ASSERT_EQ(r.mAliases.size(), 1u);
        ASSERT_EQ(r.mAliases[0].mConditions.size(), 1u);
        EXPECT_EQ(r.mAliases[0].mConditions[0].mFunction, 70);
    }

    TEST(SkyrimQuestParse, stagesKeepTheirEntriesFlagsAndOrder)
    {
        Entry a;
        a.mText = "First";
        Entry b;
        b.mFlags = LogEntryFlags::CompleteQuest;
        b.mText = "Second";
        b.mNextQuest = kQuestB;
        QuestSpec q;
        q.mStages = stage(10, StageFlags::StartUp, { a });
        q.mStages.append(stage(5, 0, { a, b }, 0x74)); // not sorted: file order is kept; the 4th INDX byte is garbage
        q.mStages.append(stage(20, StageFlags::ShutDown | StageFlags::KeepInstanceData, {}));
        const Parsed p = parse(questPlugin({ q }));
        const QuestRecord& r = p.mRecords.mQuests.at(0);
        ASSERT_EQ(r.mStages.size(), 3u);
        EXPECT_EQ(r.mStages[0].mIndex, 10);
        EXPECT_TRUE(r.mStages[0].startUp());
        EXPECT_FALSE(r.mStages[0].shutDown());
        EXPECT_EQ(r.mStages[1].mIndex, 5);
        EXPECT_EQ(r.mStages[1].mFlags, 0);
        ASSERT_EQ(r.mStages[1].mEntries.size(), 2u);
        EXPECT_EQ(r.mStages[1].mEntries[1].mFlags, LogEntryFlags::CompleteQuest);
        EXPECT_EQ(r.mStages[1].mEntries[1].mText.mText, "Second");
        EXPECT_EQ(r.mStages[1].mEntries[1].mNextQuest, kQuestB);
        EXPECT_TRUE(r.mStages[1].completesQuest());
        EXPECT_FALSE(r.mStages[1].failsQuest());
        EXPECT_FALSE(r.mStages[0].completesQuest());
        EXPECT_TRUE(r.mStages[2].shutDown());
        EXPECT_EQ(r.mStages[2].mFlags, StageFlags::ShutDown | StageFlags::KeepInstanceData);
        EXPECT_TRUE(r.mStages[2].mEntries.empty());
        ASSERT_NE(r.stage(5), nullptr);
        EXPECT_EQ(r.stage(5)->mEntries.size(), 2u);
        EXPECT_EQ(r.stage(99), nullptr);
    }

    TEST(SkyrimQuestParse, aFailQuestEntryIsFlagged)
    {
        Entry e;
        e.mFlags = LogEntryFlags::FailQuest;
        QuestSpec q;
        q.mStages = stage(30, 0, { e });
        const Parsed p = parse(questPlugin({ q }));
        EXPECT_TRUE(p.mRecords.mQuests.at(0).mStages.at(0).failsQuest());
        EXPECT_FALSE(p.mRecords.mQuests.at(0).mStages.at(0).completesQuest());
    }

    TEST(SkyrimQuestParse, theStartUpStageWithTheLowestIndexIsFound)
    {
        QuestSpec q;
        q.mStages = stage(30, StageFlags::StartUp, {});
        q.mStages.append(stage(10, 0, {}));
        q.mStages.append(stage(20, StageFlags::StartUp, {}));
        const Parsed p = parse(questPlugin({ q }));
        const QuestRecord& r = p.mRecords.mQuests.at(0);
        ASSERT_NE(r.startUpStage(), nullptr);
        EXPECT_EQ(r.startUpStage()->mIndex, 20);
        QuestSpec none;
        none.mStages = stage(10, 0, {});
        EXPECT_EQ(parse(questPlugin({ none })).mRecords.mQuests.at(0).startUpStage(), nullptr);
    }

    TEST(SkyrimQuestParse, objectivesAndTheirTargetsAreRead)
    {
        QuestSpec q;
        q.mObjectives = objective(10, 0, "Go there", { { 3, ctda(58, 2, 5.0f, kQuestA) }, { 4, Bytes() } });
        q.mObjectives.append(objective(20, 1, "Then there", {}));
        const Parsed p = parse(questPlugin({ q }));
        const QuestRecord& r = p.mRecords.mQuests.at(0);
        ASSERT_EQ(r.mObjectives.size(), 2u);
        EXPECT_EQ(r.mObjectives[0].mIndex, 10);
        EXPECT_EQ(r.mObjectives[0].mText.mText, "Go there");
        ASSERT_EQ(r.mObjectives[0].mTargets.size(), 2u);
        EXPECT_EQ(r.mObjectives[0].mTargets[0].mAlias, 3u);
        EXPECT_EQ(r.mObjectives[0].mTargets[0].mFlags, 1u);
        ASSERT_EQ(r.mObjectives[0].mTargets[0].mConditions.size(), 1u);
        EXPECT_TRUE(r.mObjectives[0].mTargets[1].mConditions.empty());
        EXPECT_EQ(r.mObjectives[1].mFlags, 1u);
        EXPECT_TRUE(r.mObjectives[1].mTargets.empty());
        // the objective's target conditions are not the quest's
        EXPECT_TRUE(r.mConditions.empty());
    }

    TEST(SkyrimQuestParse, everyKindOfAliasFillIsRecognised)
    {
        QuestSpec q;
        q.mAliases.append(refAlias(1, "Ref", fillRef(kRefA), AliasFlags::Essential | AliasFlags::QuestObject));
        q.mAliases.append(refAlias(2, "Actor", fillActor(kNpcA)));
        q.mAliases.append(locAlias(3, "Loc", sub4("ALFL", Bytes().u32(kSelf | 0x900))));
        q.mAliases.append(refAlias(4, "External",
            sub4("ALEQ", Bytes().u32(kQuestB)).append(sub4("ALEA", Bytes().u32(9)))));
        q.mAliases.append(refAlias(5, "Created", sub4("ALCO", Bytes().u32(kSelf | 0x950))));
        q.mAliases.append(refAlias(6, "Event", sub4("ALFE", Bytes().text("ADIA")).append(sub4("ALFD", Bytes().text("FNAM")))));
        q.mAliases.append(refAlias(7, "Near",
            sub4("ALNA", Bytes().u32(2)).append(sub4("ALNT", Bytes().u32(kSelf | 0x960)))));
        q.mAliases.append(refAlias(8, "InLoc",
            sub4("ALFA", Bytes().u32(3)).append(sub4("ALRT", Bytes().u32(kSelf | 0x970)))));
        q.mAliases.append(refAlias(9, "Forced", sub4("ALFI", Bytes().u32(1))));
        q.mAliases.append(refAlias(10, "Find", Bytes(), 0, ctda(72, 0, 1.0f, kNpcB)));
        q.mNextAlias = 11;
        const Parsed p = parse(questPlugin({ q }));
        const QuestRecord& r = p.mRecords.mQuests.at(0);
        ASSERT_EQ(r.mAliases.size(), 10u);
        EXPECT_EQ(r.mAliases[0].mFill, AliasFillKind::SpecificReference);
        EXPECT_EQ(r.mAliases[0].mReference, kRefA);
        EXPECT_EQ(r.mAliases[0].mFlags, AliasFlags::Essential | AliasFlags::QuestObject);
        EXPECT_EQ(r.mAliases[0].mName, "Ref");
        EXPECT_FALSE(r.mAliases[0].mIsLocation);
        EXPECT_TRUE(r.mAliases[0].dataFillable());
        EXPECT_EQ(r.mAliases[1].mFill, AliasFillKind::UniqueActor);
        EXPECT_EQ(r.mAliases[1].mActor, kNpcA);
        EXPECT_TRUE(r.mAliases[1].dataFillable());
        EXPECT_EQ(r.mAliases[2].mFill, AliasFillKind::SpecificLocation);
        EXPECT_TRUE(r.mAliases[2].mIsLocation);
        EXPECT_EQ(r.mAliases[2].mLocation, kSelf | 0x900);
        EXPECT_FALSE(r.mAliases[2].dataFillable());
        EXPECT_EQ(r.mAliases[3].mFill, AliasFillKind::ExternalAlias);
        EXPECT_EQ(r.mAliases[3].mExternalQuest, kQuestB);
        EXPECT_EQ(r.mAliases[3].mOtherAlias, 9u);
        EXPECT_EQ(r.mAliases[4].mFill, AliasFillKind::CreatedObject);
        EXPECT_EQ(r.mAliases[5].mFill, AliasFillKind::FromEvent);
        EXPECT_EQ(r.mAliases[6].mFill, AliasFillKind::NearAlias);
        EXPECT_EQ(r.mAliases[6].mOtherAlias, 2u);
        EXPECT_EQ(r.mAliases[7].mFill, AliasFillKind::InLocationAlias);
        EXPECT_EQ(r.mAliases[7].mOtherAlias, 3u);
        EXPECT_EQ(r.mAliases[7].mReferenceType, kSelf | 0x970);
        EXPECT_EQ(r.mAliases[8].mFill, AliasFillKind::FindMatching); // ALFI is an extra, not a fill
        EXPECT_EQ(r.mAliases[8].mForceIntoAlias, 1u);
        EXPECT_EQ(r.mAliases[8].mOtherAlias, 0u);
        EXPECT_EQ(r.mAliases[9].mFill, AliasFillKind::FindMatching);
        ASSERT_EQ(r.mAliases[9].mConditions.size(), 1u);
        EXPECT_FALSE(r.mAliases[9].dataFillable());
        ASSERT_NE(r.alias(2), nullptr);
        EXPECT_EQ(r.alias(2)->mName, "Actor");
        EXPECT_EQ(r.alias(77), nullptr);
    }

    TEST(SkyrimQuestParse, aSpecificReferenceKeepsItsFillBesideAForcedAliasAndIsResolvedThroughTheLoadOrder)
    {
        // other.esp is master index 2 in test.esp's list: its ids are 0x02xxxxxx
        std::vector<Master> m{ { "Skyrim.esm", 0 }, { "other.esp", 0 } };
        QuestSpec q;
        q.mAliases = refAlias(1, "Mixed", fillRef((1u << 24) | 0x123).append(sub4("ALFI", Bytes().u32(5))));
        const Parsed p = parse(questPlugin({ q }, m));
        ASSERT_EQ(p.mRecords.mQuests.size(), 1u);
        const QuestAlias& a = p.mRecords.mQuests[0].mAliases.at(0);
        EXPECT_EQ(a.mFill, AliasFillKind::SpecificReference);
        EXPECT_EQ(a.mForceIntoAlias, 5u);
        EXPECT_EQ(a.mReference, 0x02000123u); // master 1 of the file is other.esp, which loads at index 2
    }

    TEST(SkyrimQuestParse, aWrongSizedSubrecordIsOddNotGuessed)
    {
        QuestSpec q;
        q.mDnamSize = 3;
        const Parsed p = parse(questPlugin({ q }));
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mRecords.mQuests.size(), 1u);
        EXPECT_EQ(p.mRecords.mOddSubrecords, 1u);
        EXPECT_EQ(p.mRecords.mOddBy.at("QUST.DNAM"), 1u);
        EXPECT_FALSE(p.mRecords.mQuests[0].mHasDnam);
        EXPECT_EQ(p.mRecords.mQuests[0].mFlags, 0);
        EXPECT_FALSE(p.mRecords.mQuests[0].startsEnabled());
    }

    TEST(SkyrimQuestParse, aTornSubrecordFailsOnlyThatRecord)
    {
        QuestSpec good = simpleQuest(kQuestA, 0);
        // append a subrecord header that claims more bytes than the record holds, inside a new record
        Bytes torn;
        torn.append(edid("Torn"));
        torn.fourCC("DNAM").u16(200).u8(1); // claims 200 bytes, has 1
        Bytes records = questRecord(good);
        records.append(record4("QUST", 0, kQuestB, torn));
        records.append(questRecord(simpleQuest(kQuestC, 0)));
        Bytes bytes = header(masters());
        bytes.append(top("QUST", records));
        const Parsed p = parse(bytes);
        EXPECT_EQ(p.mRecords.mQuests.size(), 2u);
        EXPECT_EQ(p.mRecords.mCounts.at(Content::fourCC("QUST")).mFailed, 1u);
        EXPECT_EQ(p.mRecords.mErrors.size(), 1u);
    }

    TEST(SkyrimQuestParse, tooManyStagesFailTheRecord)
    {
        QuestSpec q;
        for (std::uint16_t i = 0; i < 6; ++i)
            q.mStages.append(stage(i, 0, {}));
        QuestLimits limits;
        limits.mMaxStages = 5;
        const Parsed p = parse(questPlugin({ q }), "test.esp", limits);
        EXPECT_TRUE(p.mRecords.mQuests.empty());
        EXPECT_EQ(p.mRecords.mCounts.at(Content::fourCC("QUST")).mFailed, 1u);
    }

    TEST(SkyrimQuestParse, tooManyAliasesAndObjectivesAndEntriesFailTheRecord)
    {
        QuestSpec aliases;
        for (std::uint32_t i = 1; i <= 4; ++i)
            aliases.mAliases.append(refAlias(i, "A", Bytes()));
        QuestLimits limits;
        limits.mMaxAliases = 3;
        EXPECT_TRUE(parse(questPlugin({ aliases }), "test.esp", limits).mRecords.mQuests.empty());
        EXPECT_EQ(parse(questPlugin({ aliases })).mRecords.mQuests.at(0).mAliases.size(), 4u); // the control

        QuestSpec objectives;
        for (std::uint16_t i = 0; i < 4; ++i)
            objectives.mObjectives.append(objective(i, 0, "x", {}));
        limits = QuestLimits();
        limits.mMaxObjectives = 3;
        EXPECT_TRUE(parse(questPlugin({ objectives }), "test.esp", limits).mRecords.mQuests.empty());
        EXPECT_EQ(parse(questPlugin({ objectives })).mRecords.mQuests.at(0).mObjectives.size(), 4u);

        QuestSpec entries;
        entries.mStages = stage(1, 0, { Entry(), Entry(), Entry() });
        limits = QuestLimits();
        limits.mMaxEntriesPerStage = 2;
        EXPECT_TRUE(parse(questPlugin({ entries }), "test.esp", limits).mRecords.mQuests.empty());
        EXPECT_EQ(parse(questPlugin({ entries })).mRecords.mQuests.at(0).mStages.at(0).mEntries.size(), 3u);
    }

    TEST(SkyrimQuestParse, aDamagedConditionMarksTheListNotTheRecord)
    {
        QuestSpec q;
        q.mHeadConditions.append(sub4("CTDA", Bytes().u32(1).u32(2))); // 8 bytes, not 32
        const Parsed p = parse(questPlugin({ q }));
        ASSERT_EQ(p.mRecords.mQuests.size(), 1u);
        EXPECT_FALSE(p.mRecords.mQuests[0].mConditionsOk);
    }

    TEST(SkyrimQuestParse, aPluginForAnotherGameIsRefused)
    {
        Bytes bytes = tes4Header(0.94f, 0, "A", "B", 1, masters(), 24, 44);
        const Parsed p = parse(bytes);
        EXPECT_FALSE(p.mOk);
    }

    TEST(SkyrimQuestParse, anAliasWithoutItsIdIsNotInventedFromThePreviousOne)
    {
        // an ALID with no ALST/ALLS before it is odd and attaches to nothing
        QuestSpec q;
        q.mAliases = sub4("ALID", Bytes().zstring("Orphan"));
        const Parsed p = parse(questPlugin({ q }));
        ASSERT_EQ(p.mRecords.mQuests.size(), 1u);
        EXPECT_TRUE(p.mRecords.mQuests[0].mAliases.empty());
        EXPECT_EQ(p.mRecords.mOddSubrecords, 1u);
    }

    // ---- the index -----------------------------------------------------------------------------------------------

    TEST(SkyrimQuestIndex, aLaterPluginReplacesTheWholeRecordAndDeletes)
    {
        QuestIndex index;
        {
            QuestSpec q = simpleQuest(kQuestA, 0, stage(10, 0, {}));
            QuestSpec r = simpleQuest(kQuestB, QuestFlags::StartGameEnabled);
            Parsed p = parse(questPlugin({ q, r }), "test.esp");
            index.add(std::move(p.mRecords), FormIdResolver(p.mPlugin->header(), "test.esp", order()));
        }
        ASSERT_EQ(index.questCount(), 2u);
        EXPECT_EQ(index.quest(kQuestA)->mStages.size(), 1u);
        {
            QuestSpec q = simpleQuest(kQuestA, QuestFlags::StartGameEnabled, stage(20, 0, {}).append(stage(30, 0, {})));
            QuestSpec r = simpleQuest(kQuestB, 0);
            r.mDeleted = true;
            // other.esp lists test.esp as a master so these ids stay test.esp's
            std::vector<Master> m{ { "Skyrim.esm", 0 }, { "test.esp", 0 } };
            Parsed p = parse(questPlugin({ q, r }, m), "other.esp");
            index.add(std::move(p.mRecords), FormIdResolver(p.mPlugin->header(), "other.esp", order()));
        }
        EXPECT_EQ(index.questCount(), 1u);
        EXPECT_EQ(index.quest(kQuestB), nullptr);
        const QuestRecord* a = index.quest(kQuestA);
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->mStages.size(), 2u); // replaced, not merged
        EXPECT_EQ(a->mStages[0].mIndex, 20);
        EXPECT_TRUE(a->startsEnabled());
        EXPECT_EQ(index.questIds(), std::vector<FormId>{ kQuestA });
        EXPECT_NE(&index.resolverOf(*a), nullptr);
    }

    // ---- the state -----------------------------------------------------------------------------------------------

    TEST(SkyrimQuestState, enabledQuestsRunAtTheirStartUpStageOtherwiseAtZero)
    {
        QuestSpec withStart = simpleQuest(kQuestA, QuestFlags::StartGameEnabled,
            stage(5, 0, {}).append(stage(10, StageFlags::StartUp, {})));
        QuestSpec noStart = simpleQuest(kQuestB, QuestFlags::StartGameEnabled, stage(5, 0, {}));
        QuestSpec idle = simpleQuest(kQuestC, 0, stage(10, StageFlags::StartUp, {}));
        const QuestIndex index = indexOf(questPlugin({ withStart, noStart, idle }));
        InitialStateReport report;
        const QuestState state = QuestState::initial(index, &report);

        EXPECT_TRUE(state.exists(kQuestA));
        EXPECT_TRUE(state.running(kQuestA));
        EXPECT_FLOAT_EQ(state.stage(kQuestA), 10.f);
        EXPECT_TRUE(state.stageDone(kQuestA, 10));
        EXPECT_FALSE(state.stageDone(kQuestA, 5));
        EXPECT_TRUE(state.running(kQuestB));
        EXPECT_FLOAT_EQ(state.stage(kQuestB), 0.f);
        EXPECT_FALSE(state.stageDone(kQuestB, 5));
        EXPECT_TRUE(state.exists(kQuestC));
        EXPECT_FALSE(state.running(kQuestC)); // not started: its start up stage waits for a script
        EXPECT_FLOAT_EQ(state.stage(kQuestC), 0.f);
        EXPECT_FALSE(state.completed(kQuestC));

        EXPECT_FALSE(state.exists(kQuestMissing));
        EXPECT_FALSE(state.running(kQuestMissing));
        EXPECT_FLOAT_EQ(state.stage(kQuestMissing), 0.f);

        EXPECT_EQ(report.mQuests, 3u);
        EXPECT_EQ(report.mStartEnabled, 2u);
        EXPECT_EQ(report.mStartEnabledWithStartUpStage, 1u);
    }

    TEST(SkyrimQuestState, aQuestWithTheCompletedFlagStartsCompleted)
    {
        const QuestIndex index = indexOf(questPlugin({ simpleQuest(kQuestA, QuestFlags::Completed) }));
        InitialStateReport report;
        const QuestState state = QuestState::initial(index, &report);
        EXPECT_TRUE(state.completed(kQuestA));
        EXPECT_EQ(report.mAlreadyCompleted, 1u);
    }

    TEST(SkyrimQuestState, aliasesFilledFromDataAreFilledAndTheRestAreCounted)
    {
        Bytes aliases = refAlias(1, "Ref", fillRef(kRefA));
        aliases.append(refAlias(2, "Actor", fillActor(kNpcA)));
        aliases.append(refAlias(3, "Find", Bytes(), 0, ctda(72, 0, 1.0f, kNpcB)));
        aliases.append(locAlias(4, "Loc", sub4("ALFL", Bytes().u32(kSelf | 0x900))));
        aliases.append(refAlias(5, "Forced", sub4("ALFI", Bytes().u32(1))));
        const QuestIndex index = indexOf(questPlugin({ simpleQuest(kQuestA, 0, Bytes(), aliases) }));
        InitialStateReport report;
        const QuestState state = QuestState::initial(index, &report);

        AliasFill f = state.aliasFill(kQuestA, 1);
        EXPECT_TRUE(f.mKnown);
        EXPECT_EQ(f.mReference, kRefA);
        f = state.aliasFill(kQuestA, 2);
        EXPECT_TRUE(f.mKnown);
        EXPECT_EQ(f.mActor, kNpcA);
        EXPECT_FALSE(state.aliasFill(kQuestA, 3).mKnown);
        EXPECT_FALSE(state.aliasFill(kQuestA, 4).mKnown);
        EXPECT_FALSE(state.aliasFill(kQuestA, 5).mKnown);
        EXPECT_FALSE(state.aliasFill(kQuestA, 99).mKnown);
        EXPECT_FALSE(state.aliasFill(kQuestMissing, 1).mKnown);

        EXPECT_EQ(report.mAliases, 5u);
        EXPECT_EQ(report.mReferenceAliases, 4u);
        EXPECT_EQ(report.mLocationAliases, 1u);
        EXPECT_EQ(report.mAliasesDataFillable, 2u);
        EXPECT_EQ(report.mAliasesConditional, 3u);
        EXPECT_EQ(report.mByFill.at(AliasFillKind::SpecificReference), 1u);
        EXPECT_EQ(report.mByFill.at(AliasFillKind::FindMatching), 2u); // "Find" and "Forced" (ALFI is no fill)
    }

    TEST(SkyrimQuestState, aScriptCanFillAnAliasAfterwards)
    {
        const QuestIndex index = indexOf(questPlugin({ simpleQuest(kQuestA, 0, Bytes(), refAlias(3, "Find", Bytes())) }));
        QuestState state = QuestState::initial(index);
        AliasFill fill;
        fill.mKnown = true;
        fill.mReference = kRefB;
        state.setAliasFill(kQuestA, 3, fill);
        EXPECT_TRUE(state.aliasFill(kQuestA, 3).mKnown);
        EXPECT_EQ(state.aliasFill(kQuestA, 3).mReference, kRefB);
    }

    TEST(SkyrimQuestState, settingAStageMakesItCurrentAndDoneAndStartsTheQuest)
    {
        const QuestIndex index = indexOf(questPlugin({ simpleQuest(kQuestA, 0, stage(10, 0, {}).append(stage(20, 0, {}))) }));
        QuestState state = QuestState::initial(index);
        EXPECT_FALSE(state.running(kQuestA));
        state.setStage(index, kQuestA, 10);
        EXPECT_TRUE(state.running(kQuestA));
        EXPECT_FLOAT_EQ(state.stage(kQuestA), 10.f);
        state.setStage(index, kQuestA, 20);
        EXPECT_FLOAT_EQ(state.stage(kQuestA), 20.f);
        EXPECT_TRUE(state.stageDone(kQuestA, 10)); // stays done
        EXPECT_TRUE(state.stageDone(kQuestA, 20));
        EXPECT_FALSE(state.stageDone(kQuestA, 30));
        // an unknown quest is not invented
        state.setStage(index, kQuestMissing, 10);
        EXPECT_FALSE(state.exists(kQuestMissing));
    }

    TEST(SkyrimQuestState, aCompletingStageCompletesAndAShutDownStageStops)
    {
        Entry done;
        done.mFlags = LogEntryFlags::CompleteQuest;
        QuestSpec q = simpleQuest(kQuestA, QuestFlags::StartGameEnabled,
            stage(10, 0, {}).append(stage(100, 0, { done })).append(stage(200, StageFlags::ShutDown, {})));
        const QuestIndex index = indexOf(questPlugin({ q }));
        QuestState state = QuestState::initial(index);
        state.setStage(index, kQuestA, 10);
        EXPECT_FALSE(state.completed(kQuestA));
        state.setStage(index, kQuestA, 100);
        EXPECT_TRUE(state.completed(kQuestA));
        EXPECT_TRUE(state.running(kQuestA)); // completing does not stop it
        state.setStage(index, kQuestA, 200);
        EXPECT_FALSE(state.running(kQuestA));
        state.start(kQuestA);
        EXPECT_TRUE(state.running(kQuestA));
        state.stop(kQuestA);
        EXPECT_FALSE(state.running(kQuestA));
    }

    // ---- the conditions world over quests ------------------------------------------------------------------------

    Bytes acbs(std::uint32_t flags, std::uint16_t templateFlags)
    {
        Bytes b;
        b.u32(flags).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(templateFlags).u16(0).u16(0);
        return b;
    }

    Bytes npc(std::uint32_t id, const std::string& name, const Bytes& factions)
    {
        Bytes b;
        b.append(edid(name));
        b.append(sub4("ACBS", acbs(0, 0)));
        b.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        b.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        b.append(factions);
        return record4("NPC_", 0, id, b);
    }

    Bytes faction(std::uint32_t id, std::int8_t rank)
    {
        return sub4("SNAM", Bytes().u32(id).u8(static_cast<std::uint8_t>(rank)).u8(0).u8(0).u8(0));
    }

    Bytes raceRecord()
    {
        Bytes b;
        b.append(edid("NordRace"));
        b.append(sub4("VTCK", Bytes().u32(kSelf | 0x803).u32(kSelf | 0x804)));
        return record4("RACE", 0, kRaceNord, b);
    }

    struct InfoSpec
    {
        std::uint32_t mId = 0;
        std::uint16_t mFlags = 0;
        Bytes mConditions;
        std::uint32_t mQuest = 0;
        std::string mText = "Hello.";
    };

    Bytes infoRecord(const InfoSpec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(s.mFlags).u16(24)));
        if (s.mQuest != 0)
            b.append(sub4("QNAM", Bytes().u32(s.mQuest)));
        Bytes t;
        t.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        b.append(sub4("TRDT", t));
        b.append(sub4("NAM1", Bytes().zstring(s.mText)));
        b.append(s.mConditions);
        return record4("INFO", 0, s.mId, b);
    }

    Bytes topicWithInfos(std::uint32_t id, float priority, std::uint32_t quest, const std::vector<InfoSpec>& infos)
    {
        Bytes children;
        for (const InfoSpec& i : infos)
            children.append(infoRecord(i));
        Bytes b;
        b.append(edid("T" + std::to_string(id & 0xFFF)));
        b.append(sub4("PNAM", Bytes().f32(priority)));
        b.append(sub4("QNAM", Bytes().u32(quest)));
        b.append(sub4("DATA", Bytes().u8(0).u8(7).u16(73)));
        b.append(sub4("SNAM", Bytes().text("HELO")));
        b.append(sub4("TIFC", Bytes().u32(1)));
        Bytes out = record4("DIAL", 0, id, b);
        out.append(group4(id, kGrupTopicChildren, children));
        return out;
    }

    struct QWorld
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        ActorRecords mActorRecords;
        DialogueRecords mDialogueRecords;
        ActorIndex mActors;
        DialogueIndex mDialogue;
        QuestIndex mQuests;
        QuestState mState;
        FormIdResolver mResolver;
        std::unique_ptr<SkyrimConditionWorld> mWorld;

        QWorld(const std::vector<QuestSpec>& quests, const Bytes& dials = Bytes(), bool withQuests = true,
            const std::string& pluginName = "test.esp")
            : mResolver(makeResolver(bytesOf(quests, dials), pluginName))
        {
            const Bytes bytes = bytesOf(quests, dials);
            Content::Error error;
            mPlugin = Content::Plugin::open(std::make_unique<std::istringstream>(bytes.str(), std::ios::binary),
                pluginName, error);
            EXPECT_TRUE(parseActorRecordsBytes(bytes.str(), pluginName, order(), ActorLimits(), mActorRecords, error));
            mActors.add(mActorRecords);
            EXPECT_TRUE(parseDialogueRecordsBytes(bytes.str(), pluginName, order(), DialogueLimits(),
                mDialogueRecords, error));
            mDialogue.add(mDialogueRecords, mResolver);
            QuestRecords qr;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), pluginName, order(), QuestLimits(), qr, error));
            mQuests.add(std::move(qr), mResolver);
            mState = QuestState::initial(mQuests);
            mWorld = std::make_unique<SkyrimConditionWorld>(mActors, mDialogue);
            if (withQuests)
                mWorld->setQuests(&mQuests, &mState);
            mWorld->setPlugin(&mResolver);
        }

        static Bytes bytesOf(const std::vector<QuestSpec>& quests, const Bytes& dials)
        {
            Bytes bytes = questPlugin(quests);
            if (dials.size() != 0)
                bytes.append(top("DIAL", dials));
            Bytes npcs = npc(kNpcA, "NpcA", faction(kFaction, 3));
            npcs.append(npc(kNpcB, "NpcB", Bytes()));
            bytes.append(top("NPC_", npcs));
            bytes.append(top("RACE", raceRecord()));
            return bytes;
        }

        static FormIdResolver makeResolver(const Bytes& bytes, const std::string& pluginName)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::open(std::make_unique<std::istringstream>(bytes.str(),
                                                          std::ios::binary), pluginName, error);
            return FormIdResolver(plugin->header(), pluginName, order());
        }

        float eval(const Bytes& condition, const DialogueSpeaker& speaker, FormId context = kNullFormId)
        {
            mWorld->setSpeaker(speaker);
            mWorld->setQuestContext(context);
            std::vector<survey::Ctda> list;
            const std::string raw = condition.str();
            EXPECT_TRUE(conditions::decodeConditions(
                { reinterpret_cast<const std::byte*>(raw.data()), raw.size() }, list));
            EXPECT_EQ(list.size(), 1u);
            conditions::Evaluator eval;
            return eval.evaluate(list, *mWorld) ? 1.f : 0.f;
        }

        bool answers(const Bytes& condition, const DialogueSpeaker& speaker, FormId context = kNullFormId)
        {
            mWorld->setSpeaker(speaker);
            mWorld->setQuestContext(context);
            std::vector<survey::Ctda> list;
            const std::string raw = condition.str();
            EXPECT_TRUE(conditions::decodeConditions(
                { reinterpret_cast<const std::byte*>(raw.data()), raw.size() }, list));
            return mWorld->answers(list.at(0));
        }
    };

    DialogueSpeaker speakerA()
    {
        DialogueSpeaker s;
        s.mBase = kNpcA;
        s.mReference = kRefA;
        return s;
    }

    DialogueSpeaker speakerB()
    {
        DialogueSpeaker s;
        s.mBase = kNpcB;
        s.mReference = kRefB;
        return s;
    }

    QuestSpec stagedQuest()
    {
        QuestSpec q = simpleQuest(kQuestA, QuestFlags::StartGameEnabled,
            stage(10, StageFlags::StartUp, {}).append(stage(20, 0, {})));
        return q;
    }

    TEST(SkyrimQuestWorld, getStageGetStageDoneRunningAndCompletedAnswerFromTheState)
    {
        Entry done;
        done.mFlags = LogEntryFlags::CompleteQuest;
        QuestSpec q = stagedQuest();
        q.mStages.append(stage(30, 0, { done }));
        QWorld w({ q, simpleQuest(kQuestB, 0) });
        const DialogueSpeaker s = speakerA();
        // stage 10 at the start
        EXPECT_EQ(w.eval(ctda(58, 0, 10.0f, kQuestA), s), 1.f);
        EXPECT_EQ(w.eval(ctda(58, 0, 0.0f, kQuestA), s), 0.f);
        EXPECT_EQ(w.eval(ctda(59, 0, 1.0f, kQuestA, 0, 0, 0, 10), s), 1.f);
        EXPECT_EQ(w.eval(ctda(59, 0, 1.0f, kQuestA, 0, 0, 0, 20), s), 0.f);
        EXPECT_EQ(w.eval(ctda(56, 0, 1.0f, kQuestA), s), 1.f);
        EXPECT_EQ(w.eval(ctda(56, 0, 1.0f, kQuestB), s), 0.f); // exists but not started
        EXPECT_EQ(w.eval(ctda(543, 0, 1.0f, kQuestA), s), 0.f);
        // the state moves, the answers move with it
        w.mState.setStage(w.mQuests, kQuestA, 20);
        EXPECT_EQ(w.eval(ctda(58, 0, 20.0f, kQuestA), s), 1.f);
        EXPECT_EQ(w.eval(ctda(59, 0, 1.0f, kQuestA, 0, 0, 0, 20), s), 1.f);
        w.mState.setStage(w.mQuests, kQuestA, 30);
        EXPECT_EQ(w.eval(ctda(543, 0, 1.0f, kQuestA), s), 1.f);
    }

    TEST(SkyrimQuestWorld, aQuestIdIsResolvedThroughThePluginAndAnUnknownOneIsStageZero)
    {
        QWorld w({ stagedQuest() });
        const DialogueSpeaker s = speakerA();
        EXPECT_EQ(w.eval(ctda(58, 0, 0.0f, kQuestMissing), s), 1.f);
        EXPECT_EQ(w.eval(ctda(56, 0, 1.0f, kQuestMissing), s), 0.f);
        EXPECT_EQ(w.eval(ctda(58, 0, 10.0f, 0x00000000), s), 0.f); // a null id matches nothing
    }

    TEST(SkyrimQuestWorld, withoutQuestsEveryQuestFunctionKeepsItsDefault)
    {
        QWorld w({ stagedQuest() }, Bytes(), false);
        const DialogueSpeaker s = speakerA();
        EXPECT_EQ(w.eval(ctda(58, 0, 0.0f, kQuestA), s), 1.f);
        EXPECT_EQ(w.eval(ctda(56, 0, 1.0f, kQuestA), s), 0.f);
        EXPECT_FALSE(w.answers(ctda(58, 0, 10.0f, kQuestA), s));
        EXPECT_FALSE(w.answers(ctda(566, 0, 1.0f, 1), s, kQuestA));
    }

    TEST(SkyrimQuestWorld, getIsAliasRefComparesTheSpeakerWithTheAliasFill)
    {
        Bytes aliases = refAlias(1, "Actor", fillActor(kNpcA));
        aliases.append(refAlias(2, "Ref", fillRef(kRefB)));
        aliases.append(refAlias(3, "Find", Bytes()));
        aliases.append(refAlias(4, "Player", fillRef(0x14)));
        QWorld w({ simpleQuest(kQuestA, QuestFlags::StartGameEnabled, Bytes(), aliases) });
        // unique actor: every reference of that base is it
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 1), speakerA(), kQuestA), 1.f);
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 1), speakerB(), kQuestA), 0.f);
        // specific reference: that placed reference only
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 2), speakerB(), kQuestA), 1.f);
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 2), speakerA(), kQuestA), 0.f);
        // a conditional alias the data cannot fill: false, and NOT answered
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 3), speakerA(), kQuestA), 0.f);
        // the player's alias: the player is the subject of run-on 1
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 4, 1), speakerA(), kQuestA), 1.f);
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 1, 1), speakerA(), kQuestA), 0.f);
        EXPECT_TRUE(w.answers(ctda(566, 0, 1.0f, 4, 1), speakerA(), kQuestA));
        EXPECT_FALSE(w.answers(ctda(566, 0, 1.0f, 3, 1), speakerA(), kQuestA)); // alias 3 is not data-fillable
        // no quest context: nothing to compare with
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 1), speakerA(), kNullFormId), 0.f);
        // the context decides which quest's alias 1 it is
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 1), speakerA(), kQuestB), 0.f);
    }

    TEST(SkyrimQuestWorld, aScriptFilledAliasIsFollowed)
    {
        QWorld w({ simpleQuest(kQuestA, QuestFlags::StartGameEnabled, Bytes(), refAlias(3, "Find", Bytes())) });
        AliasFill fill;
        fill.mKnown = true;
        fill.mActor = kNpcB;
        w.mState.setAliasFill(kQuestA, 3, fill);
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 3), speakerB(), kQuestA), 1.f);
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 3), speakerA(), kQuestA), 0.f);
        EXPECT_TRUE(w.answers(ctda(566, 0, 1.0f, 3), speakerA(), kQuestA));
    }

    TEST(SkyrimQuestWorld, aConditionRunOnAQuestAliasIsTheSpeakerWhenThatAliasIsTheSpeaker)
    {
        Bytes aliases = refAlias(1, "Actor", fillActor(kNpcA));
        aliases.append(refAlias(2, "Other", fillActor(kNpcB)));
        QWorld w({ simpleQuest(kQuestA, QuestFlags::StartGameEnabled, Bytes(), aliases) });
        // GetInFaction(kFaction) run on alias 1 (run-on 5, alias id in parameter 3)
        const Bytes onAlias1 = ctda(71, 0, 1.0f, kFaction, 5, 0, 0, 0, 1);
        EXPECT_EQ(w.eval(onAlias1, speakerA(), kQuestA), 1.f);
        EXPECT_TRUE(w.answers(onAlias1, speakerA(), kQuestA));
        // run on alias 2 while the speaker is NpcA: some other actor, whom the data cannot describe
        const Bytes onAlias2 = ctda(71, 0, 1.0f, kFaction, 5, 0, 0, 0, 2);
        EXPECT_EQ(w.eval(onAlias2, speakerA(), kQuestA), 0.f);
        EXPECT_FALSE(w.answers(onAlias2, speakerA(), kQuestA));
        // run on an alias nothing fills
        EXPECT_FALSE(w.answers(ctda(71, 0, 1.0f, kFaction, 5, 0, 0, 0, 9), speakerA(), kQuestA));
    }

    TEST(SkyrimQuestWorld, answersTellsWhatTheQuestDataSettles)
    {
        Bytes aliases = refAlias(1, "Actor", fillActor(kNpcA));
        aliases.append(refAlias(3, "Find", Bytes()));
        QWorld w({ simpleQuest(kQuestA, QuestFlags::StartGameEnabled, Bytes(), aliases) });
        const DialogueSpeaker s = speakerA();
        EXPECT_TRUE(w.answers(ctda(58, 0, 0.0f, kQuestA), s));
        EXPECT_TRUE(w.answers(ctda(59, 0, 0.0f, kQuestA), s));
        EXPECT_TRUE(w.answers(ctda(56, 0, 0.0f, kQuestA), s));
        EXPECT_TRUE(w.answers(ctda(543, 0, 0.0f, kQuestA), s));
        EXPECT_FALSE(w.answers(ctda(58, 0, 0.0f, kQuestMissing), s)); // a quest we do not have
        EXPECT_TRUE(w.answers(ctda(566, 0, 1.0f, 1), s, kQuestA));
        EXPECT_FALSE(w.answers(ctda(566, 0, 1.0f, 3), s, kQuestA)); // conditional alias
        EXPECT_FALSE(w.answers(ctda(566, 0, 1.0f, 1), s, kNullFormId)); // no owning quest
        EXPECT_FALSE(w.answers(ctda(629, 0, 1.0f, kQuestA), s)); // a Papyrus variable: not data
    }

    // ---- the choice of a response over quest state ---------------------------------------------------------------

    TEST(SkyrimQuestSelect, aQuestThatIsNotRunningHoldsBackItsInfosAndTheStageDecidesTheRest)
    {
        // quest A runs at stage 10; quest B does not run. Topic infos, in order:
        //   1) quest B (not running)            -> skipped by the running check
        //   2) quest A, needs stage 20          -> fails
        //   3) quest A, needs stage 10          -> picked
        InfoSpec one;
        one.mId = kSelf | 0x1001;
        one.mQuest = kQuestB;
        one.mText = "B.";
        InfoSpec two;
        two.mId = kSelf | 0x1002;
        two.mQuest = kQuestA;
        two.mConditions.append(ctda(58, 0, 20.0f, kQuestA));
        two.mText = "Stage twenty.";
        InfoSpec three;
        three.mId = kSelf | 0x1003;
        three.mQuest = kQuestA;
        three.mConditions.append(ctda(58, 0, 10.0f, kQuestA));
        three.mText = "Stage ten.";
        QWorld w({ stagedQuest(), simpleQuest(kQuestB, 0) }, topicWithInfos(kTopicHello, 50.f, kQuestA, { one, two, three }));
        SelectionOptions options;
        options.mQuestRunning = [&](FormId q) { return !w.mState.exists(q) || w.mState.running(q); };
        const Selection s = selectInfo(w.mDialogue, kTopicHello, *w.mWorld, speakerA(), options);
        ASSERT_NE(s.mInfo, nullptr);
        EXPECT_EQ(s.mInfo->mResponses.at(0).mText.mText, "Stage ten.");
        EXPECT_EQ(s.mUncertainBefore, 0u); // every condition before the pick was answered from data
        // after the quest moves, the other info is picked
        w.mState.setStage(w.mQuests, kQuestA, 20);
        const Selection t = selectInfo(w.mDialogue, kTopicHello, *w.mWorld, speakerA(), options);
        ASSERT_NE(t.mInfo, nullptr);
        EXPECT_EQ(t.mInfo->mResponses.at(0).mText.mText, "Stage twenty.");
    }

    TEST(SkyrimQuestSelect, theOwningQuestOfAnInfoIsTheContextOfItsAliasConditions)
    {
        // the topic belongs to quest A; its INFO names quest B in QNAM; alias 1 is the speaker only in quest B
        QuestSpec qa = simpleQuest(kQuestA, QuestFlags::StartGameEnabled, Bytes(), refAlias(1, "N", fillActor(kNpcB)));
        QuestSpec qb = simpleQuest(kQuestB, QuestFlags::StartGameEnabled, Bytes(), refAlias(1, "N", fillActor(kNpcA)));
        InfoSpec viaB;
        viaB.mId = kSelf | 0x1001;
        viaB.mQuest = kQuestB;
        viaB.mConditions.append(ctda(566, 0, 1.0f, 1));
        viaB.mText = "From B.";
        InfoSpec viaTopic;
        viaTopic.mId = kSelf | 0x1002;
        viaTopic.mConditions.append(ctda(566, 0, 1.0f, 1)); // no QNAM: the topic's quest A
        viaTopic.mText = "From A.";
        QWorld w({ qa, qb }, topicWithInfos(kTopicHello, 50.f, kQuestA, { viaTopic, viaB }));
        SelectionOptions options;
        const Selection s = selectInfo(w.mDialogue, kTopicHello, *w.mWorld, speakerA(), options);
        ASSERT_NE(s.mInfo, nullptr);
        EXPECT_EQ(s.mInfo->mResponses.at(0).mText.mText, "From B.");
        EXPECT_EQ(s.mUncertainBefore, 0u);
        EXPECT_EQ(s.mPassing, 1u);
    }

    TEST(SkyrimQuestSelect, aConditionTheStateCannotAnswerIsStillCountedUncertain)
    {
        InfoSpec first;
        first.mId = kSelf | 0x1001;
        first.mQuest = kQuestA;
        first.mConditions.append(ctda(629, 0, 1.0f, kQuestA)); // a Papyrus variable
        InfoSpec second;
        second.mId = kSelf | 0x1002;
        second.mQuest = kQuestA;
        QWorld w({ stagedQuest() }, topicWithInfos(kTopicHello, 50.f, kQuestA, { first, second }));
        SelectionOptions options;
        const Selection s = selectInfo(w.mDialogue, kTopicHello, *w.mWorld, speakerA(), options);
        // fn_629 reads a Papyrus variable the state does not hold: the first info fails and is counted uncertain
        ASSERT_NE(s.mInfo, nullptr);
        EXPECT_EQ(s.mInfo->mCommon.mId, kSelf | 0x1002);
        EXPECT_EQ(s.mUncertainBefore, 1u);
    }

    TEST(SkyrimQuestWorld, aReferenceFilledAliasIsNotAnsweredForASpeakerWithoutAReference)
    {
        Bytes aliases = refAlias(2, "Ref", fillRef(kRefB));
        aliases.append(refAlias(1, "Actor", fillActor(kNpcA)));
        QWorld w({ simpleQuest(kQuestA, QuestFlags::StartGameEnabled, Bytes(), aliases) });
        DialogueSpeaker noRef;
        noRef.mBase = kNpcB;
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 2), noRef, kQuestA), 0.f);
        EXPECT_FALSE(w.answers(ctda(566, 0, 1.0f, 2), noRef, kQuestA)); // we do not know which reference he is
        EXPECT_TRUE(w.answers(ctda(566, 0, 1.0f, 2), speakerB(), kQuestA));
        // a unique actor needs only the base
        DialogueSpeaker baseOnly;
        baseOnly.mBase = kNpcA;
        EXPECT_EQ(w.eval(ctda(566, 0, 1.0f, 1), baseOnly, kQuestA), 1.f);
        EXPECT_TRUE(w.answers(ctda(566, 0, 1.0f, 1), baseOnly, kQuestA));
    }

    TEST(SkyrimQuestSelect, aPluginThatLoadsAtAnotherIndexThanItsOwnStillResolvesItsQuestsInTheCount)
    {
        // other.esp lists only Skyrim.esm: its own index in the file is 1, but it loads at 2, so its stored 0x01xxxxxx
        // ids are 0x02xxxxxx in the load order. The uncertain count must resolve with the INFO's plugin, too.
        InfoSpec info;
        info.mId = kSelf | 0x1001;
        info.mQuest = kQuestA;
        info.mConditions.append(ctda(58, 0, 10.0f, kQuestA));
        QWorld w({ stagedQuest() }, topicWithInfos(kTopicHello, 50.f, kQuestA, { info }), true, "other.esp");
        DialogueSpeaker s;
        s.mBase = 0x02000200; // NpcA, resolved
        s.mReference = 0x020002A0;
        SelectionOptions options;
        const FormId topic = 0x02000300;
        const Selection sel = selectInfo(w.mDialogue, topic, *w.mWorld, s, options);
        ASSERT_NE(sel.mInfo, nullptr);
        EXPECT_EQ(sel.mUncertainBefore, 0u);
    }

    TEST(SkyrimQuestSelect, aQuestsOwnDialogueConditionsGateItsInfos)
    {
        // quest A's dialogue conditions: the speaker is in the faction (NpcA is, NpcB is not)
        QuestSpec qa = stagedQuest();
        qa.mHeadConditions.append(ctda(71, 0, 1.0f, kFaction));
        InfoSpec first;
        first.mId = kSelf | 0x1001;
        first.mQuest = kQuestA;
        first.mText = "Faction talk.";
        InfoSpec second;
        second.mId = kSelf | 0x1002;
        second.mQuest = kQuestB;
        second.mText = "Anyone.";
        QWorld w({ qa, simpleQuest(kQuestB, QuestFlags::StartGameEnabled) },
            topicWithInfos(kTopicHello, 50.f, kQuestA, { first, second }));
        SelectionOptions options;
        options.mQuests = &w.mQuests;
        const Selection forA = selectInfo(w.mDialogue, kTopicHello, *w.mWorld, speakerA(), options);
        ASSERT_NE(forA.mInfo, nullptr);
        EXPECT_EQ(forA.mInfo->mResponses.at(0).mText.mText, "Faction talk.");
        const Selection forB = selectInfo(w.mDialogue, kTopicHello, *w.mWorld, speakerB(), options);
        ASSERT_NE(forB.mInfo, nullptr);
        EXPECT_EQ(forB.mInfo->mResponses.at(0).mText.mText, "Anyone.");
        EXPECT_EQ(forB.mUncertainBefore, 0u);
        // without the option the quest's conditions are not asked
        const Selection plain = selectInfo(w.mDialogue, kTopicHello, *w.mWorld, speakerB(), SelectionOptions());
        ASSERT_NE(plain.mInfo, nullptr);
        EXPECT_EQ(plain.mInfo->mResponses.at(0).mText.mText, "Faction talk.");
    }

    TEST(SkyrimQuestSelect, aQuestWhoseDialogueConditionsAreDamagedHoldsBackItsInfos)
    {
        QuestSpec qa = stagedQuest();
        qa.mHeadConditions.append(sub4("CTDA", Bytes().u32(1)));
        InfoSpec first;
        first.mId = kSelf | 0x1001;
        first.mQuest = kQuestA;
        InfoSpec second;
        second.mId = kSelf | 0x1002;
        second.mQuest = kQuestB;
        second.mText = "Other.";
        QWorld w({ qa, simpleQuest(kQuestB, QuestFlags::StartGameEnabled) },
            topicWithInfos(kTopicHello, 50.f, kQuestA, { first, second }));
        SelectionOptions options;
        options.mQuests = &w.mQuests;
        const Selection s = selectInfo(w.mDialogue, kTopicHello, *w.mWorld, speakerA(), options);
        ASSERT_NE(s.mInfo, nullptr);
        EXPECT_EQ(s.mInfo->mResponses.at(0).mText.mText, "Other.");
    }

    TEST(SkyrimQuestParse, anAliasWithBothAReferenceAndAnActorIsFilledByTheReference)
    {
        QuestSpec q;
        q.mAliases = refAlias(1, "Both", fillActor(kNpcA).append(fillRef(kRefA)));
        const Parsed p = parse(questPlugin({ q }));
        const QuestAlias& a = p.mRecords.mQuests.at(0).mAliases.at(0);
        EXPECT_EQ(a.mFill, AliasFillKind::SpecificReference);
        EXPECT_EQ(a.mReference, kRefA);
        EXPECT_EQ(a.mActor, kNpcA);
    }

    TEST(SkyrimQuestSelect, anInfoWithoutAQuestUsesItsTopicsQuestAsTheContext)
    {
        InfoSpec noQuest;
        noQuest.mId = kSelf | 0x1001; // no QNAM: the topic's quest A, where alias 1 is NpcA
        noQuest.mConditions.append(ctda(566, 0, 1.0f, 1));
        noQuest.mText = "Topic quest.";
        QWorld w({ simpleQuest(kQuestA, QuestFlags::StartGameEnabled, Bytes(), refAlias(1, "N", fillActor(kNpcA))) },
            topicWithInfos(kTopicHello, 50.f, kQuestA, { noQuest }));
        const Selection s = selectInfo(w.mDialogue, kTopicHello, *w.mWorld, speakerA(), SelectionOptions());
        ASSERT_NE(s.mInfo, nullptr);
        EXPECT_EQ(s.mInfo->mResponses.at(0).mText.mText, "Topic quest.");
        EXPECT_EQ(s.mUncertainBefore, 0u);
        const Selection other = selectInfo(w.mDialogue, kTopicHello, *w.mWorld, speakerB(), SelectionOptions());
        EXPECT_EQ(other.mInfo, nullptr);
    }
}
