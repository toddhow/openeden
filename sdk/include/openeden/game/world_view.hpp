#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <openeden/version.hpp>

// A read-only view of a game's world data: its cells and the objects placed in them (task 0086, world model option 1;
// docs/design/game-packs.md). Game packs implement it over their own store; tools such as Eden Studio's cell viewer
// read it. The contract has no record type of any game and no engine, renderer or third-party type: a cell, a placed
// object and its base are plain values, and ids are opaque 32-bit numbers the pack mints (for Skyrim SE they happen to
// be load order form ids; a consumer must treat them as names, never parse them).
//
// Main thread only. A view is created by the pack and lives until the pack is destroyed; a consumer does not keep a
// pointer to it past that. Every function answers with values: nothing throws across the contract, a failure is
// `false` plus a located message, and a consumer that gets a count it did not expect (a cell with 100,000 objects) is
// expected to cap its own use of it.

namespace openeden::sdk::game
{
    /// This header's own version; the SDK as a whole is versioned by kSdkVersion (openeden/version.hpp).
    ///
    /// Version policy. An additive change (a new virtual function at the END of a class, with a default; a new type; a
    /// new enumerator at the END of an enumeration; a new member at the END of a struct that a consumer only reads)
    /// raises the minor number. A change to the layout or meaning of anything that exists (a member's type, a removed
    /// or reordered virtual function or member, a number's meaning) raises the major number. CI
    /// (CI/check_extension_sdk.sh) keeps this paragraph in place.
    inline constexpr Version kWorldViewVersion{ 1, 0 };

    /// An opaque id of a cell, world, object or base form. Zero is "none".
    using WorldId = std::uint32_t;
    inline constexpr WorldId kNoWorldId = 0;

    /// What a placed object's base is, as far as a viewer needs to tell. A pack maps its own record types onto these;
    /// Other means "a thing with a base the contract has no name for" (it may still carry a model).
    enum class BaseKind : std::uint8_t
    {
        Unknown, ///< The base form is missing from the load order
        Static,
        MovableStatic,
        Furniture,
        Door,
        Activator,
        Container,
        Flora,
        Tree,
        Light,
        Item, ///< Armour, weapon, ammunition, book, ingredient, potion, key, soul gem ... anything carried
        Actor,
        LeveledList, ///< An item or actor list that resolves when it is placed in a game
        Other,
    };

    struct WorldVec3
    {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        friend bool operator==(const WorldVec3&, const WorldVec3&) = default;
    };

    /// One cell, as a list entry. Interior cells have no grid.
    struct WorldCellInfo
    {
        WorldId id = kNoWorldId;
        std::string editorId;
        std::string name; ///< The display name in the view's language, empty if it has none
        bool interior = false;
        bool hasGrid = false;
        std::int32_t gridX = 0;
        std::int32_t gridY = 0;
        WorldId world = kNoWorldId; ///< The world an exterior cell belongs to, none for an interior
        /// How many objects the cell holds in all (before any cap a consumer applies). For an exterior cell this
        /// counts its own objects and the world's persistent objects that stand in it. Filled by loadCell; a list
        /// entry from listCells or a find has 0 here.
        std::uint32_t objectCount = 0;

        friend bool operator==(const WorldCellInfo&, const WorldCellInfo&) = default;
    };

    struct WorldInfo
    {
        WorldId id = kNoWorldId;
        std::string editorId;
        std::string name;

        friend bool operator==(const WorldInfo&, const WorldInfo&) = default;
    };

    /// An object placed in a cell with its base resolved.
    struct PlacedObject
    {
        WorldId id = kNoWorldId; ///< The placement's own id
        WorldId base = kNoWorldId; ///< The base form's id, none if the placement names none
        BaseKind baseKind = BaseKind::Unknown;
        std::string baseEditorId;
        /// The model as the data stores it, relative to the game's mesh root with the game's own separators (for Skyrim
        /// SE `architecture\whiterun\...nif`). Empty when the base has no model.
        std::string modelPath;
        WorldVec3 position; ///< Game units, Z up
        WorldVec3
            rotation; ///< Radians about X, Y and Z as the data stores them; the order they combine in is the game's
        float scale = 1.f;
        bool hasBounds = false;
        WorldVec3 boundsMin; ///< The base's bounding box, game units, in the object's own frame (before placement)
        WorldVec3 boundsMax;
        bool persistent = false;
        bool initiallyDisabled = false;

        friend bool operator==(const PlacedObject&, const PlacedObject&) = default;
    };

    /// Everything a consumer asked for about one cell.
    struct WorldCellContent
    {
        WorldCellInfo cell;
        std::vector<PlacedObject> objects; ///< In a fixed order: the placement id, ascending
        /// Placements whose base is missing from the load order, counted (they are still listed with
        /// BaseKind::Unknown).
        std::uint32_t unresolvedBases = 0;
    };

    class WorldView
    {
    public:
        virtual ~WorldView() = default;

        /// The game's id (`skyrimse`).
        [[nodiscard]] virtual std::string_view gameId() const = 0;

        /// How many cells and worlds the view holds.
        [[nodiscard]] virtual std::size_t cellCount() const = 0;
        [[nodiscard]] virtual std::size_t worldCount() const = 0;

        /// Appends the cells from `first` on, at most `maxCount`, in a fixed order (interior cells by editor id, then
        /// exterior cells by world, then grid y, then x), to `out`. Returns how many it appended.
        virtual std::size_t listCells(
            std::size_t first, std::size_t maxCount, std::vector<WorldCellInfo>& out) const = 0;
        virtual std::size_t listWorlds(std::vector<WorldInfo>& out) const = 0;

        /// Case-insensitive.
        [[nodiscard]] virtual std::optional<WorldCellInfo> findCellByEditorId(std::string_view editorId) const = 0;
        [[nodiscard]] virtual std::optional<WorldCellInfo> findCellByGrid(
            WorldId world, std::int32_t x, std::int32_t y) const = 0;
        [[nodiscard]] virtual std::optional<WorldInfo> findWorldByEditorId(std::string_view editorId) const = 0;

        /// Reads the objects of a cell (the pack may read files to do it). False with `error` when the cell is
        /// unknown or its data cannot be read; `out` is then empty.
        virtual bool loadCell(WorldId cell, WorldCellContent& out, std::string& error) = 0;
    };

    /// The slot a pack puts its view in so that other parts of the host can find it. Game packs only.
    class WorldViewService
    {
    public:
        virtual ~WorldViewService() = default;

        /// The pack keeps ownership of `view` and withdraws it before it is destroyed.
        virtual void registerWorldView(WorldView& view) = 0;
        virtual void withdrawWorldView(WorldView& view) = 0;
        /// Null when no pack registered one.
        [[nodiscard]] virtual WorldView* worldView() = 0;
    };
}
