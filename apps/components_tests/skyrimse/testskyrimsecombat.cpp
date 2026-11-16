// Task 0168 (milestone M-S5, stage S5-0): the combat contract (sdk/include/openeden/game/combat_rules.hpp, DRAFT 0.1)
// and the Skyrim SE pack's skeleton of it. Written from the header and decision 0018 first. No game data.
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <openeden/game/combat_rules.hpp>

#include <gtest/gtest.h>

#include <type_traits>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk::game;

    // A writer that overrides nothing: the defaults of the contract.
    class DefaultWriter final : public CombatRequestWriter
    {
    };

    // A rules class that implements only the pure functions: the default of onClipFinished.
    class MinimalRules final : public CombatRules
    {
    public:
        void onClipEvent(const ClipEvent&) override { ++mEvents; }
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override { ++mSweeps; }
        void combatUpdate(float, CombatRequestWriter&) override { ++mUpdates; }
        int mEvents = 0;
        int mSweeps = 0;
        int mUpdates = 0;
    };

    TEST(CombatContract, versionIsADraftZeroOne)
    {
        EXPECT_EQ(kCombatRulesVersion.major, 0);
        EXPECT_EQ(kCombatRulesVersion.minor, 6);
    }

    TEST(CombatContract, limitsAndEnumValuesArePinned)
    {
        EXPECT_EQ(kMaxCombatClipName, 64u);
        EXPECT_EQ(kMaxSweepHits, 16u);
        EXPECT_EQ(static_cast<int>(ClipSlot::None), 0);
        EXPECT_EQ(static_cast<int>(ClipSlot::Attack), 1);
        EXPECT_EQ(static_cast<int>(ClipSlot::PowerAttack), 2);
        EXPECT_EQ(static_cast<int>(ClipSlot::BlockStart), 4);
        EXPECT_EQ(static_cast<int>(ClipSlot::Death), 13);
        EXPECT_EQ(static_cast<int>(ClipEventKind::Other), 0);
        EXPECT_EQ(static_cast<int>(ClipEventKind::Hit), 3);
        EXPECT_EQ(static_cast<int>(ClipEventKind::RecoilStop), 15);
        EXPECT_EQ(static_cast<int>(ClipEnd::Completed), 0);
        EXPECT_EQ(static_cast<int>(ClipEnd::Interrupted), 1);
        EXPECT_EQ(static_cast<int>(ClipEnd::Failed), 2);
        EXPECT_EQ(static_cast<int>(SweepTarget::Actor), 0);
        EXPECT_EQ(static_cast<int>(SweepTarget::Player), 1);
        EXPECT_EQ(static_cast<int>(SweepTarget::World), 2);
    }

    TEST(CombatContract, theValueTypesArePlainAndDefaultToNothing)
    {
        static_assert(std::is_trivially_copyable_v<SweepRequest>);
        static_assert(std::is_trivially_copyable_v<SweepHit>);
        static_assert(std::is_trivially_copyable_v<DamageReport>);
        static_assert(std::is_trivially_copyable_v<ClipEvent>);
        const ClipRequest request;
        EXPECT_EQ(request.slot, ClipSlot::None);
        EXPECT_TRUE(request.clip.empty());
        EXPECT_FALSE(request.clipPerSex);
        const SweepHit hit;
        EXPECT_EQ(hit.target, SweepTarget::World);
        EXPECT_FALSE(hit.actor.valid());
        const DamageReport report;
        EXPECT_FALSE(report.attackerIsPlayer);
        EXPECT_FALSE(report.victimIsPlayer);
        EXPECT_FALSE(report.victim.valid());
    }

    TEST(CombatSubjectType, anActorOrThePlayerButNeverNothing)
    {
        EXPECT_FALSE(CombatSubject{}.valid());
        EXPECT_TRUE(CombatSubject::ofPlayer().valid());
        EXPECT_TRUE(CombatSubject::of({ 5, 1 }).valid());
        EXPECT_FALSE(CombatSubject::of({ 5, 0 }).valid()); // session 0 is no handle
        EXPECT_FALSE(CombatSubject::of({}).valid());
        EXPECT_EQ(CombatSubject::ofPlayer(), CombatSubject::ofPlayer());
        EXPECT_NE(CombatSubject::ofPlayer(), CombatSubject::of({ 5, 1 }));
        EXPECT_EQ(CombatSubject::of({ 5, 1 }), CombatSubject::of({ 5, 1 }));
    }

    TEST(CombatContract, thePlayerCanBeTheSubjectOfEveryRequest)
    {
        DefaultWriter writer;
        const CombatSubject player = CombatSubject::ofPlayer();
        ClipRequest request;
        request.slot = ClipSlot::Attack;
        request.clip = "attack_one";
        // The defaults refuse for the player too; the point is that the call exists for a subject with no handle.
        EXPECT_FALSE(writer.playClip(player, request));
        EXPECT_FALSE(writer.stopClip(player));
        EXPECT_FALSE(writer.requestSweep(player, SweepRequest{}));
    }

    TEST(CombatContract, theWriterDefaultsRefuseEverything)
    {
        DefaultWriter writer;
        const CombatSubject actor = CombatSubject::of({ 5, 1 });
        ClipRequest request;
        request.slot = ClipSlot::Attack;
        request.clip = "attack_one";
        EXPECT_FALSE(writer.playClip(actor, request));
        EXPECT_FALSE(writer.stopClip(actor));
        EXPECT_FALSE(writer.requestSweep(actor, SweepRequest{}));
        EXPECT_FALSE(writer.reportDamage(DamageReport{}));
        EXPECT_FALSE(writer.reportDeath(ActorHandle{ 5, 1 }));
    }

    TEST(CombatContract, onClipFinishedHasADefaultThatIgnoresTheCall)
    {
        MinimalRules rules;
        CombatRules& base = rules;
        base.onClipFinished(CombatSubject::of({ 1, 1 }), 7, ClipEnd::Completed);
        base.onPlayerAttack(true);
        EXPECT_EQ(rules.mEvents, 0);
        EXPECT_EQ(rules.mSweeps, 0);
        EXPECT_EQ(rules.mUpdates, 0);
    }

    TEST(SkyrimSlotTable, theSkeletonHasNoClipsAndNoSlotsBeyondTheEnum)
    {
        const SlotTable table;
        EXPECT_EQ(table.size(), 0u);
        EXPECT_TRUE(table.clipFor(ClipSlot::Attack, 1).empty());
        EXPECT_TRUE(table.clipFor(ClipSlot::Death, 0).empty());
        EXPECT_TRUE(table.clipFor(ClipSlot::None, 0).empty());
    }

    TEST(SkyrimCombat, theSkeletonRefusesAndCounts)
    {
        SkyrimCombat combat;
        DefaultWriter writer;
        EXPECT_EQ(combat.updates(), 0u);
        combat.combatUpdate(0.016f, writer);
        combat.combatUpdate(0.016f, writer);
        EXPECT_EQ(combat.updates(), 2u);

        ClipEvent event;
        event.subject = CombatSubject::ofPlayer();
        event.kind = ClipEventKind::Hit;
        combat.onClipEvent(event);
        EXPECT_EQ(combat.refusedEvents(), 1u);

        const SweepHit hit;
        combat.onSweepResult(CombatSubject::of({ 3, 1 }), 9, &hit, 1);
        combat.onSweepResult(CombatSubject::ofPlayer(), 10, nullptr, 0);
        EXPECT_EQ(combat.refusedSweepResults(), 2u);

        // Finished clips are ignored without counting (the default of the contract).
        combat.onClipFinished(CombatSubject::of({ 3, 1 }), 9, ClipEnd::Interrupted);
        combat.onPlayerAttack(false);
        EXPECT_EQ(combat.refusedEvents(), 1u);
    }

    TEST(SkyrimCombat, theSkeletonWritesNothingToTheWriter)
    {
        struct Recording final : CombatRequestWriter
        {
            bool playClip(CombatSubject, const ClipRequest&) override
            {
                ++mCalls;
                return true;
            }
            bool stopClip(CombatSubject) override
            {
                ++mCalls;
                return true;
            }
            bool requestSweep(CombatSubject, const SweepRequest&) override
            {
                ++mCalls;
                return true;
            }
            bool reportDamage(const DamageReport&) override
            {
                ++mCalls;
                return true;
            }
            bool reportDeath(ActorHandle) override
            {
                ++mCalls;
                return true;
            }
            int mCalls = 0;
        };
        SkyrimCombat combat;
        Recording writer;
        for (int i = 0; i < 50; ++i)
            combat.combatUpdate(0.016f, writer);
        EXPECT_EQ(writer.mCalls, 0);
    }

    TEST(SkyrimCellSourceCombat, theRegisteredSourceIsFoundByDynamicCast)
    {
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        WorldCellSource* asBase = &source;
        auto* rules = dynamic_cast<CombatRules*>(asBase);
        ASSERT_NE(rules, nullptr);
        // The same object also answers as the other optional interfaces: the contracts are independent.
        EXPECT_NE(dynamic_cast<ActorRules*>(asBase), nullptr);
        EXPECT_EQ(static_cast<void*>(dynamic_cast<CombatRules*>(asBase)), static_cast<void*>(rules));

        DefaultWriter writer;
        rules->combatUpdate(0.016f, writer);
        EXPECT_EQ(source.combat().updates(), 1u);
    }
}
