// Task 0200: the progression service (game_skyrimse/progression_service.hpp) without a game: building when the records
// are there, the use of a skill, the level hook (LEVL) raised once per level and never for a restored one, the save
// chunk (round trip, a restore held until the records are read, refusals, an empty chunk makes a new character) and the
// actor values. The script bindings are exercised in game only (no bindings host here). Written AFTER the code.
#include <game_skyrimse/progression_service.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::progression;

    class FakePerks final : public PerkLibrary
    {
    public:
        const PerkRecord* perk(FormId) const override { return nullptr; }
    };

    SkillTable makeTable()
    {
        SkillTable table;
        for (std::size_t s = 0; s < kSkillCount; ++s)
        {
            SkillRecord r;
            r.mCommon.mId = 0x44C + static_cast<FormId>(s);
            r.mCommon.mEditorId = std::string(skillEditorId(s));
            r.mHasSkillData = true;
            r.mUseMult = 1.f;
            r.mImproveMult = 1.f;
            table.add(r);
        }
        return table;
    }

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* b = static_cast<const std::uint8_t*>(data);
            mBytes.insert(mBytes.end(), b, b + size);
        }
        std::vector<std::uint8_t> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(std::vector<std::uint8_t> bytes)
            : mBytes(std::move(bytes))
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (mBytes.size() - mPos < size)
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }

    private:
        std::vector<std::uint8_t> mBytes;
        std::size_t mPos = 0;
    };

    struct Rig
    {
        SkillTable mTable = makeTable();
        FakePerks mPerks;
        bool mRecordsRead = true;
        std::vector<std::uint16_t> mLevels;
        std::vector<std::string> mLog;
        std::unique_ptr<ProgressionService> mService;

        Rig()
        {
            ServiceHooks hooks;
            hooks.mInputs = [this](Inputs& out) {
                if (!mRecordsRead)
                    return false;
                out.mSkills = &mTable;
                out.mPerks = &mPerks;
                out.mSettings.mSkillUseCurve = 2.f;
                return true;
            };
            hooks.mLevelUp = [this](std::uint16_t level) { mLevels.push_back(level); };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<ProgressionService>(std::move(hooks));
        }
    };

    TEST(SkyrimProgressionService, itIsNotReadyUntilTheRecordsAreRead)
    {
        Rig rig;
        rig.mRecordsRead = false;
        EXPECT_FALSE(rig.mService->poll());
        EXPECT_FALSE(rig.mService->ready());
        rig.mService->skillUsed(0, 1.0e6f); // ignored
        rig.mRecordsRead = true;
        EXPECT_TRUE(rig.mService->poll());
        ASSERT_TRUE(rig.mService->ready());
        EXPECT_EQ(rig.mService->progression()->skill(0), 15u);
        EXPECT_EQ(rig.mService->progression()->level(), 1u);
    }

    TEST(SkyrimProgressionService, aSkillUseRaisesTheSkillAndTheLevelHookRunsOncePerLevel)
    {
        Rig rig;
        ASSERT_TRUE(rig.mService->poll());
        rig.mService->skillUsed(0, 1.0e6f);
        EXPECT_GT(rig.mService->progression()->skill(0), 15u);
        EXPECT_TRUE(rig.mLevels.empty()); // a level up is chosen, not automatic
        EXPECT_TRUE(rig.mService->chooseLevelUp(Vital::Health));
        EXPECT_TRUE(rig.mService->chooseLevelUp(Vital::Magicka));
        EXPECT_EQ(rig.mLevels, (std::vector<std::uint16_t>{ 2, 3 }));
        EXPECT_TRUE(rig.mService->poll());
        EXPECT_EQ(rig.mLevels.size(), 2u); // not again
    }

    TEST(SkyrimProgressionService, aRefusedChoiceAndAUnknownPerkSayWhy)
    {
        Rig rig;
        ASSERT_TRUE(rig.mService->poll());
        EXPECT_FALSE(rig.mService->chooseLevelUp(Vital::Health));
        EXPECT_FALSE(rig.mService->lastResult().empty());
        EXPECT_FALSE(rig.mService->buyPerk(0x12345));
        EXPECT_NE(rig.mService->lastResult().find("perk"), std::string::npos);
        EXPECT_TRUE(rig.mLevels.empty());
    }

    TEST(SkyrimProgressionService, theChunkRoundTripsAndARestoredLevelIsNotANewEvent)
    {
        Rig a;
        ASSERT_TRUE(a.mService->poll());
        a.mService->skillUsed(3, 1.0e6f);
        ASSERT_TRUE(a.mService->chooseLevelUp(Vital::Stamina));
        MemoryWriter writer;
        a.mService->chunk().write(writer);
        ASSERT_FALSE(writer.mBytes.empty());

        Rig b;
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(b.mService->chunk().read(reader, 1)) << b.mService->refusal();
        EXPECT_FALSE(b.mService->ready()); // held until the records are read
        MemoryWriter held;
        b.mService->chunk().write(held); // a save made now keeps what was held
        EXPECT_EQ(held.mBytes, writer.mBytes);
        ASSERT_TRUE(b.mService->poll());
        EXPECT_EQ(b.mService->progression()->level(), 2u);
        EXPECT_EQ(b.mService->progression()->skill(3), a.mService->progression()->skill(3));
        EXPECT_FLOAT_EQ(b.mService->progression()->vitalBonus(Vital::Stamina), 10.f);
        EXPECT_TRUE(b.mLevels.empty()); // LEVL is for new levels only
        MemoryWriter again;
        b.mService->chunk().write(again);
        EXPECT_EQ(again.mBytes, writer.mBytes);
    }

    TEST(SkyrimProgressionService, aChunkReadIntoARunningServiceReplacesItsState)
    {
        Rig a;
        ASSERT_TRUE(a.mService->poll());
        a.mService->skillUsed(3, 1.0e6f);
        MemoryWriter writer;
        a.mService->chunk().write(writer);
        Rig b;
        ASSERT_TRUE(b.mService->poll());
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(b.mService->chunk().read(reader, 1)) << b.mService->refusal();
        EXPECT_EQ(b.mService->progression()->skill(3), a.mService->progression()->skill(3));
    }

    TEST(SkyrimProgressionService, anEmptyChunkMakesANewCharacterAndBadChunksAreRefused)
    {
        Rig a;
        ASSERT_TRUE(a.mService->poll());
        a.mService->skillUsed(3, 1.0e6f);
        ASSERT_TRUE(a.mService->chooseLevelUp(Vital::Health));
        {
            MemoryWriter empty;
            empty.writeValue(std::uint32_t(0));
            MemoryReader reader(empty.mBytes);
            EXPECT_TRUE(a.mService->chunk().read(reader, 1));
            EXPECT_EQ(a.mService->progression()->level(), 1u);
            EXPECT_EQ(a.mService->progression()->skill(3), 15u);
            EXPECT_EQ(a.mService->progression()->perkPoints(), 0u);
        }
        {
            MemoryWriter w;
            w.writeValue(std::uint32_t(0));
            MemoryReader reader(w.mBytes);
            EXPECT_FALSE(a.mService->chunk().read(reader, 7)); // a version it does not know
            EXPECT_FALSE(a.mService->refusal().empty());
        }
        {
            MemoryWriter w;
            w.writeValue(std::uint32_t(99)); // a length that does not match
            MemoryReader reader(w.mBytes);
            EXPECT_FALSE(a.mService->chunk().read(reader, 1));
        }
        {
            MemoryWriter w;
            w.writeValue(std::uint32_t(4));
            w.writeValue(std::uint32_t(0xDEADBEEF)); // not a progression blob
            MemoryReader reader(w.mBytes);
            EXPECT_FALSE(a.mService->chunk().read(reader, 1));
            EXPECT_EQ(a.mService->progression()->level(), 1u); // nothing half applied
        }
    }

    TEST(SkyrimProgressionService, anActorValueIsASkillAtSixToTwentyThree)
    {
        Rig rig;
        float value = -1.f;
        EXPECT_FALSE(rig.mService->actorValue(6, value)); // not ready
        ASSERT_TRUE(rig.mService->poll());
        EXPECT_TRUE(rig.mService->actorValue(17, value));
        EXPECT_FLOAT_EQ(value, 15.f);
        EXPECT_TRUE(rig.mService->actorValue(23, value));
        EXPECT_FALSE(rig.mService->actorValue(5, value));
        EXPECT_FALSE(rig.mService->actorValue(24, value));
    }

    TEST(SkyrimProgressionService, aPerkNameIsTheEditorIdInWords)
    {
        EXPECT_EQ(prettyPerkName("ArmsmanOneHanded00"), "Armsman One Handed");
        EXPECT_EQ(prettyPerkName("Armsman00"), "Armsman");
        EXPECT_EQ(prettyPerkName("00"), "00");
        EXPECT_TRUE(prettyPerkName("").empty());
    }
}
