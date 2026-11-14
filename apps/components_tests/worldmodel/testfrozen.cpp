// Task 0102, stage N2: the neutral world model's SDK headers are FROZEN at version 1.0 (world_model.hpp,
// world_source.hpp). These tests pin what a pack or a consumer built against 1.0 depends on: the version numbers, the
// numeric values of the enumerations, the order of the fields (designated initializers must compile in declaration
// order), the defaults, and the order of the virtual functions of WorldCellSource (a mirror interface called through a
// reinterpret_cast, the same device that pins ObScriptContract). After the freeze a change here is a MAJOR version
// change; an additive change appends and raises the minor number.
#include <openeden/game/world_model.hpp>
#include <openeden/game/world_source.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;

    TEST(WorldModelFrozen, theVersionsAreOnePointZero)
    {
        EXPECT_EQ(kWorldModelVersion.major, 1);
        EXPECT_EQ(
            kWorldModelVersion.minor, 1); // task 0113: the light component and cell lighting (additive, 1.0 to 1.1)
        EXPECT_EQ(kWorldSourceVersion.major, 1);
        EXPECT_EQ(kWorldSourceVersion.minor, 1); // task 0120: the terrain functions (additive, 1.0 to 1.1)
    }

    TEST(WorldModelFrozen, enumerationValuesArePinned)
    {
        EXPECT_EQ(static_cast<int>(CollisionSource::None), 0);
        EXPECT_EQ(static_cast<int>(CollisionSource::FromModel), 1);
        EXPECT_EQ(static_cast<int>(CollisionSource::Box), 2);
        // ObjectKind is BaseKind (adopted answer 8); the numbers are part of world_view.hpp 1.0.
        static_assert(std::is_same_v<ObjectKind, BaseKind>);
        EXPECT_EQ(static_cast<int>(ObjectKind::Unknown), 0);
        EXPECT_EQ(static_cast<int>(ObjectKind::Static), 1);
        EXPECT_EQ(static_cast<int>(ObjectKind::MovableStatic), 2);
        EXPECT_EQ(static_cast<int>(ObjectKind::Furniture), 3);
        EXPECT_EQ(static_cast<int>(ObjectKind::Door), 4);
        EXPECT_EQ(static_cast<int>(ObjectKind::Activator), 5);
        EXPECT_EQ(static_cast<int>(ObjectKind::Container), 6);
        EXPECT_EQ(static_cast<int>(ObjectKind::Flora), 7);
        EXPECT_EQ(static_cast<int>(ObjectKind::Tree), 8);
        EXPECT_EQ(static_cast<int>(ObjectKind::Light), 9);
        EXPECT_EQ(static_cast<int>(ObjectKind::Item), 10);
        EXPECT_EQ(static_cast<int>(ObjectKind::Actor), 11);
        EXPECT_EQ(static_cast<int>(ObjectKind::LeveledList), 12);
        EXPECT_EQ(static_cast<int>(ObjectKind::Other), 13);
    }

    TEST(WorldModelFrozen, fieldOrderIsPinnedByDesignatedInitializers)
    {
        // A designated initializer list must follow the declaration order, so reordering a field breaks this build.
        const WorldTransform transform{ .position = { 1, 2, 3 }, .rotation = { 0, 0, 0, 1 }, .scale = 2.0 };
        const CollisionHint hint{
            .source = CollisionSource::Box, .boundsMin = { -1, -2, -3 }, .boundsMax = { 1, 2, 3 }, .hasBounds = true
        };
        const OwnerComponent owner{ .owner = 7, .requiredRank = 2 };
        const LockComponent lock{ .locked = true, .level = 50, .key = 9 };
        const TeleportComponent teleport{
            .destinationCell = 11, .destinationDoor = 12, .destination = transform, .flags = 1u
        };
        const PackPayload payload{ .handle = 99 };
        const WorldObject object{ .id = 1,
            .base = 2,
            .kind = ObjectKind::Door,
            .baseEditorId = "Door",
            .modelPath = "d.nif",
            .collision = hint,
            .transform = transform,
            .persistent = true,
            .initiallyDisabled = true,
            .owner = owner,
            .lock = lock,
            .teleport = teleport,
            .payload = payload,
            .light = std::nullopt };
        const WorldCell cell{ .id = 3,
            .editorId = "E",
            .name = "N",
            .interior = true,
            .hasGrid = true,
            .gridX = -4,
            .gridY = 5,
            .world = 6,
            .objects = { object },
            .unresolvedBases = 2,
            .droppedObjects = 1,
            .lighting = std::nullopt };
        EXPECT_EQ(cell.objects.size(), 1u);
        EXPECT_EQ(cell.objects[0].teleport->flags, 1u);
        EXPECT_EQ(cell.objects[0].lock->key, 9u);
        EXPECT_EQ(cell.gridX, -4);
        EXPECT_EQ(cell.droppedObjects, 1u);
    }

    TEST(WorldModelFrozen, defaultsAreWhatConsumersAssume)
    {
        const WorldObject o;
        EXPECT_EQ(o.id, kNoWorldId);
        EXPECT_EQ(o.kind, ObjectKind::Unknown);
        EXPECT_EQ(o.collision.source, CollisionSource::None);
        EXPECT_FALSE(o.collision.hasBounds);
        EXPECT_EQ(o.transform.rotation, (Quatd{ 0, 0, 0, 1 }));
        EXPECT_EQ(o.transform.scale, 1.0);
        EXPECT_FALSE(o.persistent);
        EXPECT_FALSE(o.initiallyDisabled);
        EXPECT_FALSE(o.owner.has_value());
        EXPECT_FALSE(o.lock.has_value());
        EXPECT_FALSE(o.teleport.has_value());
        EXPECT_EQ(o.payload.handle, 0u);
        const OwnerComponent owner;
        EXPECT_EQ(owner.requiredRank, -1);
        const WorldCell c;
        EXPECT_FALSE(c.interior);
        EXPECT_FALSE(c.hasGrid);
        EXPECT_EQ(c.world, kNoWorldId);
    }

    class FullSource final : public WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "full"; }
        std::size_t cellCount() const override { return 41; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<WorldId>& out) const override
        {
            out.push_back(5);
            return 1;
        }
        bool buildCell(WorldId cell, WorldCell& out, std::string&) override
        {
            out.id = cell;
            return true;
        }
        WorldId findCellByEditorId(std::string_view editorId) const override
        {
            return editorId == "cell" ? 101 : kNoWorldId;
        }
        WorldId findCellByGrid(WorldId world, std::int32_t x, std::int32_t y) const override
        {
            return static_cast<WorldId>(world * 1000 + x * 10 + y);
        }
        WorldId findWorldByEditorId(std::string_view editorId) const override
        {
            return editorId == "world" ? 202 : kNoWorldId;
        }
        double unitsPerMetre() const override { return 70.5; }
    };

    // The 1.0 layout of the source interface: the virtual functions are called through their vtable slots, in the order
    // 1.0 declared them, so a reordered, removed or inserted virtual function changes what is called and fails here.
    // (A hand-written mirror class called through a reinterpret_cast was turned by GCC's devirtualization into pure
    // virtual calls, which aborted the test on Linux.) Slots: the destructor (Itanium: two, MSVC: one), gameId,
    // cellCount, listCells, buildCell, findCellByEditorId, findCellByGrid, findWorldByEditorId, unitsPerMetre.
    TEST(WorldSourceFrozen, theVirtualFunctionOrderOfVersionOnePointZero)
    {
        FullSource real;
        WorldCellSource* base = &real;
#if defined(_MSC_VER)
        constexpr std::size_t destructorSlots = 1;
#else
        constexpr std::size_t destructorSlots = 2;
#endif
        void** vtable = *reinterpret_cast<void***>(base);
        const auto slot = [&](std::size_t index) { return vtable[destructorSlots + index]; };
        using GameId = std::string_view (*)(const void*);
        using CellCount = std::size_t (*)(const void*);
        using ListCells = std::size_t (*)(const void*, std::size_t, std::size_t, std::vector<WorldId>&);
        using BuildCell = bool (*)(void*, WorldId, WorldCell&, std::string&);
        using FindByEditorId = WorldId (*)(const void*, std::string_view);
        using FindByGrid = WorldId (*)(const void*, WorldId, std::int32_t, std::int32_t);
        using UnitsPerMetre = double (*)(const void*);
        EXPECT_EQ(reinterpret_cast<GameId>(slot(0))(base), "full");
        EXPECT_EQ(reinterpret_cast<CellCount>(slot(1))(base), 41u);
        std::vector<WorldId> ids;
        EXPECT_EQ(reinterpret_cast<ListCells>(slot(2))(base, 0, 1, ids), 1u);
        EXPECT_EQ(ids.front(), 5u);
        WorldCell cell;
        std::string error;
        EXPECT_TRUE(reinterpret_cast<BuildCell>(slot(3))(base, 77, cell, error));
        EXPECT_EQ(cell.id, 77u);
        EXPECT_EQ(reinterpret_cast<FindByEditorId>(slot(4))(base, "cell"), 101u);
        EXPECT_EQ(reinterpret_cast<FindByGrid>(slot(5))(base, 3, 4, 5), 3045u);
        EXPECT_EQ(reinterpret_cast<FindByEditorId>(slot(6))(base, "world"), 202u);
        EXPECT_EQ(reinterpret_cast<UnitsPerMetre>(slot(7))(base), 70.5);
    }

    // A source written against the first four functions (the 0.1 draft) still compiles and answers "not supported" to
    // the additions: none, and 0 for the unit.
    class OldStyleSource final : public WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "old"; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<WorldId>&) const override { return 0; }
        bool buildCell(WorldId, WorldCell&, std::string& error) override
        {
            error = "none";
            return false;
        }
    };

    TEST(WorldSourceFrozen, aSourceThatOnlyImplementsTheCoreFunctionsAnswersNotSupported)
    {
        OldStyleSource source;
        EXPECT_EQ(source.findCellByEditorId("x"), kNoWorldId);
        EXPECT_EQ(source.findCellByGrid(kNoWorldId, 0, 0), kNoWorldId);
        EXPECT_EQ(source.findWorldByEditorId("x"), kNoWorldId);
        EXPECT_EQ(source.unitsPerMetre(), 0.0);
    }
}
