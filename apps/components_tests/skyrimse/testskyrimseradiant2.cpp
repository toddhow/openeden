// Task 0217: radiant quests that start by themselves and read right in the journal. (1) the story walk: DNAM is two 16
// bit halves (shares event 0x20000, do all 0x10000, random 0x1), quest nodes with the random bit, the children of a
// node as a forest of the plugins' sibling chains; (2) what scripts store on locations (Location.SetKeywordData /
// GetKeywordData, LocationAlias.GetLocation) and what fn_606 and fn_651 read; (3) alias names in the journal's texts;
// (4) the story manager's state as a save body. Generated records, scripts and plugins; no game data. The raw DNAM
// numbers are written out on purpose (not the StoryFlags constants), so the tests pin the data's layout. Written first
// and run red against stubs (see the task's stage log).
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/journal.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/story.hpp>
#include <game_skyrimse/story_state.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <tuple>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::ScriptBuilder;
    using PexAsm::Str;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kQuestA = kSelf | 0x800;
    constexpr FormId kQuestB = kSelf | 0x801;
    constexpr FormId kQuestC = kSelf | 0x802;
    constexpr FormId kQuestD = kSelf | 0x803;
    constexpr FormId kQuestE = kSelf | 0x804;
    constexpr FormId kQuestF = kSelf | 0x805;
    constexpr FormId kRoot = kSelf | 0x1000;
    constexpr FormId kEvent = kSelf | 0x1001;
    constexpr FormId kBranch = kSelf | 0x1002;
    constexpr FormId kNodeA = kSelf | 0x1003;
    constexpr FormId kNodeB = kSelf | 0x1004;
    constexpr FormId kNodeC = kSelf | 0x1005;
    constexpr FormId kNodeD = kSelf | 0x1006;
    constexpr FormId kNodeE = kSelf | 0x1007;
    constexpr FormId kNodeF = kSelf | 0x1008;
    constexpr FormId kTown = kSelf | 0x500;
    constexpr FormId kInn = kSelf | 0x501;
    constexpr FormId kKeywordA = kSelf | 0x600;
    constexpr FormId kKeywordB = kSelf | 0x601;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    // ---- 1. the story walk ----------------------------------------------------------------------------------------

    Bytes nodeCommon(const std::string& name, FormId parent, FormId sibling, std::uint32_t dnam)
    {
        Bytes b;
        b.append(edid(name));
        b.append(sub4("PNAM", Bytes().u32(parent)));
        b.append(sub4("SNAM", Bytes().u32(sibling)));
        b.append(sub4("CITC", Bytes().u32(0)));
        b.append(sub4("DNAM", Bytes().u32(dnam)));
        b.append(sub4("XNAM", Bytes().u32(0)));
        return b;
    }

    Bytes smen(FormId id, const std::string& event)
    {
        Bytes b = nodeCommon("Ev" + std::to_string(id & 0xFFF), kRoot, 0, 0);
        b.append(sub4("ENAM", Bytes().text(event)));
        return record4("SMEN", 0u, id, b);
    }

    Bytes smbn(FormId id, FormId parent, FormId sibling, std::uint32_t dnam = 0)
    {
        return record4("SMBN", 0u, id, nodeCommon("Br" + std::to_string(id & 0xFFF), parent, sibling, dnam));
    }

    Bytes smqn(FormId id, FormId parent, FormId sibling, const std::vector<FormId>& quests, std::uint32_t dnam)
    {
        Bytes b = nodeCommon("Qn" + std::to_string(id & 0xFFF), parent, sibling, dnam);
        b.append(sub4("QNAM", Bytes().u32(static_cast<std::uint32_t>(quests.size()))));
        for (const FormId q : quests)
            b.append(sub4("NNAM", Bytes().u32(q)));
        return record4("SMQN", 0u, id, b);
    }

    Bytes questRecord(FormId id)
    {
        Bytes b;
        b.append(edid("Q" + std::to_string(id & 0xFFF)));
        b.append(sub4("DNAM", Bytes().u16(0).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        return record4("QUST", 0u, id, b);
    }

    struct WalkRig
    {
        explicit WalkRig(const std::vector<Bytes>& nodeRecords)
        {
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes all;
            for (const Bytes& r : nodeRecords)
                all.append(r);
            bytes.append(group4(Content::fourCC("SMQN"), 0, all));
            StoryRecords parsed;
            Content::Error error;
            mOk = parseStoryRecordsBytes(bytes.str(), "test.esp", order(), StoryLimits(), parsed, error);
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mNodes.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            Bytes qbytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes qall;
            for (const FormId q : { kQuestA, kQuestB, kQuestC, kQuestD, kQuestE, kQuestF })
                qall.append(questRecord(q));
            qbytes.append(group4(Content::fourCC("QUST"), 0, qall));
            QuestRecords quests;
            EXPECT_TRUE(parseQuestRecordsBytes(qbytes.str(), "test.esp", order(), QuestLimits(), quests, error));
            std::unique_ptr<Content::Plugin> qplugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(qbytes.str(), std::ios::binary), "test.esp", error);
            mQuests.add(std::move(quests), FormIdResolver(qplugin->header(), "test.esp", order()));
            mState = QuestState::initial(mQuests);
            mManager = std::make_unique<StoryManager>(mNodes, mQuests);
        }
        std::vector<FormId> raise(const std::string& type = "SCPT")
        {
            StoryEvent e;
            e.mType = type;
            return mManager->raise(
                e, [](const std::vector<survey::Ctda>&, const FormIdResolver&) { return true; },
                [this](FormId q) { return !mState.running(q) && !mState.completed(q); });
        }
        bool mOk = false;
        StoryIndex mNodes;
        QuestIndex mQuests;
        QuestState mState;
        std::unique_ptr<StoryManager> mManager;
    };

    TEST(SkyrimRadiant2Walk, aQuestNodeWithTheSharesBitOfTheHighHalfLetsTheWalkGoOn)
    {
        // 0x20000: "shares event" in the quest flags (the data names such nodes "...SHARES")
        WalkRig rig({ smen(kEvent, "SCPT"), smqn(kNodeA, kEvent, 0, { kQuestA }, 0x20000),
            smqn(kNodeB, kEvent, kNodeA, { kQuestB }, 0) });
        ASSERT_TRUE(rig.mOk);
        EXPECT_EQ(rig.raise(), (std::vector<FormId>{ kQuestA, kQuestB }));
    }

    TEST(SkyrimRadiant2Walk, theLowBitTwoIsTheNodeWarningAndDoesNotShareTheEvent)
    {
        WalkRig rig({ smen(kEvent, "SCPT"), smqn(kNodeA, kEvent, 0, { kQuestA }, 0x2),
            smqn(kNodeB, kEvent, kNodeA, { kQuestB }, 0) });
        EXPECT_EQ(rig.raise(), (std::vector<FormId>{ kQuestA }));
    }

    TEST(SkyrimRadiant2Walk, doAllBeforeRepeatingIsTheLowBitOfTheHighHalf)
    {
        WalkRig rig({ smen(kEvent, "SCPT"), smqn(kNodeA, kEvent, 0, { kQuestA, kQuestB }, 0x10000) });
        EXPECT_EQ(rig.raise(), (std::vector<FormId>{ kQuestA }));
        EXPECT_EQ(rig.raise(), (std::vector<FormId>{ kQuestB })) << "the cycle did not move on to the other quest";
        EXPECT_EQ(rig.raise(), (std::vector<FormId>{ kQuestA })) << "a finished cycle starts again";
    }

    TEST(SkyrimRadiant2Walk, aQuestNodeWithTheRandomBitTriesItsQuestsInASeededOrder)
    {
        const std::vector<Bytes> records = { smen(kEvent, "SCPT"),
            smqn(kNodeA, kEvent, 0, { kQuestA, kQuestB, kQuestC, kQuestD, kQuestE, kQuestF }, 0x1) };
        WalkRig a(records);
        WalkRig b(records);
        std::set<FormId> firsts;
        std::vector<FormId> inOrder;
        for (int i = 0; i < 12; ++i)
        {
            const std::vector<FormId> x = a.raise();
            const std::vector<FormId> y = b.raise();
            ASSERT_EQ(x.size(), 1u);
            EXPECT_EQ(x, y) << "the order must depend only on the event number";
            firsts.insert(x[0]);
            if (i < 6)
                inOrder.push_back(x[0]);
        }
        EXPECT_GE(firsts.size(), 2u) << "a random quest node must not always start its first quest";
        // no "do all": the node does not rotate through the list in file order (the old reading of bit 0)
        EXPECT_NE(inOrder, (std::vector<FormId>{ kQuestA, kQuestB, kQuestC, kQuestD, kQuestE, kQuestF }));
    }

    TEST(SkyrimRadiant2Walk, aRandomDoAllNodeStartsEveryQuestOnceBeforeAnyRepeats)
    {
        // the BQ nodes of the data are 0x30001: random, do all, shares
        WalkRig rig({ smen(kEvent, "SCPT"), smqn(kNodeA, kEvent, 0, { kQuestA, kQuestB, kQuestC, kQuestD }, 0x30001) });
        std::set<FormId> seen;
        for (int i = 0; i < 4; ++i)
        {
            const std::vector<FormId> x = rig.raise();
            ASSERT_EQ(x.size(), 1u);
            EXPECT_TRUE(seen.insert(x[0]).second) << "a quest repeated inside one cycle";
        }
        EXPECT_EQ(seen.size(), 4u);
    }

    TEST(SkyrimRadiant2Walk, theChildrenOfANodeAreTheForestOfTheChainsInPreOrder)
    {
        // two chains meet under one event node, as Skyrim.esm's and Dragonborn's do: heads A (prev 0) and B (prev 0),
        // D after A, C after B, and a fork E and F both after D. The form id order would be A B C D E F.
        WalkRig rig({ smen(kEvent, "SCPT"), smqn(kNodeA, kEvent, 0, { kQuestA }, 0),
            smqn(kNodeB, kEvent, 0, { kQuestB }, 0), smqn(kNodeC, kEvent, kNodeB, { kQuestC }, 0),
            smqn(kNodeD, kEvent, kNodeA, { kQuestD }, 0), smqn(kNodeF, kEvent, kNodeD, { kQuestF }, 0),
            smqn(kNodeE, kEvent, kNodeD, { kQuestE }, 0) });
        EXPECT_EQ(rig.mNodes.children(kEvent),
            (std::vector<FormId>{ kNodeA, kNodeD, kNodeE, kNodeF, kNodeB, kNodeC }));
    }

    TEST(SkyrimRadiant2Walk, aSecondChainIsReachedWhenTheFirstSharesTheEvent)
    {
        WalkRig rig({ smen(kEvent, "SCPT"), smbn(kBranch, kEvent, 0), smqn(kNodeA, kBranch, 0, { kQuestA }, 0x20000),
            smqn(kNodeB, kEvent, 0, { kQuestB }, 0) });
        EXPECT_EQ(rig.raise(), (std::vector<FormId>{ kQuestA, kQuestB }));
    }

    // ---- 2. what scripts store on locations ------------------------------------------------------------------------

    TEST(SkyrimRadiant2KeywordData, aStoredValueIsReadBackAndTheDefaultIsZero)
    {
        LocationKeywordData data;
        EXPECT_EQ(data.get(kTown, kKeywordA), 0.f);
        EXPECT_TRUE(data.set(kTown, kKeywordA, 2.5f));
        EXPECT_EQ(data.get(kTown, kKeywordA), 2.5f);
        EXPECT_EQ(data.get(kTown, kKeywordB), 0.f);
        EXPECT_EQ(data.get(kInn, kKeywordA), 0.f);
        EXPECT_TRUE(data.set(kTown, kKeywordA, -1.f));
        EXPECT_EQ(data.get(kTown, kKeywordA), -1.f);
        EXPECT_EQ(data.size(), 1u);
    }

    TEST(SkyrimRadiant2KeywordData, nothingIsStoredForANullFormOrANonFiniteValue)
    {
        LocationKeywordData data;
        EXPECT_FALSE(data.set(kNullFormId, kKeywordA, 1.f));
        EXPECT_FALSE(data.set(kTown, kNullFormId, 1.f));
        EXPECT_FALSE(data.set(kUnresolvedFormId, kKeywordA, 1.f));
        EXPECT_FALSE(data.set(kTown, kKeywordA, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_FALSE(data.set(kTown, kKeywordA, std::numeric_limits<float>::infinity()));
        EXPECT_EQ(data.size(), 0u);
    }

    TEST(SkyrimRadiant2KeywordData, theTableIsCappedButAnExistingEntryStillChanges)
    {
        LocationKeywordData data;
        for (std::uint32_t i = 0; i < LocationKeywordData::kMaxEntries; ++i)
            ASSERT_TRUE(data.set(kSelf | (0x10000u + i), kKeywordA, 1.f)) << i;
        EXPECT_FALSE(data.set(kSelf | 0xF00000u, kKeywordA, 1.f));
        EXPECT_TRUE(data.set(kSelf | 0x10000u, kKeywordA, 7.f));
        EXPECT_EQ(data.get(kSelf | 0x10000u, kKeywordA), 7.f);
        EXPECT_EQ(data.size(), LocationKeywordData::kMaxEntries);
    }

    TEST(SkyrimRadiant2KeywordData, entriesAreListedInAscendingOrder)
    {
        LocationKeywordData data;
        (void)data.set(kInn, kKeywordB, 3.f);
        (void)data.set(kTown, kKeywordB, 2.f);
        (void)data.set(kTown, kKeywordA, 1.f);
        const auto list = data.entries();
        ASSERT_EQ(list.size(), 3u);
        EXPECT_EQ(list[0], std::make_tuple(kTown, kKeywordA, 1.f));
        EXPECT_EQ(list[1], std::make_tuple(kTown, kKeywordB, 2.f));
        EXPECT_EQ(list[2], std::make_tuple(kInn, kKeywordB, 3.f));
    }

    // ---- the conditions -----------------------------------------------------------------------------------------------

    Bytes lctn(FormId id, FormId parent, const std::vector<FormId>& keywords)
    {
        Bytes b;
        b.append(edid("L" + std::to_string(id & 0xFFF)));
        if (parent != 0)
            b.append(sub4("PNAM", Bytes().u32(parent)));
        if (!keywords.empty())
        {
            b.append(sub4("KSIZ", Bytes().u32(static_cast<std::uint32_t>(keywords.size()))));
            Bytes kw;
            for (const FormId k : keywords)
                kw.u32(k);
            b.append(sub4("KWDA", kw));
        }
        return record4("LCTN", 0u, id, b);
    }

    survey::Ctda cond(std::uint16_t function, std::uint32_t p1, std::uint32_t p2, std::uint8_t compare, float value)
    {
        survey::Ctda c;
        c.mFunction = function;
        c.mParam1 = p1;
        c.mParam2 = p2;
        c.mCompare = compare;
        c.mComparison = value;
        return c;
    }

    struct ConditionRig
    {
        ConditionRig()
        {
            Bytes recs = lctn(kTown, 0, {});
            recs.append(lctn(kInn, kTown, {}));
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("LCTN"), 0, recs));
            Content::Error error;
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            LocationRecords parsed;
            EXPECT_TRUE(parseLocationRecords(*plugin, order(), LocationLimits(), parsed));
            mIndex.add(std::move(parsed));
            mWorld.setLocations(&mIndex);
        }
        bool holds(const survey::Ctda& c)
        {
            return mEvaluator.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        LocationIndex mIndex;
        LocationKeywordData mData;
        conditions::Evaluator mEvaluator;
    };

    TEST(SkyrimRadiant2Conditions, getKeywordDataForLocationReadsWhatScriptsStored)
    {
        ConditionRig rig;
        rig.mWorld.setKeywordData(&rig.mData);
        // "== 0" holds before anything is stored, "== 2" afterwards; another location or keyword stays 0
        EXPECT_TRUE(rig.holds(cond(606, kTown, kKeywordA, 0, 0.f)));
        EXPECT_FALSE(rig.holds(cond(606, kTown, kKeywordA, 0, 2.f)));
        ASSERT_TRUE(rig.mData.set(kTown, kKeywordA, 2.f));
        EXPECT_TRUE(rig.holds(cond(606, kTown, kKeywordA, 0, 2.f)));
        EXPECT_FALSE(rig.holds(cond(606, kTown, kKeywordA, 0, 0.f)));
        EXPECT_TRUE(rig.holds(cond(606, kTown, kKeywordB, 0, 0.f)));
        EXPECT_TRUE(rig.holds(cond(606, kInn, kKeywordA, 0, 0.f)));
    }

    TEST(SkyrimRadiant2Conditions, withNoStoreTheAnswerIsStillZero)
    {
        ConditionRig rig;
        EXPECT_TRUE(rig.holds(cond(606, kTown, kKeywordA, 0, 0.f)));
        EXPECT_FALSE(rig.holds(cond(606, kTown, kKeywordA, 0, 1.f)));
    }

    TEST(SkyrimRadiant2Conditions, getKeywordDataForCurrentLocationReadsTheSpeakersLocation)
    {
        ConditionRig rig;
        rig.mWorld.setKeywordData(&rig.mData);
        DialogueSpeaker s;
        s.mBase = kSelf | 0x200;
        s.mLocationKnown = true;
        s.mLocation = kInn;
        rig.mWorld.setSpeaker(s);
        ASSERT_TRUE(rig.mData.set(kInn, kKeywordA, 3.f));
        ASSERT_TRUE(rig.mData.set(kTown, kKeywordA, 9.f));
        EXPECT_TRUE(rig.holds(cond(651, kKeywordA, 0, 0, 3.f)));
        EXPECT_FALSE(rig.holds(cond(651, kKeywordA, 0, 0, 9.f)));
    }

    // ---- the Papyrus natives ---------------------------------------------------------------------------------------

    class FakeData final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string& name, std::vector<std::uint8_t>& bytes) override
        {
            const auto it = mScripts.find(name);
            if (it == mScripts.end())
                return false;
            bytes = it->second;
            return true;
        }
        std::string formClass(FormId form) const override
        {
            const auto it = mClasses.find(form);
            return it != mClasses.end() ? it->second : std::string();
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        FormId locationOf(FormId reference) const override
        {
            const auto it = mLocationOf.find(reference);
            return it != mLocationOf.end() ? it->second : 0;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<FormId, FormId> mLocationOf;
    };

    constexpr FormId kQuestHolder = kSelf | 0x810;
    constexpr FormId kRef = kSelf | 0xA01;
    constexpr FormId kBase = kSelf | 0xB00;

    ScriptBuilder probe()
    {
        ScriptBuilder s("Probe");
        s.parent("ObjectReference");
        s.autoProperty("Loc", "Location");
        s.autoProperty("Kw", "Keyword");
        s.autoProperty("TheAlias", "LocationAlias");
        s.var("got", "Float");
        s.var("seen", "Int");
        s.var("flag", "Bool");
        s.var("stage", "Int");
        s.function("WhereAmI")
            .local("l", "Location")
            .callMethod("GetCurrentLocation", Id("self"), Id("l"))
            .local("n", "Int")
            .callMethod("GetFormID", Id("l"), Id("n"))
            .assign(Id("seen"), Id("n"));
        s.function("Misc")
            .local("b", "Bool")
            .callMethod("RegisterForAnimationEvent", Id("self"), Id("b"), { Id("self"), Str("Foo") })
            .assign(Id("flag"), Id("b"))
            .local("d", "Int")
            .callMethod("GetCurrentDestructionStage", Id("self"), Id("d"))
            .assign(Id("stage"), Id("d"));
        s.function("SetIt")
            .local("n", "None")
            .callMethod("SetKeywordData", Id("::Loc_var"), Id("n"), { Id("::Kw_var"), Flt(2.5f) });
        s.function("GetIt")
            .local("v", "Float")
            .callMethod("GetKeywordData", Id("::Loc_var"), Id("v"), { Id("::Kw_var") })
            .assign(Id("got"), Id("v"));
        s.function("ReadAlias")
            .local("l", "Location")
            .callMethod("GetLocation", Id("::TheAlias_var"), Id("l"))
            .local("n", "Int")
            .callMethod("GetFormID", Id("l"), Id("n"))
            .assign(Id("seen"), Id("n"));
        return s;
    }

    vmad::Property formProp(const std::string& name, FormId form, std::int16_t alias = -1)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = alias;
        return p;
    }

    Bytes locationAliasQuest()
    {
        Bytes b;
        b.append(edid("Holder"));
        b.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        b.append(sub4("ALLS", Bytes().u32(7)));
        b.append(sub4("ALID", Bytes().zstring("Dungeon")));
        b.append(sub4("FNAM", Bytes().u32(0)));
        b.append(sub4("ALED", Bytes()));
        return record4("QUST", 0u, kQuestHolder, b);
    }

    struct HostRig
    {
        HostRig()
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            form.function("RegisterForAnimationEvent", "Bool")
                .native()
                .param("akSender", "Form")
                .param("asEventName", "String");
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            ref.function("GetCurrentLocation", "Location").native();
            ref.function("GetCurrentDestructionStage", "Int").native();
            mData.add(ref);
            ScriptBuilder keyword("Keyword");
            keyword.parent("Form");
            mData.add(keyword);
            ScriptBuilder location("Location");
            location.parent("Form");
            location.function("SetKeywordData").native().param("akKeyword", "Keyword").param("afData", "Float");
            location.function("GetKeywordData", "Float").native().param("akKeyword", "Keyword");
            mData.add(location);
            ScriptBuilder quest("Quest");
            quest.parent("Form");
            mData.add(quest);
            ScriptBuilder alias("Alias");
            alias.function("GetOwningQuest", "Quest").native();
            mData.add(alias);
            ScriptBuilder locAlias("LocationAlias");
            locAlias.parent("Alias");
            locAlias.function("GetLocation", "Location").native();
            mData.add(locAlias);
            mData.add(probe());
            mData.mClasses[kQuestHolder] = "Quest";
            mData.mClasses[kTown] = "Location";
            mData.mClasses[kInn] = "Location";
            mData.mClasses[kKeywordA] = "Keyword";
            mData.mClasses[kRef] = "ObjectReference";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("QUST"), 0, locationAliasQuest()));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
        }
        ~HostRig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void attach(FormId location, std::int16_t aliasId = -1)
        {
            vmad::Script s;
            s.mName = "Probe";
            s.mProperties = { formProp("Loc", location), formProp("Kw", kKeywordA) };
            if (aliasId >= 0)
                s.mProperties.push_back(formProp("TheAlias", kQuestHolder, aliasId));
            ASSERT_EQ(mHost->attachReference(kRef, kBase, false, { std::move(s) }), 1u);
            tick();
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        bool call(const char* function)
        {
            const bool ok = mHost->callFunction(kRef, "Probe", function);
            tick();
            return ok;
        }
        FakeData mData;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimRadiant2Natives, setKeywordDataStoresAndGetKeywordDataReadsIt)
    {
        HostRig rig;
        LocationKeywordData data;
        rig.mHost->setLocationKeywordData(&data);
        rig.attach(kTown);
        ASSERT_TRUE(rig.call("SetIt"));
        EXPECT_EQ(data.get(kTown, kKeywordA), 2.5f);
        ASSERT_TRUE(data.set(kTown, kKeywordA, 4.f));
        ASSERT_TRUE(rig.call("GetIt"));
        EXPECT_EQ(rig.mHost->numberVariable(kRef, "Probe", "got"), std::optional<double>(4.0));
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimRadiant2Natives, withNoStoreTheLocationNativesAreTolerant)
    {
        HostRig rig;
        rig.attach(kTown);
        ASSERT_TRUE(rig.call("SetIt"));
        ASSERT_TRUE(rig.call("GetIt"));
        EXPECT_EQ(rig.mHost->numberVariable(kRef, "Probe", "got"), std::optional<double>(0.0));
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimRadiant2Natives, aLocationAliasGivesTheLocationThatFillsIt)
    {
        HostRig rig;
        rig.attach(kTown, 7);
        AliasFill fill;
        fill.mKnown = true;
        fill.mReference = kInn; // a location alias keeps its location in the reference word
        rig.mState.setAliasFill(kQuestHolder, 7, fill);
        ASSERT_TRUE(rig.call("ReadAlias"));
        EXPECT_EQ(rig.mHost->numberVariable(kRef, "Probe", "seen"), std::optional<double>(kInn));
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimRadiant2Natives, getCurrentLocationAsksTheDataForTheReferencesLocation)
    {
        HostRig rig;
        rig.mData.mLocationOf[kRef] = kInn;
        rig.attach(kTown);
        ASSERT_TRUE(rig.call("WhereAmI"));
        EXPECT_EQ(rig.mHost->numberVariable(kRef, "Probe", "seen"), std::optional<double>(kInn));
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimRadiant2Natives, aReferenceWithNoLocationGivesNone)
    {
        HostRig rig;
        rig.attach(kTown);
        ASSERT_TRUE(rig.call("WhereAmI"));
        EXPECT_EQ(rig.mHost->numberVariable(kRef, "Probe", "seen"), std::optional<double>(0.0));
    }

    TEST(SkyrimRadiant2Natives, theAnimationEventAndDestructionNativesAreAnsweredNotTolerated)
    {
        HostRig rig;
        rig.attach(kTown);
        ASSERT_TRUE(rig.call("Misc"));
        EXPECT_EQ(rig.mHost->numberVariable(kRef, "Probe", "flag"), std::optional<double>(1.0));
        EXPECT_EQ(rig.mHost->numberVariable(kRef, "Probe", "stage"), std::optional<double>(0.0));
        EXPECT_EQ(rig.mHost->tolerantCalls(), 0u) << "these natives must be registered, not left to the tolerant path";
    }

    TEST(SkyrimRadiant2Natives, anUnfilledLocationAliasGivesNone)
    {
        HostRig rig;
        rig.attach(kTown, 7);
        ASSERT_TRUE(rig.call("ReadAlias"));
        EXPECT_EQ(rig.mHost->numberVariable(kRef, "Probe", "seen"), std::optional<double>(0.0));
    }

    // ---- 3. alias names in the journal ---------------------------------------------------------------------------------

    TEST(SkyrimRadiant2Journal, anAliasTagBecomesTheNameOfWhatFillsTheAlias)
    {
        const journal::AliasNameResolver names = [](std::string_view alias) -> std::string {
            if (alias == "QuestGiver")
                return "Arngeir";
            if (alias == "Dungeon")
                return "Bleak Falls Barrow";
            return {};
        };
        EXPECT_EQ(
            journal::expandTags("<Alias=QuestGiver> has sent me to destroy the <Alias=Boss> at <Alias=Dungeon>.", names),
            "Arngeir has sent me to destroy the ... at Bleak Falls Barrow.");
        EXPECT_EQ(journal::expandTags("See <Alias.ShortName=QuestGiver>", names), "See Arngeir");
        EXPECT_EQ(journal::expandTags("pay <Global=Gold>", names), "pay ...");
        EXPECT_EQ(journal::expandTags("a < b", names), "a  b");
    }

    TEST(SkyrimRadiant2Journal, withNoResolverTheAliasNameStays)
    {
        EXPECT_EQ(journal::expandTags("found <Alias=Staff>."), "found Staff.");
        EXPECT_EQ(journal::expandTags("found <Alias=Staff>.", journal::AliasNameResolver()), "found Staff.");
    }

    LString plain(const std::string& text)
    {
        LString s;
        s.mPresent = true;
        s.mText = text;
        return s;
    }

    TEST(SkyrimRadiant2Journal, theSnapshotShowsTheNamesTheQuestsAliasesHave)
    {
        QuestRecord q;
        q.mCommon.mId = kQuestA;
        q.mCommon.mEditorId = "Q800";
        q.mName = plain("Culling");
        q.mFlags = QuestFlags::StartGameEnabled;
        QuestStage stage;
        stage.mIndex = 10;
        QuestLogEntry e;
        e.mText = plain("<Alias=QuestGiver> has sent me to destroy the <Alias=Boss> at <Alias=Dungeon>.");
        stage.mEntries.push_back(e);
        q.mStages.push_back(stage);
        QuestObjective o;
        o.mIndex = 10;
        o.mText = plain("Kill the <Alias=Boss> at <Alias=Dungeon>");
        q.mObjectives.push_back(o);
        QuestRecords records;
        records.mQuests.push_back(q);
        QuestIndex index;
        index.add(std::move(records), FormIdResolver(Content::Header(), "test.esp", order()));
        QuestState state = QuestState::initial(index);
        state.setStage(index, kQuestA, 10);
        journal::Inputs in;
        in.mQuests = &index;
        in.mState = &state;
        in.mObjective = [](FormId, std::uint16_t) {
            journal::ObjectiveFlags f;
            f.mDisplayed = true;
            return f;
        };
        in.mAliasName = [](FormId quest, std::string_view alias) -> std::string {
            if (quest != kQuestA)
                return {};
            if (alias == "QuestGiver")
                return "Arngeir";
            if (alias == "Dungeon")
                return "Bleak Falls Barrow";
            return {};
        };
        journal::Snapshot snapshot;
        journal::build(in, snapshot);
        ASSERT_EQ(snapshot.mRunning.size(), 1u);
        EXPECT_EQ(snapshot.mRunning[0].mEntry, "Arngeir has sent me to destroy the ... at Bleak Falls Barrow.");
        ASSERT_EQ(snapshot.mRunning[0].mObjectives.size(), 1u);
        EXPECT_EQ(snapshot.mRunning[0].mObjectives[0].mText, "Kill the ... at Bleak Falls Barrow");
    }

    // ---- 4. the story manager's state in a save ------------------------------------------------------------------------

    StoryState sampleState()
    {
        StoryState s;
        s.mManager.mEventCounter = 41;
        s.mManager.mCycle = { { kNodeA, { kQuestA, kQuestC } }, { kNodeB, { kQuestB } } };
        s.mRan = { kQuestB, kQuestD };
        s.mKeywordData = { { kTown, kKeywordA, 2.5f }, { kInn, kKeywordB, -1.f } };
        return s;
    }

    TEST(SkyrimRadiant2State, theStateRoundTripsAndTheBytesDoNotDependOnTheOrderGiven)
    {
        const StoryState state = sampleState();
        const std::vector<std::uint8_t> bytes = encodeStoryState(state);
        ASSERT_FALSE(bytes.empty());
        StoryState shuffled = state;
        std::reverse(shuffled.mRan.begin(), shuffled.mRan.end());
        std::reverse(shuffled.mManager.mCycle.begin(), shuffled.mManager.mCycle.end());
        std::reverse(shuffled.mKeywordData.begin(), shuffled.mKeywordData.end());
        EXPECT_EQ(encodeStoryState(shuffled), bytes);
        StoryState back;
        std::string error;
        ASSERT_TRUE(decodeStoryState(bytes, back, error)) << error;
        EXPECT_EQ(back, state);
    }

    TEST(SkyrimRadiant2State, anEmptyStateIsAFewBytesAndRoundTrips)
    {
        const std::vector<std::uint8_t> bytes = encodeStoryState(StoryState());
        EXPECT_GT(bytes.size(), 0u);
        EXPECT_LT(bytes.size(), 64u);
        StoryState back = sampleState();
        std::string error;
        ASSERT_TRUE(decodeStoryState(bytes, back, error)) << error;
        EXPECT_EQ(back, StoryState());
    }

    TEST(SkyrimRadiant2State, everyTruncationAndTrailingByteIsRefusedWithAnError)
    {
        const std::vector<std::uint8_t> bytes = encodeStoryState(sampleState());
        for (std::size_t n = 0; n < bytes.size(); ++n)
        {
            StoryState out = sampleState();
            std::string error;
            EXPECT_FALSE(decodeStoryState(std::span<const std::uint8_t>(bytes.data(), n), out, error)) << n;
            EXPECT_FALSE(error.empty()) << n;
            EXPECT_EQ(out, StoryState()) << "a refused body must leave nothing behind (" << n << ")";
        }
        std::vector<std::uint8_t> longer = bytes;
        longer.push_back(0);
        StoryState out;
        std::string error;
        EXPECT_FALSE(decodeStoryState(longer, out, error));
    }

    TEST(SkyrimRadiant2State, aCountOverTheCapOrANonFiniteValueIsRefused)
    {
        StoryState big;
        for (std::uint32_t i = 0; i < kMaxStoryRan + 1; ++i)
            big.mRan.push_back(kSelf | (0x1000u + i));
        const std::vector<std::uint8_t> bytes = encodeStoryState(big);
        StoryState out;
        std::string error;
        EXPECT_FALSE(decodeStoryState(bytes, out, error));
        StoryState bad = sampleState();
        std::get<2>(bad.mKeywordData[0]) = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(decodeStoryState(encodeStoryState(bad), out, error));
    }

    TEST(SkyrimRadiant2State, theManagerKeepsItsRotationAcrossASaveAndALoad)
    {
        const std::vector<Bytes> records
            = { smen(kEvent, "SCPT"), smqn(kNodeA, kEvent, 0, { kQuestA, kQuestB, kQuestC }, 0x10000) };
        WalkRig first(records);
        EXPECT_EQ(first.raise(), (std::vector<FormId>{ kQuestA }));
        const StoryManagerState saved = first.mManager->saveState();
        EXPECT_EQ(saved.mEventCounter, 1u);
        ASSERT_EQ(saved.mCycle.size(), 1u);
        EXPECT_EQ(saved.mCycle[0].first, kNodeA);
        WalkRig second(records);
        second.mManager->restoreState(saved);
        EXPECT_EQ(second.raise(), (std::vector<FormId>{ kQuestB }))
            << "the loaded manager started the first quest again";
        EXPECT_EQ(second.mManager->saveState().mEventCounter, 2u);
    }

    TEST(SkyrimRadiant2State, aRestoredCounterGivesTheSameRandomOrderAsAnUninterruptedRun)
    {
        const std::vector<Bytes> records = { smen(kEvent, "SCPT"),
            smqn(kNodeA, kEvent, 0, { kQuestA, kQuestB, kQuestC, kQuestD, kQuestE, kQuestF }, 0x1) };
        WalkRig straight(records);
        std::vector<FormId> expected;
        for (int i = 0; i < 8; ++i)
            expected.push_back(straight.raise().at(0));
        WalkRig resumed(records);
        std::vector<FormId> got;
        for (int i = 0; i < 3; ++i)
            got.push_back(resumed.raise().at(0));
        const StoryManagerState saved = resumed.mManager->saveState();
        WalkRig loaded(records);
        loaded.mManager->restoreState(saved);
        for (int i = 3; i < 8; ++i)
            got.push_back(loaded.raise().at(0));
        EXPECT_EQ(got, expected);
    }
}

// ---- the walk on the player's own masters (gated; prints the official editor ids of what a walk tries) ------------------

#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace
{
    TEST(SkyrimRadiant2Real, theWalkAtWhiterunContinuesPastNodesThatShareTheEvent)
    {
        namespace fs = std::filesystem;
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            if (fs::exists(data / name))
                paths.push_back(data / name);
        ASSERT_FALSE(paths.empty());
        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);
        SkyrimWorldView::PapyrusAccess access;
        for (std::uint64_t spins = 0; !view.papyrusAccess(access) && spins < 600; ++spins)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_NE(access.mStory, nullptr);
        const bool verbose = std::getenv("OPENEDEN_STORY_TRACE") != nullptr;
        if (verbose)
            view.setStoryTrace([](const std::string& line) { std::cout << "SKYRIMSE-RAD2 " << line << "\n"; });
        // (location left, location entered, what must come out)
        struct Case
        {
            FormId mLeft, mEntered;
            bool mFavor;
        };
        // out of the Bannered Mare (a dwelling) into Whiterun, into Whiterun from the plains (no location left: the
        // favour nodes apply), and into the hold
        for (const Case c : { Case{ 0x0001F86Du, 0x00018A56u, false }, Case{ 0u, 0x00018A56u, true },
                 Case{ 0x0001F86Du, 0x00016772u, false } })
        {
            StoryEvent event;
            event.mType = "CLOC";
            event.mLocation1 = c.mLeft;
            event.mLocation2 = c.mEntered;
            const std::vector<FormId> chosen = access.mStory(event);
            std::cout << "SKYRIMSE-RAD2 CLOC " << std::hex << c.mLeft << " -> " << c.mEntered << std::dec << " chooses "
                      << chosen.size() << ":";
            bool favor = false;
            for (const FormId q : chosen)
            {
                const std::string& id = access.mQuests->quest(q)->mCommon.mEditorId;
                std::cout << " " << id;
                favor = favor || id.rfind("Favor", 0) == 0;
            }
            std::cout << "\n";
            // a node that shares the event must let the walk reach a second quest
            EXPECT_GE(chosen.size(), c.mEntered == 0x00018A56u ? 2u : 1u);
            // Whiterun is a habitation: the favour node (radiant favours) is behind nodes that must see the event after
            // the first quest was tried; it applies to an arrival from a place that is not a dwelling
            if (c.mFavor)
                EXPECT_TRUE(favor) << "no favour quest started when arriving at Whiterun from outside";
        }
    }
}

namespace
{
    TEST(SkyrimRadiant2Real, whiterunHasTheHabitationKeywordTheStoryNodesAskFor)
    {
        namespace fs = std::filesystem;
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;
        LoadOrder loadOrder;
        LocationIndex index;
        std::string err;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
        {
            ASSERT_TRUE(loadOrder.add(name, false, err)) << err;
            Content::Error error;
            const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::openFile(data / name, error);
            ASSERT_NE(plugin, nullptr);
            LocationRecords records;
            (void)parseLocationRecords(*plugin, loadOrder, LocationLimits(), records);
            index.add(std::move(records));
        }
        const LocationRecord* whiterun = index.location(0x00018A56u);
        ASSERT_NE(whiterun, nullptr);
        std::cout << "SKYRIMSE-RAD2 WhiterunLocation keywords " << whiterun->mKeywords.size() << " name present "
                  << whiterun->mName.mPresent << " plugin " << whiterun->mPlugin << "\n";
        EXPECT_TRUE(index.hasKeyword(0x00018A56u, 0x00039793u));
        EXPECT_TRUE(whiterun->mName.mPresent);
    }
}
