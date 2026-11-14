#ifndef OPENEDEN_GAME_SKYRIMSE_FURNITURE_H
#define OPENEDEN_GAME_SKYRIMSE_FURNITURE_H

// Skyrim SE furniture markers (task 0135 stage S6b). Pure code: reads the BSFurnitureMarkerNode extra data of a
// furniture model (NIF 20.2.0.7), places a marker in the world and works out where an actor stands to approach it.
//
// What the data says (VERIFIED on Skyrim.esm with an independent Python walk, aggregates in the task log):
//  - A FURN record has FNPR entries, 4 bytes each: u16 marker type (1 sit, 2 sleep; lean is 3 in FNPR) and u16 entry
//    points, a flag set. The model's BSFurnitureMarkerNode holds the same markers with their positions: per marker a
//    vec3 offset, a float heading, a u16 animation type (1 sit, 2 sleep) and a u16 entry point flag set. For the chair
//    (FNPR 01 00 0d 00) the NIF marker is (type 1, entry 13); for a bed (02 00 0c 00) (type 2, entry 12); for the bar
//    stool (01 00 02 00) (type 1, entry 2). So the NIF is the source of positions and the FNPR agrees on type and
//    entry.
//  - Entry flags (RECALLED-VERIFY from the Creation Kit's names, consistent with the data): 1 front, 2 behind, 4 right,
//    8 left, 16 up, all relative to the SITTER: a chair allows front, right, left (13) and not behind; a bar stool only
//    behind (2); a bench front and behind (3); a bed right and left (12).
//  - Offsets are in the furniture's own frame in game units (a chair's seat marker is 34 units above its origin, a
//  bed's
//    sleep marker 37); the heading is added to the placement's Z rotation.
//
// Frame (the engine's, 0 faces +Y, positive angles turn clockwise seen from above): a heading h faces (sin h, cos h)
// and its right is (cos h, -sin h). Only the placement's Z rotation is used (furniture is level); X and Y are ignored.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace game_skyrimse
{
    namespace MarkerType
    {
        inline constexpr std::uint16_t Sit = 1;
        inline constexpr std::uint16_t Sleep = 2;
    }

    namespace EntryBit
    {
        inline constexpr std::uint16_t Front = 1;
        inline constexpr std::uint16_t Behind = 2;
        inline constexpr std::uint16_t Right = 4;
        inline constexpr std::uint16_t Left = 8;
    }

    /// How far from the seat an actor stands before it sits (a chair) or lies down (a bed). Estimates.
    inline constexpr double kSitApproachDistance = 64.0;
    inline constexpr double kSleepApproachDistance = 96.0;

    struct FurnitureMarker
    {
        float mOx = 0.f; ///< offset in the furniture's frame, game units
        float mOy = 0.f;
        float mOz = 0.f;
        float mHeading = 0.f; ///< radians about Z, added to the placement's rotation
        std::uint16_t mType = 0; ///< MarkerType (others exist: lean and more; they are listed and not used)
        std::uint16_t mEntry = 0; ///< EntryBit flags

        friend bool operator==(const FurnitureMarker&, const FurnitureMarker&) = default;
    };

    /// Limits on a model's header (a hostile file is refused, not trusted).
    struct FurnitureLimits
    {
        std::uint32_t mMaxBlocks = 200000;
        std::uint32_t mMaxBlockTypes = 4096;
        std::uint32_t mMaxStrings = 1000000;
        std::uint32_t mMaxMarkers = 64;
        std::size_t mMaxTypeNameBytes = 256;
    };

    /// Reads the markers of every BSFurnitureMarker / BSFurnitureMarkerNode block of a NIF 20.2.0.7 (Skyrim). `out` is
    /// cleared first. True when the file is a readable NIF of that version, whether or not it holds a marker; false
    /// with `error` otherwise (`out` is then empty).
    [[nodiscard]] bool readFurnitureMarkers(std::span<const std::byte> nif, std::vector<FurnitureMarker>& out,
        std::string& error, const FurnitureLimits& limits = {});

    struct FurniturePlacement
    {
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
        double mRotZ = 0.0; ///< radians, the data's convention
        double mScale = 1.0;
    };

    struct SeatPoint
    {
        double mX = 0.0;
        double mY = 0.0;
        /// Where the ACTOR'S ORIGIN goes: the furniture's own height, not the marker's. Measured: the bed's sleep clip
        /// puts the pelvis ("NPC COM") 53.2 units above the root and the chair's sit clip 39.0 above it, against marker
        /// offsets of 37.1 (bed) and 34 (chair); a pelvis rests about 16 above a mattress and about 5 to 10 above a
        /// seat, so the root belongs on the floor under the furniture (a root at the marker's height put the sleeper
        /// about 35 units above the mattress in a frame).
        double mZ = 0.0;
        float mHeading = 0.f; ///< the sitter's facing, engine convention
        double mSurfaceZ = 0.0; ///< the marker's own height (seat or mattress surface); informational
    };

    /// Where the marker is in the world and which way the person on it faces.
    [[nodiscard]] SeatPoint markerInWorld(const FurniturePlacement& placement, const FurnitureMarker& marker);

    enum class Side : std::uint8_t
    {
        Front,
        Left,
        Right,
        Behind,
    };

    struct EntryPoint
    {
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
        Side mSide = Side::Front;
    };

    /// The points an actor can walk to before it uses the marker: for each entry flag the marker allows, the spot
    /// `distance` units from the seat on that side of the sitter, at the furniture's floor height (`placement.mZ`).
    /// Order: front, left, right, behind. A marker that allows none of the four sides gets all four (the flags of the
    /// models in the data always name at least one).
    [[nodiscard]] std::vector<EntryPoint> entryPoints(
        const FurniturePlacement& placement, const SeatPoint& seat, std::uint16_t entryFlags, double distance);
}

#endif
