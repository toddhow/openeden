// Task 0069 stage d: the pure part of drawing a cached merged body (components/body/mergedruntime.*). Written before
// the implementation and run red against stubs. Synthetic bodies only.

#include <components/body/mergedruntime.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace Body;

    MergedBodyFacts allGood()
    {
        MergedBodyFacts facts;
        facts.mSettingOn = true;
        facts.mThirdPerson = true;
        facts.mWerewolf = false;
        facts.mCacheLoaded = true;
        facts.mEntryPresent = true;
        facts.mKeyComputed = true;
        facts.mKeyCurrent = true;
        facts.mPayloadRead = true;
        facts.mBuilt = true;
        facts.mSlotsCovered = false;
        return facts;
    }

    TEST(MergedBodyDecision, all_facts_good_draws_the_merged_body)
    {
        EXPECT_EQ(decideMergedBody(allGood()), MergedBodyReason::Merged);
    }

    TEST(MergedBodyDecision, default_facts_keep_the_stock_parts)
    {
        EXPECT_NE(decideMergedBody(MergedBodyFacts{}), MergedBodyReason::Merged);
        EXPECT_EQ(decideMergedBody(MergedBodyFacts{}), MergedBodyReason::SettingOff);
    }

    TEST(MergedBodyDecision, each_missing_fact_names_its_reason)
    {
        struct Case
        {
            void (*mBreak)(MergedBodyFacts&);
            MergedBodyReason mExpected;
        };
        const Case cases[] = {
            { [](MergedBodyFacts& f) { f.mSettingOn = false; }, MergedBodyReason::SettingOff },
            { [](MergedBodyFacts& f) { f.mThirdPerson = false; }, MergedBodyReason::NotThirdPerson },
            { [](MergedBodyFacts& f) { f.mWerewolf = true; }, MergedBodyReason::Werewolf },
            { [](MergedBodyFacts& f) { f.mCacheLoaded = false; }, MergedBodyReason::NoCache },
            { [](MergedBodyFacts& f) { f.mEntryPresent = false; }, MergedBodyReason::NoEntry },
            { [](MergedBodyFacts& f) { f.mKeyComputed = false; }, MergedBodyReason::KeyNotComputed },
            { [](MergedBodyFacts& f) { f.mKeyCurrent = false; }, MergedBodyReason::StaleKey },
            { [](MergedBodyFacts& f) { f.mPayloadRead = false; }, MergedBodyReason::PayloadRefused },
            { [](MergedBodyFacts& f) { f.mBuilt = false; }, MergedBodyReason::BuildFailed },
            { [](MergedBodyFacts& f) { f.mSlotsCovered = true; }, MergedBodyReason::EquipmentCovers },
        };
        for (const Case& c : cases)
        {
            MergedBodyFacts facts = allGood();
            c.mBreak(facts);
            EXPECT_EQ(decideMergedBody(facts), c.mExpected);
        }
    }

    TEST(MergedBodyDecision, the_first_missing_fact_in_struct_order_decides)
    {
        MergedBodyFacts facts = allGood();
        facts.mKeyCurrent = false;
        facts.mSlotsCovered = true;
        facts.mEntryPresent = false;
        EXPECT_EQ(decideMergedBody(facts), MergedBodyReason::NoEntry);
        facts.mThirdPerson = false;
        EXPECT_EQ(decideMergedBody(facts), MergedBodyReason::NotThirdPerson);
    }

    TEST(MergedBodyDecision, every_reason_has_a_phrase)
    {
        const MergedBodyReason reasons[] = { MergedBodyReason::Merged, MergedBodyReason::SettingOff,
            MergedBodyReason::NotThirdPerson, MergedBodyReason::Werewolf, MergedBodyReason::NoCache,
            MergedBodyReason::NoEntry, MergedBodyReason::KeyNotComputed, MergedBodyReason::StaleKey,
            MergedBodyReason::PayloadRefused, MergedBodyReason::BuildFailed, MergedBodyReason::EquipmentCovers };
        for (const MergedBodyReason reason : reasons)
        {
            EXPECT_FALSE(describe(reason).empty());
        }
        EXPECT_NE(describe(MergedBodyReason::StaleKey).find("--bake-bodies"), std::string_view::npos);
    }

    TEST(MergedBodySlots, a_slot_with_priority_one_or_more_is_covered)
    {
        const std::vector<int> merged{ 1, 3, 5 };
        std::vector<int> priorities(10, 0);
        EXPECT_FALSE(mergedSlotsCovered(merged, priorities));
        priorities[4] = 7; // not a merged slot
        EXPECT_FALSE(mergedSlotsCovered(merged, priorities));
        priorities[5] = 1;
        EXPECT_TRUE(mergedSlotsCovered(merged, priorities));
        priorities[5] = 0;
        priorities[1] = 23;
        EXPECT_TRUE(mergedSlotsCovered(merged, priorities));
    }

    TEST(MergedBodySlots, a_slot_outside_the_priorities_counts_as_covered)
    {
        const std::vector<int> merged{ 2, 12 };
        const std::vector<int> priorities(10, 0);
        EXPECT_TRUE(mergedSlotsCovered(merged, priorities));
        const std::vector<int> negative{ -1 };
        EXPECT_TRUE(mergedSlotsCovered(negative, priorities));
    }

    TEST(MergedBodySlots, no_merged_slot_is_never_covered)
    {
        const std::vector<int> priorities(10, 5);
        EXPECT_FALSE(mergedSlotsCovered(std::vector<int>{}, priorities));
    }

    MergedBody twoSlotBody()
    {
        // Slot 3: two triangles over vertices 0..3 (material 0); slot 7: one triangle over 2, 3, 4 (material 1) and
        // one over 4, 5, 6 (material 0). Vertices 2 and 3 are shared between the slots (a weld).
        MergedBody body;
        for (int i = 0; i < 7; ++i)
            body.mPositions.push_back({ static_cast<float>(i), 0.f, 0.f });
        body.mTriangles = { 0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 5, 6 };
        body.mMaterials = { "a|-", "b|-" };
        body.mRegions = { { 3, 0, 2 }, { 7, 2, 2 } };
        body.mSubmeshes = { { 0, 3, 0, 2 }, { 1, 7, 2, 1 }, { 0, 7, 3, 1 } };
        return body;
    }

    TEST(MergedBodySlots, merged_slots_are_the_distinct_region_slots_ascending)
    {
        MergedBody body = twoSlotBody();
        body.mRegions = { { 7, 2, 2 }, { 3, 0, 2 }, { 7, 4, 0 } };
        EXPECT_EQ(mergedSlotsOf(body), (std::vector<int>{ 3, 7 }));
    }

    TEST(MergedBodyDraws, one_draw_per_submesh_with_its_own_compact_vertices)
    {
        const std::vector<MergedDraw> draws = planMergedDraws(twoSlotBody());
        ASSERT_EQ(draws.size(), 3u);

        EXPECT_EQ(draws[0].mMaterial, 0u);
        EXPECT_EQ(draws[0].mSlot, 3);
        EXPECT_EQ(draws[0].mVertices, (std::vector<std::uint32_t>{ 0, 1, 2, 3 }));
        EXPECT_EQ(draws[0].mIndices, (std::vector<std::uint32_t>{ 0, 1, 2, 2, 1, 3 }));

        EXPECT_EQ(draws[1].mMaterial, 1u);
        EXPECT_EQ(draws[1].mSlot, 7);
        EXPECT_EQ(draws[1].mVertices, (std::vector<std::uint32_t>{ 2, 3, 4 }));
        EXPECT_EQ(draws[1].mIndices, (std::vector<std::uint32_t>{ 0, 1, 2 }));

        EXPECT_EQ(draws[2].mVertices, (std::vector<std::uint32_t>{ 4, 5, 6 }));
        EXPECT_EQ(draws[2].mIndices, (std::vector<std::uint32_t>{ 0, 1, 2 }));
    }

    TEST(MergedBodyDraws, draw_vertices_map_back_to_the_same_positions)
    {
        const MergedBody body = twoSlotBody();
        for (const MergedDraw& draw : planMergedDraws(body))
        {
            for (std::size_t i = 0; i < draw.mIndices.size(); ++i)
            {
                const std::uint32_t local = draw.mIndices[i];
                ASSERT_LT(local, draw.mVertices.size());
            }
        }
        // Triangle 2 of the body (2, 3, 4) is draw 1's only triangle.
        const std::vector<MergedDraw> draws = planMergedDraws(body);
        ASSERT_EQ(draws.size(), 3u);
        const MergedDraw& d = draws[1];
        ASSERT_EQ(d.mIndices.size(), 3u);
        EXPECT_EQ(d.mVertices[d.mIndices[0]], 2u);
        EXPECT_EQ(d.mVertices[d.mIndices[1]], 3u);
        EXPECT_EQ(d.mVertices[d.mIndices[2]], 4u);
    }

    TEST(MergedBodyDraws, a_submesh_outside_the_triangles_gives_no_draws)
    {
        MergedBody body = twoSlotBody();
        body.mSubmeshes.push_back({ 0, 7, 3, 2 }); // triangles 3 and 4: there are 4 triangles
        EXPECT_TRUE(planMergedDraws(body).empty());
    }

    TEST(MergedBodyDraws, a_triangle_outside_the_vertices_gives_no_draws)
    {
        MergedBody body = twoSlotBody();
        body.mTriangles[11] = 7; // there are 7 vertices
        EXPECT_TRUE(planMergedDraws(body).empty());
    }

    TEST(MergedBodyDraws, a_material_outside_the_list_gives_no_draws)
    {
        MergedBody body = twoSlotBody();
        body.mSubmeshes[1].mMaterial = 2;
        EXPECT_TRUE(planMergedDraws(body).empty());
    }

    TEST(MergedBodyDraws, an_empty_body_gives_no_draws)
    {
        EXPECT_TRUE(planMergedDraws(MergedBody{}).empty());
    }

    Affine34 transformOf(double angle, double sx, double tx, double ty, double tz)
    {
        // Rotation about z by angle, uniform scale sx, translation (tx, ty, tz): row-major [M3 | t].
        const double c = std::cos(angle) * sx;
        const double s = std::sin(angle) * sx;
        return { c, -s, 0, tx, s, c, 0, ty, 0, 0, sx, tz };
    }

    std::array<double, 3> applyAffine(const Affine34& m, std::array<double, 3> v)
    {
        return { m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3], m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7],
            m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11] };
    }

    TEST(MergedBodyBind, the_inverse_undoes_the_matrix)
    {
        const Affine34 m = transformOf(0.7, 2.0, 10.0, -3.0, 4.5);
        Affine34 inverse{};
        ASSERT_TRUE(invertAffine(m, inverse));
        const std::array<double, 3> points[] = { { 0, 0, 0 }, { 1, 2, 3 }, { -40, 7.5, 0.25 } };
        for (const auto& p : points)
        {
            const auto there = applyAffine(m, p);
            const auto back = applyAffine(inverse, there);
            for (int k = 0; k < 3; ++k)
            {
                EXPECT_NEAR(back[k], p[k], 1e-9);
            }
        }
    }

    TEST(MergedBodyBind, the_identity_inverts_to_itself)
    {
        const Affine34 identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        Affine34 inverse{};
        ASSERT_TRUE(invertAffine(identity, inverse));
        EXPECT_EQ(inverse, identity);
    }

    TEST(MergedBodyBind, a_singular_or_non_finite_matrix_is_refused_and_out_untouched)
    {
        const Affine34 untouched{ 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
        Affine34 out = untouched;
        const Affine34 singular{ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
        EXPECT_FALSE(invertAffine(singular, out));
        EXPECT_EQ(out, untouched);
        Affine34 nan = transformOf(0.2, 1.0, 0, 0, 0);
        nan[3] = std::nan("");
        EXPECT_FALSE(invertAffine(nan, out));
        EXPECT_EQ(out, untouched);
        Affine34 inf = transformOf(0.2, 1.0, 0, 0, 0);
        inf[0] = std::numeric_limits<double>::infinity();
        EXPECT_FALSE(invertAffine(inf, out));
        EXPECT_EQ(out, untouched);
    }
}
