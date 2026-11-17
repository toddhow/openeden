// Task 0203: lock tiers, the lock picking attempt, BOOK records, book text and the save chunk of lock picking and
// books. Written before the code and run red against a stub first. Generated plugins; no game data.
#include <game_skyrimse/lockbooks.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::lockbooks;

    constexpr float kStep = 1.f / 60.f;

    // ---- tiers -----------------------------------------------------------------------------------------------------

    TEST(SkyrimLockTiers, theLevelsOfTheDataFallIntoTheirTiers)
    {
        // the XLOC levels measured on Skyrim.esm: 0, 1, 25, 50, 75, 100, 255
        EXPECT_EQ(tierOf(0), LockTier::Novice);
        EXPECT_EQ(tierOf(1), LockTier::Novice);
        EXPECT_EQ(tierOf(24), LockTier::Novice);
        EXPECT_EQ(tierOf(25), LockTier::Apprentice);
        EXPECT_EQ(tierOf(49), LockTier::Apprentice);
        EXPECT_EQ(tierOf(50), LockTier::Adept);
        EXPECT_EQ(tierOf(74), LockTier::Adept);
        EXPECT_EQ(tierOf(75), LockTier::Expert);
        EXPECT_EQ(tierOf(99), LockTier::Expert);
        EXPECT_EQ(tierOf(100), LockTier::Master);
        EXPECT_EQ(tierOf(254), LockTier::Master);
        EXPECT_EQ(tierOf(255), LockTier::RequiresKey);
    }

    TEST(SkyrimLockTiers, aLevelOutsideZeroTo255RequiresItsKey)
    {
        EXPECT_EQ(tierOf(-1), LockTier::RequiresKey);
        EXPECT_EQ(tierOf(256), LockTier::RequiresKey);
        EXPECT_EQ(tierOf(2000000000), LockTier::RequiresKey);
        EXPECT_FALSE(pickable(-1));
        EXPECT_FALSE(pickable(255));
        EXPECT_FALSE(pickable(256));
        EXPECT_TRUE(pickable(0));
        EXPECT_TRUE(pickable(254));
    }

    TEST(SkyrimLockTiers, theTiersHaveNames)
    {
        EXPECT_EQ(tierName(LockTier::Novice), "novice");
        EXPECT_EQ(tierName(LockTier::Apprentice), "apprentice");
        EXPECT_EQ(tierName(LockTier::Adept), "adept");
        EXPECT_EQ(tierName(LockTier::Expert), "expert");
        EXPECT_EQ(tierName(LockTier::Master), "master");
        EXPECT_EQ(tierName(LockTier::RequiresKey), "requires key");
    }

    TEST(SkyrimLockTiers, theWindowShrinksWithTheTierAndGrowsWithTheSkill)
    {
        const PickSettings s;
        EXPECT_FLOAT_EQ(windowDegrees(LockTier::Novice, 0.f, s), 20.f);
        EXPECT_FLOAT_EQ(windowDegrees(LockTier::Master, 0.f, s), 2.5f);
        EXPECT_FLOAT_EQ(windowDegrees(LockTier::Novice, 100.f, s), 25.f); // 20 + 100 x 0.05
        EXPECT_GT(windowDegrees(LockTier::Adept, 50.f, s), windowDegrees(LockTier::Adept, 10.f, s));
        EXPECT_GT(windowDegrees(LockTier::Apprentice, 0.f, s), windowDegrees(LockTier::Adept, 0.f, s));
        PickSettings tiny;
        tiny.mWindow = { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f };
        EXPECT_FLOAT_EQ(windowDegrees(LockTier::Novice, 0.f, tiny), 1.f); // the floor
        EXPECT_FLOAT_EQ(windowDegrees(LockTier::Novice, std::nanf(""), s), 20.f); // a bad skill counts as 0
        EXPECT_FLOAT_EQ(windowDegrees(LockTier::Novice, -50.f, s), 20.f);
    }

    TEST(SkyrimLockTiers, theExperienceGrowsWithTheTier)
    {
        const PickSettings s;
        EXPECT_FLOAT_EQ(unlockExperience(LockTier::Novice, s), 10.f);
        EXPECT_FLOAT_EQ(unlockExperience(LockTier::Master, s), 50.f);
    }

    TEST(SkyrimLockTiers, theSweetSpotIsFixedPerPlacementAndInRange)
    {
        EXPECT_FLOAT_EQ(sweetSpotOf(185905), sweetSpotOf(185905));
        std::set<int> seen;
        for (std::uint64_t p = 1; p <= 400; ++p)
        {
            const float v = sweetSpotOf(p);
            EXPECT_GE(v, -85.f);
            EXPECT_LT(v, 85.f);
            seen.insert(static_cast<int>(std::floor(v / 10.f)));
        }
        EXPECT_GE(seen.size(), 12u) << "the sweet spots cover the range";
        EXPECT_NE(sweetSpotOf(1), sweetSpotOf(2));
    }

    // ---- the attempt -----------------------------------------------------------------------------------------------

    // Steps until the state leaves Picking (at most `limit` steps); returns the steps taken.
    int runUntilDone(Attempt& a, float angle, bool turning, int limit = 600)
    {
        int n = 0;
        while (a.state() == Attempt::State::Picking && n < limit)
        {
            a.update(kStep, angle, turning);
            ++n;
        }
        return n;
    }

    // An angle with turn limit 0 for this attempt.
    float farAngle(const Attempt& a)
    {
        return a.sweet() > 0.f ? -90.f : 90.f;
    }

    TEST(SkyrimLockAttempt, aLockThatNeedsItsKeyIsRefused)
    {
        Attempt a;
        EXPECT_EQ(a.state(), Attempt::State::Idle);
        EXPECT_FALSE(a.begin(5, 255, 0.f));
        EXPECT_EQ(a.state(), Attempt::State::Refused);
        EXPECT_FALSE(a.begin(5, -3, 0.f));
        EXPECT_EQ(a.state(), Attempt::State::Refused);
        a.update(kStep, 0.f, true);
        EXPECT_EQ(a.state(), Attempt::State::Refused); // an update does not revive it
    }

    TEST(SkyrimLockAttempt, beginStartsAtZeroWithTheTiersWindow)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(77, 50, 20.f));
        EXPECT_EQ(a.state(), Attempt::State::Picking);
        EXPECT_EQ(a.tier(), LockTier::Adept);
        EXPECT_EQ(a.level(), 50);
        EXPECT_EQ(a.placement(), 77u);
        EXPECT_FLOAT_EQ(a.progress(), 0.f);
        EXPECT_FLOAT_EQ(a.strain(), 0.f);
        EXPECT_FLOAT_EQ(a.window(), windowDegrees(LockTier::Adept, 20.f, PickSettings()));
        EXPECT_FLOAT_EQ(a.sweet(), sweetSpotOf(77));
    }

    TEST(SkyrimLockAttempt, turningAtTheSweetSpotOpensTheLockInAboutTheTurnTime)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        const int steps = runUntilDone(a, a.sweet(), true);
        EXPECT_EQ(a.state(), Attempt::State::Opened);
        EXPECT_GE(steps, 35); // 0.6 s at 60 steps a second
        EXPECT_LE(steps, 38);
        EXPECT_FLOAT_EQ(a.strain(), 0.f);
    }

    TEST(SkyrimLockAttempt, theEdgeOfTheWindowStillOpens)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        runUntilDone(a, a.sweet() + a.window() - 0.01f, true);
        EXPECT_EQ(a.state(), Attempt::State::Opened);
    }

    TEST(SkyrimLockAttempt, theTurnLimitFallsLinearlyOutsideTheWindow)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        const float w = a.window();
        const float sweet = a.sweet();
        EXPECT_FLOAT_EQ(a.turnLimit(sweet), 1.f);
        EXPECT_FLOAT_EQ(a.turnLimit(sweet + w), 1.f);
        EXPECT_NEAR(a.turnLimit(sweet + w + 30.f), 0.5f, 1e-4f);
        EXPECT_NEAR(a.turnLimit(sweet - w - 30.f), 0.5f, 1e-4f);
        EXPECT_FLOAT_EQ(a.turnLimit(sweet + w + 60.f), 0.f);
        EXPECT_FLOAT_EQ(a.turnLimit(sweet + w + 500.f), 0.f);
    }

    TEST(SkyrimLockAttempt, aWrongAngleStopsTheTurnShortAndTheStrainBreaksThePick)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        float angle = a.sweet() + a.window() + 30.f; // the limit is 0.5
        if (angle > 90.f)
            angle = a.sweet() - a.window() - 30.f;
        ASSERT_NEAR(a.turnLimit(angle), 0.5f, 1e-4f);
        const int steps = runUntilDone(a, angle, true);
        EXPECT_EQ(a.state(), Attempt::State::Broke);
        EXPECT_NEAR(a.progress(), 0.5f, 0.02f); // it turned up to the limit and no further
        // 0.3 s to the limit, then 0.9 s of strain
        EXPECT_GE(steps, 70);
        EXPECT_LE(steps, 76);
    }

    TEST(SkyrimLockAttempt, farFromTheSpotThePickBreaksAfterTheStrainTime)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        const float angle = farAngle(a);
        ASSERT_FLOAT_EQ(a.turnLimit(angle), 0.f);
        const int steps = runUntilDone(a, angle, true);
        EXPECT_EQ(a.state(), Attempt::State::Broke);
        EXPECT_GE(steps, 53); // 0.9 s
        EXPECT_LE(steps, 56);
        EXPECT_FLOAT_EQ(a.progress(), 0.f);
    }

    TEST(SkyrimLockAttempt, releasingTheTurnRelaxesProgressAndRecoversTheStrain)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        const float bad = farAngle(a);
        for (int i = 0; i < 30; ++i) // half a second of the 0.9 s strain time
            a.update(kStep, bad, true);
        EXPECT_NEAR(a.strain(), 0.5555f, 0.02f);
        for (int i = 0; i < 12; ++i) // 0.2 s at 2 a second
            a.update(kStep, bad, false);
        EXPECT_NEAR(a.strain(), 0.1555f, 0.03f);
        EXPECT_EQ(a.state(), Attempt::State::Picking);
        for (int i = 0; i < 30; ++i)
            a.update(kStep, a.sweet(), true);
        const float turned = a.progress();
        EXPECT_GT(turned, 0.4f);
        for (int i = 0; i < 12; ++i)
            a.update(kStep, a.sweet(), false);
        EXPECT_LT(a.progress(), turned - 0.3f);
        EXPECT_GE(a.progress(), 0.f);
    }

    TEST(SkyrimLockAttempt, aHarderLockTakesAnExactAngle)
    {
        Attempt easy, hard;
        ASSERT_TRUE(easy.begin(9, 0, 0.f));
        ASSERT_TRUE(hard.begin(9, 100, 0.f));
        const float angle = easy.sweet() + 10.f; // inside the novice window (20), outside the master's (2.5)
        runUntilDone(easy, angle, true);
        EXPECT_EQ(easy.state(), Attempt::State::Opened);
        runUntilDone(hard, angle, true);
        EXPECT_EQ(hard.state(), Attempt::State::Broke);
    }

    TEST(SkyrimLockAttempt, skillWidensTheWindowSoALargerErrorOpens)
    {
        Attempt low, high;
        ASSERT_TRUE(low.begin(9, 75, 0.f));
        ASSERT_TRUE(high.begin(9, 75, 100.f));
        EXPECT_GT(high.window(), low.window() + 4.f);
        const float angle = low.sweet() + (low.window() + high.window()) * 0.5f;
        runUntilDone(low, angle, true);
        runUntilDone(high, angle, true);
        EXPECT_NE(low.state(), Attempt::State::Opened);
        EXPECT_EQ(high.state(), Attempt::State::Opened);
    }

    TEST(SkyrimLockAttempt, cancelEndsItAndLaterUpdatesDoNothing)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        for (int i = 0; i < 10; ++i)
            a.update(kStep, a.sweet(), true);
        a.cancel();
        EXPECT_EQ(a.state(), Attempt::State::Cancelled);
        const float p = a.progress();
        a.update(kStep, a.sweet(), true);
        EXPECT_EQ(a.state(), Attempt::State::Cancelled);
        EXPECT_FLOAT_EQ(a.progress(), p);
    }

    TEST(SkyrimLockAttempt, anOpenedLockStaysOpenedAndKeepsItsProgress)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        runUntilDone(a, a.sweet(), true);
        ASSERT_EQ(a.state(), Attempt::State::Opened);
        a.update(kStep, -90.f, false);
        a.update(kStep, 90.f, true);
        EXPECT_EQ(a.state(), Attempt::State::Opened);
        EXPECT_FLOAT_EQ(a.progress(), 1.f);
        a.cancel(); // too late
        EXPECT_EQ(a.state(), Attempt::State::Opened);
    }

    TEST(SkyrimLockAttempt, badNumbersAreHarmless)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        a.update(std::nanf(""), a.sweet(), true);
        a.update(-1.f, a.sweet(), true);
        a.update(0.f, a.sweet(), true);
        EXPECT_FLOAT_EQ(a.progress(), 0.f); // a step of no time or a bad time changes nothing
        a.update(kStep, std::nanf(""), true); // not a number counts as 0 degrees
        EXPECT_TRUE(std::isfinite(a.progress()));
        a.update(kStep, 1.0e9f, false);
        EXPECT_TRUE(std::isfinite(a.strain()));
        Attempt b;
        ASSERT_TRUE(b.begin(9, 0, 0.f));
        b.update(1.0e9f, b.sweet(), true); // a huge frame counts as at most mMaxStep (0.1 s)
        EXPECT_NEAR(b.progress(), 0.1f / 0.6f, 1e-4f);
    }

    TEST(SkyrimLockAttempt, twoRunsOfTheSameInputsAreIdentical)
    {
        Attempt a, b;
        ASSERT_TRUE(a.begin(31, 50, 33.f));
        ASSERT_TRUE(b.begin(31, 50, 33.f));
        for (int i = 0; i < 200; ++i)
        {
            const float angle = a.sweet() + static_cast<float>((i % 40) - 20);
            const bool turning = (i / 7) % 2 == 0;
            a.update(kStep, angle, turning);
            b.update(kStep, angle, turning);
            ASSERT_EQ(a.state(), b.state());
            ASSERT_EQ(a.progress(), b.progress());
            ASSERT_EQ(a.strain(), b.strain());
        }
    }

    TEST(SkyrimLockAttempt, beginAgainStartsFresh)
    {
        Attempt a;
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        runUntilDone(a, farAngle(a), true);
        ASSERT_EQ(a.state(), Attempt::State::Broke);
        ASSERT_TRUE(a.begin(9, 0, 0.f));
        EXPECT_EQ(a.state(), Attempt::State::Picking);
        EXPECT_FLOAT_EQ(a.progress(), 0.f);
        EXPECT_FLOAT_EQ(a.strain(), 0.f);
    }

    // ---- BOOK records ----------------------------------------------------------------------------------------------

    constexpr FormId kSelf = 0x01000000; // the plugin itself (one master)

    Bytes bookRecord(FormId id, const std::string& edid, std::uint8_t flags, std::uint32_t teaches, std::int32_t value,
        float weight, const std::string& name, const std::string& desc, std::size_t dataSize = 16, bool deleted = false)
    {
        Bytes subs = sub4("EDID", Bytes().zstring(edid));
        subs.append(sub4("FULL", Bytes().zstring(name)));
        subs.append(sub4("DESC", Bytes().zstring(desc)));
        Bytes data;
        data.u8(flags).u8(0).u16(0).u32(teaches).u32(static_cast<std::uint32_t>(value)).f32(weight);
        std::string bytes = data.str();
        bytes.resize(dataSize, '\0');
        Bytes d;
        d.text(bytes);
        subs.append(sub4("DATA", d));
        return record4("BOOK", deleted ? 0x20u : 0u, id, deleted ? Bytes() : subs);
    }

    Bytes plugin(const std::vector<Bytes>& books, std::uint32_t flags = 0)
    {
        Bytes all = tes4Header(1.7f, flags, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes children;
        for (const Bytes& b : books)
            children.append(b);
        all.append(group4(Content::fourCC("BOOK"), 0, children));
        return all;
    }

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    BookRecords parse(const Bytes& bytes, bool* ok = nullptr)
    {
        Content::Error error;
        const auto p = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        BookRecords out;
        EXPECT_NE(p, nullptr) << error.mMessage;
        const bool result = p != nullptr && parseBookRecords(*p, order(), BookLimits(), out);
        if (ok != nullptr)
            *ok = result;
        return out;
    }

    TEST(SkyrimBookRecords, aSkillBookKeepsItsFlagsTeachesValueAndText)
    {
        bool ok = false;
        const BookRecords r = parse(plugin({ bookRecord(kSelf | 0x100, "SkillBookAlchemy", BookFlags::TeachesSkill, 16,
                                        200, 1.5f, "Alchemist's Handbook", "Some <p>text") }),
            &ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(r.mBooks.size(), 1u);
        const BookRecord& b = r.mBooks[0];
        EXPECT_EQ(b.mId, kSelf | 0x100);
        EXPECT_EQ(b.mEditorId, "SkillBookAlchemy");
        EXPECT_EQ(b.mFlags, BookFlags::TeachesSkill);
        EXPECT_EQ(b.mValue, 200);
        EXPECT_FLOAT_EQ(b.mWeight, 1.5f);
        EXPECT_TRUE(b.mDecoded);
        EXPECT_EQ(b.kind(), BookKind::Skill);
        EXPECT_EQ(b.mTeaches, 16u);
        EXPECT_EQ(b.skillSlot(), 10); // actor value 16 = slot 10, Alchemy
        EXPECT_EQ(b.mName.mText, "Alchemist's Handbook");
        EXPECT_FALSE(b.mName.mLocalized);
        EXPECT_EQ(b.mDesc.mText, "Some <p>text");
        EXPECT_EQ(r.mFailed, 0u);
        EXPECT_EQ(r.mOdd, 0u);
    }

    TEST(SkyrimBookRecords, everyActorValueOfASkillIsASlot)
    {
        for (std::uint32_t av = 6; av <= 23; ++av)
        {
            BookRecord b;
            b.mFlags = BookFlags::TeachesSkill;
            b.mTeaches = av;
            EXPECT_EQ(b.skillSlot(), static_cast<int>(av) - 6) << av;
        }
        BookRecord bad;
        bad.mFlags = BookFlags::TeachesSkill;
        bad.mTeaches = 5;
        EXPECT_EQ(bad.skillSlot(), -1);
        bad.mTeaches = 24;
        EXPECT_EQ(bad.skillSlot(), -1);
        BookRecord plain;
        plain.mTeaches = 10;
        EXPECT_EQ(plain.skillSlot(), -1); // no flag: nothing is taught
    }

    TEST(SkyrimBookRecords, aSpellTomeResolvesItsSpellThroughTheLoadOrder)
    {
        const BookRecords r = parse(plugin({ bookRecord(kSelf | 0x101, "SpellTomeA", BookFlags::TeachesSpell,
                                                 0x0010F7EE, 50, 1.f, "Tome A", "x"),
            bookRecord(kSelf | 0x102, "SpellTomeB", BookFlags::TeachesSpell, kSelf | 0x300, 50, 1.f, "Tome B", "y") }));
        ASSERT_EQ(r.mBooks.size(), 2u);
        EXPECT_EQ(r.mBooks[0].kind(), BookKind::Spell);
        EXPECT_EQ(r.mBooks[0].mTeaches, 0x0010F7EEu); // a form of the master
        EXPECT_EQ(r.mBooks[1].mTeaches, 0x01000300u); // a form of the plugin itself (its slot is 1)
    }

    TEST(SkyrimBookRecords, bothFlagsMakeASkillBookAndAreCounted)
    {
        const BookRecords r = parse(plugin({ bookRecord(
            kSelf | 0x103, "Both", BookFlags::TeachesSkill | BookFlags::TeachesSpell, 7, 1, 1.f, "Both", "z") }));
        ASSERT_EQ(r.mBooks.size(), 1u);
        EXPECT_EQ(r.mBooks[0].kind(), BookKind::Skill);
        EXPECT_EQ(r.mBothFlags, 1u);
    }

    TEST(SkyrimBookRecords, aPlainBookAndACannotBeTakenBook)
    {
        const BookRecords r = parse(plugin({ bookRecord(kSelf | 0x104, "Plain", 0, 0, 5, 1.f, "Plain", "p"),
            bookRecord(kSelf | 0x105, "Fixed", BookFlags::CannotBeTaken, 0, 0, 0.f, "Fixed", "q") }));
        ASSERT_EQ(r.mBooks.size(), 2u);
        EXPECT_EQ(r.mBooks[0].kind(), BookKind::Plain);
        EXPECT_EQ(r.mBooks[1].kind(), BookKind::Plain);
        EXPECT_EQ(r.mBooks[1].mFlags & BookFlags::CannotBeTaken, BookFlags::CannotBeTaken);
    }

    TEST(SkyrimBookRecords, aDataOfAnotherSizeIsOddAndTheRecordIsNotDecoded)
    {
        const BookRecords r
            = parse(plugin({ bookRecord(kSelf | 0x106, "Odd", BookFlags::TeachesSkill, 8, 1, 1.f, "Odd", "o", 12) }));
        ASSERT_EQ(r.mBooks.size(), 1u);
        EXPECT_FALSE(r.mBooks[0].mDecoded);
        EXPECT_EQ(r.mBooks[0].kind(), BookKind::Plain); // never guessed at
        EXPECT_EQ(r.mOdd, 1u);
    }

    TEST(SkyrimBookRecords, aDeletedRecordIsListedAsRemoved)
    {
        const BookRecords r = parse(plugin({ bookRecord(kSelf | 0x107, "Gone", 0, 0, 1, 1.f, "", "", 16, true) }));
        EXPECT_TRUE(r.mBooks.empty());
        ASSERT_EQ(r.mRemoved.size(), 1u);
        EXPECT_EQ(r.mRemoved[0], kSelf | 0x107);
    }

    TEST(SkyrimBookRecords, aLocalisedPluginGivesStringIdsNotText)
    {
        Bytes subs = sub4("EDID", Bytes().zstring("Loc"));
        subs.append(sub4("FULL", Bytes().u32(0x1234)));
        subs.append(sub4("DESC", Bytes().u32(0x5678)));
        Bytes data;
        data.u8(0).u8(0).u16(0).u32(0).u32(3).f32(1.f);
        subs.append(sub4("DATA", data));
        const BookRecords r = parse(plugin({ record4("BOOK", 0, kSelf | 0x108, subs) }, 0x80));
        ASSERT_EQ(r.mBooks.size(), 1u);
        EXPECT_TRUE(r.mLocalized);
        EXPECT_TRUE(r.mBooks[0].mName.mLocalized);
        EXPECT_EQ(r.mBooks[0].mName.mId, 0x1234u);
        EXPECT_TRUE(r.mBooks[0].mDesc.mLocalized);
        EXPECT_EQ(r.mBooks[0].mDesc.mId, 0x5678u);
        EXPECT_TRUE(r.mBooks[0].mDesc.mPresent);
    }

    TEST(SkyrimBookRecords, aPluginThatIsNotSkyrimSEIsRefusedWithAWalkError)
    {
        Bytes tes3 = tes3Header(1.3f, 0, "A", "B", 0, {});
        Content::Error error;
        const auto p
            = Content::Plugin::open(std::make_unique<std::istringstream>(tes3.str(), std::ios::binary), "m.esp", error);
        ASSERT_NE(p, nullptr);
        BookRecords out;
        EXPECT_FALSE(parseBookRecords(*p, order(), BookLimits(), out));
        EXPECT_TRUE(out.mWalkError.has_value());
    }

    TEST(SkyrimBookIndex, aLaterPluginWinsAndRemembersItsPlugin)
    {
        BookRecords first = parse(plugin({ bookRecord(kSelf | 0x110, "Old", 0, 0, 1, 1.f, "Old name", "old") }));
        BookRecords second = parse(plugin({ bookRecord(kSelf | 0x110, "New", 0, 0, 2, 1.f, "New name", "new"),
            bookRecord(kSelf | 0x111, "Other", 0, 0, 2, 1.f, "Other", "o") }));
        BookIndex index;
        index.add(std::move(first), 0);
        index.add(std::move(second), 3);
        EXPECT_EQ(index.size(), 2u);
        const BookRecord* b = index.book(kSelf | 0x110);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(b->mName.mText, "New name");
        EXPECT_EQ(b->mPlugin, 3u);
        EXPECT_EQ(index.book(kSelf | 0x999), nullptr);
    }

    TEST(SkyrimBookIndex, aRemovedBookGoes)
    {
        BookRecords first = parse(plugin({ bookRecord(kSelf | 0x112, "Old", 0, 0, 1, 1.f, "A", "a") }));
        BookRecords second = parse(plugin({ bookRecord(kSelf | 0x112, "", 0, 0, 1, 1.f, "", "", 16, true) }));
        BookIndex index;
        index.add(std::move(first), 0);
        index.add(std::move(second), 1);
        EXPECT_EQ(index.book(kSelf | 0x112), nullptr);
    }

    // ---- book text -------------------------------------------------------------------------------------------------

    using Pages = std::vector<std::string>;

    TEST(SkyrimBookText, plainTextIsOnePage)
    {
        EXPECT_EQ(renderBookText("Hello"), Pages{ "Hello" });
        EXPECT_TRUE(renderBookText("").empty());
        EXPECT_TRUE(renderBookText("   \r\n  ").empty());
    }

    TEST(SkyrimBookText, paragraphsAreSeparatedByOneEmptyLine)
    {
        EXPECT_EQ(renderBookText("<p>One</p><p>Two</p>"), Pages{ "One\n\nTwo" });
        EXPECT_EQ(renderBookText("<p align=\"center\">Title</p>Body"), Pages{ "Title\n\nBody" });
        EXPECT_EQ(renderBookText("<div>A</div><div>B</div>"), Pages{ "A\n\nB" });
    }

    TEST(SkyrimBookText, aLineBreakIsOneNewline)
    {
        EXPECT_EQ(renderBookText("A<br>B"), Pages{ "A\nB" });
        EXPECT_EQ(renderBookText("A<BR/>B<br />C"), Pages{ "A\nB\nC" });
    }

    TEST(SkyrimBookText, twoBreaksInARowAreAnEmptyLineAsInTheRealBooks)
    {
        // measured on Skyrim.esm: "...which.<br>\r\n<br>\r\nIt is well known" (the Twin Secrets skill book)
        EXPECT_EQ(renderBookText("A<br><br>B"), Pages{ "A\n\nB" });
        EXPECT_EQ(renderBookText("A<br>\r\n<br>\r\nB"), Pages{ "A\n\nB" });
        EXPECT_EQ(renderBookText("A<br>\r\nB"), Pages{ "A\nB" });
    }

    TEST(SkyrimBookText, rawNewlinesStayLineBreaksAndRunsCollapseToAParagraph)
    {
        EXPECT_EQ(renderBookText("a\r\nb"), Pages{ "a\nb" });
        EXPECT_EQ(renderBookText("a\n\n\n\nb"), Pages{ "a\n\nb" });
    }

    TEST(SkyrimBookText, fontTagsAreDroppedAndTheTextStays)
    {
        EXPECT_EQ(renderBookText("<font size=20 color='#ffffff' face=\"$HandwrittenFont\">Big</font> text"),
            Pages{ "Big text" });
    }

    TEST(SkyrimBookText, aPictureBecomesAPlaceholder)
    {
        EXPECT_EQ(renderBookText("See <img src='img://textures/x.dds' width=10 height=10> here"),
            Pages{ "See [picture] here" });
    }

    TEST(SkyrimBookText, anyOtherTagIsDroppedAndTheTextStays)
    {
        EXPECT_EQ(renderBookText("x<blink>y</blink><b>z</b>"), Pages{ "xyz" });
    }

    TEST(SkyrimBookText, aPageBreakStartsANewPage)
    {
        EXPECT_EQ(renderBookText("Page one[pagebreak]Page two"), (Pages{ "Page one", "Page two" }));
        EXPECT_EQ(renderBookText("[pagebreak]Only"), Pages{ "Only" }); // no empty page
        EXPECT_EQ(renderBookText("A[pagebreak][pagebreak]B"), (Pages{ "A", "B" }));
    }

    TEST(SkyrimBookText, entitiesAreDecoded)
    {
        EXPECT_EQ(renderBookText("a&nbsp;b &amp; &lt;c&gt; &quot;d&quot; &apos;e&apos; &#65;&#x42;"),
            Pages{ "a b & <c> \"d\" 'e' AB" });
        EXPECT_EQ(renderBookText("&#233;"), Pages{ "\xC3\xA9" }); // e acute, as UTF-8
    }

    TEST(SkyrimBookText, aBrokenEntityStaysAsText)
    {
        EXPECT_EQ(renderBookText("fish &chips; and &"), Pages{ "fish &chips; and &" });
        EXPECT_EQ(renderBookText("&#99999999; &#0; &#xD800;"), Pages{ "&#99999999; &#0; &#xD800;" });
    }

    TEST(SkyrimBookText, windows1252IsConvertedToUtf8)
    {
        EXPECT_EQ(renderBookText("caf\xE9"), Pages{ "caf\xC3\xA9" });
        EXPECT_EQ(renderBookText("\x93q\x94"),
            Pages{ "\xE2\x80\x9C"
                   "q"
                   "\xE2\x80\x9D" });
        EXPECT_EQ(renderBookText("\x85"), Pages{ "\xE2\x80\xA6" }); // ellipsis
    }

    TEST(SkyrimBookText, pagesBreakAtParagraphsNearThePageSize)
    {
        std::string text;
        for (int i = 0; i < 5; ++i)
            text += "<p>" + std::string(400, static_cast<char>('a' + i)) + "</p>";
        const Pages pages = renderBookText(text, 1000);
        ASSERT_EQ(pages.size(), 3u);
        EXPECT_EQ(pages[0], std::string(400, 'a') + "\n\n" + std::string(400, 'b'));
        EXPECT_EQ(pages[1], std::string(400, 'c') + "\n\n" + std::string(400, 'd'));
        EXPECT_EQ(pages[2], std::string(400, 'e'));
    }

    TEST(SkyrimBookText, aLongParagraphIsCutAtASpace)
    {
        std::string word;
        for (int i = 0; i < 500; ++i)
            word += "word" + std::to_string(i % 10) + " ";
        const Pages pages = renderBookText("<p>" + word + "</p>", 1000);
        ASSERT_GE(pages.size(), 3u);
        std::string joined;
        for (const std::string& p : pages)
        {
            EXPECT_LE(p.size(), 1000u);
            EXPECT_FALSE(p.empty());
            EXPECT_NE(p.back(), ' ');
            joined += p + " ";
        }
        EXPECT_EQ(joined, word); // nothing lost, nothing added
    }

    TEST(SkyrimBookText, aWordLongerThanAPageIsCutHard)
    {
        const Pages pages = renderBookText(std::string(2500, 'x'), 1000);
        ASSERT_EQ(pages.size(), 3u);
        EXPECT_EQ(pages[0].size(), 1000u);
        EXPECT_EQ(pages[2].size(), 500u);
    }

    TEST(SkyrimBookText, hostileInputIsBoundedAndNeverThrows)
    {
        EXPECT_NO_THROW({
            const Pages big = renderBookText(std::string(3 * 1024 * 1024, 'a'));
            std::size_t total = 0;
            for (const std::string& p : big)
                total += p.size();
            EXPECT_LE(total, kMaxBookTextBytes);
            EXPECT_GE(total, kMaxBookTextBytes - 4096);
        });
        EXPECT_NO_THROW((void)renderBookText("<p"));
        EXPECT_NO_THROW((void)renderBookText("<<<<<<<>>>>><p><p><p>"));
        EXPECT_NO_THROW((void)renderBookText(std::string(100000, '<')));
        EXPECT_NO_THROW((void)renderBookText(std::string(100000, '&')));
        EXPECT_NO_THROW((void)renderBookText(std::string("a\0b", 3)));
        EXPECT_EQ(renderBookText("tail <p"), Pages{ "tail" }); // an unterminated tag is dropped
        EXPECT_NO_THROW((void)renderBookText("x", 0)); // a zero page size is treated as the default
    }

    // ---- state and the chunk ---------------------------------------------------------------------------------------

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(const std::vector<unsigned char>& bytes)
            : mBytes(bytes)
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
        const std::vector<unsigned char>& mBytes;
        std::size_t mPos = 0;
    };

    TEST(SkyrimLockBooksModel, picksReadsAndSpellsAreRemembered)
    {
        LockBooksModel m;
        const std::uint32_t r0 = m.revision();
        EXPECT_TRUE(m.markPicked(185905));
        EXPECT_FALSE(m.markPicked(185905));
        EXPECT_TRUE(m.isPicked(185905));
        EXPECT_FALSE(m.isPicked(1));
        EXPECT_TRUE(m.markRead(0x10F7F5));
        EXPECT_FALSE(m.markRead(0x10F7F5));
        EXPECT_TRUE(m.isRead(0x10F7F5));
        EXPECT_TRUE(m.learnSpell(0x20));
        EXPECT_TRUE(m.learnSpell(0x10));
        EXPECT_FALSE(m.learnSpell(0x20));
        EXPECT_EQ(m.spells(), (std::vector<FormId>{ 0x20, 0x10 })); // the order learned
        EXPECT_TRUE(m.knowsSpell(0x10));
        EXPECT_FALSE(m.knowsSpell(0x30));
        m.addBroken();
        m.addBroken();
        EXPECT_EQ(m.broken(), 2u);
        EXPECT_NE(m.revision(), r0);
        m.clear();
        EXPECT_FALSE(m.isPicked(185905));
        EXPECT_TRUE(m.spells().empty());
        EXPECT_EQ(m.broken(), 0u);
    }

    TEST(SkyrimLockBooksModel, theTablesHaveCaps)
    {
        LockBooksModel m;
        for (std::uint64_t i = 0; i < LockBooksModel::kMaxLocks; ++i)
            ASSERT_TRUE(m.markPicked(i + 1));
        EXPECT_FALSE(m.markPicked(999999));
        for (FormId i = 0; i < LockBooksModel::kMaxSpells; ++i)
            ASSERT_TRUE(m.learnSpell(i + 1));
        EXPECT_FALSE(m.learnSpell(0x7FFFFFFF));
        LockBooksModel n;
        EXPECT_FALSE(n.learnSpell(0)); // the null form is no spell
    }

    std::vector<unsigned char> expectedBytes()
    {
        // picked {5}, read {0x10}, spells {0x20, 0x10} in that order, broken 3
        MemoryWriter w;
        w.writeValue(std::uint32_t(1));
        w.writeValue(std::uint64_t(5));
        w.writeValue(std::uint32_t(1));
        w.writeValue(std::uint32_t(0x10));
        w.writeValue(std::uint32_t(2));
        w.writeValue(std::uint32_t(0x20));
        w.writeValue(std::uint32_t(0x10));
        w.writeValue(std::uint32_t(3));
        return w.mBytes;
    }

    void fillSample(LockBooksModel& m)
    {
        m.markPicked(5);
        m.markRead(0x10);
        m.learnSpell(0x20);
        m.learnSpell(0x10);
        m.addBroken();
        m.addBroken();
        m.addBroken();
    }

    TEST(SkyrimLockBooksChunk, theBytesAreTheDocumentedLayout)
    {
        LockBooksModel m;
        fillSample(m);
        LockBooksChunk chunk(m);
        EXPECT_EQ(chunk.chunkName(), "lockbooks");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
        MemoryWriter w;
        chunk.write(w);
        EXPECT_EQ(w.mBytes, expectedBytes());
        EXPECT_EQ(w.mBytes.size(), 36u);
    }

    TEST(SkyrimLockBooksChunk, aChunkRoundTripsInANewModel)
    {
        LockBooksModel a;
        fillSample(a);
        MemoryWriter w;
        LockBooksChunk(a).write(w);
        LockBooksModel b;
        LockBooksChunk chunk(b);
        MemoryReader r(w.mBytes);
        ASSERT_TRUE(chunk.read(r, 1)) << chunk.refusal();
        EXPECT_TRUE(b.isPicked(5));
        EXPECT_TRUE(b.isRead(0x10));
        EXPECT_EQ(b.spells(), (std::vector<FormId>{ 0x20, 0x10 }));
        EXPECT_EQ(b.broken(), 3u);
        MemoryWriter again;
        LockBooksChunk(b).write(again);
        EXPECT_EQ(again.mBytes, w.mBytes); // canonical
    }

    TEST(SkyrimLockBooksChunk, everyTruncationRefusesAndChangesNothing)
    {
        const std::vector<unsigned char> good = expectedBytes();
        for (std::size_t cut = 0; cut < good.size(); ++cut)
        {
            LockBooksModel m;
            m.markPicked(99);
            std::vector<unsigned char> part(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut));
            LockBooksChunk chunk(m);
            MemoryReader r(part);
            EXPECT_FALSE(chunk.read(r, 1)) << cut;
            EXPECT_TRUE(m.isPicked(99)) << cut;
            EXPECT_EQ(m.pickedCount(), 1u) << cut;
            EXPECT_FALSE(chunk.refusal().empty()) << cut;
        }
    }

    TEST(SkyrimLockBooksChunk, trailingBytesAndAnotherVersionRefuse)
    {
        std::vector<unsigned char> good = expectedBytes();
        LockBooksModel m;
        LockBooksChunk chunk(m);
        {
            MemoryReader r(good);
            EXPECT_FALSE(chunk.read(r, 2));
            EXPECT_NE(chunk.refusal().find("version"), std::string::npos);
        }
        good.push_back(0);
        MemoryReader r(good);
        EXPECT_FALSE(chunk.read(r, 1));
        EXPECT_EQ(m.pickedCount(), 0u);
    }

    TEST(SkyrimLockBooksChunk, unsortedDuplicateAndOversizedListsRefuse)
    {
        const auto tryBytes = [](const std::vector<unsigned char>& bytes) {
            LockBooksModel m;
            LockBooksChunk chunk(m);
            MemoryReader r(bytes);
            const bool ok = chunk.read(r, 1);
            if (!ok)
            {
                EXPECT_EQ(m.pickedCount() + m.readCount() + m.spells().size(), 0u);
            }
            return ok;
        };
        {
            MemoryWriter w; // locks not ascending
            w.writeValue(std::uint32_t(2));
            w.writeValue(std::uint64_t(9));
            w.writeValue(std::uint64_t(5));
            w.writeValue(std::uint32_t(0));
            w.writeValue(std::uint32_t(0));
            w.writeValue(std::uint32_t(0));
            EXPECT_FALSE(tryBytes(w.mBytes));
        }
        {
            MemoryWriter w; // a spell twice
            w.writeValue(std::uint32_t(0));
            w.writeValue(std::uint32_t(0));
            w.writeValue(std::uint32_t(2));
            w.writeValue(std::uint32_t(7));
            w.writeValue(std::uint32_t(7));
            w.writeValue(std::uint32_t(0));
            EXPECT_FALSE(tryBytes(w.mBytes));
        }
        {
            MemoryWriter w; // a count over the cap
            w.writeValue(std::uint32_t(LockBooksModel::kMaxLocks + 1));
            EXPECT_FALSE(tryBytes(w.mBytes));
        }
        {
            MemoryWriter w; // a null form
            w.writeValue(std::uint32_t(0));
            w.writeValue(std::uint32_t(1));
            w.writeValue(std::uint32_t(0));
            w.writeValue(std::uint32_t(0));
            w.writeValue(std::uint32_t(0));
            EXPECT_FALSE(tryBytes(w.mBytes));
        }
        EXPECT_TRUE(tryBytes(expectedBytes()));
    }
}
