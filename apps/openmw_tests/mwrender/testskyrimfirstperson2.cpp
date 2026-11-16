// Task 0247 (the player's first-person view, second slice): the pure rules of drawing and sheathing the hands, the
// cross-fade between clips and the weapon the hands show. No renderer, no file. Written BEFORE the code (against stubs
// that answer nothing: see docs/tasks/0247-skyrim-first-person-2.md for the red counts).
#include <apps/openmw/mwrender/skyrimfirstperson.hpp>
#include <apps/openmw/mwrender/skyrimheldgear.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>

namespace
{
    using namespace MWRender::SkyrimFirstPerson;

    TEST(SkyrimFirstPersonDrawClips, everyWeaponTypeRaisesAndLowersTheHandsWithItsOwnClip)
    {
        EXPECT_EQ(equipClipFor(1, Stance::Weapon), "1hm_equip");
        EXPECT_EQ(equipClipFor(2, Stance::Weapon), "dag_equip");
        EXPECT_EQ(equipClipFor(3, Stance::Weapon), "axe_equip");
        EXPECT_EQ(equipClipFor(4, Stance::Weapon), "mac_equip");
        EXPECT_EQ(equipClipFor(5, Stance::Weapon), "2hc_equip");
        EXPECT_EQ(equipClipFor(6, Stance::Weapon), "2hw_equip");
        EXPECT_EQ(equipClipFor(7, Stance::Weapon), "bow_equip");
        EXPECT_EQ(equipClipFor(0, Stance::Weapon), "h2h_equip");
        EXPECT_EQ(unequipClipFor(1, Stance::Weapon), "1hm_unequip");
        EXPECT_EQ(unequipClipFor(2, Stance::Weapon), "dag_unequip");
        EXPECT_EQ(unequipClipFor(3, Stance::Weapon), "axe_unequip");
        EXPECT_EQ(unequipClipFor(4, Stance::Weapon), "mac_unequip");
        EXPECT_EQ(unequipClipFor(5, Stance::Weapon), "2hc_unequip");
        EXPECT_EQ(unequipClipFor(6, Stance::Weapon), "2hw_unequip");
        EXPECT_EQ(unequipClipFor(7, Stance::Weapon), "bow_unequip");
        EXPECT_EQ(unequipClipFor(0, Stance::Weapon), "h2h_unequip");
    }

    TEST(SkyrimFirstPersonDrawClips, aSpellHandHasItsOwnClipsWhateverTheWeaponIs)
    {
        for (const int type : { 0, 1, 5, 6 })
        {
            EXPECT_EQ(equipClipFor(static_cast<std::uint8_t>(type), Stance::Spell), "mrh_equip") << type;
            EXPECT_EQ(unequipClipFor(static_cast<std::uint8_t>(type), Stance::Spell), "mrh_unequip") << type;
        }
    }

    TEST(SkyrimFirstPersonDrawClips, sheathedHandsAndUnknownTypesHaveNoDrawClip)
    {
        EXPECT_TRUE(equipClipFor(1, Stance::Sheathed).empty());
        EXPECT_TRUE(unequipClipFor(1, Stance::Sheathed).empty());
        EXPECT_EQ(equipClipFor(200, Stance::Weapon), "h2h_equip"); // an unknown type is the fists
    }

    TEST(SkyrimFirstPersonDrawClips, everyDrawClipNameIsAPlainFirstPersonClipName)
    {
        for (std::uint8_t type = 0; type <= 7; ++type)
            for (const Stance stance : { Stance::Weapon, Stance::Spell })
            {
                EXPECT_FALSE(armsClipName(equipClipFor(type, stance)).empty()) << int(type);
                EXPECT_FALSE(armsClipName(unequipClipFor(type, stance)).empty()) << int(type);
            }
    }

    TEST(SkyrimFirstPersonDrawTransition, aChangeOfTheDrawStatePlaysTheMatchingClip)
    {
        EXPECT_EQ(drawTransitionClip(1, kDrawNothing, kDrawWeapon), "1hm_equip");
        EXPECT_EQ(drawTransitionClip(5, kDrawNothing, kDrawWeapon), "2hc_equip");
        EXPECT_EQ(drawTransitionClip(1, kDrawWeapon, kDrawNothing), "1hm_unequip");
        EXPECT_EQ(drawTransitionClip(0, kDrawWeapon, kDrawNothing), "h2h_unequip");
        EXPECT_EQ(drawTransitionClip(1, kDrawNothing, kDrawSpell), "mrh_equip");
        EXPECT_EQ(drawTransitionClip(1, kDrawSpell, kDrawNothing), "mrh_unequip");
    }

    TEST(SkyrimFirstPersonDrawTransition, goingFromTheWeaponToASpellRaisesTheSpellHandAndBackRaisesTheWeapon)
    {
        EXPECT_EQ(drawTransitionClip(1, kDrawWeapon, kDrawSpell), "mrh_equip");
        EXPECT_EQ(drawTransitionClip(2, kDrawSpell, kDrawWeapon), "dag_equip");
    }

    TEST(SkyrimFirstPersonDrawTransition, noChangeAndNonsenseValuesPlayNothingOrTheSafestClip)
    {
        EXPECT_TRUE(drawTransitionClip(1, kDrawWeapon, kDrawWeapon).empty());
        EXPECT_TRUE(drawTransitionClip(1, kDrawNothing, kDrawNothing).empty());
        EXPECT_TRUE(drawTransitionClip(1, kDrawSpell, kDrawSpell).empty());
        // a value above 2 is read as nothing
        EXPECT_TRUE(drawTransitionClip(1, 9, kDrawNothing).empty());
        EXPECT_EQ(drawTransitionClip(1, 9, kDrawWeapon), "1hm_equip");
    }

    TEST(SkyrimFirstPersonStance, theEnginesDrawStateDecidesAndTheHoldsOnlyCoverAnUndrawnPlayer)
    {
        EXPECT_EQ(stanceFor(kDrawWeapon, false, false), Stance::Weapon);
        EXPECT_EQ(stanceFor(kDrawSpell, false, false), Stance::Spell);
        EXPECT_EQ(stanceFor(kDrawNothing, false, false), Stance::Sheathed);
        // an attack without drawing (the test rule, the old behaviour) shows the hands for the hold
        EXPECT_EQ(stanceFor(kDrawNothing, true, false), Stance::Weapon);
        EXPECT_EQ(stanceFor(kDrawNothing, false, true), Stance::Spell);
        EXPECT_EQ(stanceFor(kDrawNothing, true, true), Stance::Spell);
        // a drawn weapon is not turned into a spell hand by an old hold, and the other way round
        EXPECT_EQ(stanceFor(kDrawWeapon, false, true), Stance::Weapon);
        EXPECT_EQ(stanceFor(kDrawSpell, true, false), Stance::Spell);
    }

    TEST(SkyrimFirstPersonStance, theWeaponIsInTheHandWhileDrawnOrWhileItIsBeingPutAway)
    {
        EXPECT_TRUE(weaponInHand(Stance::Weapon, false));
        EXPECT_TRUE(weaponInHand(Stance::Weapon, true));
        EXPECT_TRUE(weaponInHand(Stance::Sheathed, true));
        EXPECT_FALSE(weaponInHand(Stance::Sheathed, false));
        EXPECT_FALSE(weaponInHand(Stance::Spell, false));
    }

    HavokPack::Transform tr(float x, float y, float z, std::array<float, 4> q, float s = 1.f)
    {
        HavokPack::Transform t;
        t.mTranslation = { x, y, z };
        t.mRotation = q;
        t.mScale = { s, s, s };
        return t;
    }

    float rotLength(const HavokPack::Transform& t)
    {
        return std::sqrt(t.mRotation[0] * t.mRotation[0] + t.mRotation[1] * t.mRotation[1]
            + t.mRotation[2] * t.mRotation[2] + t.mRotation[3] * t.mRotation[3]);
    }

    TEST(SkyrimFirstPersonBlendPose, theEndsAreTheTwoPoses)
    {
        const auto a = tr(1.f, 2.f, 3.f, { 0.f, 0.f, 0.f, 1.f });
        const auto b = tr(5.f, 6.f, 7.f, { 0.f, 0.f, 0.70710678f, 0.70710678f }, 2.f);
        const auto at0 = blendTransform(a, b, 0.f);
        const auto at1 = blendTransform(a, b, 1.f);
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_NEAR(at0.mTranslation[i], a.mTranslation[i], 1e-6f);
            EXPECT_NEAR(at1.mTranslation[i], b.mTranslation[i], 1e-6f);
        }
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_NEAR(at0.mRotation[i], a.mRotation[i], 1e-5f);
            EXPECT_NEAR(at1.mRotation[i], b.mRotation[i], 1e-5f);
        }
        EXPECT_NEAR(at0.mScale[0], 1.f, 1e-6f);
        EXPECT_NEAR(at1.mScale[2], 2.f, 1e-6f);
    }

    TEST(SkyrimFirstPersonBlendPose, theMiddleIsHalfwayAndTheRotationStaysAUnitQuaternion)
    {
        const auto a = tr(0.f, 0.f, 0.f, { 0.f, 0.f, 0.f, 1.f });
        const auto b = tr(10.f, -4.f, 2.f, { 0.f, 0.f, 0.70710678f, 0.70710678f }, 3.f);
        const auto m = blendTransform(a, b, 0.5f);
        EXPECT_NEAR(m.mTranslation[0], 5.f, 1e-5f);
        EXPECT_NEAR(m.mTranslation[1], -2.f, 1e-5f);
        EXPECT_NEAR(m.mTranslation[2], 1.f, 1e-5f);
        EXPECT_NEAR(m.mScale[1], 2.f, 1e-5f);
        EXPECT_NEAR(rotLength(m), 1.f, 1e-5f);
        // halfway between no turn and a quarter turn about z is an eighth of a turn about z
        EXPECT_NEAR(m.mRotation[2], std::sin(3.14159265f / 8.f), 2e-3f);
        EXPECT_NEAR(m.mRotation[3], std::cos(3.14159265f / 8.f), 2e-3f);
    }

    TEST(SkyrimFirstPersonBlendPose, theShortWayRoundIsTakenWhenTheQuaternionsAreOppositeSigns)
    {
        // q and -q are the same turn: the blend must not swing through the long way
        const auto a = tr(0.f, 0.f, 0.f, { 0.f, 0.f, 0.f, 1.f });
        const auto b = tr(0.f, 0.f, 0.f, { 0.f, 0.f, 0.f, -1.f });
        const auto m = blendTransform(a, b, 0.5f);
        EXPECT_NEAR(rotLength(m), 1.f, 1e-5f);
        EXPECT_NEAR(std::fabs(m.mRotation[3]), 1.f, 1e-4f);
    }

    TEST(SkyrimFirstPersonBlendPose, aWeightOutsideZeroToOneIsClamped)
    {
        const auto a = tr(1.f, 0.f, 0.f, { 0.f, 0.f, 0.f, 1.f });
        const auto b = tr(3.f, 0.f, 0.f, { 0.f, 0.f, 0.f, 1.f });
        EXPECT_NEAR(blendTransform(a, b, -4.f).mTranslation[0], 1.f, 1e-6f);
        EXPECT_NEAR(blendTransform(a, b, 9.f).mTranslation[0], 3.f, 1e-6f);
    }

    float lengthOf(const std::string& name)
    {
        if (name == "swing")
            return 1.0f;
        if (name == "slow")
            return kNotReady;
        return 4.0f; // loops
    }

    TEST(SkyrimFirstPersonTimelineBlend, theFirstPickHasNothingToFadeFrom)
    {
        Timeline t;
        t.setIdle("mt_idle");
        const auto f = t.pick(5.f, lengthOf);
        EXPECT_TRUE(f.from.empty());
        EXPECT_NEAR(f.blend, 1.f, 1e-6f);
    }

    TEST(SkyrimFirstPersonTimelineBlend, aSwingFadesInFromTheLoopWhichKeepsRunningUnderIt)
    {
        Timeline t;
        t.setIdle("mt_idle");
        (void)t.pick(10.f, lengthOf);
        (void)t.pick(10.5f, lengthOf);
        t.playOnce("swing");
        auto f = t.pick(11.f, lengthOf); // the swing starts here; the loop has run 1 s
        EXPECT_EQ(f.clip, "swing");
        EXPECT_EQ(f.from, "mt_idle");
        EXPECT_NEAR(f.fromTime, 1.0f, 1e-5f);
        EXPECT_NEAR(f.blend, 0.f, 1e-5f);
        f = t.pick(11.f + kBlendSeconds * 0.5f, lengthOf);
        EXPECT_EQ(f.from, "mt_idle");
        EXPECT_NEAR(f.fromTime, 1.0f + kBlendSeconds * 0.5f, 1e-5f);
        EXPECT_NEAR(f.blend, 0.5f, 1e-4f);
        f = t.pick(11.f + kBlendSeconds + 0.01f, lengthOf);
        EXPECT_TRUE(f.from.empty());
        EXPECT_NEAR(f.blend, 1.f, 1e-6f);
        EXPECT_EQ(f.clip, "swing");
    }

    TEST(SkyrimFirstPersonTimelineBlend, theLoopFadesInFromTheEndOfASwingWhichHoldsItsLastPose)
    {
        Timeline t;
        t.setIdle("mt_idle");
        (void)t.pick(0.f, lengthOf);
        t.playOnce("swing");
        (void)t.pick(1.f, lengthOf);
        (void)t.pick(1.9f, lengthOf);
        auto f = t.pick(2.2f, lengthOf); // the swing (1 s long) is over at 2.0
        EXPECT_EQ(f.clip, "mt_idle");
        EXPECT_EQ(f.from, "swing");
        EXPECT_LE(f.fromTime, 1.0f); // never past the clip's own end
        EXPECT_GT(f.fromTime, 0.9f);
        EXPECT_NEAR(f.blend, 0.f, 1e-5f);
        f = t.pick(2.2f + kBlendSeconds * 0.25f, lengthOf);
        EXPECT_EQ(f.from, "swing");
        EXPECT_LE(f.fromTime, 1.0f);
        EXPECT_NEAR(f.blend, 0.25f, 1e-4f);
    }

    TEST(SkyrimFirstPersonTimelineBlend, aSecondSwingFadesFromTheFirstOne)
    {
        Timeline t;
        t.setIdle("mt_idle");
        t.playOnce("swing");
        (void)t.pick(1.f, lengthOf);
        (void)t.pick(1.4f, lengthOf);
        (void)t.pick(1.6f, lengthOf);
        t.playOnce("swing");
        const auto f = t.pick(1.7f, lengthOf);
        EXPECT_EQ(f.clip, "swing");
        EXPECT_EQ(f.from, "swing");
        EXPECT_NEAR(f.fromTime, 0.7f, 1e-4f);
        EXPECT_NEAR(f.time, 0.f, 1e-6f);
    }

    TEST(SkyrimFirstPersonTimelineBlend, aNewLoopFadesFromTheOldOneAndOnlyThenIsThereAFade)
    {
        Timeline t;
        t.setIdle("mt_idle");
        (void)t.pick(1.f, lengthOf);
        (void)t.pick(1.2f, lengthOf);
        t.setIdle("1hm_idle");
        auto f = t.pick(1.5f, lengthOf);
        EXPECT_EQ(f.clip, "1hm_idle");
        EXPECT_EQ(f.from, "mt_idle");
        EXPECT_NEAR(f.fromTime, 0.5f, 1e-5f);
        f = t.pick(5.f, lengthOf);
        EXPECT_TRUE(f.from.empty());
        // the same loop asked for again is no change
        t.setIdle("1hm_idle");
        f = t.pick(5.1f, lengthOf);
        EXPECT_TRUE(f.from.empty());
    }

    TEST(SkyrimFirstPersonTimelineBlend, aSwitchDuringAFadeStartsANewFadeFromWhatWasShown)
    {
        Timeline t;
        t.setIdle("mt_idle");
        (void)t.pick(0.f, lengthOf);
        t.playOnce("swing");
        (void)t.pick(1.f, lengthOf); // fade from mt_idle starts
        t.setIdle("2hm_idle");
        (void)t.pick(1.0f + kBlendSeconds * 0.4f, lengthOf);
        t.playOnce("swing"); // replaced while the first fade is under way
        const auto f = t.pick(1.0f + kBlendSeconds * 0.5f, lengthOf);
        EXPECT_EQ(f.from, "swing");
        EXPECT_NEAR(f.blend, 0.f, 1e-4f);
    }

    TEST(SkyrimFirstPersonTimelineBlend, aClipThatIsNotReadyDoesNotStartAFade)
    {
        Timeline t;
        t.setIdle("mt_idle");
        (void)t.pick(0.f, lengthOf);
        t.playOnce("slow");
        const auto f = t.pick(0.5f, lengthOf);
        EXPECT_EQ(f.clip, "mt_idle");
        EXPECT_TRUE(f.from.empty());
    }

    TEST(SkyrimFirstPersonTimelineBlend, thePickedClipAndTimeAreWhatTheyWereBeforeTheFade)
    {
        // the fade only ADDS fields: clip, time and once are the old answers
        Timeline t;
        t.setIdle("mt_idle");
        (void)t.pick(0.f, lengthOf);
        t.playOnce("swing");
        const auto f = t.pick(1.f, lengthOf);
        EXPECT_EQ(f.clip, "swing");
        EXPECT_TRUE(f.once);
        EXPECT_NEAR(f.time, 0.f, 1e-6f);
        const auto g = t.pick(1.5f, lengthOf);
        EXPECT_NEAR(g.time, 0.5f, 1e-6f);
    }

    TEST(SkyrimFirstPersonTimelinePlaying, onlyAStartedClipCountsAsPlaying)
    {
        Timeline t;
        t.setIdle("mt_idle");
        EXPECT_FALSE(t.playingOnce());
        t.playOnce("slow");
        (void)t.pick(1.f, lengthOf);
        EXPECT_FALSE(t.playingOnce()); // still decoding
        Timeline u;
        u.setIdle("mt_idle");
        u.playOnce("swing");
        (void)u.pick(1.f, lengthOf);
        EXPECT_TRUE(u.playingOnce());
        (void)u.pick(1.5f, lengthOf);
        EXPECT_TRUE(u.playingOnce());
        (void)u.pick(2.5f, lengthOf);
        EXPECT_FALSE(u.playingOnce());
    }
}

namespace
{
    using namespace MWRender::SkyrimHeldGear;

    bool shownSwitch(const vsg::Switch& sw)
    {
        return !sw.children.empty() && sw.children[0].mask != vsg::MASK_OFF;
    }

    vsg::ref_ptr<vsg::Switch> oneChild(bool on)
    {
        auto sw = vsg::Switch::create();
        sw->addChild(on, vsg::Group::create());
        return sw;
    }

    TEST(SkyrimHeldGearForced, aForcedClockIgnoresTheHoldAndTheNote)
    {
        DrawClock clock;
        clock.force(true);
        EXPECT_TRUE(clock.update(1.f));
        EXPECT_TRUE(clock.update(100.f)); // no 8 s hold ends it
        clock.force(false);
        clock.note(); // a combat clip does not draw it while forced off
        EXPECT_FALSE(clock.update(101.f));
        EXPECT_FALSE(clock.update(102.f));
    }

    TEST(SkyrimHeldGearForced, unforcingGoesBackToTheClockAndKeepsItsHold)
    {
        DrawClock clock;
        clock.force(true);
        (void)clock.update(1.f);
        clock.unforce();
        EXPECT_FALSE(clock.update(2.f)); // nothing noted
        clock.note();
        EXPECT_TRUE(clock.update(3.f));
        EXPECT_FALSE(clock.update(3.f + kDrawnHoldSeconds + 1.f));
    }

    TEST(SkyrimHeldGearForced, aClockThatWasNeverForcedBehavesAsBefore)
    {
        DrawClock clock;
        EXPECT_FALSE(clock.update(0.f));
        clock.note();
        EXPECT_TRUE(clock.update(1.f));
        EXPECT_TRUE(clock.update(1.f + kDrawnHoldSeconds - 0.5f));
        EXPECT_FALSE(clock.update(1.f + kDrawnHoldSeconds + 0.5f));
    }

    TEST(SkyrimHeldGearForced, theControllerShowsTheCopyTheArmsAskFor)
    {
        auto sheathed = oneChild(true);
        auto drawn = oneChild(false);
        auto controller = vsg::ref_ptr<Controller>(new Controller(sheathed, drawn));
        auto dummy = vsg::Group::create();
        controller->run(*dummy, 0.f);
        EXPECT_TRUE(shownSwitch(*sheathed));
        EXPECT_FALSE(shownSwitch(*drawn));
        controller->force(true);
        controller->run(*dummy, 0.1f);
        EXPECT_FALSE(shownSwitch(*sheathed));
        EXPECT_TRUE(shownSwitch(*drawn));
        controller->force(false);
        controller->run(*dummy, 0.2f);
        EXPECT_TRUE(shownSwitch(*sheathed));
        EXPECT_FALSE(shownSwitch(*drawn));
    }

    TEST(SkyrimHeldGearForced, setDrawnFindsTheControllerInTheKeepAliveList)
    {
        auto sheathed = oneChild(true);
        auto drawn = oneChild(false);
        auto controller = vsg::ref_ptr<Controller>(new Controller(sheathed, drawn));
        std::vector<vsg::ref_ptr<vsg::Object>> kept{ vsg::Group::create(), controller };
        auto dummy = vsg::Group::create();
        setDrawn(kept, true);
        controller->run(*dummy, 0.f);
        EXPECT_TRUE(shownSwitch(*drawn));
        setDrawn(kept, false);
        controller->run(*dummy, 0.1f);
        EXPECT_FALSE(shownSwitch(*drawn));
        // no controller: nothing happens
        std::vector<vsg::ref_ptr<vsg::Object>> none{ vsg::Group::create() };
        setDrawn(none, true);
        setDrawn({}, true);
    }
}
