// Task 0291 (swimming and water in a Skyrim session): interior water for a neutral cell, the water an actor stands in,
// and where a swimming neutral actor rests. Written before the implementation; the first run was against stubs.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <components/esm3/readerscache.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/game/world_water.hpp>

#include "apps/openmw/mwclass/neutral.hpp"
#include "apps/openmw/mwphysics/neutralactor.hpp"
#include "apps/openmw/mwworld/cell.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutralphysicswater.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

namespace
{
    using namespace openeden::sdk::game;

    constexpr WorldId kWorld = 20;
    constexpr WorldId kExterior = 30;
    constexpr WorldId kWet = 40; // an interior with water at 12.5
    constexpr WorldId kDry = 41; // an interior without

    WorldCell interiorCell(WorldId id, std::string editorId)
    {
        WorldCell cell;
        cell.id = id;
        cell.editorId = std::move(editorId);
        cell.interior = true;
        return cell;
    }

    // ---- the record ----

    TEST(NeutralInteriorWater, a_record_with_water_has_the_flag_and_the_height_and_stays_an_interior)
    {
        const ESM::Cell record = MWWorld::makeNeutralCellRecord("fakegame", interiorCell(kWet, "Wet"), 12.5f);
        EXPECT_NE(record.mData.mFlags & ESM::Cell::HasWater, 0u);
        EXPECT_NE(record.mData.mFlags & ESM::Cell::Interior, 0u);
        EXPECT_FLOAT_EQ(record.mWater, 12.5f);
        const MWWorld::Cell cell(record);
        EXPECT_TRUE(cell.hasWater());
        EXPECT_FALSE(cell.isExterior());
        EXPECT_FLOAT_EQ(cell.getWaterHeight(), 12.5f);
    }

    TEST(NeutralInteriorWater, a_record_without_water_is_the_old_record)
    {
        const WorldCell cell = interiorCell(kDry, "Dry");
        const ESM::Cell plain = MWWorld::makeNeutralCellRecord("fakegame", cell);
        const ESM::Cell none = MWWorld::makeNeutralCellRecord("fakegame", cell, std::nullopt);
        EXPECT_EQ(plain.mData.mFlags, none.mData.mFlags);
        EXPECT_EQ(plain.mWater, none.mWater);
        EXPECT_EQ(none.mData.mFlags & ESM::Cell::HasWater, 0u);
        EXPECT_FALSE(MWWorld::Cell(none).hasWater());
    }

    // ---- the world model asks the source ----

    class Source : public WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "fakegame"; }
        std::size_t cellCount() const override { return 3; }
        std::size_t listCells(std::size_t first, std::size_t max, std::vector<WorldId>& out) const override
        {
            const WorldId all[] = { kExterior, kWet, kDry };
            for (std::size_t i = first; i < 3 && out.size() < max; ++i)
                out.push_back(all[i]);
            return out.size();
        }
        bool buildCell(WorldId id, WorldCell& out, std::string& error) override
        {
            out = {};
            out.id = id;
            if (id == kExterior)
            {
                out.world = kWorld;
                out.hasGrid = true;
                out.gridX = 2;
                out.gridY = 3;
                out.editorId = "Outside";
                return true;
            }
            if (id == kWet || id == kDry)
            {
                out.interior = true;
                out.editorId = id == kWet ? "Wet" : "Dry";
                return true;
            }
            error = "no such cell";
            return false;
        }
        WorldId findCellByEditorId(std::string_view editorId) const override
        {
            return editorId == "Wet" ? kWet : editorId == "Dry" ? kDry : kNoWorldId;
        }
        WorldId findCellByGrid(WorldId world, std::int32_t x, std::int32_t y) const override
        {
            return world == kWorld && x == 2 && y == 3 ? kExterior : kNoWorldId;
        }
        bool worldInfo(WorldId world, WorldSpaceInfo& out) const override
        {
            if (world != kWorld)
                return false;
            out = {};
            out.id = kWorld;
            out.editorId = "TestWorld";
            return true;
        }
    };

    class WaterSource final : public Source, public CellWaterSource
    {
    public:
        bool cellWater(WorldId world, std::int32_t x, std::int32_t y, CellWater& out) const override
        {
            out = {};
            if (world != kWorld)
                return false;
            if (x == 2 && y == 3)
                out = { true, 500.f };
            return true;
        }
        bool interiorWater(WorldId cell, CellWater& out) const override
        {
            out = {};
            if (cell != kWet)
                return false;
            out = { true, 12.5f };
            return true;
        }
    };

    template <class S>
    struct Fixture : testing::Test
    {
        void SetUp() override
        {
            MWClass::Neutral::registerSelf();
            std::string error;
            ASSERT_TRUE(mNeutral.addSource(mSource, error)) << error;
            mModel = std::make_unique<MWWorld::WorldModel>(mStore, mReaders);
        }
        MWWorld::CellStore* interior(WorldId id)
        {
            const MWWorld::NeutralCellLookup found = mModel->findNeutralCell(mNeutral, "fakegame", id);
            return found.mStore;
        }
        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        S mSource;
        NeutralWorld::NeutralWorldStore mNeutral;
        std::unique_ptr<MWWorld::WorldModel> mModel;
    };

    using Wet = Fixture<WaterSource>;
    using NoInterface = Fixture<Source>;

    TEST_F(Wet, an_interior_the_source_gives_water_has_it)
    {
        MWWorld::CellStore* store = interior(kWet);
        ASSERT_NE(store, nullptr);
        EXPECT_TRUE(store->getCell()->hasWater());
        EXPECT_FLOAT_EQ(store->getWaterLevel(), 12.5f);
        EXPECT_FALSE(store->isExterior());
    }

    TEST_F(Wet, an_interior_the_source_gives_no_water_is_dry)
    {
        MWWorld::CellStore* store = interior(kDry);
        ASSERT_NE(store, nullptr);
        EXPECT_FALSE(store->getCell()->hasWater());
    }

    TEST_F(NoInterface, a_source_without_the_interface_gives_dry_interiors)
    {
        MWWorld::CellStore* store = interior(kWet);
        ASSERT_NE(store, nullptr);
        EXPECT_FALSE(store->getCell()->hasWater());
    }

    // ---- the water an actor stands in ----

    TEST_F(Wet, an_interior_with_water_is_wet_everywhere)
    {
        MWWorld::CellStore* store = interior(kWet);
        ASSERT_NE(store, nullptr);
        const std::optional<float> a = MWWorld::neutralWaterLevelAt(*store, 0.f, 0.f);
        const std::optional<float> b = MWWorld::neutralWaterLevelAt(*store, -9000.f, 777.f);
        ASSERT_TRUE(a.has_value());
        ASSERT_TRUE(b.has_value());
        EXPECT_FLOAT_EQ(*a, 12.5f);
        EXPECT_FLOAT_EQ(*b, 12.5f);
    }

    TEST_F(Wet, a_dry_interior_gives_no_level)
    {
        MWWorld::CellStore* store = interior(kDry);
        ASSERT_NE(store, nullptr);
        EXPECT_FALSE(MWWorld::neutralWaterLevelAt(*store, 0.f, 0.f).has_value());
    }

    TEST_F(Wet, an_exterior_square_is_wet_only_inside_its_own_square)
    {
        const MWWorld::NeutralCellLookup found = mModel->findNeutralCell(mNeutral, "fakegame", kExterior);
        ASSERT_FALSE(found.mWorldspace.empty());
        MWWorld::CellStore* store
            = mModel->findNeutralExterior(mNeutral, ESM::ExteriorCellLocation(2, 3, found.mWorldspace));
        ASSERT_NE(store, nullptr);
        // square (2, 3) is x 8192 to 12288, y 12288 to 16384
        const std::optional<float> inside = MWWorld::neutralWaterLevelAt(*store, 10000.f, 13000.f);
        ASSERT_TRUE(inside.has_value());
        EXPECT_FLOAT_EQ(*inside, 500.f);
        EXPECT_TRUE(MWWorld::neutralWaterLevelAt(*store, 8192.f, 12288.f).has_value()); // the corner belongs to it
        EXPECT_FALSE(MWWorld::neutralWaterLevelAt(*store, 12288.f, 13000.f).has_value()); // the next square
        EXPECT_FALSE(MWWorld::neutralWaterLevelAt(*store, 10000.f, 12287.f).has_value());
        EXPECT_FALSE(MWWorld::neutralWaterLevelAt(*store, std::nanf(""), 13000.f).has_value());
    }

    // ---- the physics plane of the player's cell ----

    TEST_F(Wet, the_physics_plane_of_an_interior_with_water_is_at_its_height)
    {
        MWWorld::CellStore* store = interior(kWet);
        ASSERT_NE(store, nullptr);
        const std::optional<float> level = MWWorld::neutralPhysicsWaterLevel(*store);
        ASSERT_TRUE(level.has_value());
        EXPECT_FLOAT_EQ(*level, 12.5f);
    }

    TEST_F(Wet, a_dry_interior_has_no_physics_plane)
    {
        MWWorld::CellStore* store = interior(kDry);
        ASSERT_NE(store, nullptr);
        EXPECT_FALSE(MWWorld::neutralPhysicsWaterLevel(*store).has_value());
    }

    // ---- where a swimming neutral actor rests ----

    using MWPhysics::NeutralActor::swimRest;

    TEST(NeutralSwim, no_water_above_the_floor_is_not_swimming)
    {
        const auto r = swimRest(100.f, 50.f, 128.f); // the surface is below the floor
        EXPECT_FALSE(r.mSwims);
        EXPECT_FLOAT_EQ(r.mFeetZ, 100.f);
    }

    TEST(NeutralSwim, wading_in_water_up_to_the_waist_is_not_swimming)
    {
        const auto r = swimRest(0.f, 64.f, 128.f); // exactly half the height: not deeper than it
        EXPECT_FALSE(r.mSwims);
        EXPECT_FLOAT_EQ(r.mFeetZ, 0.f);
    }

    TEST(NeutralSwim, water_deeper_than_half_the_height_is_swimming)
    {
        const auto r = swimRest(0.f, 70.f, 128.f);
        EXPECT_TRUE(r.mSwims);
        // 70 - 0.8 * 128 = -32.4 is below the floor: the feet stay on the floor until the water is deeper
        EXPECT_FLOAT_EQ(r.mFeetZ, 0.f);
    }

    TEST(NeutralSwim, in_deep_water_the_feet_float_below_the_surface_with_the_head_out)
    {
        const auto r = swimRest(-300.f, 0.f, 128.f);
        EXPECT_TRUE(r.mSwims);
        EXPECT_NEAR(r.mFeetZ, -102.4f, 1e-3f); // 0 - 0.8 * 128
        EXPECT_GT(r.mFeetZ + 128.f, 0.f); // the head is above the surface
    }

    TEST(NeutralSwim, the_float_height_scales_with_the_actor)
    {
        const auto small = swimRest(-1000.f, 0.f, 64.f);
        const auto big = swimRest(-1000.f, 0.f, 256.f);
        EXPECT_NEAR(small.mFeetZ, -51.2f, 1e-3f);
        EXPECT_NEAR(big.mFeetZ, -204.8f, 1e-3f);
    }

    TEST(NeutralSwim, bad_numbers_mean_dry)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(swimRest(0.f, nan, 128.f).mSwims);
        EXPECT_FALSE(swimRest(0.f, inf, 128.f).mSwims);
        EXPECT_FALSE(swimRest(0.f, 100.f, 0.f).mSwims);
        EXPECT_FALSE(swimRest(0.f, 100.f, -5.f).mSwims);
        EXPECT_FALSE(swimRest(nan, 100.f, 128.f).mSwims);
        EXPECT_FLOAT_EQ(swimRest(5.f, nan, 128.f).mFeetZ, 5.f);
    }

    TEST(NeutralSwim, the_rest_is_never_below_the_floor_and_never_above_the_surface)
    {
        for (float floorZ = -500.f; floorZ <= 500.f; floorZ += 37.f)
            for (float water = -400.f; water <= 400.f; water += 41.f)
            {
                const auto r = swimRest(floorZ, water, 128.f);
                EXPECT_GE(r.mFeetZ, floorZ);
                if (r.mSwims)
                {
                    EXPECT_LE(r.mFeetZ, std::max(floorZ, water - 0.8f * 128.f) + 1e-3f);
                }
            }
    }
}
