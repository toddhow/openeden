// Task 0069 stage d, the fresh review's findings 4 and 6: the merged body is drawn one draw per material key (not per
// slot and material), and the game checks a cached body by a quick key of records and file sizes that the bake writes
// beside the manifest (quick.txt), not by hashing every part file again. Written before the implementation and run red
// against stubs. Synthetic data only.

#include <components/body/bodycache.hpp>
#include <components/body/mergedruntime.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace Body;

    // Four slots, two materials: slots 1 and 3 use material 0, slots 2 and 4 material 1. Each slot is one triangle
    // with its own three vertices, except slots 3 and 4, which share a vertex with slot 1 (a weld group).
    MergedBody fourSlotBody()
    {
        MergedBody body;
        for (int i = 0; i < 10; ++i)
            body.mPositions.push_back({ static_cast<float>(i), 0.f, 0.f });
        body.mMaterials = { "skin|a.dds|0|0", "skin|b.dds|0|0" };
        body.mTriangles = {
            0, 1, 2, // slot 1, material 0
            3, 4, 5, // slot 2, material 1
            2, 6, 7, // slot 3, material 0 (vertex 2 shared with slot 1)
            5, 8, 9, // slot 4, material 1 (vertex 5 shared with slot 2)
        };
        body.mSubmeshes = { { 0, 1, 0, 1 }, { 1, 2, 1, 1 }, { 0, 3, 2, 1 }, { 1, 4, 3, 1 } };
        return body;
    }

    std::multiset<std::uint32_t> drawnTriangleVertices(const std::vector<MergedDraw>& draws)
    {
        std::multiset<std::uint32_t> out;
        for (const MergedDraw& draw : draws)
        {
            for (const std::uint32_t index : draw.mIndices)
                out.insert(draw.mVertices.at(index));
        }
        return out;
    }

    TEST(MergedBodyDrawsByMaterial, one_draw_per_material_in_first_use_order)
    {
        const std::vector<MergedDraw> draws = planMergedDrawsByMaterial(fourSlotBody());
        ASSERT_EQ(draws.size(), 2u);
        EXPECT_EQ(draws[0].mMaterial, 0u);
        EXPECT_EQ(draws[1].mMaterial, 1u);
    }

    TEST(MergedBodyDrawsByMaterial, a_draw_takes_its_look_from_the_first_slot_of_its_material)
    {
        const std::vector<MergedDraw> draws = planMergedDrawsByMaterial(fourSlotBody());
        ASSERT_EQ(draws.size(), 2u);
        EXPECT_EQ(draws[0].mSlot, 1);
        EXPECT_EQ(draws[1].mSlot, 2);
    }

    TEST(MergedBodyDrawsByMaterial, every_triangle_is_drawn_once_with_its_own_vertices)
    {
        const MergedBody body = fourSlotBody();
        const std::vector<MergedDraw> draws = planMergedDrawsByMaterial(body);
        const std::multiset<std::uint32_t> expected(body.mTriangles.begin(), body.mTriangles.end());
        EXPECT_EQ(drawnTriangleVertices(draws), expected);
        std::size_t indices = 0;
        for (const MergedDraw& draw : draws)
            indices += draw.mIndices.size();
        EXPECT_EQ(indices, body.mTriangles.size());
    }

    TEST(MergedBodyDrawsByMaterial, a_shared_vertex_appears_once_per_draw)
    {
        const std::vector<MergedDraw> draws = planMergedDrawsByMaterial(fourSlotBody());
        ASSERT_EQ(draws.size(), 2u);
        // Material 0 covers vertices 0, 1, 2, 6, 7: vertex 2 is shared by slots 1 and 3 and listed once.
        EXPECT_EQ(draws[0].mVertices, (std::vector<std::uint32_t>{ 0, 1, 2, 6, 7 }));
        EXPECT_EQ(draws[0].mIndices, (std::vector<std::uint32_t>{ 0, 1, 2, 2, 3, 4 }));
        EXPECT_EQ(draws[1].mVertices, (std::vector<std::uint32_t>{ 3, 4, 5, 8, 9 }));
    }

    TEST(MergedBodyDrawsByMaterial, the_same_body_gives_the_same_plan)
    {
        EXPECT_EQ(planMergedDrawsByMaterial(fourSlotBody()), planMergedDrawsByMaterial(fourSlotBody()));
    }

    TEST(MergedBodyDrawsByMaterial, a_bad_submesh_gives_no_draws)
    {
        MergedBody body = fourSlotBody();
        body.mSubmeshes[2].mTriangleCount = 9;
        EXPECT_TRUE(planMergedDrawsByMaterial(body).empty());
        body = fourSlotBody();
        body.mSubmeshes[1].mMaterial = 7;
        EXPECT_TRUE(planMergedDrawsByMaterial(body).empty());
        body = fourSlotBody();
        body.mTriangles[4] = 99;
        EXPECT_TRUE(planMergedDrawsByMaterial(body).empty());
    }

    TEST(MergedBodyDrawsByMaterial, fewer_draws_than_the_slot_plan_when_slots_share_a_material)
    {
        const MergedBody body = fourSlotBody();
        EXPECT_LT(planMergedDrawsByMaterial(body).size(), planMergedDraws(body).size());
    }

    const std::string keyA(64, 'a');
    const std::string keyB = std::string(63, '0') + "f";

    TEST(BodyQuickKeys, written_and_read_back_unchanged)
    {
        const std::map<std::string, std::string> keys
            = { { "bodies/dark-elf-female-0123.bodymesh", keyA }, { "bodies/breton-male-4567.bodymesh", keyB } };
        std::string error;
        const std::optional<std::map<std::string, std::string>> read = readQuickKeys(writeQuickKeys(keys), error);
        ASSERT_TRUE(read.has_value()) << error;
        EXPECT_EQ(*read, keys);
    }

    TEST(BodyQuickKeys, the_text_is_sorted_and_ends_with_a_newline)
    {
        const std::map<std::string, std::string> keys
            = { { "bodies/b.bodymesh", keyA }, { "bodies/a.bodymesh", keyB } };
        const std::string text = writeQuickKeys(keys);
        EXPECT_EQ(text, "openeden.bodyquick\t1\nbodies/a.bodymesh\t" + keyB + "\nbodies/b.bodymesh\t" + keyA + "\n");
    }

    TEST(BodyQuickKeys, refuses_what_is_not_the_canonical_text)
    {
        const std::string good = "openeden.bodyquick\t1\nbodies/a.bodymesh\t" + keyA + "\n";
        const std::vector<std::string> bad = {
            "",
            "openeden.bodyquick\t2\nbodies/a.bodymesh\t" + keyA + "\n", // another version
            "openeden.bodyquick\t1\nbodies/a.bodymesh\t" + keyA, // no final newline
            "openeden.bodyquick\t1\nbodies/a.bodymesh\t" + keyA.substr(1) + "\n", // short key
            "openeden.bodyquick\t1\nbodies/a.bodymesh\t" + std::string(64, 'A') + "\n", // upper case
            "openeden.bodyquick\t1\nbodies/../a.bodymesh\t" + keyA + "\n", // unsafe name
            "openeden.bodyquick\t1\nother/a.bodymesh\t" + keyA + "\n", // not a body entry
            "openeden.bodyquick\t1\nbodies/b.bodymesh\t" + keyA + "\nbodies/a.bodymesh\t" + keyA + "\n", // order
            "openeden.bodyquick\t1\nbodies/a.bodymesh\t" + keyA + "\nbodies/a.bodymesh\t" + keyA + "\n", // twice
            "openeden.bodyquick\t1\r\nbodies/a.bodymesh\t" + keyA + "\r\n", // CR
            "openeden.bodyquick\t1\nbodies/a.bodymesh\t" + keyA + "\textra\n", // a third field
        };
        std::string error;
        ASSERT_TRUE(readQuickKeys(good, error).has_value()) << error;
        for (const std::string& text : bad)
        {
            error.clear();
            EXPECT_FALSE(readQuickKeys(text, error).has_value()) << text;
            EXPECT_FALSE(error.empty()) << text;
        }
    }

    TEST(BodyQuickKeys, refuses_a_text_over_the_size_cap)
    {
        std::string text = "openeden.bodyquick\t1\n";
        text.append(CacheLimits::sMaxManifestBytes, 'x');
        std::string error;
        EXPECT_FALSE(readQuickKeys(text, error).has_value());
    }

    TEST(BodyQuickKeys, an_empty_list_round_trips)
    {
        std::string error;
        const auto read = readQuickKeys(writeQuickKeys({}), error);
        ASSERT_TRUE(read.has_value()) << error;
        EXPECT_TRUE(read->empty());
    }
}
