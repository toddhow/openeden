#ifndef OPENMW_MWWORLD_NEUTRALSAVE_H
#define OPENMW_MWWORLD_NEUTRALSAVE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <components/esm/defs.hpp>
#include <components/esm/position.hpp>
#include <components/esm3/cellref.hpp>

// Saves of a neutral-world session (decision 0016, design document stage N6, task 0177): the pure part.
//
// A neutral cell is rebuilt from the game pack at every load, so a save keeps only what differs from the pack: for each
// cell the placements that were moved, scaled, enabled or disabled against the pack's state, or deleted; the player's
// cell; the locks that were opened and the keys held (NeutralLockState). Everything is identified by values that the
// pack's plugins fix (game id, the pack's cell id and placement id, which are the plugin-and-local-form-id identities
// of the pack), never by anything run-specific. The engine's ESM::Cell id of a neutral cell is derived from them.
//
// The bytes are one canonical little-endian blob, written into one engine record (ESM::REC_NWLD). The decoder is
// strict: every count is bounded by the bytes left and by a cap before anything is allocated, and every error says
// where it happened. Plain values, no shared state; the engine collects and writes on the main thread and applies on
// the main thread after the load.

namespace MWWorld
{
    /// The record of the .omwsave stream that holds the blob ("NWLD", subrecord "NWDA"). Written only by a neutral
    /// session with [Game] neutral saves on, so every other save is byte for byte what it always was; a reader that
    /// does not know it skips it (the default branch of MWState::LoadGame).
    inline constexpr unsigned int kNeutralSaveRecord = ESM::esm3Recname("NWLD");

    /// Version 1: moved, scaled, enabled-changed and deleted placements of the pack. Version 2 (task 0202) adds objects
    /// a script created (PlaceAtMe) and the counter of their ids; it is written ONLY when the state has either, so
    /// every
    /// other save keeps its version 1 bytes. Both are read.
    inline constexpr std::uint32_t kNeutralSaveVersion = 1;
    inline constexpr std::uint32_t kNeutralSaveVersionCreated = 2;
    inline constexpr std::size_t kNeutralSaveMaxCreated = 65536;

    /// Caps of the decoder: a hostile save must not allocate without bound.
    inline constexpr std::size_t kNeutralSaveMaxBytes = std::size_t{ 64 } << 20;
    inline constexpr std::size_t kNeutralSaveMaxCells = 65536;
    inline constexpr std::size_t kNeutralSaveMaxObjects = std::size_t{ 1 } << 20;
    inline constexpr std::size_t kNeutralSaveMaxEntries = std::size_t{ 1 } << 20;
    inline constexpr std::size_t kNeutralSaveMaxString = 255;

    /// Which cell: an interior by the pack's cell id, an exterior by the world and the grid square.
    struct NeutralSaveCellKey
    {
        enum class Kind : std::uint8_t
        {
            None = 0,
            Interior = 1,
            Exterior = 2,
        };
        Kind mKind = Kind::None;
        std::string mGameId;
        std::uint32_t mCell = 0; ///< Interior: the pack's cell id
        std::uint32_t mWorld = 0; ///< Exterior: the pack's world id
        std::int32_t mX = 0; ///< Exterior: the grid square
        std::int32_t mY = 0;

        friend bool operator==(const NeutralSaveCellKey&, const NeutralSaveCellKey&) = default;
    };

    enum NeutralSaveObjectFlags : std::uint8_t
    {
        NeutralSaveMoved = 1, ///< mPos and mRot are the object's position and rotation (they differ from the pack's)
        NeutralSaveScaled = 2, ///< mScale differs
        NeutralSaveEnabledChanged = 4, ///< the enabled state differs from the pack's; NeutralSaveEnabled is its value
        NeutralSaveEnabled = 8,
        NeutralSaveDeleted = 16,
        /// Version 2: the placement was created by a script. It is saved whole: NeutralSaveMoved is always set
        /// (position and
        /// rotation), the flags say a scale or a disabled state, and the entry carries the base's description.
        NeutralSaveCreated = 32,
    };

    /// What the engine needs to rebuild a created object without asking the pack (the pack's plugins may differ in a
    /// later session, the object must not): the description of its base. Strings are capped like every save string.
    struct NeutralCreatedInfo
    {
        std::uint32_t mBase = 0;
        std::uint8_t mKind = 0; ///< openeden::sdk::game::ObjectKind
        std::string mEditorId;
        std::string mModel;
        bool mHasBounds = false;
        float mBoundsMin[3] = {};
        float mBoundsMax[3] = {};

        friend bool operator==(const NeutralCreatedInfo&, const NeutralCreatedInfo&) = default;
    };

    struct NeutralSavedObject
    {
        std::uint32_t mPlacement = 0;
        std::uint8_t mFlags = 0;
        float mPos[3] = {};
        float mRot[3] = {};
        float mScale = 1.f;
        std::optional<NeutralCreatedInfo> mCreated; ///< set exactly when NeutralSaveCreated is

        friend bool operator==(const NeutralSavedObject&, const NeutralSavedObject&) = default;
    };

    struct NeutralSavedCell
    {
        NeutralSaveCellKey mKey;
        std::vector<NeutralSavedObject> mObjects; ///< ascending placement id, no duplicates

        friend bool operator==(const NeutralSavedCell&, const NeutralSavedCell&) = default;
    };

    struct NeutralSaveState
    {
        NeutralSaveCellKey mPlayerCell;
        std::vector<NeutralSavedCell> mCells; ///< cells with at least one changed object, in key order
        /// (game id, placement id) of opened locks and (game id, form id) of held keys, both sorted
        std::vector<std::pair<std::string, std::uint32_t>> mUnlocked;
        std::vector<std::pair<std::string, std::uint32_t>> mKeys;
        /// Version 2: the index the next created object gets (WorldSourceServiceImpl::nextCreatedIndex); 0 when nothing
        /// was
        /// ever created. When set it is above the index of every created object.
        std::uint32_t mNextCreatedIndex = 0;

        friend bool operator==(const NeutralSaveState&, const NeutralSaveState&) = default;
    };

    [[nodiscard]] bool operator<(const NeutralSaveCellKey& a, const NeutralSaveCellKey& b);

    /// Sorts and removes duplicates so that a collected state is canonical.
    void canonicaliseNeutralSave(NeutralSaveState& state);

    /// The canonical bytes of a state. Cells are written in key order, objects in placement order, entries sorted, so
    /// equal states give equal bytes whatever the order they were collected in. \throws std::length_error for a state
    /// beyond the caps (a save must not be written that cannot be read back).
    [[nodiscard]] std::vector<std::byte> encodeNeutralSave(NeutralSaveState state);

    /// Reads bytes back. False with `error` (what and at which byte) for a malformed, truncated, over-cap, newer or
    /// non-canonical blob; `out` is left empty then.
    [[nodiscard]] bool decodeNeutralSave(std::span<const std::byte> bytes, NeutralSaveState& out, std::string& error);

    /// The current state of one placement.
    struct NeutralObjectNow
    {
        ESM::Position mPosition;
        float mScale = 1.f;
        bool mEnabled = true;
        bool mDeleted = false;
    };

    /// What differs between a placement as the pack describes it (`original`: the ESM::CellRef makeNeutralCellRef
    /// built; `originallyEnabled`: not initiallyDisabled) and as it is now. Nullopt when nothing differs. Positions
    /// compare exactly (bit equal floats), because the engine copies the pack's values and a move writes new ones.
    [[nodiscard]] std::optional<NeutralSavedObject> diffNeutralObject(
        std::uint32_t placement, const ESM::CellRef& original, bool originallyEnabled, const NeutralObjectNow& now);

    /// Version 2 (task 0202): the entry of an object a script created, saved whole: NeutralSaveCreated and
    /// NeutralSaveMoved
    /// always, NeutralSaveScaled when the scale is not 1, NeutralSaveEnabledChanged (without NeutralSaveEnabled) when
    /// it is
    /// disabled. A deleted created object has no entry (the caller leaves it out).
    [[nodiscard]] NeutralSavedObject saveCreatedNeutralObject(
        std::uint32_t placement, const NeutralCreatedInfo& info, const NeutralObjectNow& now);

    /// Applies a saved object over the pack's own state in `now`.
    void applyNeutralSavedObject(const NeutralSavedObject& saved, NeutralObjectNow& now);
}

#endif
