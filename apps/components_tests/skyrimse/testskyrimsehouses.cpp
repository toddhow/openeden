// Task 0261 (milestone M-S7): buying a house (Breezehome, first slice). The pack's side, over generated plugins and
// scripts, no game data: the house ledger and its save chunk (who owns a cell after Cell.SetFactionOwner), the rules
// that read it (trespass, theft, beds), the dialogue changes the steward's offer needs (a shared info's responses,
// topics that are only choices of a branch, the choice menu after a response with TCLT links, a script's live quest
// variable) and the Papyrus side (Cell.SetFactionOwner, a stage fragment gated by its log entry's conditions, a script
// variable read from outside). Written FIRST: against the parent commit none of it compiles (the new API), and against
// stubs the suites failed as the task's stage log counts.
#include <game_skyrimse/actor_mechanics.hpp>
#include <game_skyrimse/crime.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/dialogue_runtime.hpp>
#include <game_skyrimse/houses.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
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
    using PexAsm::Bool;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::Nil;
    using PexAsm::ScriptBuilder;

    constexpr FormId kCellA = 0x01000100;
    constexpr FormId kCellB = 0x01000101;
    constexpr FormId kFactionOther = 0x01000200;

    // ---- the ledger -----------------------------------------------------------------------------------------------

    TEST(SkyrimHousesLedger, aCellOwnerIsKeptAndReadBack)
    {
        houses::Ledger ledger;
        EXPECT_FALSE(ledger.cellOwner(kCellA).has_value());
        EXPECT_TRUE(ledger.setCellOwner(kCellA, kPlayerFactionId));
        ASSERT_TRUE(ledger.cellOwner(kCellA).has_value());
        EXPECT_EQ(*ledger.cellOwner(kCellA), kPlayerFactionId);
        EXPECT_FALSE(ledger.cellOwner(kCellB).has_value());
    }

    TEST(SkyrimHousesLedger, nullAndUnresolvedIdsAreRefusedAndChangeNothing)
    {
        houses::Ledger ledger;
        EXPECT_FALSE(ledger.setCellOwner(kNullFormId, kPlayerFactionId));
        EXPECT_FALSE(ledger.setCellOwner(kCellA, kNullFormId));
        EXPECT_FALSE(ledger.setCellOwner(kUnresolvedFormId, kPlayerFactionId));
        EXPECT_FALSE(ledger.setCellOwner(kCellA, kUnresolvedFormId));
        EXPECT_TRUE(ledger.data().empty());
        EXPECT_EQ(ledger.revision(), 0u);
    }

    TEST(SkyrimHousesLedger, theRevisionMovesOnlyWhenTheOwnerChanges)
    {
        houses::Ledger ledger;
        ASSERT_TRUE(ledger.setCellOwner(kCellA, kPlayerFactionId));
        const std::uint64_t first = ledger.revision();
        EXPECT_GT(first, 0u);
        EXPECT_TRUE(ledger.setCellOwner(kCellA, kPlayerFactionId));
        EXPECT_EQ(ledger.revision(), first);
        EXPECT_TRUE(ledger.setCellOwner(kCellA, kFactionOther));
        EXPECT_GT(ledger.revision(), first);
    }

    TEST(SkyrimHousesLedger, aPlacementKeepsItsOwnOwnerAndOtherwiseTakesTheCellsOne)
    {
        houses::Ledger ledger;
        ASSERT_TRUE(ledger.setCellOwner(kCellA, kPlayerFactionId));
        EXPECT_EQ(ledger.effectiveOwner(kNullFormId, kCellA), kPlayerFactionId);
        EXPECT_EQ(ledger.effectiveOwner(kFactionOther, kCellA), kFactionOther);
        EXPECT_EQ(ledger.effectiveOwner(kNullFormId, kCellB), kNullFormId);
        EXPECT_EQ(ledger.effectiveOwner(kFactionOther, kCellB), kFactionOther);
    }

    TEST(SkyrimHousesLedger, theBedFactionOfThePlayerIsThePlayersOwnFactionWhateverTheLedgerHolds)
    {
        houses::Ledger ledger;
        EXPECT_EQ(ledger.effectiveOwner(houses::kPlayerBedOwnershipFaction, kCellB), kPlayerFactionId);
        ASSERT_TRUE(ledger.setCellOwner(kCellA, kFactionOther));
        EXPECT_EQ(ledger.effectiveOwner(houses::kPlayerBedOwnershipFaction, kCellA), kPlayerFactionId);
        EXPECT_TRUE(crime::isPlayersOwner(ledger.effectiveOwner(houses::kPlayerBedOwnershipFaction, kCellA)));
    }

    TEST(SkyrimHousesLedger, enteringABoughtHouseIsNoLongerATrespass)
    {
        crime::CellAccess access;
        access.mKnown = true;
        access.mInterior = true;
        access.mOwner = kFactionOther;
        ASSERT_TRUE(crime::cellEntryIsTrespass(access));
        houses::Ledger ledger;
        ledger.applyToAccess(kCellA, access); // not in the ledger: unchanged
        EXPECT_TRUE(crime::cellEntryIsTrespass(access));
        ASSERT_TRUE(ledger.setCellOwner(kCellA, kPlayerFactionId));
        ledger.applyToAccess(kCellA, access);
        EXPECT_EQ(access.mOwner, kPlayerFactionId);
        EXPECT_FALSE(crime::cellEntryIsTrespass(access));
    }

    TEST(SkyrimHousesLedger, aFullLedgerRefusesANewCellButStillUpdatesOne)
    {
        houses::Ledger ledger;
        for (std::uint32_t i = 1; i <= houses::kMaxHouseEntries; ++i)
            ASSERT_TRUE(ledger.setCellOwner(i, kPlayerFactionId));
        EXPECT_FALSE(ledger.setCellOwner(0x7000, kPlayerFactionId));
        EXPECT_TRUE(ledger.setCellOwner(1, kFactionOther));
        EXPECT_EQ(*ledger.cellOwner(1), kFactionOther);
    }

    // ---- the save chunk -------------------------------------------------------------------------------------------

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* p = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), p, p + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(std::vector<unsigned char> bytes)
            : mBytes(std::move(bytes))
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (mAt + size > mBytes.size())
                return false;
            std::copy(mBytes.begin() + static_cast<std::ptrdiff_t>(mAt),
                mBytes.begin() + static_cast<std::ptrdiff_t>(mAt + size), static_cast<unsigned char*>(data));
            mAt += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mAt; }
        std::vector<unsigned char> mBytes;
        std::size_t mAt = 0;
    };

    std::vector<unsigned char> bytesOf(std::initializer_list<std::uint32_t> words)
    {
        std::vector<unsigned char> out;
        for (const std::uint32_t w : words)
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<unsigned char>((w >> (8 * i)) & 0xFF));
        return out;
    }

    TEST(SkyrimHousesChunk, theNameVersionAndBytesAreFixed)
    {
        std::map<FormId, FormId> data{ { 0x165A8, 0xDB1 }, { 0x165B2, 0x7 } };
        houses::HousesChunk chunk([&] { return data; }, nullptr);
        MemoryWriter w;
        chunk.write(w);
        EXPECT_EQ(w.mBytes, bytesOf({ 2, 0x165A8, 0xDB1, 0x165B2, 7 }));
        EXPECT_EQ(chunk.chunkName(), "houses");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
    }

    TEST(SkyrimHousesChunk, aWrittenLedgerIsReadBackEqual)
    {
        std::map<FormId, FormId> data{ { 5, 9 }, { 6, 10 }, { 0x165A8, 0xDB1 } };
        std::map<FormId, FormId> restored;
        houses::HousesChunk chunk([&] { return data; },
            [&](const std::map<FormId, FormId>& d) {
                restored = d;
                return true;
            });
        MemoryWriter w;
        chunk.write(w);
        MemoryReader r(w.mBytes);
        EXPECT_TRUE(chunk.read(r, houses::HousesChunk::kVersion));
        EXPECT_EQ(restored, data);
    }

    TEST(SkyrimHousesChunk, damagedChunksAreRefusedAndChangeNothing)
    {
        int sets = 0;
        houses::HousesChunk chunk([] { return std::map<FormId, FormId>(); },
            [&](const std::map<FormId, FormId>&) {
                ++sets;
                return true;
            });
        const std::vector<std::vector<unsigned char>> bad = {
            {},
            bytesOf({ 1, 5 }), // an entry cut short
            bytesOf({ 2, 6, 9, 5, 9 }), // not ascending
            bytesOf({ 2, 5, 9, 5, 9 }), // a duplicate cell
            bytesOf({ 1, 0, 9 }), // a zero cell
            bytesOf({ 1, 5, 0 }), // a zero owner
            bytesOf({ 0, 7 }), // trailing bytes
            bytesOf({ 0xFFFFFFFFu }), // a huge count
            bytesOf({ static_cast<std::uint32_t>(houses::kMaxHouseEntries) + 1 }),
        };
        for (const auto& b : bad)
        {
            MemoryReader r(b);
            EXPECT_FALSE(chunk.read(r, houses::HousesChunk::kVersion));
            EXPECT_FALSE(chunk.refusal().empty());
        }
        MemoryReader good(bytesOf({ 0 }));
        EXPECT_FALSE(chunk.read(good, 2)); // another version
        EXPECT_EQ(sets, 0);
    }

    TEST(SkyrimHousesChunk, aSetterThatRefusesRefusesTheChunk)
    {
        houses::HousesChunk chunk(
            [] { return std::map<FormId, FormId>(); }, [](const std::map<FormId, FormId>&) { return false; });
        MemoryReader r(bytesOf({ 0 }));
        EXPECT_FALSE(chunk.read(r, houses::HousesChunk::kVersion));
    }

    // ---- the dialogue -----------------------------------------------------------------------------------------------

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kNpc = kSelf | 0x200;
    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kBranch = kSelf | 0x500;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicOffer = kSelf | 0x310; // the start topic of the branch
    constexpr FormId kTopicTake = kSelf | 0x311; // a member of the branch: only a choice
    constexpr FormId kTopicLeave = kSelf | 0x312; // another member
    constexpr FormId kTopicShared = kSelf | 0x313; // a Misc topic that holds responses other INFOs share
    constexpr FormId kTopicPlain = kSelf | 0x314; // an ordinary topic, no branch

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

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0,
        const std::string& string2 = std::string())
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(0).u32(0).u32(0).u32(0);
        Bytes out = sub4("CTDA", b);
        if (!string2.empty())
            out.append(sub4("CIS2", Bytes().zstring(string2)));
        return out;
    }

    struct InfoSpec
    {
        std::uint32_t mId = 0;
        std::string mText; ///< empty: no response at all
        std::uint32_t mShared = 0; ///< DNAM
        std::vector<std::uint32_t> mLinks; ///< TCLT
        Bytes mConditions;
    };

    Bytes infoRecord(const InfoSpec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(0).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        for (const std::uint32_t link : s.mLinks)
            b.append(sub4("TCLT", Bytes().u32(link)));
        if (s.mShared != 0)
            b.append(sub4("DNAM", Bytes().u32(s.mShared)));
        if (!s.mText.empty())
        {
            Bytes t;
            t.u32(0).u32(50).u32(0).u8(1).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
            b.append(sub4("TRDT", t));
            b.append(sub4("NAM1", Bytes().zstring(s.mText)));
            b.append(sub4("NAM2", Bytes().u8(0)));
        }
        b.append(s.mConditions);
        return record4("INFO", 0u, s.mId, b);
    }

    /// A topic with its infos. `category` 0: a player topic (named); 7: Miscellaneous. `branch` 0: none.
    Bytes topic(std::uint32_t id, const std::string& name, const std::string& subtype, std::uint8_t category,
        std::uint32_t branch, const std::vector<InfoSpec>& infos)
    {
        Bytes children;
        for (const InfoSpec& i : infos)
            children.append(infoRecord(i));
        Bytes b;
        b.append(edid(name));
        if (category == 0)
            b.append(sub4("FULL", Bytes().zstring(name)));
        b.append(sub4("PNAM", Bytes().f32(1.0f)));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        if (branch != 0)
            b.append(sub4("BNAM", Bytes().u32(branch)));
        b.append(sub4("DATA", Bytes().u8(0).u8(category).u16(0)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(static_cast<std::uint32_t>(infos.size()))));
        Bytes record = record4("DIAL", 0, id, b);
        record.append(group4(id, kGrupTopicChildren, children));
        return record;
    }

    Bytes branchRecord(std::uint32_t id, std::uint32_t start, std::uint32_t flags)
    {
        Bytes b;
        b.append(edid("Branch"));
        b.append(sub4("QNAM", Bytes().u32(kQuest)));
        b.append(sub4("DNAM", Bytes().u32(flags)));
        b.append(sub4("SNAM", Bytes().u32(start)));
        return record4("DLBR", 0, id, b);
    }

    Bytes npcRecord()
    {
        Bytes b;
        b.append(edid("Steward"));
        Bytes acbs;
        acbs.u32(0).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(0).u16(0).u16(0);
        b.append(sub4("ACBS", acbs));
        b.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        b.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        return record4("NPC_", 0, kNpc, b);
    }

    Bytes raceRecord()
    {
        Bytes b;
        b.append(edid("NordRace"));
        b.append(sub4("VTCK", Bytes().u32(kSelf | 0x803).u32(kSelf | 0x804)));
        return record4("RACE", 0, kRaceNord, b);
    }

    struct Plugin
    {
        Bytes mDials;
        Bytes mBranches;
    };

    Bytes pluginBytes(const Plugin& p)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("DIAL"), kGrupTop, p.mDials));
        if (p.mBranches.size() != 0)
            bytes.append(group4(Content::fourCC("DLBR"), kGrupTop, p.mBranches));
        bytes.append(group4(Content::fourCC("NPC_"), kGrupTop, npcRecord()));
        bytes.append(group4(Content::fourCC("RACE"), kGrupTop, raceRecord()));
        return bytes;
    }

    using Turn = openeden::sdk::game::DialogueTurn;

    struct Talk
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActorRecords;
        ActorIndex mActors;
        DialogueIndex mIndex;
        QuestIndex mQuests;
        std::unique_ptr<DialogueRuntime> mRuntime;

        explicit Talk(const Plugin& plugin)
        {
            const Bytes bytes = pluginBytes(plugin);
            Content::Error error;
            EXPECT_TRUE(parseDialogueRecordsBytes(bytes.str(), "test.esp", order(), DialogueLimits(), mDialogue, error))
                << error.mMessage;
            mPlugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            Content::Error e2;
            EXPECT_TRUE(parseActorRecordsBytes(bytes.str(), "test.esp", order(), ActorLimits(), mActorRecords, e2))
                << e2.mMessage;
            mActors.add(mActorRecords);
            mIndex.add(mDialogue, FormIdResolver(mPlugin->header(), "test.esp", order()));
            mRuntime = std::make_unique<DialogueRuntime>(
                mActors, mIndex, mQuests, QuestState::initial(mQuests),
                [](const LString& text, std::uint32_t, StringTableKind) { return text.mText; },
                DialogueRuntimeLimits());
        }

        static DialogueSpeaker speaker()
        {
            DialogueSpeaker s;
            s.mBase = kNpc;
            s.mReference = kNpc | 0x1000;
            return s;
        }
    };

    std::vector<std::string> names(const Turn& turn)
    {
        std::vector<std::string> out;
        for (const auto& t : turn.topics)
            out.push_back(t.text);
        return out;
    }

    std::uint32_t idOf(const Turn& turn, const std::string& name)
    {
        for (const auto& t : turn.topics)
            if (t.text == name)
                return t.id;
        ADD_FAILURE() << "topic not offered: " << name;
        return 0;
    }

    InfoSpec info(std::uint32_t local, const std::string& text)
    {
        InfoSpec s;
        s.mId = kSelf | local;
        s.mText = text;
        return s;
    }

    Plugin stewardPlugin()
    {
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        // the offer: its response is shared (INFO 0x40 of the Misc topic), and it links to the two choices
        InfoSpec offer = info(0x21, "");
        offer.mShared = kSelf | 0x40;
        offer.mLinks = { kTopicTake, kTopicLeave };
        p.mDials.append(topic(kTopicOffer, "Buy a house", "CUST", 0, kBranch, { offer }));
        InfoSpec take = info(0x22, "");
        take.mShared = kSelf | 0x41;
        p.mDials.append(topic(kTopicTake, "I'll take it", "CUST", 0, kBranch, { take }));
        p.mDials.append(topic(kTopicLeave, "Not now", "CUST", 0, kBranch, { info(0x23, "As you wish.") }));
        p.mDials.append(topic(kTopicShared, "SharedReplies", "MISC", 7, 0,
            { info(0x40, "The house is five thousand gold. Take it?"), info(0x41, "It is yours.") }));
        p.mDials.append(topic(kTopicPlain, "Rumors", "CUST", 0, 0, { info(0x24, "Dragons, they say.") }));
        p.mBranches = branchRecord(kBranch, kTopicOffer, 1);
        return p;
    }

    TEST(SkyrimHousesDialogue, anInfoWithNoResponsesSpeaksTheResponsesOfItsSharedInfo)
    {
        Talk talk(stewardPlugin());
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Buy a house"), reply, error)) << error;
        EXPECT_EQ(reply.text, "The house is five thousand gold. Take it?");
    }

    TEST(SkyrimHousesDialogue, anInfoWithItsOwnResponsesKeepsThemWhateverItsDnamNames)
    {
        Plugin p = stewardPlugin();
        InfoSpec own = info(0x25, "Mine.");
        own.mShared = kSelf | 0x40;
        p.mDials.append(topic(kSelf | 0x315, "Own words", "CUST", 0, 0, { own }));
        Talk talk(p);
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Own words"), reply, error)) << error;
        EXPECT_EQ(reply.text, "Mine.");
    }

    TEST(SkyrimHousesDialogue, aSharedInfoThatDoesNotExistGivesNoTextAndNoCrash)
    {
        Plugin p = stewardPlugin();
        InfoSpec lost = info(0x26, "");
        lost.mShared = kSelf | 0x99;
        p.mDials.append(topic(kSelf | 0x316, "Lost words", "CUST", 0, 0, { lost }));
        Talk talk(p);
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Lost words"), reply, error)) << error;
        EXPECT_TRUE(reply.text.empty());
    }

    TEST(SkyrimHousesDialogue, theIndexFindsAnyInfoByIdAndKnowsABranchsStartTopic)
    {
        Talk talk(stewardPlugin());
        ASSERT_NE(talk.mIndex.findInfo(kSelf | 0x41), nullptr);
        EXPECT_EQ(talk.mIndex.findInfo(kSelf | 0x41)->mTopic, kTopicShared);
        EXPECT_EQ(talk.mIndex.findInfo(kSelf | 0x99), nullptr);
        EXPECT_TRUE(talk.mIndex.isBranchStart(kTopicOffer));
        EXPECT_FALSE(talk.mIndex.isBranchStart(kTopicTake));
        EXPECT_TRUE(talk.mIndex.isBranchStart(kTopicPlain)); // no branch
        EXPECT_TRUE(talk.mIndex.isBranchStart(kSelf | 0x999)); // unknown topic: not hidden by this rule
    }

    TEST(SkyrimHousesDialogue, aBranchMembersTopicIsNotOfferedFromTheNormalList)
    {
        Talk talk(stewardPlugin());
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(names(turn), (std::vector<std::string>{ "Buy a house", "Rumors" }));
    }

    TEST(SkyrimHousesDialogue, withNoBranchRecordEveryTopicIsOfferedAsBefore)
    {
        Plugin p = stewardPlugin();
        p.mBranches = Bytes();
        Talk talk(p);
        Turn turn;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(names(turn), (std::vector<std::string>{ "Buy a house", "I'll take it", "Not now", "Rumors" }));
    }

    TEST(SkyrimHousesDialogue, aResponseThatLinksMakesItsLinkedTopicsTheNextMenu)
    {
        Talk talk(stewardPlugin());
        Turn turn, reply, after;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Buy a house"), reply, error)) << error;
        EXPECT_EQ(names(reply), (std::vector<std::string>{ "I'll take it", "Not now" }));
        ASSERT_TRUE(talk.mRuntime->choose(idOf(reply, "I'll take it"), after, error)) << error;
        EXPECT_EQ(after.text, "It is yours.");
        EXPECT_EQ(names(after), (std::vector<std::string>{ "Buy a house", "Rumors" })) << "no links: the normal list";
    }

    TEST(SkyrimHousesDialogue, aChoiceThatLinksNothingReturnsToTheNormalListAfterItsResponse)
    {
        Talk talk(stewardPlugin());
        Turn turn, reply, after;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Buy a house"), reply, error));
        ASSERT_TRUE(talk.mRuntime->choose(idOf(reply, "Not now"), after, error));
        EXPECT_EQ(after.text, "As you wish.");
        EXPECT_EQ(names(after), (std::vector<std::string>{ "Buy a house", "Rumors" }));
    }

    TEST(SkyrimHousesDialogue, whenNoLinkedTopicPassesTheNormalListIsOfferedAgain)
    {
        Plugin p = stewardPlugin();
        // both choices fail: GetGlobalValue of an unknown global is 0, "== 1" does not hold
        Plugin q;
        q.mBranches = p.mBranches;
        InfoSpec offer = info(0x21, "Offer.");
        offer.mLinks = { kTopicTake, kTopicLeave };
        InfoSpec take = info(0x22, "Take.");
        take.mConditions = ctda(74, 0, 1.0f, kSelf | 0x901);
        InfoSpec leave = info(0x23, "Leave.");
        leave.mConditions = ctda(74, 0, 1.0f, kSelf | 0x901);
        q.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        q.mDials.append(topic(kTopicOffer, "Buy a house", "CUST", 0, kBranch, { offer }));
        q.mDials.append(topic(kTopicTake, "I'll take it", "CUST", 0, kBranch, { take }));
        q.mDials.append(topic(kTopicLeave, "Not now", "CUST", 0, kBranch, { leave }));
        Talk talk(q);
        Turn turn, reply;
        std::string error;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        ASSERT_TRUE(talk.mRuntime->choose(idOf(turn, "Buy a house"), reply, error));
        EXPECT_EQ(names(reply), (std::vector<std::string>{ "Buy a house" }));
    }

    TEST(SkyrimHousesDialogue, aScriptsLiveQuestVariableDecidesAConditionOnItsValue)
    {
        Plugin p;
        p.mDials.append(topic(kTopicHello, "Hello", "HELO", 7, 0, { info(0x20, "Yes?") }));
        InfoSpec offer = info(0x21, "For sale.");
        offer.mConditions = ctda(629, 0, 0.0f, kQuest, "::HouseVar_var"); // GetVMQuestVariable == 0
        p.mDials.append(topic(kTopicOffer, "Buy a house", "CUST", 0, 0, { offer }));
        Talk talk(p);
        Turn turn;
        std::string error;
        // no source of any kind: the variable reads 0 (an unanswered value), so "== 0" holds
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error)) << error;
        EXPECT_EQ(names(turn), (std::vector<std::string>{ "Buy a house" }));
        float value = 0.f;
        talk.mRuntime->setLiveQuestVariable([&](FormId quest, const std::string& name) -> std::optional<float> {
            return quest == kQuest && name == "::HouseVar_var" ? std::optional<float>(value) : std::nullopt;
        });
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error));
        EXPECT_EQ(names(turn), (std::vector<std::string>{ "Buy a house" })) << "0 == 0";
        value = 1.f;
        ASSERT_TRUE(talk.mRuntime->begin(Talk::speaker(), turn, error));
        EXPECT_TRUE(names(turn).empty()) << "1 != 0: the offer is gone once the house is bought";
    }

    // ---- the Papyrus side ------------------------------------------------------------------------------------------

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kActorRef = 0x01000A01;
    constexpr FormId kFactionForm = 0x01000A02;
    constexpr FormId kBaseForm = 0x01000B00;

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
        bool referencePosition(FormId, double&, double&, double&) const override { return false; }
        FormId baseOf(FormId) const override { return 0; }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
    };

    class OwnerHooks final : public PapyrusWorldHooks
    {
    public:
        bool setCellOwner(FormId cell, FormId owner) override
        {
            mOwners.emplace_back(cell, owner);
            return true;
        }
        std::vector<std::pair<FormId, FormId>> mOwners;
    };

    void addClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder cell("Cell");
        cell.parent("Form");
        cell.function("SetFactionOwner").native().param("akFaction", "Form");
        cell.function("SetActorOwner").native().param("akActor", "Form");
        data.add(cell);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        data.add(actor);
        ScriptBuilder quest("Quest");
        quest.parent("Form");
        quest.function("SetStage", "Bool").native().param("auiStage", "Int");
        data.add(quest);
        ScriptBuilder game("Game");
        game.function("IncrementStat").global().native().param("asStatName", "String").param("aiModAmount", "Int");
        data.add(game);
    }

    vmad::Property objProp(const std::string& name, FormId form)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = -1;
        return p;
    }

    ScriptBuilder buyerScript()
    {
        ScriptBuilder s("Buyer");
        s.parent("Actor");
        s.autoProperty("House", "Cell");
        s.autoProperty("Owner", "Form");
        s.function("Buy")
            .local("n", "None")
            .callMethod("SetFactionOwner", Id("::House_var"), Id("n"), { Id("::Owner_var") })
            .callStatic("Game", "IncrementStat", Id("n"), { PexAsm::Str("Houses Owned"), Int(1) });
        s.function("BuyFromNone")
            .local("n", "None")
            .callMethod("SetFactionOwner", Nil(), Id("n"), { Id("::Owner_var") });
        return s;
    }

    ScriptBuilder fragmentScript()
    {
        ScriptBuilder s("QF_Houses");
        s.parent("Quest");
        s.var("ranZero", "Int");
        s.var("ranOne", "Int");
        s.var("HouseVar", "Int", Int(7));
        s.function("Fragment_0").assign(Id("ranZero"), Int(1));
        s.function("Fragment_1").assign(Id("ranOne"), Int(1));
        return s;
    }

    Bytes stageBytes(std::uint16_t index)
    {
        Bytes b;
        b.append(sub4("INDX", Bytes().u16(index).u8(0).u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        return b;
    }

    struct Rig
    {
        Rig()
        {
            addClasses(mData);
            mData.add(buyerScript());
            mData.add(fragmentScript());
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kActorRef] = "Actor";
            mData.mClasses[0x01000100] = "Cell";
            mData.mClasses[kFactionForm] = "Form";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            quest.append(stageBytes(10));
            quest.append(sub4("ANAM", Bytes().u32(8)));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestA, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mVmad.mHasFragments = true;
            mVmad.mFragmentScript = "QF_Houses";
            vmad::Script qs;
            qs.mName = "QF_Houses";
            mVmad.mScripts = { qs };
            mVmad.mFragments.push_back({ 10, 0, "QF_Houses", "Fragment_0" });
            mVmad.mFragments.push_back({ 10, 1, "QF_Houses", "Fragment_1" });
            mHost->setQuests(&mIndex, &mState,
                [this](FormId questId) -> const vmad::Vmad* { return questId == kQuestA ? &mVmad : nullptr; });
            mHost->startGame();
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }

        FakeData mData;
        OwnerHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        vmad::Vmad mVmad;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimHousesPapyrus, CellSetFactionOwnerReachesTheHookWithTheCellAndTheOwner)
    {
        Rig rig;
        vmad::Script s;
        s.mName = "Buyer";
        s.mProperties = { objProp("House", 0x01000100), objProp("Owner", kFactionForm) };
        ASSERT_EQ(rig.mHost->attachReference(kActorRef, kBaseForm, true, { s }), 1u);
        rig.tick();
        ASSERT_TRUE(rig.mHost->callFunction(kActorRef, "Buyer", "Buy"));
        rig.tick();
        ASSERT_EQ(rig.mHooks.mOwners.size(), 1u);
        EXPECT_EQ(rig.mHooks.mOwners[0].first, 0x01000100u);
        EXPECT_EQ(rig.mHooks.mOwners[0].second, kFactionForm);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimHousesPapyrus, aCallOnNoCellDoesNothingAndIsNotAnError)
    {
        Rig rig;
        vmad::Script s;
        s.mName = "Buyer";
        s.mProperties = { objProp("Owner", kFactionForm) };
        ASSERT_EQ(rig.mHost->attachReference(kActorRef, kBaseForm, true, { s }), 1u);
        rig.tick();
        (void)rig.mHost->callFunction(kActorRef, "Buyer", "BuyFromNone");
        rig.tick();
        EXPECT_TRUE(rig.mHooks.mOwners.empty());
    }

    TEST(SkyrimHousesPapyrus, withNoStageEntryCheckEveryFragmentOfTheStageRuns)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuestA, 10));
        rig.tick();
        EXPECT_EQ(rig.mHost->numberVariable(kQuestA, "QF_Houses", "ranZero"), 1.0);
        EXPECT_EQ(rig.mHost->numberVariable(kQuestA, "QF_Houses", "ranOne"), 1.0);
    }

    TEST(SkyrimHousesPapyrus, aFragmentWhoseLogEntryConditionsFailDoesNotRun)
    {
        Rig rig;
        std::vector<std::int32_t> asked;
        rig.mHost->setStageEntryCheck([&](FormId quest, std::uint32_t stage, std::int32_t entry) {
            EXPECT_EQ(quest, kQuestA);
            EXPECT_EQ(stage, 10u);
            asked.push_back(entry);
            return entry == 1;
        });
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuestA, 10));
        rig.tick();
        EXPECT_EQ(rig.mHost->numberVariable(kQuestA, "QF_Houses", "ranZero"), 0.0);
        EXPECT_EQ(rig.mHost->numberVariable(kQuestA, "QF_Houses", "ranOne"), 1.0);
        EXPECT_EQ(asked, (std::vector<std::int32_t>{ 0, 1 }));
    }

    TEST(SkyrimHousesPapyrus, aScriptsVariableIsReadFromOutsideByTheNameAConditionUses)
    {
        Rig rig;
        ASSERT_TRUE(rig.mHost->startQuestForTest(kQuestA, 10));
        rig.tick();
        EXPECT_EQ(rig.mHost->anyNumberVariable(kQuestA, "HouseVar"), 7.0);
        EXPECT_EQ(rig.mHost->anyNumberVariable(kQuestA, "housevar"), 7.0) << "without regard to case";
        EXPECT_FALSE(rig.mHost->anyNumberVariable(kQuestA, "NoSuchVariable").has_value());
        EXPECT_FALSE(rig.mHost->anyNumberVariable(0x01000999, "HouseVar").has_value());
    }
}
