#ifndef OPENMW_MWWORLD_NEUTRALCELL_H
#define OPENMW_MWWORLD_NEUTRALCELL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <components/esm/refid.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadcell.hpp>

#include <openeden/game/world_model.hpp>

// Neutral cells in the engine (decision 0016, design document section 10, stage N3a-3, task 0109): the pure part.
//
// A neutral cell (openeden::sdk::game::WorldCell, handed over by a game pack) becomes, in the engine, an ordinary
// MWWorld::CellStore whose Cell wraps a synthetic ESM::Cell (an interior with no content file, so the Cell, the world
// model and every cell visitor keep working unchanged) and whose references are LiveCellRef<NeutralBase>. Everything
// here is a plain function over values, so it can be tested without an engine:
//
//  - the rotation (R3): the neutral quaternion becomes the three stored angles Misc::Convert::makeOsgQuat combines;
//  - the reference number (R6): a placement id becomes an ESM::RefNum whose content file is the reserved negative
//    kNeutralRefNumFile, so hasContentFile() is false, as for every generated reference, and no reader of a content
//    file index ever sees it;
//  - the cell record and the ESM::CellRef of one placement.
//
// Main thread only (nothing here is shared). No allocation beyond the strings and the records it returns.

namespace MWWorld
{
    /// The reserved content file index of a neutral placement's RefNum: negative (never a content file, like a
    /// generated reference) and far from World's generated counter, which starts at -1 and only counts down when its
    /// 32-bit index wraps.
    inline constexpr std::int32_t kNeutralRefNumFile = -0x40000000;

    /// The stored angles (radians, the order of ESM::Position::rot) that Misc::Convert::makeOsgQuat turns back into
    /// `q` (within float rounding: 1e-5 on the quaternion). `q` need not be normalised; a zero or non-finite one gives
    /// the identity. At gimbal lock (the middle angle +-90 degrees) the first angle is 0 and the third takes the rest.
    [[nodiscard]] std::array<float, 3> neutralRotationAngles(const openeden::sdk::game::Quatd& q);

    /// The RefNum of a placement. False (and `refNum` untouched) when the id is none or does not fit the index.
    [[nodiscard]] bool neutralRefNum(openeden::sdk::game::WorldId placement, ESM::RefNum& refNum);

    /// The id the synthetic cell record has: "<gameId>:c<cell id>". Empty RefId when the game id cannot be carried.
    [[nodiscard]] ESM::RefId neutralCellRecordId(std::string_view gameId, openeden::sdk::game::WorldId cell);

    /// A flat ambient light for a neutral interior whose WorldCell carries no lighting: RGB 0x60 each, so that a placed
    /// object is not drawn black. Named, not magic.
    inline constexpr std::uint32_t kNeutralCellAmbient = 0x00606060;

    /// The interior ESM::Cell record that stands for the neutral cell: id, name = the editor id (what the console and
    /// findInterior use), the Interior flag, no content, the flat ambient above, or the cell's own lighting (WorldCell::lighting) when it has one.
    [[nodiscard]] ESM::Cell makeNeutralCellRecord(std::string_view gameId, const openeden::sdk::game::WorldCell& cell);

    /// The reference of one placement: the base id (neutralBaseId), the RefNum, the position, the stored angles, the
    /// scale. False (with `error`) when the object has no usable id or base, a RefNum that does not fit, or a value
    /// that is not finite.
    [[nodiscard]] bool makeNeutralCellRef(std::string_view gameId, const openeden::sdk::game::WorldObject& object,
        ESM::CellRef& ref, std::string& error);

    /// Where a new game puts the player in a neutral interior (the pack gives no entry marker): the median x and y of the
    /// objects that collide, and the top of the lowest of those objects that stand over that point (the floor, as far
    /// as boxes of bounds can say), else the top of the nearest one. A heuristic, named as one: the player then falls
    /// the last few units onto the floor when the engine adjusts the position. nullopt when nothing in the cell
    /// collides.
    [[nodiscard]] std::optional<openeden::sdk::game::Vec3d> neutralStartPosition(const openeden::sdk::game::WorldCell& cell);

    /// Objects of a neutral cell by kind (the enumeration of openeden::sdk::game::ObjectKind, in its order).
    struct NeutralKindCounts
    {
        static constexpr std::size_t kKinds = static_cast<std::size_t>(openeden::sdk::game::ObjectKind::Other) + 1;
        std::array<std::size_t, kKinds> mCount{};

        void add(openeden::sdk::game::ObjectKind kind);
        [[nodiscard]] std::size_t total() const;
        /// "Static 400, Light 12 ..." for the non-zero kinds, in the enumeration's order; "none" when empty.
        [[nodiscard]] std::string describe() const;
    };

    [[nodiscard]] std::string_view neutralKindName(openeden::sdk::game::ObjectKind kind);
}

#endif
