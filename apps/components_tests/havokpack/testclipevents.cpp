// Task 0170: which sidecar events a clip passes in one frame (components/havokpack/clipevents.hpp). Expected values are
// derived by hand from the event lists below.
#include <components/havokpack/clipevents.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using HavokPack::ClipMeta;

    ClipMeta attack()
    {
        ClipMeta meta;
        meta.mDuration = 1.3f;
        meta.mEvents = { { "attackStop", 1.287f }, { "HitFrame", 0.367f }, { "weaponSwing", 0.333f },
            { "preHitFrame", 0.233f } }; // deliberately unsorted
        meta.sortEvents();
        return meta;
    }

    std::vector<std::pair<std::string, float>> run(
        const ClipMeta& meta, float previous, float current, bool loop, bool* finished = nullptr)
    {
        std::vector<std::pair<std::string, float>> out;
        const bool done = HavokPack::advanceClipEvents(meta, previous, current, loop,
            [&](const HavokPack::ClipMetaEvent& e, float time) { out.emplace_back(e.mName, time); });
        if (finished != nullptr)
            *finished = done;
        return out;
    }

    TEST(HavokClipEvents, sortEventsOrdersByTimeAndDropsBadTimes)
    {
        ClipMeta meta = attack();
        meta.mEvents.push_back({ "bad", -1.f });
        meta.mEvents.push_back({ "nan", std::numeric_limits<float>::quiet_NaN() });
        meta.sortEvents();
        ASSERT_EQ(meta.mEvents.size(), 4u);
        EXPECT_EQ(meta.mEvents[0].mName, "preHitFrame");
        EXPECT_EQ(meta.mEvents[3].mName, "attackStop");
    }

    TEST(HavokClipEvents, eachEventFiresOnceInTheFrameThatPassesItInTimeOrder)
    {
        const ClipMeta meta = attack();
        std::vector<std::string> all;
        float previous = -1.f;
        for (float current = 0.f; current < 1.5f; current += 0.1f)
        {
            for (auto& [name, time] : run(meta, previous, current, false))
                all.push_back(name);
            previous = current;
        }
        const std::vector<std::string> expected = { "preHitFrame", "weaponSwing", "HitFrame", "attackStop" };
        EXPECT_EQ(all, expected);
    }

    TEST(HavokClipEvents, aStepThatPassesSeveralEventsDeliversThemInOrderWithTheirOwnTimes)
    {
        const auto passed = run(attack(), 0.2f, 0.4f, false);
        ASSERT_EQ(passed.size(), 3u);
        EXPECT_EQ(passed[0].first, "preHitFrame");
        EXPECT_FLOAT_EQ(passed[0].second, 0.233f);
        EXPECT_EQ(passed[1].first, "weaponSwing");
        EXPECT_EQ(passed[2].first, "HitFrame");
        EXPECT_FLOAT_EQ(passed[2].second, 0.367f);
    }

    TEST(HavokClipEvents, theRangeIsOpenAtTheStartAndClosedAtTheEnd)
    {
        ClipMeta meta;
        meta.mDuration = 2.f;
        meta.mEvents = { { "a", 0.f }, { "b", 0.5f }, { "c", 1.f } };
        EXPECT_EQ(run(meta, -1.f, 0.f, false).size(), 1u); // an event at time 0 fires with the first step from -1
        EXPECT_EQ(run(meta, 0.f, 0.5f, false).size(), 1u); // "b" at 0.5 fires, "a" at 0 does not fire again
        EXPECT_EQ(run(meta, 0.5f, 1.f, false).size(), 1u);
    }

    TEST(HavokClipEvents, aClipEndsWhenTheClockReachesItsPlaybackDuration)
    {
        bool finished = true;
        run(attack(), -1.f, 1.2f, false, &finished);
        EXPECT_FALSE(finished);
        const auto last = run(attack(), 1.2f, 1.4f, false, &finished);
        EXPECT_TRUE(finished);
        ASSERT_EQ(last.size(), 1u);
        EXPECT_EQ(last[0].first, "attackStop");
    }

    TEST(HavokClipEvents, eventsPastTheEndNeverFire)
    {
        ClipMeta meta;
        meta.mDuration = 1.f;
        meta.mEvents = { { "inside", 0.9f }, { "outside", 1.5f } };
        const auto passed = run(meta, -1.f, 5.f, false);
        ASSERT_EQ(passed.size(), 1u);
        EXPECT_EQ(passed[0].first, "inside");
    }

    TEST(HavokClipEvents, thePlaybackSpeedMakesTheClipShorterWithoutMovingStoredTimes)
    {
        ClipMeta meta = attack();
        meta.mSpeed = 2.f; // 1.3 animation seconds play in 0.65 playback seconds
        EXPECT_FLOAT_EQ(meta.playbackDuration(), 0.65f);
        EXPECT_FLOAT_EQ(meta.animationTime(0.5f), 1.0f);
        bool finished = false;
        const auto passed = run(meta, -1.f, 0.7f, false, &finished);
        EXPECT_TRUE(finished);
        EXPECT_EQ(passed.size(), 3u); // events at 0.233, 0.333, 0.367 (stored playback times); 1.287 is past the end
    }

    TEST(HavokClipEvents, aSpeedThatIsNotPositiveAndFinitePlaysAsOne)
    {
        ClipMeta meta = attack();
        for (float speed : { 0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
        {
            meta.mSpeed = speed;
            EXPECT_FLOAT_EQ(meta.speed(), 1.f);
            EXPECT_FLOAT_EQ(meta.playbackDuration(), 1.3f);
        }
    }

    TEST(HavokClipEvents, aLoopingClipWrapsAndFiresTheEventsOfTheNextLap)
    {
        ClipMeta meta;
        meta.mDuration = 1.f;
        meta.mEvents = { { "a", 0.25f }, { "b", 0.75f } };
        // From 0.9 to 1.5: b is not passed (0.75 < 0.9), the clip ends at 1.0, the next lap passes a (0.25) up to 0.5.
        bool finished = true;
        const auto passed = run(meta, 0.9f, 1.5f, true, &finished);
        EXPECT_FALSE(finished);
        ASSERT_EQ(passed.size(), 1u);
        EXPECT_EQ(passed[0].first, "a");
        // From 0.5 to 2.6: b of lap 1, a and b of lap 2, a of lap 3, in that order.
        const auto many = run(meta, 0.5f, 2.6f, true);
        std::vector<std::string> names;
        for (const auto& p : many)
            names.push_back(p.first);
        const std::vector<std::string> expected = { "b", "a", "b", "a" };
        EXPECT_EQ(names, expected);
    }

    TEST(HavokClipEvents, bogusClocksFireNothing)
    {
        EXPECT_TRUE(run(attack(), 1.f, 0.5f, false).empty()); // going backwards
        EXPECT_TRUE(run(attack(), std::numeric_limits<float>::quiet_NaN(), 1.f, false).empty());
    }

    TEST(HavokClipEvents, aClipWithNoEventsStillEnds)
    {
        ClipMeta meta;
        meta.mDuration = 0.5f;
        bool finished = false;
        EXPECT_TRUE(run(meta, -1.f, 0.4f, false, &finished).empty());
        EXPECT_FALSE(finished);
        run(meta, 0.4f, 0.6f, false, &finished);
        EXPECT_TRUE(finished);
    }
}
