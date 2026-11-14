// Task 0109 (neutral world, stage N3a-3): the pure part of a neutral cell in the engine (apps/openmw/mwworld/
// neutralcell.hpp): the rotation round trip (R3), the reference number (R6), the cell record, the reference of a
// placement, the start position, the kind counts. Written from the task spec before the implementation; the first run
// was against stubs.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>

#include <components/esm/position.hpp>
#include <components/misc/convert.hpp>

#include "apps/openmw/mwworld/neutralcell.hpp"

namespace
{
    using namespace openeden::sdk::game;
    using MWWorld::neutralRotationAngles;

    constexpr double kPi = 3.14159265358979323846;

    // The engine's combination of the three stored angles, as a unit quaternion (x, y, z, w).
    Quatd engineQuat(float a0, float a1, float a2)
    {
        const float rot[3] = { a0, a1, a2 };
        const vsg::dquat q = Misc::Convert::makeOsgQuat(rot);
        return Quatd{ q.x, q.y, q.z, q.w };
    }

    // The distance between two unit quaternions as rotations: 0 when they are the same rotation (q and -q are one).
    double rotationDistance(const Quatd& a, const Quatd& b)
    {
        const double dot = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
        return std::sqrt(std::max(0.0, 1.0 - std::min(1.0, dot * dot)));
    }

    Quatd normalised(Quatd q)
    {
        const double length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        return Quatd{ q.x / length, q.y / length, q.z / length, q.w / length };
    }

    void expectRoundTrip(const Quatd& q)
    {
        const std::array<float, 3> angles = neutralRotationAngles(q);
        const Quatd back = engineQuat(angles[0], angles[1], angles[2]);
        EXPECT_LT(rotationDistance(normalised(q), back), 1e-5)
            << "q = (" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ") angles " << angles[0] << ", "
            << angles[1] << ", " << angles[2];
    }

    TEST(NeutralRotation, the_identity_gives_no_angles)
    {
        const auto angles = neutralRotationAngles(Quatd{ 0, 0, 0, 1 });
        EXPECT_FLOAT_EQ(angles[0], 0.f);
        EXPECT_FLOAT_EQ(angles[1], 0.f);
        EXPECT_FLOAT_EQ(angles[2], 0.f);
    }

    TEST(NeutralRotation, each_axis_alone_comes_back_as_the_engines_angle)
    {
        for (const float angle : { 0.3f, -0.7f, 1.2f, -2.9f })
        {
            const auto z = neutralRotationAngles(engineQuat(0.f, 0.f, angle));
            EXPECT_NEAR(z[2], angle, 1e-6);
            EXPECT_NEAR(z[0], 0.f, 1e-6);
            EXPECT_NEAR(z[1], 0.f, 1e-6);
            const auto x = neutralRotationAngles(engineQuat(angle, 0.f, 0.f));
            EXPECT_NEAR(x[0], angle, 1e-6);
            const float yAngle = std::clamp(angle, -1.4f, 1.4f);
            const auto y = neutralRotationAngles(engineQuat(0.f, yAngle, 0.f));
            EXPECT_NEAR(y[1], yAngle, 1e-6);
        }
    }

    TEST(NeutralRotation, away_from_the_poles_the_engines_own_angles_come_back)
    {
        // Away from the poles of the middle angle the inverse returns the angles the quaternion was made from.
        std::mt19937 gen(1234);
        std::uniform_real_distribution<float> outer(-3.1f, 3.1f);
        std::uniform_real_distribution<float> middle(-1.5f, 1.5f);
        for (int i = 0; i < 2000; ++i)
        {
            const float a0 = outer(gen);
            const float a1 = middle(gen);
            const float a2 = outer(gen);
            const auto back = neutralRotationAngles(engineQuat(a0, a1, a2));
            EXPECT_NEAR(back[0], a0, 2e-4) << i;
            EXPECT_NEAR(back[1], a1, 2e-4) << i;
            EXPECT_NEAR(back[2], a2, 2e-4) << i;
        }
    }

    TEST(NeutralRotation, random_rotations_round_trip_through_the_engines_combination)
    {
        std::mt19937 gen(99);
        std::normal_distribution<double> n(0.0, 1.0);
        for (int i = 0; i < 5000; ++i)
        {
            Quatd q{ n(gen), n(gen), n(gen), n(gen) };
            if (std::abs(q.x) + std::abs(q.y) + std::abs(q.z) + std::abs(q.w) < 1e-3)
                continue;
            expectRoundTrip(q);
        }
    }

    TEST(NeutralRotation, the_poles_of_the_middle_angle_round_trip)
    {
        // Gimbal lock: the middle angle at +-90 degrees, with many first and third angles.
        for (const double sign : { 1.0, -1.0 })
            for (int i = 0; i < 24; ++i)
                for (int j = 0; j < 24; ++j)
                {
                    const float a0 = static_cast<float>(-kPi + i * kPi / 12.0);
                    const float a2 = static_cast<float>(-kPi + j * kPi / 12.0);
                    expectRoundTrip(engineQuat(a0, static_cast<float>(sign * kPi / 2.0), a2));
                }
    }

    TEST(NeutralRotation, near_the_poles_of_the_middle_angle_round_trip)
    {
        for (const double offset : { 1e-2, 1e-3, 1e-4, 1e-5, 1e-6 })
            for (const double sign : { 1.0, -1.0 })
                expectRoundTrip(engineQuat(0.7f, static_cast<float>(sign * (kPi / 2.0 - offset)), -1.1f));
    }

    TEST(NeutralRotation, a_quaternion_that_is_not_normalised_is_normalised)
    {
        const Quatd q = engineQuat(0.4f, -0.3f, 2.0f);
        const Quatd scaled{ q.x * 7.5, q.y * 7.5, q.z * 7.5, q.w * 7.5 };
        const auto a = neutralRotationAngles(q);
        const auto b = neutralRotationAngles(scaled);
        for (int i = 0; i < 3; ++i)
            EXPECT_NEAR(a[i], b[i], 1e-6);
    }

    TEST(NeutralRotation, a_zero_or_non_finite_quaternion_is_the_identity)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        for (const Quatd& q : { Quatd{ 0, 0, 0, 0 }, Quatd{ nan, 0, 0, 1 }, Quatd{ 0, 0, 0, nan },
                 Quatd{ std::numeric_limits<double>::infinity(), 0, 0, 1 } })
        {
            const auto angles = neutralRotationAngles(q);
            EXPECT_EQ(angles[0], 0.f);
            EXPECT_EQ(angles[1], 0.f);
            EXPECT_EQ(angles[2], 0.f);
        }
    }

    // --- The reference number -------------------------------------------------------------------------------------

    TEST(NeutralRefNum, a_placement_id_becomes_an_index_with_the_reserved_negative_file)
    {
        ESM::RefNum refNum;
        ASSERT_TRUE(MWWorld::neutralRefNum(1234, refNum));
        EXPECT_EQ(refNum.mIndex, 1234u);
        EXPECT_EQ(refNum.mContentFile, MWWorld::kNeutralRefNumFile);
        EXPECT_FALSE(refNum.hasContentFile());
        EXPECT_TRUE(refNum.isSet());
    }

    TEST(NeutralRefNum, none_and_too_large_ids_are_refused_and_leave_the_value_alone)
    {
        ESM::RefNum refNum{ 5, 6 };
        EXPECT_FALSE(MWWorld::neutralRefNum(kNoWorldId, refNum));
        EXPECT_FALSE(MWWorld::neutralRefNum(static_cast<WorldId>(std::uint64_t{ 1 } << 32), refNum));
        EXPECT_EQ(refNum.mIndex, 5u);
        EXPECT_EQ(refNum.mContentFile, 6);
        EXPECT_TRUE(MWWorld::neutralRefNum(std::numeric_limits<std::uint32_t>::max(), refNum));
    }

    TEST(NeutralRefNum, the_reserved_file_is_far_from_the_generated_counter)
    {
        // World's generated RefNums start at content file -1 and count down only when their index wraps.
        EXPECT_LT(MWWorld::kNeutralRefNumFile, -1);
        EXPECT_LT(MWWorld::kNeutralRefNumFile, -0x1000000);
    }

    // --- The cell record and the reference of a placement ----------------------------------------------------------

    WorldCell sampleCell()
    {
        WorldCell cell;
        cell.id = 77;
        cell.editorId = "WhiterunBanneredMare";
        cell.name = "The Bannered Mare";
        cell.interior = true;
        return cell;
    }

    WorldObject sampleObject(WorldId id, WorldId base)
    {
        WorldObject o;
        o.id = id;
        o.base = base;
        o.kind = ObjectKind::Static;
        o.baseEditorId = "WRFloor01";
        o.modelPath = "architecture\\whiterun\\wrfloor.nif";
        o.collision.source = CollisionSource::FromModel;
        o.collision.hasBounds = true;
        o.collision.boundsMin = { -100, -100, -10 };
        o.collision.boundsMax = { 100, 100, 0 };
        o.transform.position = { 10.0, 20.0, 30.0 };
        o.transform.scale = 1.5;
        return o;
    }

    TEST(NeutralCellRecord, is_an_interior_named_by_the_editor_id)
    {
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", sampleCell());
        EXPECT_EQ(record.mId, ESM::RefId::stringRefId("skyrimse:c77"));
        EXPECT_EQ(record.mName, "WhiterunBanneredMare");
        EXPECT_FALSE(record.isExterior());
        EXPECT_TRUE(record.mContextList.empty()) << "a neutral cell has no content file to read references from";
        EXPECT_EQ(record.mAmbi.mAmbient, MWWorld::kNeutralCellAmbient);
    }

    TEST(NeutralCellRecord, the_id_names_the_game_and_the_cell)
    {
        EXPECT_EQ(MWWorld::neutralCellRecordId("skyrimse", 5), ESM::RefId::stringRefId("skyrimse:c5"));
        EXPECT_NE(MWWorld::neutralCellRecordId("skyrimse", 5), MWWorld::neutralCellRecordId("other", 5));
        EXPECT_TRUE(MWWorld::neutralCellRecordId("", 5).empty());
        EXPECT_TRUE(MWWorld::neutralCellRecordId("a:b", 5).empty());
    }

    TEST(NeutralCellRef, carries_the_base_the_refnum_the_position_the_angles_and_the_scale)
    {
        WorldObject o = sampleObject(9, 42);
        o.transform.rotation = engineQuat(0.2f, -0.4f, 1.3f);
        ESM::CellRef ref;
        std::string error;
        ASSERT_TRUE(MWWorld::makeNeutralCellRef("skyrimse", o, ref, error)) << error;
        EXPECT_EQ(ref.mRefID, ESM::RefId::stringRefId("skyrimse:b42"));
        EXPECT_EQ(ref.mRefNum.mIndex, 9u);
        EXPECT_EQ(ref.mRefNum.mContentFile, MWWorld::kNeutralRefNumFile);
        EXPECT_FLOAT_EQ(ref.mPos.pos[0], 10.f);
        EXPECT_FLOAT_EQ(ref.mPos.pos[1], 20.f);
        EXPECT_FLOAT_EQ(ref.mPos.pos[2], 30.f);
        EXPECT_NEAR(ref.mPos.rot[0], 0.2f, 1e-5);
        EXPECT_NEAR(ref.mPos.rot[1], -0.4f, 1e-5);
        EXPECT_NEAR(ref.mPos.rot[2], 1.3f, 1e-5);
        EXPECT_FLOAT_EQ(ref.mScale, 1.5f);
        EXPECT_EQ(ref.mCount, 1);
    }

    TEST(NeutralCellRef, a_placement_without_a_base_is_keyed_by_itself)
    {
        ESM::CellRef ref;
        std::string error;
        ASSERT_TRUE(MWWorld::makeNeutralCellRef("skyrimse", sampleObject(9, kNoWorldId), ref, error)) << error;
        EXPECT_EQ(ref.mRefID, ESM::RefId::stringRefId("skyrimse:o9"));
    }

    TEST(NeutralCellRef, refuses_what_the_engine_cannot_hold)
    {
        ESM::CellRef ref;
        std::string error;
        EXPECT_FALSE(MWWorld::makeNeutralCellRef("skyrimse", sampleObject(kNoWorldId, kNoWorldId), ref, error));
        EXPECT_FALSE(error.empty());

        error.clear();
        EXPECT_FALSE(MWWorld::makeNeutralCellRef(
            "skyrimse", sampleObject(static_cast<WorldId>(std::uint64_t{ 1 } << 40), 3), ref, error));
        EXPECT_FALSE(error.empty());

        WorldObject nanPosition = sampleObject(9, 42);
        nanPosition.transform.position.y = std::numeric_limits<double>::quiet_NaN();
        error.clear();
        EXPECT_FALSE(MWWorld::makeNeutralCellRef("skyrimse", nanPosition, ref, error));

        WorldObject zeroScale = sampleObject(9, 42);
        zeroScale.transform.scale = 0.0;
        error.clear();
        EXPECT_FALSE(MWWorld::makeNeutralCellRef("skyrimse", zeroScale, ref, error));

        error.clear();
        EXPECT_FALSE(MWWorld::makeNeutralCellRef("", sampleObject(9, 42), ref, error));
    }

    // --- The start position -------------------------------------------------------------------------------------

    WorldObject box(WorldId id, Vec3d position, Vec3d lo, Vec3d hi, ObjectKind kind = ObjectKind::Static)
    {
        WorldObject o = sampleObject(id, id + 1000);
        o.kind = kind;
        o.transform.position = position;
        o.transform.scale = 1.0;
        o.collision.boundsMin = lo;
        o.collision.boundsMax = hi;
        return o;
    }

    TEST(NeutralStart, stands_on_the_lowest_surface_over_the_median_point)
    {
        WorldCell cell = sampleCell();
        // A floor slab (top 0) and a ceiling slab (top 300) over the same square, and a small table (top 80).
        cell.objects.push_back(box(1, { 0, 0, -10 }, { -500, -500, -10 }, { 500, 500, 10 }));
        cell.objects.push_back(box(2, { 0, 0, 290 }, { -500, -500, -10 }, { 500, 500, 10 }));
        cell.objects.push_back(box(3, { 0, 0, 0 }, { -50, -50, 0 }, { 50, 50, 80 }));
        const auto start = MWWorld::neutralStartPosition(cell);
        ASSERT_TRUE(start.has_value());
        EXPECT_NEAR(start->x, 0.0, 1e-9);
        EXPECT_NEAR(start->y, 0.0, 1e-9);
        // The lowest top over (0, 0): the floor slab (z = -10 + 10 = 0), under the table (80) and the ceiling (300).
        EXPECT_NEAR(start->z, 0.0, 1e-9);
    }

    TEST(NeutralStart, the_transform_places_the_bounds)
    {
        WorldCell cell = sampleCell();
        WorldObject o = box(1, { 1000, -2000, 50 }, { -10, -10, -5 }, { 10, 10, 5 });
        o.transform.scale = 2.0;
        cell.objects.push_back(o);
        const auto start = MWWorld::neutralStartPosition(cell);
        ASSERT_TRUE(start.has_value());
        EXPECT_NEAR(start->x, 1000.0, 1e-9);
        EXPECT_NEAR(start->y, -2000.0, 1e-9);
        EXPECT_NEAR(start->z, 60.0, 1e-9); // 50 + 5 * 2
    }

    TEST(NeutralStart, a_rotated_slab_is_measured_in_the_world)
    {
        WorldCell cell = sampleCell();
        // A bar 200 long in x, 10 in y and 10 in z, turned a quarter about +X: its y extent (+-5) becomes z (+-5).
        WorldObject o = box(1, { 0, 0, 0 }, { -100, -5, -2 }, { 100, 5, 2 });
        const double s = std::sqrt(0.5);
        o.transform.rotation = Quatd{ s, 0, 0, s };
        cell.objects.push_back(o);
        const auto start = MWWorld::neutralStartPosition(cell);
        ASSERT_TRUE(start.has_value());
        EXPECT_NEAR(start->z, 5.0, 1e-9);
    }

    TEST(NeutralStart, without_a_surface_over_the_median_it_uses_the_nearest_one)
    {
        WorldCell cell = sampleCell();
        cell.objects.push_back(box(1, { -1000, 0, 0 }, { -10, -10, -10 }, { 10, 10, 10 }));
        cell.objects.push_back(box(2, { 1000, 0, 0 }, { -10, -10, -10 }, { 10, 10, 10 }));
        cell.objects.push_back(box(3, { 3000, 0, 40 }, { -10, -10, -10 }, { 10, 10, 10 }));
        const auto start = MWWorld::neutralStartPosition(cell);
        ASSERT_TRUE(start.has_value());
        EXPECT_NEAR(start->x, 1000.0, 1e-9); // the median x of -1000, 1000, 3000 is 1000, and box 2 is over it
        EXPECT_NEAR(start->z, 10.0, 1e-9);

        // Medians that no box covers (y = 0 is covered only if a box spans it): the nearest centre decides.
        cell.objects.clear();
        cell.objects.push_back(box(1, { 0, 0, 0 }, { -10, -10, -10 }, { 10, 10, 20 }));
        cell.objects.push_back(box(2, { 1000, 1000, 0 }, { -10, -10, -10 }, { 10, 10, 30 }));
        cell.objects.push_back(box(3, { 1010, 1000, 0 }, { -10, -10, -10 }, { 10, 10, 40 }));
        // x medians: 0, 1000, 1010 -> 1000; y medians: 0, 1000, 1000 -> 1000. Box 2 spans (1000, 1000): its top 30.
        const auto covered = MWWorld::neutralStartPosition(cell);
        ASSERT_TRUE(covered.has_value());
        EXPECT_NEAR(covered->z, 30.0, 1e-9);

        cell.objects.clear();
        cell.objects.push_back(box(1, { 0, 0, 0 }, { -10, -10, -10 }, { 10, 10, 20 }));
        cell.objects.push_back(box(2, { 100, 500, 0 }, { -10, -10, -10 }, { 10, 10, 30 }));
        cell.objects.push_back(box(3, { 500, 100, 0 }, { -10, -10, -10 }, { 10, 10, 40 }));
        // x medians 0, 100, 500 -> 100; y medians 0, 100, 500 -> 100: nothing spans (100, 100); the nearest centre
        // to it is box 1 at (0, 0)? no: box 2 is at (100, 500) (400 away), box 3 at (500, 100) (400 away), box 1 at
        // (0, 0) (141 away). Box 1 wins: its top is 20.
        const auto nearest = MWWorld::neutralStartPosition(cell);
        ASSERT_TRUE(nearest.has_value());
        EXPECT_NEAR(nearest->x, 0.0, 1e-9);
        EXPECT_NEAR(nearest->y, 0.0, 1e-9);
        EXPECT_NEAR(nearest->z, 20.0, 1e-9);
    }

    TEST(NeutralStart, objects_that_do_not_collide_or_are_disabled_do_not_count)
    {
        WorldCell cell = sampleCell();
        cell.objects.push_back(box(1, { 0, 0, 500 }, { -10, -10, -10 }, { 10, 10, 10 }, ObjectKind::Light));
        cell.objects.push_back(box(2, { 0, 0, 500 }, { -10, -10, -10 }, { 10, 10, 10 }, ObjectKind::Actor));
        cell.objects.push_back(box(3, { 0, 0, 500 }, { -10, -10, -10 }, { 10, 10, 10 }, ObjectKind::Item));
        WorldObject disabled = box(4, { 0, 0, 500 }, { -10, -10, -10 }, { 10, 10, 10 });
        disabled.initiallyDisabled = true;
        cell.objects.push_back(disabled);
        WorldObject noCollision = box(5, { 0, 0, 500 }, { -10, -10, -10 }, { 10, 10, 10 });
        noCollision.collision.source = CollisionSource::None;
        cell.objects.push_back(noCollision);
        WorldObject noBounds = box(6, { 0, 0, 500 }, { -10, -10, -10 }, { 10, 10, 10 });
        noBounds.collision.hasBounds = false;
        cell.objects.push_back(noBounds);
        EXPECT_FALSE(MWWorld::neutralStartPosition(cell).has_value());
        EXPECT_FALSE(MWWorld::neutralStartPosition(sampleCell()).has_value());

        cell.objects.push_back(box(7, { 5, 6, 0 }, { -10, -10, -10 }, { 10, 10, 10 }));
        const auto start = MWWorld::neutralStartPosition(cell);
        ASSERT_TRUE(start.has_value());
        EXPECT_NEAR(start->x, 5.0, 1e-9);
    }

    TEST(NeutralStart, a_box_with_a_non_finite_transform_is_ignored)
    {
        WorldCell cell = sampleCell();
        WorldObject bad = box(1, { 0, 0, 0 }, { -10, -10, -10 }, { 10, 10, 10 });
        bad.transform.position.x = std::numeric_limits<double>::infinity();
        cell.objects.push_back(bad);
        EXPECT_FALSE(MWWorld::neutralStartPosition(cell).has_value());
    }

    // --- The counts ----------------------------------------------------------------------------------------------

    TEST(NeutralKindCounts, counts_and_describes_in_the_enumerations_order)
    {
        MWWorld::NeutralKindCounts counts;
        EXPECT_EQ(counts.total(), 0u);
        EXPECT_EQ(counts.describe(), "none");
        counts.add(ObjectKind::Light);
        counts.add(ObjectKind::Static);
        counts.add(ObjectKind::Static);
        counts.add(ObjectKind::Door);
        EXPECT_EQ(counts.total(), 4u);
        EXPECT_EQ(counts.describe(), "Static 2, Door 1, Light 1");
    }

    TEST(NeutralKindCounts, every_kind_has_a_name)
    {
        for (std::size_t i = 0; i < MWWorld::NeutralKindCounts::kKinds; ++i)
            EXPECT_FALSE(MWWorld::neutralKindName(static_cast<ObjectKind>(i)).empty()) << i;
    }
}
