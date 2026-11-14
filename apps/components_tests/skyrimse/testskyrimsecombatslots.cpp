// Task 0170 (combat stage S5-2): the Skyrim pack's slot table and its test swing. The names are those of
// Skyrim - Animations.bsa (meshes/actors/character/animations, read from the archive listing).
#include <game_skyrimse/combat.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using game_skyrimse::SkyrimCombat;
    using game_skyrimse::SlotTable;

    class RecordingWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject subject, const ClipRequest& request) override
        {
            mSubjects.push_back(subject);
            mClips.emplace_back(request.clip);
            mSlots.push_back(request.slot);
            mTokens.push_back(request.token);
            mPerSex.push_back(request.clipPerSex);
            return mAccept;
        }
        std::vector<CombatSubject> mSubjects;
        std::vector<std::string> mClips;
        std::vector<ClipSlot> mSlots;
        std::vector<std::uint32_t> mTokens;
        std::vector<bool> mPerSex;
        bool mAccept = true;
    };

    ActorInfo actor(WorldId reference, std::uint32_t session, std::string_view editorId)
    {
        ActorInfo info;
        info.handle = { reference, session };
        info.editorId = editorId;
        return info;
    }

    TEST(SkyrimCombatSlots, theOneHandedSwordTableNamesTheHumanoidClips)
    {
        const SlotTable table = SlotTable::humanoid();
        EXPECT_EQ(table.size(), 7u);
        EXPECT_EQ(table.clipFor(ClipSlot::Attack, 1), "1HM_AttackRight");
        EXPECT_EQ(table.clipFor(ClipSlot::AttackLeft, 1), "1HM_AttackLeft");
        EXPECT_EQ(table.clipFor(ClipSlot::PowerAttack, 1), "1HM_AttackPower");
        EXPECT_EQ(table.clipFor(ClipSlot::Recoil, 1), "1HM_RecoilRight");
        EXPECT_EQ(table.clipFor(ClipSlot::Stagger, 1), "1HM_StaggerBackMedium");
        EXPECT_EQ(table.clipFor(ClipSlot::Bash, 1), "1HM_BlockBash");
        EXPECT_EQ(table.clipFor(ClipSlot::BlockHit, 1), "1HM_BlockHit");
    }

    TEST(SkyrimCombatSlots, otherWeaponTypesAndSlotsHaveNoClipYet)
    {
        const SlotTable table = SlotTable::humanoid();
        EXPECT_TRUE(table.clipFor(ClipSlot::Attack, 0).empty());
        EXPECT_TRUE(table.clipFor(ClipSlot::Attack, 5).empty());
        EXPECT_TRUE(table.clipFor(ClipSlot::BowDraw, 7).empty());
        EXPECT_TRUE(table.clipFor(ClipSlot::Death, 1).empty());
        EXPECT_TRUE(table.clipFor(ClipSlot::None, 1).empty());
        EXPECT_EQ(SlotTable().size(), 0u); // the default table is still empty
    }

    TEST(SkyrimCombatSlots, withoutTheSettingNoSwingIsEverRequested)
    {
        SkyrimCombat combat;
        combat.onActorAdded(actor(10, 1, "BanneredMareHulda"));
        RecordingWriter writer;
        for (int i = 0; i < 100; ++i)
            combat.combatUpdate(0.1f, writer);
        EXPECT_TRUE(writer.mClips.empty());
        EXPECT_EQ(combat.swingers(), 0u);
    }

    TEST(SkyrimCombatSlots, theNamedActorSwingsAfterTheFirstDelayAndThenEveryPeriod)
    {
        SkyrimCombat combat;
        ASSERT_TRUE(combat.setTestSwing("BanneredMareHulda"));
        combat.onActorAdded(actor(10, 1, "banneredmarehulda")); // case-insensitive
        combat.onActorAdded(actor(11, 2, "SomeoneElse"));
        EXPECT_EQ(combat.swingers(), 1u);
        RecordingWriter writer;
        combat.combatUpdate(2.9f, writer);
        EXPECT_TRUE(writer.mClips.empty());
        combat.combatUpdate(0.2f, writer); // clock 3.1 >= 3
        ASSERT_EQ(writer.mClips.size(), 1u);
        EXPECT_EQ(writer.mClips[0], "1HM_AttackRight");
        EXPECT_EQ(writer.mSlots[0], ClipSlot::Attack);
        EXPECT_FALSE(writer.mPerSex[0]);
        EXPECT_TRUE(writer.mSubjects[0] == CombatSubject::of(ActorHandle{ 10, 1 }));
        combat.combatUpdate(3.8f, writer); // clock 6.9 < 3.1 + 4
        EXPECT_EQ(writer.mClips.size(), 1u);
        combat.combatUpdate(0.3f, writer); // clock 7.2 >= 7.1
        ASSERT_EQ(writer.mClips.size(), 2u);
        EXPECT_NE(writer.mTokens[0], writer.mTokens[1]);
        EXPECT_EQ(combat.swingsRequested(), 2u);
    }

    TEST(SkyrimCombatSlots, aRemovedActorStopsSwingingAndAReplacedSessionTakesTheNewHandle)
    {
        SkyrimCombat combat;
        combat.setTestSwing("hulda");
        combat.onActorAdded(actor(10, 1, "Hulda"));
        combat.onActorAdded(actor(10, 2, "Hulda")); // the same placement, a new session
        EXPECT_EQ(combat.swingers(), 1u);
        RecordingWriter writer;
        combat.combatUpdate(4.f, writer);
        ASSERT_EQ(writer.mSubjects.size(), 1u);
        EXPECT_EQ(writer.mSubjects[0].actor.session, 2u);
        combat.onActorRemoved(ActorHandle{ 10, 2 });
        EXPECT_EQ(combat.swingers(), 0u);
        combat.combatUpdate(10.f, writer);
        EXPECT_EQ(writer.mClips.size(), 1u);
    }

    TEST(SkyrimCombatSlots, aSettingThatIsTooLongIsRefusedAndTheRuleStaysOff)
    {
        SkyrimCombat combat;
        EXPECT_FALSE(combat.setTestSwing(std::string(200, 'x')));
        EXPECT_TRUE(combat.testSwing().empty());
    }

    TEST(SkyrimCombatSlots, eventsAndFinishesAreCounted)
    {
        SkyrimCombat combat;
        ClipEvent event;
        combat.onClipEvent(event);
        combat.onClipFinished(CombatSubject::of(ActorHandle{ 1, 1 }), 1, ClipEnd::Completed);
        combat.onClipFinished(CombatSubject::of(ActorHandle{ 1, 1 }), 2, ClipEnd::Interrupted);
        EXPECT_EQ(combat.eventsSeen(), 1u);
        EXPECT_EQ(combat.clipsFinished(), 2u);
        EXPECT_EQ(combat.clipsCompleted(), 1u);
    }
}
