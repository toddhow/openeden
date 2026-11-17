// Task 0239 (milestone M-S7): follower commands through the game's own dialogue and scripts. The pack's side: the
// follower state the scripts write (factions, relationship ranks, the teammate flag, globals, WaitingForPlayer), its
// save chunk, the condition overlay that makes the DialogueFollower topics' own conditions pass for a recruited actor,
// the ALFC factions of a quest alias, the package brain's "wait here" and the Papyrus natives of
// DialogueFollowerScript / HirelingQuest. Pure tests over fakes and generated scripts; no game data. Written FIRST and
// run against stubs (see the task's stage log for the red counts).
#include <game_skyrimse/actor_table.hpp>
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/follower.hpp>
#include <game_skyrimse/follower_state.hpp>
#include <game_skyrimse/package_brain.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
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
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;
    using ContentTests::Bytes;
    using ContentTests::group4;
    using ContentTests::record4;
    using ContentTests::sub4;
    using ContentTests::tes4Header;
    using PexAsm::Bool;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::Nil;
    using PexAsm::ScriptBuilder;
    using PexAsm::Str;

    // ---- the state -----------------------------------------------------------------------------------------------

    constexpr FormId kActorRef = 0x0001A66E;
    constexpr FormId kFactionCurrent = 0x0005C84E;
    constexpr FormId kFactionPlayer = 0x00084D1B;
    constexpr FormId kPlayerRef = 0x14;

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
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }

    private:
        std::vector<unsigned char> mBytes;
        std::size_t mPos = 0;
    };

    std::vector<unsigned char> bytesOf(std::initializer_list<std::uint32_t> words)
    {
        std::vector<unsigned char> r;
        for (const std::uint32_t w : words)
        {
            unsigned char b[4];
            std::memcpy(b, &w, 4);
            r.insert(r.end(), b, b + 4);
        }
        return r;
    }

    TEST(SkyrimFollowerCmdState, anUntouchedActorHasNoOpinionOfItsOwn)
    {
        FollowerState state;
        EXPECT_FALSE(state.factionRank(kActorRef, kFactionCurrent).has_value());
        EXPECT_FALSE(state.globalValue(0x500).has_value());
        EXPECT_FALSE(state.relationshipRank(kActorRef, kPlayerRef).has_value());
        EXPECT_FALSE(state.isPlayerTeammate(kActorRef));
        EXPECT_FALSE(state.actorValue(kActorRef, kWaitingForPlayerIndex).has_value());
        EXPECT_FALSE(state.waiting(kActorRef));
    }

    TEST(SkyrimFollowerCmdState, addingAndRemovingAFactionIsSeenByTheOverlay)
    {
        FollowerState state;
        ASSERT_TRUE(state.addToFaction(kActorRef, kFactionCurrent));
        ASSERT_TRUE(state.factionRank(kActorRef, kFactionCurrent).has_value());
        EXPECT_FLOAT_EQ(*state.factionRank(kActorRef, kFactionCurrent), 0.f);
        EXPECT_FALSE(state.factionRank(kActorRef, kFactionPlayer).has_value()); // another faction: untouched
        EXPECT_FALSE(state.factionRank(0x77, kFactionCurrent).has_value()); // another actor: untouched
        ASSERT_TRUE(state.removeFromFaction(kActorRef, kFactionCurrent));
        ASSERT_TRUE(state.factionRank(kActorRef, kFactionCurrent).has_value());
        EXPECT_FLOAT_EQ(*state.factionRank(kActorRef, kFactionCurrent), -1.f); // removed beats the data
        ASSERT_TRUE(state.addToFaction(kActorRef, kFactionCurrent, 2));
        EXPECT_FLOAT_EQ(*state.factionRank(kActorRef, kFactionCurrent), 2.f);
    }

    TEST(SkyrimFollowerCmdState, aZeroIdIsRefusedAndNothingIsKept)
    {
        FollowerState state;
        EXPECT_FALSE(state.addToFaction(0, kFactionCurrent));
        EXPECT_FALSE(state.addToFaction(kActorRef, 0));
        EXPECT_FALSE(state.removeFromFaction(0, kFactionCurrent));
        EXPECT_FALSE(state.setRelationshipRank(0, kPlayerRef, 1));
        EXPECT_FALSE(state.setTeammate(0, true));
        EXPECT_FALSE(state.setGlobal(0, 1.f));
        EXPECT_TRUE(state.data() == FollowerStateData());
    }

    TEST(SkyrimFollowerCmdState, theStateIsBoundedAndAKnownEntryStillChangesWhenFull)
    {
        FollowerState state;
        for (FormId i = 1; i <= kMaxFollowerStateEntries; ++i)
            EXPECT_TRUE(state.addToFaction(i, kFactionCurrent)) << i;
        EXPECT_FALSE(state.addToFaction(kMaxFollowerStateEntries + 1, kFactionCurrent));
        EXPECT_TRUE(state.addToFaction(5, kFactionCurrent, 3)); // an entry that exists changes
        EXPECT_FLOAT_EQ(*state.factionRank(5, kFactionCurrent), 3.f);
        for (FormId i = 1; i <= kMaxFollowerStateEntries; ++i)
            EXPECT_TRUE(state.setGlobal(i, 1.f)) << i;
        EXPECT_FALSE(state.setGlobal(kMaxFollowerStateEntries + 1, 1.f));
    }

    TEST(SkyrimFollowerCmdState, relationshipRanksAreClampedToTheGamesRange)
    {
        FollowerState state;
        ASSERT_TRUE(state.setRelationshipRank(kActorRef, kPlayerRef, 3));
        EXPECT_FLOAT_EQ(*state.relationshipRank(kActorRef, kPlayerRef), 3.f);
        ASSERT_TRUE(state.setRelationshipRank(kActorRef, kPlayerRef, 99));
        EXPECT_FLOAT_EQ(*state.relationshipRank(kActorRef, kPlayerRef), 4.f);
        ASSERT_TRUE(state.setRelationshipRank(kActorRef, kPlayerRef, -99));
        EXPECT_FLOAT_EQ(*state.relationshipRank(kActorRef, kPlayerRef), -4.f);
        EXPECT_FALSE(state.relationshipRank(kPlayerRef, kActorRef).has_value()); // not symmetric
    }

    TEST(SkyrimFollowerCmdState, theTeammateFlagAndGlobalsAreKept)
    {
        FollowerState state;
        ASSERT_TRUE(state.setTeammate(kActorRef, true));
        EXPECT_TRUE(state.isPlayerTeammate(kActorRef));
        EXPECT_FALSE(state.isPlayerTeammate(0x99));
        ASSERT_TRUE(state.setTeammate(kActorRef, false));
        EXPECT_FALSE(state.isPlayerTeammate(kActorRef));
        ASSERT_TRUE(state.setGlobal(0xBCC98, 1.f));
        ASSERT_TRUE(state.globalValue(0xBCC98).has_value());
        EXPECT_FLOAT_EQ(*state.globalValue(0xBCC98), 1.f);
        ASSERT_TRUE(state.setGlobal(0xBCC98, 0.f));
        EXPECT_FLOAT_EQ(*state.globalValue(0xBCC98), 0.f);
    }

    TEST(SkyrimFollowerCmdState, waitingForPlayerComesFromTheActorValueSourceAndAnswersTheConditionIndex)
    {
        FollowerState state;
        EXPECT_FALSE(state.waiting(kActorRef)); // no source
        std::map<FormId, float> values;
        state.setActorValueSource([&](FormId ref, std::string_view key) -> std::optional<float> {
            if (key != kWaitingForPlayerKey)
                return std::nullopt;
            const auto it = values.find(ref);
            if (it == values.end())
                return std::nullopt;
            return it->second;
        });
        EXPECT_FALSE(state.waiting(kActorRef));
        EXPECT_FALSE(state.actorValue(kActorRef, kWaitingForPlayerIndex).has_value()); // nothing ever set it
        values[kActorRef] = 1.f;
        EXPECT_TRUE(state.waiting(kActorRef));
        ASSERT_TRUE(state.actorValue(kActorRef, kWaitingForPlayerIndex).has_value());
        EXPECT_FLOAT_EQ(*state.actorValue(kActorRef, kWaitingForPlayerIndex), 1.f);
        EXPECT_FALSE(state.actorValue(kActorRef, kWaitingForPlayerIndex + 1).has_value()); // only this one is known
        values[kActorRef] = 0.f;
        EXPECT_FALSE(state.waiting(kActorRef));
        ASSERT_TRUE(state.actorValue(kActorRef, kWaitingForPlayerIndex).has_value());
        EXPECT_FLOAT_EQ(*state.actorValue(kActorRef, kWaitingForPlayerIndex), 0.f); // set to 0 is an answer, not none
    }

    TEST(SkyrimFollowerCmdState, restoreReplacesTheStateAndAnInvalidOneChangesNothing)
    {
        FollowerState state;
        ASSERT_TRUE(state.addToFaction(1, 2));
        FollowerStateData data;
        data.mFactions[{ 5, 7 }] = 0;
        data.mFactions[{ 5, 8 }] = -1;
        data.mRanks[{ 5, kPlayerRef }] = 3;
        data.mTeammates.insert(5);
        data.mGlobals[9] = 1.f;
        ASSERT_TRUE(state.restore(data));
        EXPECT_TRUE(state.data() == data);
        EXPECT_FALSE(state.factionRank(1, 2).has_value()); // the old state is gone

        FollowerStateData bad = data;
        bad.mTeammates.insert(0); // a zero id
        EXPECT_FALSE(state.restore(bad));
        EXPECT_TRUE(state.data() == data);
        FollowerStateData big;
        for (FormId i = 1; i <= kMaxFollowerStateEntries + 1; ++i)
            big.mTeammates.insert(i);
        EXPECT_FALSE(state.restore(big));
        EXPECT_TRUE(state.data() == data);
    }

    // ---- the save chunk ------------------------------------------------------------------------------------------

    TEST(SkyrimFollowerCmdChunk, writesEveryPartInAscendingOrderAsHandDerived)
    {
        FollowerStateData data;
        data.mFactions[{ 5, 7 }] = 0;
        data.mRanks[{ 5, kPlayerRef }] = 3;
        data.mTeammates.insert(5);
        data.mGlobals[9] = 1.f;
        FollowerStateChunk chunk([&] { return data; }, nullptr);
        MemoryWriter w;
        chunk.write(w);
        EXPECT_EQ(w.mBytes, bytesOf({ 1, 5, 7, 0, 1, 5, 0x14, 3, 1, 5, 1, 9, 0x3F800000u }));
        EXPECT_EQ(chunk.chunkName(), "followerstate");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
    }

    TEST(SkyrimFollowerCmdChunk, anEmptyStateIsFourZeroCounts)
    {
        FollowerStateChunk chunk([] { return FollowerStateData(); }, nullptr);
        MemoryWriter w;
        chunk.write(w);
        EXPECT_EQ(w.mBytes, bytesOf({ 0, 0, 0, 0 }));
    }

    TEST(SkyrimFollowerCmdChunk, aWrittenStateIsReadBackEqual)
    {
        FollowerStateData data;
        data.mFactions[{ 5, 7 }] = 0;
        data.mFactions[{ 5, 9 }] = -1;
        data.mFactions[{ 6, 7 }] = 2;
        data.mRanks[{ 5, kPlayerRef }] = -2;
        data.mTeammates = { 5, 6 };
        data.mGlobals[9] = 1.f;
        data.mGlobals[11] = 0.5f;
        FollowerStateData restored;
        FollowerStateChunk chunk([&] { return data; },
            [&](const FollowerStateData& d) {
                restored = d;
                return true;
            });
        MemoryWriter w;
        chunk.write(w);
        MemoryReader r(w.mBytes);
        EXPECT_TRUE(chunk.read(r, FollowerStateChunk::kVersion));
        EXPECT_TRUE(restored == data);
    }

    TEST(SkyrimFollowerCmdChunk, damagedChunksAreRefusedAndChangeNothing)
    {
        int sets = 0;
        FollowerStateChunk chunk([] { return FollowerStateData(); },
            [&](const FollowerStateData&) {
                ++sets;
                return true;
            });
        const std::vector<std::vector<unsigned char>> bad = {
            {}, // nothing
            bytesOf({ 1, 5, 7 }), // a faction entry cut short
            bytesOf({ 2, 6, 7, 0, 5, 7, 0, 0, 0, 0 }), // factions not ascending
            bytesOf({ 2, 5, 7, 0, 5, 7, 0, 0, 0, 0 }), // a duplicate key
            bytesOf({ 1, 0, 7, 0, 0, 0, 0 }), // a zero actor
            bytesOf({ 0, 0, 2, 6, 5, 0 }), // teammates not ascending
            bytesOf({ 0, 0, 0, 2, 9, 0, 9, 0 }), // globals with a duplicate id
            bytesOf({ 0, 0, 0, 0, 7 }), // trailing bytes
            bytesOf({ 0xFFFFFFFFu }), // a huge count with nothing behind it
            bytesOf({ static_cast<std::uint32_t>(kMaxFollowerStateEntries) + 1 }), // over the cap
        };
        for (const auto& b : bad)
        {
            MemoryReader r(b);
            EXPECT_FALSE(chunk.read(r, FollowerStateChunk::kVersion));
            EXPECT_FALSE(chunk.refusal().empty());
        }
        MemoryReader good(bytesOf({ 0, 0, 0, 0 }));
        EXPECT_FALSE(chunk.read(good, 2)); // another version
        EXPECT_EQ(sets, 0);
    }

    TEST(SkyrimFollowerCmdChunk, aSetterThatRefusesRefusesTheChunk)
    {
        FollowerStateChunk chunk([] { return FollowerStateData(); }, [](const FollowerStateData&) { return false; });
        MemoryReader r(bytesOf({ 0, 0, 0, 0 }));
        EXPECT_FALSE(chunk.read(r, FollowerStateChunk::kVersion));
    }

    // ---- the condition overlay -----------------------------------------------------------------------------------

    constexpr FormId kNpc = 0x01000200;

    survey::Ctda cond(std::uint16_t function, std::uint32_t p1, std::uint8_t compare, float value,
        std::uint32_t runOn = 0)
    {
        survey::Ctda c;
        c.mFunction = function;
        c.mParam1 = p1;
        c.mRunOn = runOn;
        c.mCompare = compare;
        c.mComparison = value;
        return c;
    }

    struct WorldRig
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        FollowerState mState;

        WorldRig()
        {
            DialogueSpeaker s;
            s.mBase = kNpc;
            s.mReference = kActorRef;
            mWorld.setSpeaker(s);
        }
        bool holds(const survey::Ctda& c)
        {
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    TEST(SkyrimFollowerCmdConditions, getInFactionFollowsTheOverlayOnlyWhenOneIsSet)
    {
        WorldRig r;
        EXPECT_FALSE(r.holds(cond(71, kFactionCurrent, 0, 1.f))); // the data alone: not in it
        r.mState.addToFaction(kActorRef, kFactionCurrent);
        EXPECT_FALSE(r.holds(cond(71, kFactionCurrent, 0, 1.f))); // no overlay given to the world yet
        r.mWorld.setFollowerOverlay(&r.mState);
        EXPECT_TRUE(r.holds(cond(71, kFactionCurrent, 0, 1.f)));
        EXPECT_FALSE(r.holds(cond(71, kFactionPlayer, 0, 1.f))); // another faction: the data
        r.mState.removeFromFaction(kActorRef, kFactionCurrent);
        EXPECT_FALSE(r.holds(cond(71, kFactionCurrent, 0, 1.f)));
        EXPECT_TRUE(r.holds(cond(71, kFactionCurrent, 0, 0.f)));
    }

    TEST(SkyrimFollowerCmdConditions, getFactionRankReadsTheOverlay)
    {
        WorldRig r;
        r.mWorld.setFollowerOverlay(&r.mState);
        r.mState.addToFaction(kActorRef, kFactionCurrent, 2);
        EXPECT_TRUE(r.holds(cond(73, kFactionCurrent, 0, 2.f))); // GetFactionRank
    }

    TEST(SkyrimFollowerCmdConditions, theWaitAndFollowTopicsSeeTheWaitingActorValue)
    {
        WorldRig r;
        r.mWorld.setFollowerOverlay(&r.mState);
        float waiting = 0.f;
        bool set = false;
        r.mState.setActorValueSource([&](FormId, std::string_view) -> std::optional<float> {
            return set ? std::optional<float>(waiting) : std::nullopt;
        });
        // Follow needs GetActorValue(0x5F) == 1, Wait needs == 0: before anything set it, the data (0) answers
        EXPECT_TRUE(r.holds(cond(14, kWaitingForPlayerIndex, 0, 0.f)));
        EXPECT_FALSE(r.holds(cond(14, kWaitingForPlayerIndex, 0, 1.f)));
        set = true;
        waiting = 1.f;
        EXPECT_TRUE(r.holds(cond(14, kWaitingForPlayerIndex, 0, 1.f)));
        EXPECT_FALSE(r.holds(cond(14, kWaitingForPlayerIndex, 0, 0.f)));
    }

    TEST(SkyrimFollowerCmdConditions, aGlobalAScriptSetIsWhatTheNextConditionReads)
    {
        WorldRig r;
        r.mWorld.setFollowerOverlay(&r.mState);
        const FormId kCount = 0x000BCC98; // PlayerFollowerCount
        EXPECT_TRUE(r.holds(cond(74, kCount, 0, 0.f))); // the data's value is 0
        r.mState.setGlobal(kCount, 1.f);
        EXPECT_TRUE(r.holds(cond(74, kCount, 0, 1.f)));
        EXPECT_FALSE(r.holds(cond(74, kCount, 0, 0.f)));
    }

    TEST(SkyrimFollowerCmdConditions, theTeammateFlagAndTheRelationshipRankComeFromTheOverlay)
    {
        WorldRig r;
        r.mWorld.setFollowerOverlay(&r.mState);
        EXPECT_FALSE(r.holds(cond(453, 0, 0, 1.f))); // GetPlayerTeammate
        r.mState.setTeammate(kActorRef, true);
        EXPECT_TRUE(r.holds(cond(453, 0, 0, 1.f)));
        EXPECT_FALSE(r.holds(cond(403, 0, 0, 3.f))); // no rank was set: acquaintance (0)
        r.mState.setRelationshipRank(kActorRef, kPlayerRef, 3);
        EXPECT_TRUE(r.holds(cond(403, 0, 0, 3.f)));
    }

    // A hireling is listed in CurrentFollowerFaction and CurrentHireling with rank -1 in the data (Jenassa, Njada,
    // Uthgerd: 255 in the stored byte): that is "not a member". GetInFaction answers 1 only from rank 0 up.
    TEST(SkyrimFollowerCmdConditions, aFactionWithRankMinusOneInTheDataIsNotAMembership)
    {
        constexpr FormId kBaseJ = 0x00000A10;
        constexpr FormId kMember = 0x00000B01;
        constexpr FormId kNotMember = 0x00000B02;
        ActorRecords records;
        ActorNpc npc;
        npc.mCommon.mId = kBaseJ;
        FactionMembership member;
        member.mFaction = kMember;
        member.mRank = 0;
        FactionMembership notMember;
        notMember.mFaction = kNotMember;
        notMember.mRank = -1;
        npc.mFactions = { member, notMember };
        records.mNpcs.push_back(npc);
        ActorIndex actors;
        actors.add(records);
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        DialogueSpeaker speaker;
        speaker.mBase = kBaseJ;
        world.setSpeaker(speaker);
        conditions::Evaluator eval;
        const auto holds = [&](std::uint16_t function, std::uint32_t faction, std::uint8_t compare, float value) {
            const survey::Ctda c = cond(function, faction, compare, value);
            return eval.evaluate(std::span<const survey::Ctda>(&c, 1), world);
        };
        EXPECT_TRUE(holds(71, kMember, 0, 1.f));
        EXPECT_FALSE(holds(71, kNotMember, 0, 1.f)) << "rank -1 is not in the faction";
        EXPECT_TRUE(holds(71, kNotMember, 0, 0.f));
        EXPECT_FLOAT_EQ(world.factionRank(kBaseJ, kNotMember), -1.f);
    }

    // ---- the package brain: wait here ----------------------------------------------------------------------------

    constexpr FormId kBase = 0x00013BA7;
    constexpr WorldId kRef = 0x10;

    class Source final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId base, std::vector<const PackageRecord*>& out) const override
        {
            const auto it = mLists.find(base);
            if (it == mLists.end())
                return false;
            out = it->second;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord& p) const override
        {
            const auto it = mBehavior.find(p.mCommon.mId);
            return it == mBehavior.end() ? PackageBehavior::Stand : it->second;
        }
        bool conditionsPass(const PackageRecord&, FormId, FormId) override { return true; }
        bool locate(FormId reference, double& x, double& y, double& z) const override
        {
            const auto it = mPlaces.find(reference);
            if (it == mPlaces.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        std::map<FormId, std::vector<const PackageRecord*>> mLists;
        std::map<FormId, PackageBehavior> mBehavior;
        std::map<FormId, std::array<double, 3>> mPlaces;
    };

    struct Event
    {
        enum Kind
        {
            Goto,
            Cancel
        } mKind;
        double mX = 0;
        double mY = 0;
    };

    class Writer final : public ActorRequestWriter
    {
    public:
        bool setMovement(ActorHandle, float, float) override { return true; }
        bool goTo(ActorHandle, const ActorGoal& g) override
        {
            mEvents.push_back({ Event::Goto, g.x, g.y });
            return true;
        }
        bool cancelGoal(ActorHandle) override
        {
            mEvents.push_back({ Event::Cancel, 0, 0 });
            return true;
        }
        bool actorPosition(ActorHandle h, double& x, double& y, double& z) const override
        {
            const auto it = mPositions.find(h.reference);
            if (it == mPositions.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        bool playerPosition(double& x, double& y, double& z) const override
        {
            x = mPlayer[0];
            y = mPlayer[1];
            z = mPlayer[2];
            return true;
        }
        ActorClock clock() const override { return { 12.f, 0, true }; }
        std::size_t count(Event::Kind kind) const
        {
            std::size_t n = 0;
            for (const Event& e : mEvents)
                if (e.mKind == kind)
                    ++n;
            return n;
        }
        std::vector<Event> mEvents;
        std::map<WorldId, std::array<double, 3>> mPositions;
        std::array<double, 3> mPlayer{ 0, 0, 66 };
    };

    ActorInfo infoOf(WorldId ref, std::uint32_t session, double x, double y, double z)
    {
        ActorInfo i;
        i.handle = { ref, session };
        i.base = kBase;
        i.editorId = "Test";
        i.x = x;
        i.y = y;
        i.z = z;
        return i;
    }

    void step(PackageBrain& brain, Writer& w, int steps, float dt = 0.1f)
    {
        for (int i = 0; i < steps; ++i)
            brain.update(dt, w);
    }

    struct Rig
    {
        Source src;
        PackageRecord pkg;
        Writer w;
        std::unique_ptr<PackageBrain> brain;

        Rig()
        {
            pkg.mCommon.mId = 1;
            pkg.mHasPkdt = true;
            pkg.mType = 18;
            pkg.mLocations.push_back({ PackageLocationType::NearReference, 0x500, 0 });
            src.mLists[kBase] = { &pkg };
            src.mBehavior[1] = PackageBehavior::Travel;
            src.mPlaces[0x500] = { 9000, 9000, 0 };
            brain = std::make_unique<PackageBrain>(src);
            w.mPositions[kRef] = { 1000, 0, 66 };
            brain->onActorAdded(infoOf(kRef, 1, 1000, 0, 66));
        }
    };

    TEST(SkyrimFollowerCmdBrain, aWaitingFollowerIssuesNoGoalAndItsGoalInTheAirIsCancelled)
    {
        Rig r;
        ASSERT_TRUE(r.brain->setFollowPlayer(kRef, true));
        step(*r.brain, r.w, 5);
        ASSERT_GE(r.w.count(Event::Goto), 1u); // it was on its way to the player
        const std::size_t gotos = r.w.count(Event::Goto);
        const std::size_t cancels = r.w.count(Event::Cancel);
        EXPECT_TRUE(r.brain->setFollowerWaiting(kRef, true));
        EXPECT_TRUE(r.brain->followerWaiting(kRef));
        step(*r.brain, r.w, 40);
        EXPECT_EQ(r.w.count(Event::Goto), gotos) << "no new goal while waiting";
        EXPECT_GT(r.w.count(Event::Cancel), cancels) << "the goal that was out is cancelled";
        EXPECT_EQ(r.brain->stats().mFollowerWaits, 1u);
        EXPECT_TRUE(r.brain->followsPlayer(kRef)) << "it is still a follower";
    }

    TEST(SkyrimFollowerCmdBrain, followMeResumesTheWalkToThePlayer)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        step(*r.brain, r.w, 3);
        r.brain->setFollowerWaiting(kRef, true);
        step(*r.brain, r.w, 20);
        const std::size_t gotos = r.w.count(Event::Goto);
        EXPECT_TRUE(r.brain->setFollowerWaiting(kRef, false));
        step(*r.brain, r.w, 20);
        EXPECT_GT(r.w.count(Event::Goto), gotos);
        EXPECT_FALSE(r.brain->followerWaiting(kRef));
    }

    TEST(SkyrimFollowerCmdBrain, theCallsReportOnlyARealChange)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        EXPECT_TRUE(r.brain->setFollowerWaiting(kRef, true));
        EXPECT_FALSE(r.brain->setFollowerWaiting(kRef, true));
        EXPECT_TRUE(r.brain->setFollowerWaiting(kRef, false));
        EXPECT_FALSE(r.brain->setFollowerWaiting(kRef, false));
        EXPECT_FALSE(r.brain->setFollowerWaiting(kNoWorldId, true));
    }

    TEST(SkyrimFollowerCmdBrain, aWaitToldBeforeTheActorAppearsAppliesWhenItDoes)
    {
        Source src;
        PackageBrain brain(src);
        Writer w;
        brain.setFollowPlayer(kRef, true);
        EXPECT_TRUE(brain.setFollowerWaiting(kRef, true));
        w.mPositions[kRef] = { 1000, 0, 66 };
        brain.onActorAdded(infoOf(kRef, 5, 1000, 0, 66)); // a door removed and added it again
        step(brain, w, 30);
        EXPECT_EQ(w.count(Event::Goto), 0u);
    }

    TEST(SkyrimFollowerCmdBrain, dismissingAWaitingFollowerForgetsTheWait)
    {
        Rig r;
        r.brain->setFollowPlayer(kRef, true);
        r.brain->setFollowerWaiting(kRef, true);
        EXPECT_TRUE(r.brain->setFollowPlayer(kRef, false));
        EXPECT_FALSE(r.brain->followerWaiting(kRef));
        r.brain->setFollowPlayer(kRef, true); // hired again later: it follows, it does not wait
        step(*r.brain, r.w, 5);
        EXPECT_GE(r.w.count(Event::Goto), 1u);
    }

    TEST(SkyrimFollowerCmdBrain, theWaitFlagOfAnActorThatDoesNotFollowChangesNothing)
    {
        Rig r;
        r.brain->setFollowerWaiting(kRef, true);
        step(*r.brain, r.w, 30);
        ASSERT_GE(r.w.count(Event::Goto), 1u); // its package still sends it to (9000, 9000)
        double farthest = 0;
        for (const Event& e : r.w.mEvents)
            if (e.mKind == Event::Goto)
                farthest = std::max(farthest, e.mX);
        EXPECT_GT(farthest, 5000.0);
    }

    TEST(SkyrimFollowerCmdTable, theTablePassesTheWaitToTheBrainAndRefusesWithoutOne)
    {
        SkyrimActorTable none;
        EXPECT_FALSE(none.setFollowerWaiting(0x30, true));
        EXPECT_FALSE(none.followerWaiting(0x30));
        Source src;
        SkyrimActorTable table;
        table.enablePackages(src, 1);
        ASSERT_TRUE(table.recruit(0x30));
        EXPECT_TRUE(table.setFollowerWaiting(0x30, true));
        EXPECT_TRUE(table.followerWaiting(0x30));
        EXPECT_FALSE(table.setFollowerWaiting(0x30, true));
        EXPECT_TRUE(table.setFollowerWaiting(0x30, false));
        EXPECT_FALSE(table.followerWaiting(0x30));
    }

    // A follower told to wait stays where it is when the player goes through a door: the core carries only the
    // followers this list names, and it is still the player's follower (the side).
    TEST(SkyrimFollowerCmdTable, aWaitingFollowerIsNotCarriedButStillFollowsAndStaysOnTheList)
    {
        Source src;
        SkyrimActorTable table;
        table.enablePackages(src, 1);
        ASSERT_TRUE(table.recruit(0x20));
        ASSERT_TRUE(table.recruit(0x30));
        ASSERT_TRUE(table.setFollowerWaiting(0x20, true));
        WorldId out[4] = { 0, 0, 0, 0 };
        EXPECT_EQ(table.followers(out, 4), 1u) << "the carry list holds the one that follows";
        EXPECT_EQ(out[0], 0x30u);
        EXPECT_TRUE(table.isFollower(0x20)) << "a waiting follower is still on the player's side";
        EXPECT_EQ(table.followerIds(), (std::vector<WorldId>{ 0x20, 0x30 })) << "and still a follower to save";
        ASSERT_TRUE(table.setFollowerWaiting(0x20, false));
        EXPECT_EQ(table.followers(out, 4), 2u);
    }

    // ---- the quest alias's factions (ALFC) -----------------------------------------------------------------------

    TEST(SkyrimFollowerCmdQuest, anAliasKeepsTheFactionsItsFillIsAddedTo)
    {
        const FormId kQuest = 0x01000400;
        LoadOrder order;
        std::string err;
        ASSERT_TRUE(order.add("Skyrim.esm", false, err));
        ASSERT_TRUE(order.add("test.esp", false, err));
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes quest;
        quest.append(sub4("EDID", Bytes().zstring("Q1")));
        quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
        quest.append(sub4("NEXT", Bytes()));
        quest.append(sub4("ANAM", Bytes().u32(1)));
        quest.append(sub4("ALST", Bytes().u32(0)));
        quest.append(sub4("ALID", Bytes().zstring("Follower")));
        quest.append(sub4("FNAM", Bytes().u32(0)));
        quest.append(sub4("ALFC", Bytes().u32(kFactionCurrent)));
        quest.append(sub4("ALFC", Bytes().u32(kFactionPlayer)));
        quest.append(sub4("ALED", Bytes()));
        quest.append(sub4("ALST", Bytes().u32(1)));
        quest.append(sub4("ALID", Bytes().zstring("Animal")));
        quest.append(sub4("FNAM", Bytes().u32(0)));
        quest.append(sub4("ALED", Bytes()));
        bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuest, quest)));
        QuestRecords parsed;
        Content::Error error;
        ASSERT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order, QuestLimits(), parsed, error));
        ASSERT_EQ(parsed.mQuests.size(), 1u);
        ASSERT_EQ(parsed.mQuests[0].mAliases.size(), 2u);
        EXPECT_EQ(parsed.mQuests[0].mAliases[0].mFactions, (std::vector<FormId>{ kFactionCurrent, kFactionPlayer }));
        EXPECT_TRUE(parsed.mQuests[0].mAliases[1].mFactions.empty());
    }
    // ---- the natives of DialogueFollowerScript and HirelingQuest -------------------------------------------------

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kActorA = 0x01000A01;
    constexpr FormId kOtherA = 0x01000A02;
    constexpr FormId kBaseA = 0x01000B00;
    constexpr FormId kFactionA = 0x01000C01;
    constexpr FormId kGlobalA = 0x01000D01;
    constexpr FormId kInfoA = 0x01000E01;

    LoadOrder packOrder()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

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
        bool aliasFactions(FormId quest, std::uint32_t alias, std::vector<FormId>& out) const override
        {
            out.clear();
            const auto it = mAliasFactions.find({ quest, alias });
            if (it == mAliasFactions.end())
                return false;
            out = it->second;
            return true;
        }
        FormId baseOf(FormId reference) const override { return reference == kActorA ? kBaseA : 0; }
        bool isTopicInfo(FormId form) const override { return form == kInfoA; }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<std::pair<FormId, std::uint32_t>, std::vector<FormId>> mAliasFactions;
    };

    void addClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder glob("GlobalVariable");
        glob.parent("Form");
        glob.function("SetValue").native().param("afValue", "Float");
        glob.function("GetValue", "Float").native();
        data.add(glob);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        actor.function("AddToFaction").native().param("akFaction", "Form");
        actor.function("RemoveFromFaction").native().param("akFaction", "Form");
        actor.function("IsInFaction", "Bool").native().param("akFaction", "Form");
        actor.function("SetFactionRank").native().param("akFaction", "Form").param("aiRank", "Int");
        actor.function("GetFactionRank", "Int").native().param("akFaction", "Form");
        actor.function("SetPlayerTeammate").native().param("abTeammate", "Bool").param("abCanDoFavor", "Bool");
        actor.function("IsPlayerTeammate", "Bool").native();
        actor.function("SetRelationshipRank").native().param("akOther", "Actor").param("aiRank", "Int");
        actor.function("GetRelationshipRank", "Int").native().param("akOther", "Actor");
        actor.function("SetAV").native().param("asValueName", "String").param("afValue", "Float");
        actor.function("SetActorValue").native().param("asValueName", "String").param("afValue", "Float");
        actor.function("GetActorValue", "Float").native().param("asValueName", "String");
        data.add(actor);
        ScriptBuilder quest("Quest");
        quest.parent("Form");
        data.add(quest);
        ScriptBuilder topicInfo("TopicInfo");
        topicInfo.parent("Form");
        data.add(topicInfo);
        ScriptBuilder alias("Alias");
        alias.function("GetOwningQuest", "Quest").native();
        data.add(alias);
        ScriptBuilder refAlias("ReferenceAlias");
        refAlias.parent("Alias");
        refAlias.function("ForceRefTo").native().param("akNewRef", "ObjectReference");
        refAlias.function("Clear").native();
        refAlias.function("GetActorRef", "Actor").native();
        data.add(refAlias);
    }

    ScriptBuilder probeScript()
    {
        ScriptBuilder s("Probe");
        s.parent("Actor");
        s.autoProperty("Fac", "Form");
        s.autoProperty("Other", "Actor");
        s.autoProperty("Glob", "GlobalVariable");
        s.autoProperty("TheAlias", "ReferenceAlias");
        s.var("inFac", "Bool");
        s.var("team", "Bool");
        s.var("rank", "Int");
        s.var("av", "Float");
        s.var("aliasActor", "Int");
        s.var("facRank", "Int");
        s.function("SetFacRank2")
            .local("n", "None")
            .callMethod("SetFactionRank", Id("self"), Id("n"), { Id("::Fac_var"), Int(2) });
        s.function("GetFacRank")
            .local("v", "Int")
            .callMethod("GetFactionRank", Id("self"), Id("v"), { Id("::Fac_var") })
            .assign(Id("facRank"), Id("v"));
        s.function("CastCall")
            .local("n", "None")
            .local("other", "Probe2")
            .op(PexAsm::Op::Cast, { Id("other"), Id("self") })
            .callMethod("Hit", Id("other"), Id("n"));
        s.function("AddF").local("n", "None").callMethod("AddToFaction", Id("self"), Id("n"), { Id("::Fac_var") });
        s.function("RemF").local("n", "None").callMethod("RemoveFromFaction", Id("self"), Id("n"), { Id("::Fac_var") });
        s.function("InF")
            .local("b", "Bool")
            .callMethod("IsInFaction", Id("self"), Id("b"), { Id("::Fac_var") })
            .assign(Id("inFac"), Id("b"));
        s.function("TeamOn")
            .local("n", "None")
            .callMethod("SetPlayerTeammate", Id("self"), Id("n"), { Bool(true), Bool(true) });
        s.function("TeamOff")
            .local("n", "None")
            .callMethod("SetPlayerTeammate", Id("self"), Id("n"), { Bool(false), Bool(true) });
        s.function("IsTeam")
            .local("b", "Bool")
            .callMethod("IsPlayerTeammate", Id("self"), Id("b"))
            .assign(Id("team"), Id("b"));
        s.function("Rank3")
            .local("n", "None")
            .callMethod("SetRelationshipRank", Id("self"), Id("n"), { Id("::Other_var"), Int(3) });
        s.function("GetRank")
            .local("v", "Int")
            .callMethod("GetRelationshipRank", Id("self"), Id("v"), { Id("::Other_var") })
            .assign(Id("rank"), Id("v"));
        s.function("Wait1")
            .local("n", "None")
            .callMethod("SetAV", Id("self"), Id("n"), { Str("WaitingForPlayer"), Flt(1.0f) });
        s.function("Wait0")
            .local("n", "None")
            .callMethod("SetAV", Id("self"), Id("n"), { Str("WaitingForPlayer"), Flt(0.0f) });
        s.function("WaitViaSetActorValue")
            .local("n", "None")
            .callMethod("SetActorValue", Id("self"), Id("n"), { Str("WaitingForPlayer"), Flt(1.0f) });
        s.function("ReadWait")
            .local("v", "Float")
            .callMethod("GetActorValue", Id("self"), Id("v"), { Str("WaitingForPlayer") })
            .assign(Id("av"), Id("v"));
        s.function("Force").local("n", "None").callMethod("ForceRefTo", Id("::TheAlias_var"), Id("n"), { Id("self") });
        s.function("Clear").local("n", "None").callMethod("Clear", Id("::TheAlias_var"), Id("n"));
        s.function("SetGlob").local("n", "None").callMethod("SetValue", Id("::Glob_var"), Id("n"), { Flt(1.0f) });
        s.function("ReadAlias")
            .local("r", "Actor")
            .local("id", "Int")
            .callMethod("GetActorRef", Id("::TheAlias_var"), Id("r"))
            .callMethod("GetFormID", Id("r"), Id("id"))
            .assign(Id("aliasActor"), Id("id"));
        return s;
    }

    ScriptBuilder probe2Script()
    {
        ScriptBuilder s("Probe2");
        s.parent("Actor");
        s.var("hits", "Int");
        s.function("Hit").iadd(Id("hits"), Id("hits"), Int(1));
        return s;
    }

    vmad::Property objProp(const std::string& name, FormId form, std::int16_t alias = -1)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = alias;
        return p;
    }

    vmad::Script probe2On()
    {
        vmad::Script s;
        s.mName = "Probe2";
        return s;
    }

    vmad::Script probeOn()
    {
        vmad::Script s;
        s.mName = "Probe";
        s.mProperties = { objProp("Fac", kFactionA), objProp("Other", kOtherA), objProp("Glob", kGlobalA),
            objProp("TheAlias", kQuestA, 0) };
        return s;
    }

    struct HostRig
    {
        explicit HostRig(bool withState = true)
        {
            addClasses(mData);
            mData.add(probeScript());
            mData.add(probe2Script());
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kActorA] = "Actor";
            mData.mClasses[kOtherA] = "Actor";
            mData.mClasses[kFactionA] = "Form";
            mData.mClasses[kGlobalA] = "GlobalVariable";
            mData.mAliasFactions[{ kQuestA, 0 }] = { kFactionCurrent, kFactionPlayer };
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            if (withState)
                mHost->setFollowerState(&mState);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            quest.append(sub4("ANAM", Bytes().u32(1)));
            quest.append(sub4("ALST", Bytes().u32(0)));
            quest.append(sub4("ALID", Bytes().zstring("Follower")));
            quest.append(sub4("FNAM", Bytes().u32(0)));
            quest.append(sub4("ALED", Bytes()));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestA, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", packOrder(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", packOrder()));
            mQuests = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mQuests, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
            EXPECT_EQ(mHost->attachReference(kActorA, kBaseA, true, { probeOn(), probe2On() }), 2u);
            tick();
        }
        ~HostRig()
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
        bool call(const char* function)
        {
            const bool ok = mHost->callFunction(kActorA, "Probe", function);
            tick();
            return ok;
        }
        std::optional<double> number(const char* name) { return mHost->numberVariable(kActorA, "Probe", name); }

        FakeData mData;
        FollowerState mState;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mQuests;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimFollowerCmdNatives, addToFactionAndRemoveFromFactionWriteTheStateAndIsInFactionReadsIt)
    {
        HostRig rig;
        ASSERT_TRUE(rig.call("InF"));
        EXPECT_EQ(rig.number("inFac"), 0.0);
        ASSERT_TRUE(rig.call("AddF"));
        ASSERT_TRUE(rig.mState.factionRank(kActorA, kFactionA).has_value());
        EXPECT_FLOAT_EQ(*rig.mState.factionRank(kActorA, kFactionA), 0.f);
        ASSERT_TRUE(rig.call("InF"));
        EXPECT_EQ(rig.number("inFac"), 1.0);
        ASSERT_TRUE(rig.call("RemF"));
        ASSERT_TRUE(rig.mState.factionRank(kActorA, kFactionA).has_value());
        EXPECT_FLOAT_EQ(*rig.mState.factionRank(kActorA, kFactionA), -1.f);
        ASSERT_TRUE(rig.call("InF"));
        EXPECT_EQ(rig.number("inFac"), 0.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimFollowerCmdNatives, setPlayerTeammateAndIsPlayerTeammate)
    {
        HostRig rig;
        ASSERT_TRUE(rig.call("TeamOn"));
        EXPECT_TRUE(rig.mState.isPlayerTeammate(kActorA));
        ASSERT_TRUE(rig.call("IsTeam"));
        EXPECT_EQ(rig.number("team"), 1.0);
        ASSERT_TRUE(rig.call("TeamOff"));
        EXPECT_FALSE(rig.mState.isPlayerTeammate(kActorA));
        ASSERT_TRUE(rig.call("IsTeam"));
        EXPECT_EQ(rig.number("team"), 0.0);
    }

    TEST(SkyrimFollowerCmdNatives, relationshipRanksAreKeptAndReadBack)
    {
        HostRig rig;
        ASSERT_TRUE(rig.call("GetRank"));
        EXPECT_EQ(rig.number("rank"), 0.0) << "no record and nothing set: acquaintances";
        ASSERT_TRUE(rig.call("Rank3"));
        ASSERT_TRUE(rig.mState.relationshipRank(kActorA, kOtherA).has_value());
        EXPECT_FLOAT_EQ(*rig.mState.relationshipRank(kActorA, kOtherA), 3.f);
        ASSERT_TRUE(rig.call("GetRank"));
        EXPECT_EQ(rig.number("rank"), 3.0);
    }

    TEST(SkyrimFollowerCmdNatives, setAVWaitingForPlayerFlipsTheWaitTheStateReadsAndSetActorValueDoesToo)
    {
        HostRig rig;
        EXPECT_FALSE(rig.mState.waiting(kActorA));
        ASSERT_TRUE(rig.call("Wait1"));
        EXPECT_TRUE(rig.mState.waiting(kActorA));
        ASSERT_TRUE(rig.call("ReadWait"));
        EXPECT_EQ(rig.number("av"), 1.0);
        ASSERT_TRUE(rig.call("Wait0"));
        EXPECT_FALSE(rig.mState.waiting(kActorA));
        ASSERT_TRUE(rig.call("WaitViaSetActorValue"));
        EXPECT_TRUE(rig.mState.waiting(kActorA));
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimFollowerCmdNatives, aGlobalVariableSetValueReachesTheState)
    {
        HostRig rig;
        ASSERT_TRUE(rig.call("SetGlob"));
        ASSERT_TRUE(rig.mState.globalValue(kGlobalA).has_value());
        EXPECT_FLOAT_EQ(*rig.mState.globalValue(kGlobalA), 1.f);
    }

    TEST(SkyrimFollowerCmdNatives, forcingAnActorIntoTheAliasAddsItsFactionsAndClearingRemovesThem)
    {
        HostRig rig;
        ASSERT_TRUE(rig.call("Force"));
        EXPECT_EQ(rig.mHost->forcedAlias(kQuestA, 0), kActorA);
        ASSERT_TRUE(rig.mState.factionRank(kActorA, kFactionCurrent).has_value());
        EXPECT_FLOAT_EQ(*rig.mState.factionRank(kActorA, kFactionCurrent), 0.f);
        ASSERT_TRUE(rig.mState.factionRank(kActorA, kFactionPlayer).has_value());
        EXPECT_FLOAT_EQ(*rig.mState.factionRank(kActorA, kFactionPlayer), 0.f);
        ASSERT_TRUE(rig.call("ReadAlias"));
        EXPECT_EQ(rig.number("aliasActor"), static_cast<double>(kActorA)) << "GetActorRef answers the forced fill";
        ASSERT_TRUE(rig.call("Clear"));
        EXPECT_EQ(rig.mHost->forcedAlias(kQuestA, 0), 0u);
        ASSERT_TRUE(rig.mState.factionRank(kActorA, kFactionCurrent).has_value());
        EXPECT_FLOAT_EQ(*rig.mState.factionRank(kActorA, kFactionCurrent), -1.f);
        EXPECT_FLOAT_EQ(*rig.mState.factionRank(kActorA, kFactionPlayer), -1.f);
    }

    TEST(SkyrimFollowerCmdNatives, withoutAStateTheNativesAreTolerantAndAnswerNo)
    {
        HostRig rig(false);
        ASSERT_TRUE(rig.call("AddF"));
        ASSERT_TRUE(rig.call("InF"));
        EXPECT_EQ(rig.number("inFac"), 0.0);
        ASSERT_TRUE(rig.call("TeamOn"));
        ASSERT_TRUE(rig.call("Force"));
        EXPECT_EQ(rig.mHost->forcedAlias(kQuestA, 0), kActorA) << "the alias itself still works";
        EXPECT_TRUE(rig.mErrors.empty());
    }
    // A quest (or a reference) with several scripts is ONE object to a script: DialogueFollowerScript lives on a quest
    // that also carries HirelingCommentScript, and the TIF fragments and HirelingQuest cast the quest to it.
    TEST(SkyrimFollowerCmdNatives, aCastToAnotherScriptOfTheSameFormFindsThatScript)
    {
        HostRig rig;
        ASSERT_TRUE(rig.call("CastCall"));
        EXPECT_EQ(rig.mHost->numberVariable(kActorA, "Probe2", "hits"), std::optional<double>(1.0));
        EXPECT_TRUE(rig.mErrors.empty());
    }
    // Actor.AddToFaction is a SCRIPT function in the game's Actor.pex (IsInFaction, then SetFactionRank), so
    // the natives that matter for HirelingQuest.PayHireling and SetFollower are SetFactionRank and GetFactionRank.
    TEST(SkyrimFollowerCmdNatives, setFactionRankAndGetFactionRankKeepAndReadTheMembership)
    {
        HostRig rig;
        ASSERT_TRUE(rig.call("GetFacRank"));
        EXPECT_EQ(rig.number("facRank"), -2.0) << "not a member: -2";
        ASSERT_TRUE(rig.call("SetFacRank2"));
        ASSERT_TRUE(rig.mState.factionRank(kActorA, kFactionA).has_value());
        EXPECT_FLOAT_EQ(*rig.mState.factionRank(kActorA, kFactionA), 2.f);
        ASSERT_TRUE(rig.call("GetFacRank"));
        EXPECT_EQ(rig.number("facRank"), 2.0);
        ASSERT_TRUE(rig.call("InF"));
        EXPECT_EQ(rig.number("inFac"), 1.0);
        ASSERT_TRUE(rig.call("RemF"));
        ASSERT_TRUE(rig.call("GetFacRank"));
        EXPECT_EQ(rig.number("facRank"), -2.0) << "removed: not a member again";
    }
    // The line a follower topic runs leaves a script instance on its INFO (a fragment script); a save made after it
    // must restore: before this task the whole Papyrus chunk (the forced alias fill, WaitingForPlayer) was refused on
    // load ("the game no longer has what its attach key names").
    TEST(SkyrimFollowerCmdNatives, aSaveMadeAfterADialogueFragmentRanIsRestoredNotRefused)
    {
        HostRig rig;
        ScriptBuilder fragment("TIF__01000E01");
        fragment.parent("TopicInfo");
        fragment.var("ran", "Int");
        fragment.function("Fragment_0").param("akSpeakerRef", "ObjectReference").iadd(Id("ran"), Id("ran"), Int(1));
        rig.mData.add(fragment);
        vmad::Vmad v;
        v.mHasInfoFragments = true;
        vmad::Script script;
        script.mName = "TIF__01000E01";
        v.mScripts.push_back(script);
        v.mInfo.mFlags = 2;
        v.mInfo.mEndScript = "TIF__01000E01";
        v.mInfo.mEndFunction = "Fragment_0";
        ASSERT_EQ(rig.mHost->runInfoFragments(kInfoA, v, kActorA, kBaseA), 1u);
        rig.tick();
        EXPECT_EQ(rig.mHost->numberVariable(kInfoA, "TIF__01000E01", "ran"), std::optional<double>(1.0));
        const openeden::script::SaveBlob blob = rig.mHost->serialiseState();
        ASSERT_FALSE(blob.bytes.empty());
        EXPECT_TRUE(rig.mHost->restoreState(blob)) << "an INFO is something the game still has";
    }

    // Task 0270: a script's ForceRefTo is what the conversation's conditions read for a condition run on that alias
    // (the marriage quest asks GetRelationshipRank on its LoveInterest alias), so it also fills the quest state.
    TEST(SkyrimMarriageAlias, forcingAnActorIntoTheAliasFillsTheQuestStatesAliasAndClearingEmptiesIt)
    {
        HostRig rig;
        EXPECT_FALSE(rig.mQuests.aliasFill(kQuestA, 0).mKnown);
        ASSERT_TRUE(rig.call("Force"));
        const AliasFill filled = rig.mQuests.aliasFill(kQuestA, 0);
        EXPECT_TRUE(filled.mKnown);
        EXPECT_EQ(filled.mReference, kActorA);
        ASSERT_TRUE(rig.call("Clear"));
        EXPECT_FALSE(rig.mQuests.aliasFill(kQuestA, 0).mKnown);
    }
}
