// Task 0203: ProgressionService::raiseSkill, the one hook the skill books need. Written before the code and run red
// against a stub first. No game data.
#include <game_skyrimse/progression_service.hpp>

#include <gtest/gtest.h>

#include <string>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::progression;

    class FakePerks final : public PerkLibrary
    {
    public:
        const PerkRecord* perk(FormId) const override { return nullptr; }
    };

    SkillTable makeTable(float mult, float offset, bool withData = true)
    {
        SkillTable table;
        for (std::size_t s = 0; s < kSkillCount; ++s)
        {
            SkillRecord r;
            r.mCommon.mId = 0x44C + static_cast<FormId>(s);
            r.mCommon.mEditorId = std::string(skillEditorId(s));
            r.mHasSkillData = withData;
            r.mUseMult = mult;
            r.mUseOffset = offset;
            r.mImproveMult = 1.f;
            table.add(r);
        }
        return table;
    }

    struct Rig
    {
        SkillTable mTable;
        FakePerks mPerks;
        std::unique_ptr<ProgressionService> mService;

        explicit Rig(float mult = 1.f, float offset = 0.f, bool withData = true)
            : mTable(makeTable(mult, offset, withData))
        {
            ServiceHooks hooks;
            hooks.mInputs = [this](Inputs& out) {
                out.mSkills = &mTable;
                out.mPerks = &mPerks;
                return true;
            };
            mService = std::make_unique<ProgressionService>(std::move(hooks));
        }
        std::uint8_t skill(std::size_t slot) { return mService->progression()->skill(slot); }
    };

    TEST(SkyrimRaiseSkill, itDoesNothingBeforeTheRecordsAreRead)
    {
        Rig rig;
        EXPECT_FALSE(rig.mService->raiseSkill(3));
        ASSERT_TRUE(rig.mService->poll());
        EXPECT_EQ(rig.skill(3), 15u);
    }

    TEST(SkyrimRaiseSkill, eachCallRaisesTheSkillByExactlyOneLevel)
    {
        Rig rig;
        ASSERT_TRUE(rig.mService->poll());
        for (std::uint8_t expected = 16; expected <= 20; ++expected)
        {
            EXPECT_TRUE(rig.mService->raiseSkill(3));
            EXPECT_EQ(rig.skill(3), expected);
        }
        EXPECT_EQ(rig.skill(4), 15u); // no other skill moved
    }

    TEST(SkyrimRaiseSkill, experienceAlreadyGainedDoesNotMakeItRaiseTwice)
    {
        Rig rig;
        ASSERT_TRUE(rig.mService->poll());
        rig.mService->skillUsed(5, 3.f); // some experience, short of a level
        const std::uint8_t before = rig.skill(5);
        ASSERT_EQ(before, 15u);
        EXPECT_TRUE(rig.mService->raiseSkill(5));
        EXPECT_EQ(rig.skill(5), 16u);
    }

    TEST(SkyrimRaiseSkill, itWorksWithOtherUseNumbers)
    {
        Rig rig(0.37f, 5.f);
        ASSERT_TRUE(rig.mService->poll());
        for (std::uint8_t expected = 16; expected <= 19; ++expected)
        {
            EXPECT_TRUE(rig.mService->raiseSkill(8));
            EXPECT_EQ(rig.skill(8), expected);
        }
    }

    TEST(SkyrimRaiseSkill, aSkillAtTheTopOrWithoutUseNumbersIsRefused)
    {
        {
            Rig rig;
            ASSERT_TRUE(rig.mService->poll());
            rig.mService->skillUsed(2, 1.0e9f);
            ASSERT_EQ(rig.skill(2), 100u);
            EXPECT_FALSE(rig.mService->raiseSkill(2));
            EXPECT_EQ(rig.skill(2), 100u);
        }
        {
            Rig rig(0.f, 0.f);
            ASSERT_TRUE(rig.mService->poll());
            EXPECT_FALSE(rig.mService->raiseSkill(1));
            EXPECT_EQ(rig.skill(1), 15u);
        }
        {
            Rig rig;
            ASSERT_TRUE(rig.mService->poll());
            EXPECT_FALSE(rig.mService->raiseSkill(kSkillCount));
            EXPECT_FALSE(rig.mService->raiseSkill(9999));
        }
    }

    TEST(SkyrimRaiseSkill, theRevisionMoves)
    {
        Rig rig;
        ASSERT_TRUE(rig.mService->poll());
        const std::uint64_t before = rig.mService->progression()->revision();
        EXPECT_TRUE(rig.mService->raiseSkill(0));
        EXPECT_NE(rig.mService->progression()->revision(), before);
    }
}
