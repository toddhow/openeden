// Tests for the choice of the shadowed point lights, Render::PointShadow::select (task 0077, stage a). Written from the
// task spec alone ("Selecting the lights", the "Interfaces" rules (1) to (5) and the acceptance criteria of
// docs/tasks/0077-point-light-shadows.md), without seeing any implementation. The numbers are chosen so that scores
// are exact in floating point where a rule has a strict comparison. Pure CPU logic: no device, window or game data.

#include <components/render/pointshadowselect.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include <vsg/maths/vec3.h>

namespace
{
    using namespace Render::PointShadow;
    using V3 = vsg::dvec3;

    const V3 origin(0.0, 0.0, 0.0);

    Candidate cand(std::uint64_t id, double x, double y, double z, float radius, bool casts = true)
    {
        Candidate candidate;
        candidate.id = id;
        candidate.position = V3(x, y, z);
        candidate.radius = radius;
        candidate.casts = casts;
        return candidate;
    }

    Slot held(std::uint64_t id, double x, double y, double z, float radius, std::uint32_t heldFrames, float fade)
    {
        Slot slot;
        slot.used = true;
        slot.id = id;
        slot.position = V3(x, y, z);
        slot.radius = radius;
        slot.heldFrames = heldFrames;
        slot.fade = fade;
        return slot;
    }

    SelectionParams params(int slots)
    {
        SelectionParams result;
        result.maxSlots = slots;
        return result;
    }

    double distanceBetween(const V3& a, const V3& b)
    {
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        const double dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // The test's own copy of rule (1).
    bool eligible(const Candidate& c, const V3& camera, const SelectionParams& p)
    {
        if (!c.casts || !(c.radius > 0.f) || !std::isfinite(c.position.x) || !std::isfinite(c.position.y)
            || !std::isfinite(c.position.z))
        {
            return false;
        }
        return distanceBetween(camera, c.position) <= 2.0 * static_cast<double>(c.radius) + p.maxDistance;
    }

    bool sameV3(const V3& a, const V3& b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    bool sameSlot(const Slot& a, const Slot& b, bool compareId)
    {
        return a.used == b.used && (!compareId || a.id == b.id) && sameV3(a.position, b.position)
            && a.radius == b.radius && a.heldFrames == b.heldFrames && a.fade == b.fade;
    }

    bool sameSelection(const Selection& a, const Selection& b)
    {
        if (a.slots.size() != b.slots.size() || a.slotOfCandidate != b.slotOfCandidate)
        {
            return false;
        }
        for (std::size_t i = 0; i < a.slots.size(); ++i)
        {
            if (!sameSlot(a.slots[i], b.slots[i], true))
            {
                return false;
            }
        }
        return true;
    }

    // The invariants every result must have, whatever the inputs.
    void expectConsistent(
        const Selection& result, const std::vector<Candidate>& candidates, const V3& camera, const SelectionParams& p)
    {
        const std::size_t slotCount = static_cast<std::size_t>(std::max(p.maxSlots, 0));
        ASSERT_EQ(result.slots.size(), slotCount);
        ASSERT_EQ(result.slotOfCandidate.size(), candidates.size());
        std::vector<int> owner(slotCount, -1);
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            const int slot = result.slotOfCandidate[i];
            if (!eligible(candidates[i], camera, p))
            {
                EXPECT_EQ(slot, -1) << "candidate " << i << " is not eligible";
                continue;
            }
            if (slot < 0)
            {
                continue;
            }
            ASSERT_LT(static_cast<std::size_t>(slot), slotCount);
            const Slot& s = result.slots[static_cast<std::size_t>(slot)];
            EXPECT_TRUE(s.used);
            EXPECT_EQ(s.id, candidates[i].id);
            EXPECT_TRUE(sameV3(s.position, candidates[i].position));
            EXPECT_EQ(s.radius, candidates[i].radius);
            EXPECT_EQ(owner[static_cast<std::size_t>(slot)], -1) << "slot " << slot << " given to two candidates";
            owner[static_cast<std::size_t>(slot)] = static_cast<int>(i);
        }
        for (std::size_t slot = 0; slot < slotCount; ++slot)
        {
            const Slot& s = result.slots[slot];
            EXPECT_EQ(s.used, owner[slot] >= 0) << "slot " << slot;
            if (s.used)
            {
                EXPECT_GE(s.fade, 0.f);
                EXPECT_LE(s.fade, 1.f);
            }
        }
    }

    // select() with its shapes checked: a result of the wrong size is reported once and padded to the right size, so
    // that the test goes on to its real assertions instead of reading past the end of a vector.
    Selection selectChecked(const std::vector<Candidate>& candidates, const V3& camera,
        const std::vector<Slot>& previous, const SelectionParams& p)
    {
        Selection result = select(candidates, camera, previous, p);
        const std::size_t slotCount = static_cast<std::size_t>(std::max(p.maxSlots, 0));
        if (result.slots.size() != slotCount || result.slotOfCandidate.size() != candidates.size())
        {
            ADD_FAILURE() << "select returned " << result.slots.size() << " slots and " << result.slotOfCandidate.size()
                          << " candidate entries; expected " << slotCount << " and " << candidates.size();
            result.slots.resize(slotCount);
            result.slotOfCandidate.resize(candidates.size(), -1);
        }
        return result;
    }

    int usedSlots(const Selection& selection)
    {
        return static_cast<int>(
            std::count_if(selection.slots.begin(), selection.slots.end(), [](const Slot& slot) { return slot.used; }));
    }

    // --- importance and reach
    // ------------------------------------------------------------------------------------------

    TEST(RenderPointShadowSelect, importanceIsRadiusSquaredOverDistanceSquaredPlusOne)
    {
        EXPECT_DOUBLE_EQ(importance(10.f, 3.0), 10.0); // 100 / (9 + 1)
        EXPECT_DOUBLE_EQ(importance(5.f, 0.0), 25.0);
        EXPECT_DOUBLE_EQ(importance(5.f, 1.0), 12.5);
        EXPECT_DOUBLE_EQ(importance(0.f, 7.0), 0.0);
        EXPECT_NEAR(importance(300.f, 2000.0), 90000.0 / 4000001.0, 1e-15);
    }

    TEST(RenderPointShadowSelect, importanceFallsWithDistanceAndRisesWithRadius)
    {
        EXPECT_GT(importance(100.f, 10.0), importance(100.f, 11.0));
        EXPECT_GT(importance(100.f, 10.0), importance(100.f, 500.0));
        EXPECT_GT(importance(101.f, 10.0), importance(100.f, 10.0));
        // Quadratic in the radius, as the spec says.
        EXPECT_NEAR(importance(200.f, 30.0), 4.0 * importance(100.f, 30.0), 1e-12);
        // Finite at distance 0.
        EXPECT_TRUE(std::isfinite(importance(100.f, 0.0)));
    }

    TEST(RenderPointShadowSelect, reachIsTwiceTheRadius)
    {
        EXPECT_DOUBLE_EQ(reach(100.f), 200.0);
        EXPECT_DOUBLE_EQ(reach(0.5f), 1.0);
        EXPECT_DOUBLE_EQ(reach(0.f), 0.0);
        EXPECT_DOUBLE_EQ(reach(1234.f), 2468.0);
    }

    TEST(RenderPointShadowSelect, theDefaultParametersAreTheOnesOfTheSpec)
    {
        const SelectionParams p;
        EXPECT_EQ(p.maxSlots, 4);
        EXPECT_EQ(p.holdFactor, 1.25);
        EXPECT_EQ(p.minHoldFrames, 30u);
        EXPECT_EQ(p.maxDistance, 2048.0);
        EXPECT_EQ(p.fadeFrames, 8u);
    }

    // --- Rule 1: eligibility
    // -------------------------------------------------------------------------------------------

    TEST(RenderPointShadowSelect, onlyLightsThatCastWithAPositiveRadiusAndAFinitePositionAndInReachAreEligible)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        const std::vector<Candidate> candidates = {
            cand(1, 100, 0, 0, 100.f), // 0: fine
            cand(2, 100, 0, 1, 100.f, false), // 1: does not cast
            cand(3, 100, 0, 2, 0.f), // 2: radius 0
            cand(4, 100, 0, 3, -5.f), // 3: negative radius
            cand(5, nan, 0, 4, 100.f), // 4: NaN position
            cand(6, inf, 0, 5, 100.f), // 5: infinite position
            cand(7, 2248.5, 0, 0, 100.f), // 6: reach 200 + 2048 = 2248: just too far
            cand(8, 2248.0, 0, 0, 100.f), // 7: exactly at the limit: eligible
            cand(9, 0, -inf, 0, 100.f), // 8: -inf
            cand(10, 0, 0, nan, 100.f), // 9: NaN z
            cand(11, 100, 0, 6, std::numeric_limits<float>::quiet_NaN()), // 10: NaN radius
        };
        const Selection result = selectChecked(candidates, origin, {}, params(8));
        ASSERT_EQ(result.slotOfCandidate.size(), candidates.size());
        for (const std::size_t ineligible : { 1u, 2u, 3u, 4u, 5u, 6u, 8u, 9u, 10u })
        {
            EXPECT_EQ(result.slotOfCandidate[ineligible], -1) << ineligible;
        }
        // The two eligible ones: the near one scores higher and takes slot 0.
        EXPECT_EQ(result.slotOfCandidate[0], 0);
        EXPECT_EQ(result.slotOfCandidate[7], 1);
        EXPECT_TRUE(result.slots[0].used);
        EXPECT_TRUE(result.slots[1].used);
        EXPECT_FALSE(result.slots[2].used);
        expectConsistent(result, candidates, origin, params(8));
    }

    TEST(RenderPointShadowSelect, theReachIsMeasuredFromTheCameraAndGrowsWithTheRadius)
    {
        const V3 camera(1000.0, 0.0, 0.0);
        SelectionParams p = params(4);
        p.maxDistance = 100.0;
        // radius 50: reach 100 + 100 = 200.
        const std::vector<Candidate> candidates = {
            cand(1, 1200.0, 0, 0, 50.f), // exactly 200 away
            cand(2, 1200.25, 0, 0, 50.f), // a little beyond
            cand(3, 800.0, 0, 0, 50.f), // 200 away on the other side
            cand(4, 1200.25, 0, 0, 51.f), // reach 102 + 100: in
        };
        const Selection result = selectChecked(candidates, camera, {}, p);
        EXPECT_GE(result.slotOfCandidate[0], 0);
        EXPECT_EQ(result.slotOfCandidate[1], -1);
        EXPECT_GE(result.slotOfCandidate[2], 0);
        EXPECT_GE(result.slotOfCandidate[3], 0);
        expectConsistent(result, candidates, camera, p);
    }

    TEST(RenderPointShadowSelect, theResultHasMaxSlotsSlotsEvenWithoutCandidatesOrPrevious)
    {
        for (const int slots : { 0, 1, 4, 8 })
        {
            const Selection result = selectChecked({}, origin, {}, params(slots));
            EXPECT_EQ(result.slots.size(), static_cast<std::size_t>(slots));
            EXPECT_TRUE(result.slotOfCandidate.empty());
            for (const Slot& slot : result.slots)
            {
                EXPECT_FALSE(slot.used);
            }
        }
    }

    TEST(RenderPointShadowSelect, withNoSlotsNobodyGetsOne)
    {
        const std::vector<Candidate> candidates = { cand(1, 1, 0, 0, 100.f), cand(2, 2, 0, 0, 100.f) };
        const Selection result = selectChecked(candidates, origin, {}, params(0));
        EXPECT_TRUE(result.slots.empty());
        ASSERT_EQ(result.slotOfCandidate.size(), 2u);
        EXPECT_EQ(result.slotOfCandidate[0], -1);
        EXPECT_EQ(result.slotOfCandidate[1], -1);
    }

    // --- Rule 2: holders
    // -----------------------------------------------------------------------------------------------

    TEST(RenderPointShadowSelect, aHolderKeepsItsIndexGrowsInHeldFramesAndFadeAndTakesThePositionOfTheCandidate)
    {
        std::vector<Slot> previous(4);
        previous[1] = held(11, 5, 5, 5, 80.f, 5, 0.5f);
        previous[3] = held(12, 6, 6, 6, 80.f, 7, 0.f);
        const std::vector<Candidate> candidates = {
            cand(20, 1, 0, 0, 200.f), // a strong newcomer: it must not push the holders out
            cand(12, 60, 1, 2, 90.f), // the old holder of slot 3, moved and resized
            cand(11, 50, 3, 4, 70.f),
        };
        const Selection result = selectChecked(candidates, origin, previous, params(4));
        EXPECT_EQ(result.slotOfCandidate[1], 3);
        EXPECT_EQ(result.slotOfCandidate[2], 1);
        EXPECT_EQ(result.slotOfCandidate[0], 0); // the lowest free slot

        const Slot& a = result.slots[1];
        EXPECT_TRUE(a.used);
        EXPECT_EQ(a.id, 11u);
        EXPECT_EQ(a.heldFrames, 6u);
        EXPECT_FLOAT_EQ(a.fade, 0.625f); // 0.5 + 1 / 8
        EXPECT_TRUE(sameV3(a.position, V3(50, 3, 4)));
        EXPECT_EQ(a.radius, 70.f);

        const Slot& b = result.slots[3];
        EXPECT_EQ(b.id, 12u);
        EXPECT_EQ(b.heldFrames, 8u);
        EXPECT_FLOAT_EQ(b.fade, 0.125f);
        EXPECT_TRUE(sameV3(b.position, V3(60, 1, 2)));
        EXPECT_EQ(b.radius, 90.f);

        const Slot& fresh = result.slots[0];
        EXPECT_TRUE(fresh.used);
        EXPECT_EQ(fresh.id, 20u);
        EXPECT_EQ(fresh.heldFrames, 0u);
        EXPECT_EQ(fresh.fade, 0.f);
        EXPECT_FALSE(result.slots[2].used);
        expectConsistent(result, candidates, origin, params(4));
    }

    TEST(RenderPointShadowSelect, theFadeRisesByOneOverFadeFramesAndStopsAtOne)
    {
        SelectionParams p = params(2);
        p.fadeFrames = 4;
        std::vector<Slot> previous = { held(1, 1, 0, 0, 100.f, 0, 0.f), held(2, 2, 0, 0, 100.f, 0, 0.9f) };
        const std::vector<Candidate> candidates = { cand(1, 1, 0, 0, 100.f), cand(2, 2, 0, 0, 100.f) };
        Selection result = selectChecked(candidates, origin, previous, p);
        EXPECT_FLOAT_EQ(result.slots[0].fade, 0.25f);
        EXPECT_FLOAT_EQ(result.slots[1].fade, 1.f); // 0.9 + 0.25 clamps
        // Rising from zero to one takes fadeFrames calls.
        previous = result.slots;
        for (int call = 2; call <= 4; ++call)
        {
            result = selectChecked(candidates, origin, previous, p);
            EXPECT_FLOAT_EQ(result.slots[0].fade, 0.25f * static_cast<float>(call)) << call;
            previous = result.slots;
        }
        EXPECT_FLOAT_EQ(result.slots[0].fade, 1.f);
        result = selectChecked(candidates, origin, previous, p);
        EXPECT_FLOAT_EQ(result.slots[0].fade, 1.f);
        EXPECT_FLOAT_EQ(result.slots[1].fade, 1.f);
    }

    TEST(RenderPointShadowSelect, heldFramesSaturates)
    {
        std::vector<Slot> previous = { held(1, 1, 0, 0, 100.f, std::numeric_limits<std::uint32_t>::max(), 1.f),
            held(2, 2, 0, 0, 100.f, std::numeric_limits<std::uint32_t>::max() - 1, 1.f) };
        const std::vector<Candidate> candidates = { cand(1, 1, 0, 0, 100.f), cand(2, 2, 0, 0, 100.f) };
        const Selection result = selectChecked(candidates, origin, previous, params(2));
        EXPECT_EQ(result.slots[0].heldFrames, std::numeric_limits<std::uint32_t>::max());
        EXPECT_EQ(result.slots[1].heldFrames, std::numeric_limits<std::uint32_t>::max());
    }

    TEST(RenderPointShadowSelect, aHolderKeepsItsSlotWhateverItsScoreWhileItIsEligible)
    {
        // A very weak holder (far away, small) against two strong newcomers and room for two only.
        std::vector<Slot> previous = { held(1, 900, 0, 0, 10.f, 2, 1.f), {} };
        const std::vector<Candidate> candidates
            = { cand(2, 1, 0, 0, 300.f), cand(3, 2, 0, 0, 300.f), cand(1, 900, 0, 0, 10.f) };
        const Selection result = selectChecked(candidates, origin, previous, params(2));
        EXPECT_EQ(result.slotOfCandidate[2], 0); // kept, only 3 calls held
        EXPECT_EQ(result.slotOfCandidate[0], 1); // the free slot, the strongest newcomer
        EXPECT_EQ(result.slotOfCandidate[1], -1); // would have to displace a holder that is held for 3 frames
        EXPECT_EQ(result.slots[0].heldFrames, 3u);
    }

    TEST(RenderPointShadowSelect, anUnusedPreviousSlotHoldsNobodyEvenWithAMatchingId)
    {
        Slot stale;
        stale.used = false;
        stale.id = 7;
        stale.heldFrames = 99;
        stale.fade = 1.f;
        std::vector<Slot> previous = { stale, {} };
        const std::vector<Candidate> candidates = { cand(7, 5, 0, 0, 100.f) };
        const Selection result = selectChecked(candidates, origin, previous, params(2));
        EXPECT_EQ(result.slotOfCandidate[0], 0);
        EXPECT_EQ(result.slots[0].heldFrames, 0u);
        EXPECT_EQ(result.slots[0].fade, 0.f);
    }

    TEST(RenderPointShadowSelect, twoEligibleCandidatesWithTheSameIdTheFirstInCanonicalOrderHoldsIt)
    {
        std::vector<Slot> previous(4);
        previous[1] = held(5, 50, 0, 0, 50.f, 10, 1.f);
        // The weak one is listed first; the strong one comes first in the canonical order and holds the slot.
        const std::vector<Candidate> candidates = { cand(5, 50, 0, 0, 10.f), cand(5, 50, 0, 0, 50.f) };
        const Selection result = selectChecked(candidates, origin, previous, params(4));
        EXPECT_EQ(result.slotOfCandidate[1], 1);
        EXPECT_EQ(result.slots[1].heldFrames, 11u);
        EXPECT_EQ(result.slots[1].radius, 50.f);
        // The other is a non-holder: the lowest free slot, fresh.
        EXPECT_EQ(result.slotOfCandidate[0], 0);
        EXPECT_EQ(result.slots[0].heldFrames, 0u);
        EXPECT_EQ(result.slots[0].fade, 0.f);
        EXPECT_EQ(result.slots[0].radius, 10.f);
        expectConsistent(result, candidates, origin, params(4));
    }

    // --- Rule 3: freed slots and the length of previous
    // -----------------------------------------------------------------

    TEST(RenderPointShadowSelect, aSlotWhoseLightIsGoneBecomesUnusedAndCanBeTakenAtOnce)
    {
        std::vector<Slot> previous = { held(9, 1, 0, 0, 100.f, 50, 1.f), held(8, 2, 0, 0, 100.f, 50, 1.f) };
        const std::vector<Candidate> candidates = { cand(8, 2, 0, 0, 100.f), cand(30, 40, 0, 0, 100.f) };
        const Selection result = selectChecked(candidates, origin, previous, params(3));
        EXPECT_EQ(result.slotOfCandidate[0], 1);
        EXPECT_EQ(result.slotOfCandidate[1], 0); // the freed slot is the lowest free one
        EXPECT_EQ(result.slots[0].id, 30u);
        EXPECT_EQ(result.slots[0].heldFrames, 0u);
        EXPECT_FALSE(result.slots[2].used);
        // And when nobody takes it, it is simply unused.
        const Selection alone = selectChecked({ cand(8, 2, 0, 0, 100.f) }, origin, previous, params(3));
        EXPECT_FALSE(alone.slots[0].used);
        EXPECT_TRUE(alone.slots[1].used);
        EXPECT_FALSE(alone.slots[2].used);
    }

    TEST(RenderPointShadowSelect, aHolderThatBecomesIneligibleLosesItsSlot)
    {
        std::vector<Slot> previous = { held(1, 10, 0, 0, 100.f, 50, 1.f), held(2, 20, 0, 0, 100.f, 50, 1.f) };
        const std::vector<Candidate> candidates = { cand(1, 10, 0, 0, 100.f, false), cand(2, 20, 0, 0, 100.f) };
        const Selection result = selectChecked(candidates, origin, previous, params(2));
        EXPECT_EQ(result.slotOfCandidate[0], -1);
        EXPECT_EQ(result.slotOfCandidate[1], 1);
        EXPECT_FALSE(result.slots[0].used);
    }

    TEST(RenderPointShadowSelect, aPreviousLongerThanMaxSlotsIsTruncated)
    {
        std::vector<Slot> previous;
        std::vector<Candidate> candidates;
        for (std::uint64_t i = 0; i < 6; ++i)
        {
            const double x = 10.0 + 10.0 * static_cast<double>(i);
            previous.push_back(held(10 + i, x, 0, 0, 100.f, 5, 1.f));
            candidates.push_back(cand(10 + i, x, 0, 0, 100.f));
        }
        const Selection result = selectChecked(candidates, origin, previous, params(4));
        ASSERT_EQ(result.slots.size(), 4u);
        for (int i = 0; i < 4; ++i)
        {
            EXPECT_EQ(result.slotOfCandidate[static_cast<std::size_t>(i)], i);
            EXPECT_EQ(result.slots[static_cast<std::size_t>(i)].heldFrames, 6u);
        }
        // The two that did not fit are non-holders with nobody to displace (all held for 6 frames only).
        EXPECT_EQ(result.slotOfCandidate[4], -1);
        EXPECT_EQ(result.slotOfCandidate[5], -1);
    }

    TEST(RenderPointShadowSelect, aTruncatedHolderIsAFreshNonHolderWhenASlotIsFree)
    {
        // previous has 3 entries but only 2 slots exist; the third's light is a non-holder when a slot is free.
        std::vector<Slot> previous = { held(1, 10, 0, 0, 100.f, 40, 1.f), {}, held(3, 12, 0, 0, 100.f, 40, 1.f) };
        const std::vector<Candidate> candidates = { cand(3, 12, 0, 0, 100.f) };
        const Selection result = selectChecked(candidates, origin, previous, params(2));
        ASSERT_EQ(result.slots.size(), 2u);
        EXPECT_EQ(result.slotOfCandidate[0], 0); // the slot freed by the vanished light 1
        EXPECT_EQ(result.slots[0].heldFrames, 0u);
        EXPECT_EQ(result.slots[0].fade, 0.f);
    }

    // --- Rule 4: non-holders
    // -------------------------------------------------------------------------------------------

    TEST(RenderPointShadowSelect, nonHoldersTakeFreeSlotsInOrderOfScore)
    {
        const std::vector<Candidate> candidates = { cand(1, 300, 0, 0, 100.f), cand(2, 10, 0, 0, 100.f),
            cand(3, 50, 0, 0, 100.f), cand(4, 20, 0, 0, 100.f) };
        const Selection result = selectChecked(candidates, origin, {}, params(3));
        EXPECT_EQ(result.slotOfCandidate[1], 0); // closest
        EXPECT_EQ(result.slotOfCandidate[3], 1);
        EXPECT_EQ(result.slotOfCandidate[2], 2);
        EXPECT_EQ(result.slotOfCandidate[0], -1);
        for (const Slot& slot : result.slots)
        {
            EXPECT_EQ(slot.heldFrames, 0u);
            EXPECT_EQ(slot.fade, 0.f);
        }
    }

    TEST(RenderPointShadowSelect, aBiggerRadiusCanOutscoreADistance)
    {
        const std::vector<Candidate> candidates = { cand(1, 10, 0, 0, 20.f), cand(2, 30, 0, 0, 300.f) };
        const Selection result = selectChecked(candidates, origin, {}, params(1));
        EXPECT_EQ(result.slotOfCandidate[1], 0);
        EXPECT_EQ(result.slotOfCandidate[0], -1);
    }

    TEST(RenderPointShadowSelect, theCameraDistanceIsMeasuredInThreeDimensions)
    {
        const V3 camera(10.0, 10.0, 10.0);
        const std::vector<Candidate> candidates
            = { cand(1, 10, 10, 40, 100.f), cand(2, 10, 10, -20, 100.f), cand(3, 10, 25, 10, 100.f) };
        const Selection result = selectChecked(candidates, camera, {}, params(3));
        EXPECT_EQ(result.slotOfCandidate[2], 0); // 15 away
        EXPECT_EQ(result.slotOfCandidate[1], 1); // 30 away
        EXPECT_EQ(
            result.slotOfCandidate[0], 2); // 30 away as well; the tie goes to the lower z, so (10,10,-20) is first
    }

    TEST(RenderPointShadowSelect, equalScoresAreOrderedByThePositionXThenYThenZ)
    {
        // Four lights with the same radius and the same distance (10), hence exactly the same score.
        const std::vector<Candidate> candidates
            = { cand(1, 10, 0, 0, 50.f), cand(2, 0, 10, 0, 50.f), cand(3, 0, 0, 10, 50.f), cand(4, -10, 0, 0, 50.f) };
        const Selection result = selectChecked(candidates, origin, {}, params(4));
        // Ascending by x, then y, then z: (-10,0,0), (0,0,10), (0,10,0), (10,0,0).
        EXPECT_EQ(result.slotOfCandidate[3], 0);
        EXPECT_EQ(result.slotOfCandidate[2], 1);
        EXPECT_EQ(result.slotOfCandidate[1], 2);
        EXPECT_EQ(result.slotOfCandidate[0], 3);
    }

    TEST(RenderPointShadowSelect, theYAndZKeysBreakTiesWhenXIsEqual)
    {
        // Same x = 10, same distance sqrt(100 + 4 * 0.0625^2...) is not exact; use +-0.25 offsets in y and z instead.
        const std::vector<Candidate> byY = { cand(1, 10, 0.25, 0, 50.f), cand(2, 10, -0.25, 0, 50.f) };
        const Selection y = selectChecked(byY, origin, {}, params(2));
        EXPECT_EQ(y.slotOfCandidate[1], 0);
        EXPECT_EQ(y.slotOfCandidate[0], 1);

        const std::vector<Candidate> byZ = { cand(1, 10, 0, 0.25, 50.f), cand(2, 10, 0, -0.25, 50.f) };
        const Selection z = selectChecked(byZ, origin, {}, params(2));
        EXPECT_EQ(z.slotOfCandidate[1], 0);
        EXPECT_EQ(z.slotOfCandidate[0], 1);
    }

    TEST(RenderPointShadowSelect, identicalLightsAreOrderedByTheirIndexInTheInput)
    {
        const std::vector<Candidate> candidates
            = { cand(7, 10, 0, 0, 50.f), cand(3, 10, 0, 0, 50.f), cand(9, 10, 0, 0, 50.f) };
        const Selection result = selectChecked(candidates, origin, {}, params(2));
        EXPECT_EQ(result.slotOfCandidate[0], 0);
        EXPECT_EQ(result.slotOfCandidate[1], 1);
        EXPECT_EQ(result.slotOfCandidate[2], -1);
        // The ids (7, 3, 9) played no part: the input order decided.
    }

    TEST(RenderPointShadowSelect, nonHoldersTakeTheLowestFreeSlotFirst)
    {
        std::vector<Slot> previous = { {}, held(1, 1, 0, 0, 100.f, 5, 1.f), {}, held(2, 2, 0, 0, 100.f, 5, 1.f), {} };
        const std::vector<Candidate> candidates = { cand(1, 1, 0, 0, 100.f), cand(2, 2, 0, 0, 100.f),
            cand(10, 3, 0, 0, 100.f), cand(11, 4, 0, 0, 100.f), cand(12, 5, 0, 0, 100.f), cand(13, 6, 0, 0, 100.f) };
        const Selection result = selectChecked(candidates, origin, previous, params(5));
        EXPECT_EQ(result.slotOfCandidate[0], 1);
        EXPECT_EQ(result.slotOfCandidate[1], 3);
        EXPECT_EQ(result.slotOfCandidate[2], 0); // the strongest of the newcomers: slot 0
        EXPECT_EQ(result.slotOfCandidate[3], 2); // then slot 2
        EXPECT_EQ(result.slotOfCandidate[4], 4); // then slot 4
        EXPECT_EQ(result.slotOfCandidate[5], -1); // full; the holders are only 6 calls old
        expectConsistent(result, candidates, origin, params(5));
    }

    TEST(RenderPointShadowSelect, theCapIsMaxSlotsAndTheStrongestLightsWin)
    {
        std::vector<Candidate> candidates;
        for (int i = 0; i < 20; ++i)
        {
            candidates.push_back(cand(100 + static_cast<std::uint64_t>(i), 10.0 * (i + 1), 0, 0, 100.f));
        }
        const Selection result = selectChecked(candidates, origin, {}, params(5));
        int used = 0;
        for (const Slot& slot : result.slots)
        {
            used += slot.used ? 1 : 0;
        }
        EXPECT_EQ(used, 5);
        for (int i = 0; i < 20; ++i)
        {
            EXPECT_EQ(result.slotOfCandidate[static_cast<std::size_t>(i)], i < 5 ? i : -1) << i;
        }
        expectConsistent(result, candidates, origin, params(5));
    }

    // --- Rule 4: displacement
    // ------------------------------------------------------------------------------------------

    TEST(RenderPointShadowSelect, aNewcomerDisplacesAHolderOnlyAfterTheMinimumHold)
    {
        const auto run = [](std::uint32_t heldFrames) {
            std::vector<Slot> previous = { held(1, 3, 0, 0, 10.f, heldFrames, 1.f) };
            const std::vector<Candidate> candidates = { cand(1, 3, 0, 0, 10.f), cand(2, 0, 0, 0, 100.f) };
            return selectChecked(candidates, origin, previous, params(1));
        };
        // 28 + 1 < 30: still protected, whether the rule counts before or after this call's increment.
        Selection result = run(28);
        EXPECT_EQ(result.slotOfCandidate[0], 0);
        EXPECT_EQ(result.slotOfCandidate[1], -1);
        // 30 qualifies either way.
        result = run(30);
        EXPECT_EQ(result.slotOfCandidate[0], -1);
        EXPECT_EQ(result.slotOfCandidate[1], 0);
        EXPECT_EQ(result.slots[0].id, 2u);
        EXPECT_EQ(result.slots[0].heldFrames, 0u);
        EXPECT_EQ(result.slots[0].fade, 0.f);
        result = run(1000);
        EXPECT_EQ(result.slotOfCandidate[1], 0);
        // A different minimum hold changes the answer.
        SelectionParams p = params(1);
        p.minHoldFrames = 100;
        std::vector<Slot> previous = { held(1, 3, 0, 0, 10.f, 60, 1.f) };
        const std::vector<Candidate> candidates = { cand(1, 3, 0, 0, 10.f), cand(2, 0, 0, 0, 100.f) };
        EXPECT_EQ(selectChecked(candidates, origin, previous, p).slotOfCandidate[1], -1);
        p.minHoldFrames = 10;
        EXPECT_EQ(selectChecked(candidates, origin, previous, p).slotOfCandidate[1], 0);
    }

    TEST(RenderPointShadowSelect, aNewcomerMustBeatTheHolderByTheHoldFactorStrictly)
    {
        // Holder: radius 10 at distance 3 scores 100 / 10 = 10. The factor 1.25 makes the bar 12.5.
        std::vector<Slot> previous = { held(1, 3, 0, 0, 10.f, 100, 1.f) };
        const auto newcomer = [&](double x, SelectionParams p) {
            const std::vector<Candidate> candidates = { cand(1, 3, 0, 0, 10.f), cand(2, x, 0, 0, 5.f) };
            return selectChecked(candidates, origin, previous, p);
        };
        // Radius 5 at distance 1 scores exactly 25 / 2 = 12.5: not greater than 1.25 * 10.
        Selection result = newcomer(1.0, params(1));
        EXPECT_EQ(result.slotOfCandidate[0], 0);
        EXPECT_EQ(result.slotOfCandidate[1], -1);
        // Radius 5 at 0.9: 25 / 1.81 = 13.8 beats it.
        result = newcomer(0.9, params(1));
        EXPECT_EQ(result.slotOfCandidate[0], -1);
        EXPECT_EQ(result.slotOfCandidate[1], 0);
        // The factor is a parameter.
        SelectionParams strict = params(1);
        strict.holdFactor = 2.0;
        result = newcomer(0.9, strict);
        EXPECT_EQ(result.slotOfCandidate[0], 0);
        SelectionParams loose = params(1);
        loose.holdFactor = 1.0;
        result = newcomer(1.0, loose); // 12.5 > 10
        EXPECT_EQ(result.slotOfCandidate[1], 0);
    }

    TEST(RenderPointShadowSelect, theWeakestMatureHolderIsDisplacedFirstAndTheLowestSlotIndexWinsATie)
    {
        // Slot 0 scores 10 (radius 10 at 3); slots 1 and 2 both score 64 / 26 = 2.46 (radius 8 at 5): a tie.
        std::vector<Slot> previous
            = { held(10, 3, 0, 0, 10.f, 100, 1.f), held(11, 0, 5, 0, 8.f, 100, 1.f), held(12, 0, 0, 5, 8.f, 100, 1.f) };
        const std::vector<Candidate> candidates = {
            cand(10, 3, 0, 0, 10.f), cand(11, 0, 5, 0, 8.f), cand(12, 0, 0, 5, 8.f),
            cand(20, 1, 0, 0, 20.f), // 400 / 2 = 200
            cand(21, 0, 0, 1, 10.f), // 100 / 2 = 50
            cand(22, 3, 0, 0, 9.f), // 81 / 10 = 8.1, below 1.25 * 10
        };
        const Selection result = selectChecked(candidates, origin, previous, params(3));
        EXPECT_EQ(result.slotOfCandidate[0], 0);
        EXPECT_EQ(result.slotOfCandidate[1], -1); // displaced by the strongest newcomer (tie: lowest slot index 1)
        EXPECT_EQ(result.slotOfCandidate[2], -1); // displaced by the next one
        EXPECT_EQ(result.slotOfCandidate[3], 1);
        EXPECT_EQ(result.slotOfCandidate[4], 2);
        EXPECT_EQ(result.slotOfCandidate[5], -1);
        EXPECT_EQ(result.slots[1].id, 20u);
        EXPECT_EQ(result.slots[2].id, 21u);
        EXPECT_EQ(result.slots[0].id, 10u);
        EXPECT_EQ(result.slots[0].heldFrames, 101u);
        expectConsistent(result, candidates, origin, params(3));
    }

    TEST(RenderPointShadowSelect, anImmatureWeakHolderIsSkippedForTheWeakestMatureOne)
    {
        // Slot 0: very weak but held only 5 frames. Slot 1: stronger but old. A strong newcomer takes slot 1.
        std::vector<Slot> previous = { held(1, 500, 0, 0, 10.f, 5, 1.f), held(2, 0, 5, 0, 8.f, 100, 1.f) };
        const std::vector<Candidate> candidates
            = { cand(1, 500, 0, 0, 10.f), cand(2, 0, 5, 0, 8.f), cand(3, 1, 0, 0, 20.f) };
        const Selection result = selectChecked(candidates, origin, previous, params(2));
        EXPECT_EQ(result.slotOfCandidate[0], 0);
        EXPECT_EQ(result.slotOfCandidate[1], -1);
        EXPECT_EQ(result.slotOfCandidate[2], 1);
    }

    TEST(RenderPointShadowSelect, aLightAssignedInThisCallIsNeverDisplacedByALaterOne)
    {
        // With no minimum hold and a factor below one a later, weaker light could "displace" the one just assigned.
        SelectionParams p = params(1);
        p.minHoldFrames = 0;
        p.holdFactor = 0.5;
        const std::vector<Candidate> candidates
            = { cand(1, 3, 0, 0, 10.f), cand(2, 1, 0, 0, 4.f) }; // scores 10 and 16 / 2 = 8 (above 0.5 * 10)
        const Selection result = selectChecked(candidates, origin, {}, p);
        EXPECT_EQ(result.slotOfCandidate[0], 0);
        EXPECT_EQ(result.slotOfCandidate[1], -1);
        EXPECT_EQ(result.slots[0].id, 1u);
    }

    TEST(RenderPointShadowSelect, aDisplacedHoldersCandidateGetsNoSlotAndTheNewHolderStartsFresh)
    {
        std::vector<Slot> previous = { held(1, 20, 0, 0, 10.f, 200, 1.f) };
        const std::vector<Candidate> candidates = { cand(1, 20, 0, 0, 10.f), cand(2, 0, 0, 0, 50.f) };
        const Selection result = selectChecked(candidates, origin, previous, params(1));
        EXPECT_EQ(result.slotOfCandidate[0], -1);
        EXPECT_EQ(result.slotOfCandidate[1], 0);
        EXPECT_EQ(result.slots[0].heldFrames, 0u);
        EXPECT_EQ(result.slots[0].fade, 0.f);
        EXPECT_TRUE(result.slots[0].used);
    }

    // --- Rule 5: invariance
    // -----------------------------------------------------------------------------------------------

    // A fixed generator with our own uniform mapping, so the "random" inputs are the same everywhere.
    class Rng
    {
    public:
        explicit Rng(std::uint64_t seed)
            : mEngine(seed)
        {
        }

        double uniform(double low, double high)
        {
            const double unit = static_cast<double>(mEngine() >> 11) * (1.0 / 9007199254740992.0);
            return low + (high - low) * unit;
        }

        std::uint64_t integer(std::uint64_t bound) { return mEngine() % bound; }

        std::mt19937_64& engine() { return mEngine; }

    private:
        std::mt19937_64 mEngine;
    };

    struct Pool
    {
        std::vector<Candidate> lights;
    };

    Pool makePool(Rng& rng, int count)
    {
        Pool pool;
        for (int i = 0; i < count; ++i)
        {
            pool.lights.push_back(cand(1000 + static_cast<std::uint64_t>(i), rng.uniform(-300.0, 300.0),
                rng.uniform(-300.0, 300.0), rng.uniform(-50.0, 150.0), static_cast<float>(rng.uniform(40.0, 300.0))));
        }
        return pool;
    }

    TEST(RenderPointShadowSelect, theResultDoesNotDependOnTheOrderOfTheCandidates)
    {
        Rng rng(0x5e1ec7);
        SelectionParams p = params(4);
        p.minHoldFrames = 3;
        for (int trial = 0; trial < 20; ++trial)
        {
            const Pool pool = makePool(rng, 30);
            V3 camera(rng.uniform(-100, 100), rng.uniform(-100, 100), 0.0);
            std::vector<Slot> previous;
            for (int frame = 0; frame < 40; ++frame)
            {
                camera.x += rng.uniform(-6.0, 9.0);
                camera.y += rng.uniform(-6.0, 6.0);
                std::vector<std::size_t> order(pool.lights.size());
                for (std::size_t i = 0; i < order.size(); ++i)
                {
                    order[i] = i;
                }
                std::shuffle(order.begin(), order.end(), rng.engine());
                std::vector<Candidate> shuffled;
                for (const std::size_t index : order)
                {
                    shuffled.push_back(pool.lights[index]);
                }
                const Selection a = selectChecked(pool.lights, camera, previous, p);
                const Selection b = selectChecked(shuffled, camera, previous, p);
                EXPECT_EQ(usedSlots(a), p.maxSlots) << "30 eligible lights fill 4 slots";
                ASSERT_TRUE(a.slots.size() == b.slots.size());
                for (std::size_t i = 0; i < a.slots.size(); ++i)
                {
                    EXPECT_TRUE(sameSlot(a.slots[i], b.slots[i], true))
                        << "trial " << trial << " frame " << frame << " slot " << i;
                }
                for (std::size_t k = 0; k < order.size(); ++k)
                {
                    EXPECT_EQ(b.slotOfCandidate[k], a.slotOfCandidate[order[k]])
                        << "trial " << trial << " frame " << frame;
                }
                previous = a.slots;
            }
        }
    }

    TEST(RenderPointShadowSelect, theIdsPlayNoPartInTheOrdering)
    {
        // The same lights with ids renumbered (reversed and spread) get the same slots, frame after frame.
        Rng rng(0x1d5);
        SelectionParams p = params(4);
        p.minHoldFrames = 3;
        const Pool pool = makePool(rng, 30);
        std::vector<Candidate> renumbered = pool.lights;
        for (Candidate& c : renumbered)
        {
            c.id = 900000000ull - 7919ull * (c.id - 1000ull);
        }
        V3 camera(0.0, 0.0, 0.0);
        std::vector<Slot> previousA;
        std::vector<Slot> previousB;
        for (int frame = 0; frame < 200; ++frame)
        {
            camera.x += 1.5 * std::sin(0.07 * frame);
            camera.y += 1.0 * std::cos(0.05 * frame);
            const Selection a = selectChecked(pool.lights, camera, previousA, p);
            const Selection b = selectChecked(renumbered, camera, previousB, p);
            EXPECT_EQ(usedSlots(a), p.maxSlots) << "30 eligible lights fill 4 slots, frame " << frame;
            ASSERT_EQ(a.slots.size(), b.slots.size());
            EXPECT_EQ(a.slotOfCandidate, b.slotOfCandidate) << "frame " << frame;
            for (std::size_t i = 0; i < a.slots.size(); ++i)
            {
                EXPECT_TRUE(sameSlot(a.slots[i], b.slots[i], false)) << "frame " << frame << " slot " << i;
            }
            previousA = a.slots;
            previousB = b.slots;
        }
    }

    TEST(RenderPointShadowSelect, theSameInputsGiveTheSameResultOverAThousandRandomFrames)
    {
        Rng rng(0xde7e);
        SelectionParams p = params(4);
        p.minHoldFrames = 5;
        const Pool pool = makePool(rng, 25);
        std::vector<Slot> previous;
        for (int frame = 0; frame < 1000; ++frame)
        {
            std::vector<Candidate> candidates;
            for (const Candidate& light : pool.lights)
            {
                if (rng.integer(10) < 2)
                {
                    continue; // not in the list this frame
                }
                Candidate c = light;
                c.casts = rng.integer(10) != 0;
                if (rng.integer(25) == 0)
                {
                    c.radius = 0.f;
                }
                if (rng.integer(10) == 0)
                {
                    c.position = V3(rng.uniform(-3000, 3000), rng.uniform(-3000, 3000), 0.0);
                }
                candidates.push_back(c);
            }
            const V3 camera(rng.uniform(-400, 400), rng.uniform(-400, 400), rng.uniform(-50, 50));
            if (rng.integer(50) == 0)
            {
                // Scramble the held state now and then: the function must still be a function of its inputs.
                for (Slot& slot : previous)
                {
                    slot.heldFrames = static_cast<std::uint32_t>(rng.integer(100));
                    slot.fade = static_cast<float>(rng.uniform(0.0, 1.0));
                }
            }
            const Selection first = selectChecked(candidates, camera, previous, p);
            const Selection second = selectChecked(candidates, camera, previous, p);
            ASSERT_TRUE(sameSelection(first, second)) << "frame " << frame;
            expectConsistent(first, candidates, camera, p);
            if (::testing::Test::HasFatalFailure())
            {
                return;
            }
            previous = first.slots;
        }
    }

    // --- Stability
    // -----------------------------------------------------------------------------------------------------------

    // The number of times the content of a slot changes between two calls: a light taking a free slot or displacing
    // another. The test also reports the smallest gap between two changes of the same slot.
    struct ChangeCount
    {
        int changes = 0;
        int smallestGap = 1 << 30;
    };

    ChangeCount countChanges(
        const std::vector<Candidate>& lights, int frames, const SelectionParams& p, const std::vector<V3>& cameraPath)
    {
        ChangeCount count;
        std::vector<Slot> previous(static_cast<std::size_t>(p.maxSlots));
        std::vector<int> lastChange(static_cast<std::size_t>(p.maxSlots), -1);
        for (int frame = 0; frame < frames; ++frame)
        {
            const Selection result = selectChecked(lights, cameraPath[static_cast<std::size_t>(frame)], previous, p);
            for (std::size_t slot = 0; slot < result.slots.size(); ++slot)
            {
                const bool changed = result.slots[slot].used != previous[slot].used
                    || (result.slots[slot].used && result.slots[slot].id != previous[slot].id);
                if (changed)
                {
                    ++count.changes;
                    if (lastChange[slot] >= 0)
                    {
                        count.smallestGap = std::min(count.smallestGap, frame - lastChange[slot]);
                    }
                    lastChange[slot] = frame;
                }
            }
            previous = result.slots;
        }
        return count;
    }

    TEST(RenderPointShadowSelect, thirtyLightsAndACameraMovingHalfAUnitAFrameChangeSlotsRarely)
    {
        // Derivation of the bound. The lights are static, all within 400 units of the camera path, so every one is
        // eligible all the time (reach is at least 2 * 40 + 2048): no slot is ever lost to ineligibility. A slot
        // therefore changes only (a) once, to fill it at the start, and (b) when a newcomer displaces its holder. By
        // rule (4) a displacement needs the holder to have held the slot for minHoldFrames (30) calls, and the new
        // holder starts again at 0, so one slot is displaced at most once every 30 frames: in 600 frames at most
        // floor(599 / 30) = 19 times. With 4 slots, changes <= 4 * (1 + 19) = 80, i.e. 0.13 a frame.
        Rng rng(0x57ab1e);
        const Pool pool = makePool(rng, 30);
        const SelectionParams p = params(4);
        std::vector<V3> path;
        for (int frame = 0; frame < 600; ++frame)
        {
            path.push_back(V3(-150.0 + 0.5 * frame, 20.0, 0.0));
        }
        const ChangeCount count = countChanges(pool.lights, 600, p, path);
        const int bound = p.maxSlots * (1 + 599 / static_cast<int>(p.minHoldFrames));
        EXPECT_EQ(bound, 80);
        EXPECT_GE(count.changes, p.maxSlots) << "the slots must at least be filled";
        EXPECT_LE(count.changes, bound);
        EXPECT_GE(count.smallestGap, static_cast<int>(p.minHoldFrames))
            << "a slot changed twice within the minimum hold";
    }

    TEST(RenderPointShadowSelect, twoNearlyEqualLightsAndACameraSwayingAroundTheMidpointNeverSwap)
    {
        // A at x = -100 and B at x = 100, radius 50 each. The camera sways between x = -3 and x = 3: the distances stay
        // within 97 and 103, so the scores differ by at most (103^2 + 1) / (97^2 + 1) = 1.1275 < 1.25 = holdFactor, and
        // the single slot, once given, is never displaced: exactly one change (the first fill) in 600 frames.
        const std::vector<Candidate> lights = { cand(1, -100, 0, 0, 50.f), cand(2, 100, 0, 0, 50.f) };
        std::vector<V3> path;
        for (int frame = 0; frame < 600; ++frame)
        {
            path.push_back(V3(3.0 * std::sin(0.1 * frame), 0.0, 0.0));
        }
        const ChangeCount count = countChanges(lights, 600, params(1), path);
        EXPECT_EQ(count.changes, 1);
    }

    TEST(RenderPointShadowSelect, aRampPastTheHysteresisSwapsExactlyOnceAtTheFirstFrameThatPassesTheBar)
    {
        // The same two lights; the camera walks from x = 0 to x = 20 at 0.1 a frame, so B (at +100) gets nearer and A
        // farther. A is picked first (a tie at x = 0 goes to the lower x), B displaces it at the first frame at which
        // B's score exceeds 1.25 times A's, which is well after the minimum hold of 30 calls.
        const std::vector<Candidate> lights = { cand(1, -100, 0, 0, 50.f), cand(2, 100, 0, 0, 50.f) };
        SelectionParams p = params(1);
        std::vector<Slot> previous;
        int swapFrame = -1;
        int expectedFrame = -1;
        int changes = 0;
        for (int frame = 0; frame < 200; ++frame)
        {
            const V3 camera(0.1 * frame, 0.0, 0.0);
            const auto score = [](double distance) { return 2500.0 / (distance * distance + 1.0); };
            const double scoreA = score(100.0 + camera.x);
            const double scoreB = score(100.0 - camera.x);
            if (expectedFrame < 0 && frame >= 31 && scoreB > 1.25 * scoreA)
            {
                expectedFrame = frame;
            }
            const Selection result = selectChecked(lights, camera, previous, p);
            if (frame == 0)
            {
                EXPECT_EQ(result.slots[0].id, 1u);
            }
            else if (result.slots[0].id != previous[0].id)
            {
                ++changes;
                swapFrame = frame;
            }
            previous = result.slots;
        }
        EXPECT_EQ(changes, 1);
        EXPECT_GT(expectedFrame, 31) << "the ramp must pass the bar well after the minimum hold";
        EXPECT_EQ(swapFrame, expectedFrame);
        EXPECT_EQ(previous[0].id, 2u);
    }

    TEST(RenderPointShadowSelect, aSelectionAtRestIsStable)
    {
        Rng rng(0xa11ce);
        const Pool pool = makePool(rng, 30);
        const SelectionParams p = params(4);
        std::vector<V3> path(300, V3(10.0, 20.0, 5.0));
        const ChangeCount count = countChanges(pool.lights, 300, p, path);
        EXPECT_EQ(count.changes, p.maxSlots);
    }
}

namespace
{
    // Task 0077, review: a light whose far plane would be under two near planes casts no shadow, and the version of
    // the function that works in the caller's vectors answers as the plain one does.
    TEST(RenderPointShadowSelect, a_light_with_a_tiny_radius_is_not_eligible)
    {
        using namespace Render::PointShadow;
        std::vector<Candidate> candidates(2);
        candidates[0].id = 1;
        candidates[0].position = vsg::dvec3(0.0, 0.0, 0.0);
        candidates[0].radius = 1.f;
        candidates[1].id = 2;
        candidates[1].position = vsg::dvec3(1.0, 0.0, 0.0);
        candidates[1].radius = 100.f;
        const Selection selection = select(candidates, vsg::dvec3(0.0, 0.0, 0.0), {}, SelectionParams{});
        EXPECT_EQ(selection.slotOfCandidate[0], -1);
        EXPECT_GE(selection.slotOfCandidate[1], 0);
    }

    TEST(RenderPointShadowSelect, selectInto_reuses_its_vectors_and_equals_select)
    {
        using namespace Render::PointShadow;
        std::vector<Candidate> candidates(6);
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            candidates[i].id = 10 + i;
            candidates[i].position = vsg::dvec3(static_cast<double>(i) * 50.0, 0.0, 0.0);
            candidates[i].radius = 100.f + static_cast<float>(i);
        }
        const SelectionParams params;
        Selection reused;
        SelectionScratch scratch;
        std::vector<Slot> previous;
        for (int frame = 0; frame < 3; ++frame)
        {
            const Selection plain = select(candidates, vsg::dvec3(0.0, 0.0, 0.0), previous, params);
            selectInto(candidates, vsg::dvec3(0.0, 0.0, 0.0), previous, params, reused, scratch);
            ASSERT_EQ(plain.slots.size(), reused.slots.size());
            EXPECT_EQ(plain.slotOfCandidate, reused.slotOfCandidate);
            for (std::size_t i = 0; i < plain.slots.size(); ++i)
            {
                EXPECT_EQ(plain.slots[i].used, reused.slots[i].used);
                EXPECT_EQ(plain.slots[i].id, reused.slots[i].id);
                EXPECT_EQ(plain.slots[i].heldFrames, reused.slots[i].heldFrames);
            }
            previous = reused.slots;
        }
    }
}
