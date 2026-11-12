#ifndef OPENEDEN_GAME_SKYRIMSE_NEUTRAL_WORLD_H
#define OPENEDEN_GAME_SKYRIMSE_NEUTRAL_WORLD_H

// The Skyrim SE adapter into the neutral world model (decision 0016, task 0099 stage N0). It reads a cell through the
// SDK's WorldView contract (SkyrimWorldView implements it) and hands back a WorldCell: the same objects, with a
// canonical double precision transform, a collision hint and the pack's payload. This file is also THE named place
// where Skyrim's stored Euler angles become a quaternion (the guide's rule: convert at one named boundary, never ad
// hoc).
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only (it calls WorldView::loadCell). Deterministic: the same
// view and cell give equal results, on one platform; floating point results are not claimed to be bit-identical across
// compilers. Nothing throws: a failure is `false` plus a located message.

#include <game_skyrimse/records.hpp>
#include <game_skyrimse/terrain_source.hpp>

#include <openeden/game/actor_appearance.hpp>
#include <openeden/game/world_model.hpp>
#include <openeden/game/world_source.hpp>
#include <openeden/game/world_view.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace game_skyrimse
{
    /// Values past these are refused as hostile (a Skyrim world is about 1e5 units across).
    struct NeutralLimits
    {
        double mMaxCoordinate = 1.0e7;
        double mMaxScale = 1.0e3;
        /// A light's radius past this is refused as hostile (Skyrim's largest in the masters is 2,000 units).
        double mMaxLightRadius = 1.0e6;
        /// A light's fade value past this is refused (the masters use 0.1 to 10).
        double mMaxLightFade = 1.0e3;
    };

    /// The neutral components of one placement (task 0099 stage N4, the record-decoding half): who owns it, how it is
    /// locked and where it leads. Built from the decoded references by makeComponents; the SDK's WorldView 1.0 has
    /// nowhere to carry them, so they travel beside the placed objects through a pack-internal ComponentSource.
    struct PlacementComponents
    {
        std::optional<openeden::sdk::game::OwnerComponent> owner;
        std::optional<openeden::sdk::game::LockComponent> lock;
        std::optional<openeden::sdk::game::TeleportComponent> teleport;
        /// The light of a LIGH base (task 0113, stage N3a-5), filled by the view from the placement's base.
        std::optional<openeden::sdk::game::LightComponent> light;

        [[nodiscard]] bool empty() const { return !owner && !lock && !teleport && !light; }
        friend bool operator==(const PlacementComponents&, const PlacementComponents&) = default;
    };

    /// What makeComponents could not use, counted (never listed).
    struct ComponentStats
    {
        std::uint64_t mOwnersDropped = 0; ///< XOWN whose owner is not in the load order: no owner component
        std::uint64_t mKeysUnresolved = 0; ///< XLOC whose key is not in the load order: the lock has no key
        std::uint64_t mTeleportsDropped = 0; ///< XTEL with no usable door, or a position or rotation that is hostile
        std::uint64_t mTeleportCellsUnknown = 0; ///< XTEL kept, but its door is in no cell the store knows
        std::uint64_t mLightsDropped = 0; ///< LIGH whose radius or fade is not usable: no light component
        std::uint64_t mLightFadesReset = 0; ///< LIGH whose FNAM is not finite, not positive or past the limit: fade 1
    };

    /// Maps a door reference's id to the cell it stands in (the store's index of teleport doors).
    using RefCellIndex = std::unordered_map<FormId, FormId>;

    /// Owner, lock and teleport of one decoded reference as neutral components. An owner or teleport that cannot be
    /// used is left out and counted; the object itself is never dropped for it. `refCells` names the destination
    /// door's cell (none: the cell stays kNoWorldId and is counted). Hostile numbers: a non-finite or past-the-limit
    /// arrival position or rotation drops the teleport.
    [[nodiscard]] PlacementComponents makeComponents(
        const PlacedRef& ref, const RefCellIndex& refCells, ComponentStats& stats, const NeutralLimits& limits);

    /// The light of a LIGH base as a neutral component: radius, colour, the fade value (FNAM; 1 when absent or
    /// unusable, counted), the animation (Flicker, else FlickerSlow, else Pulse, else PulseSlow: the order the engine's
    /// own light controller tests them in) and the Negative and Off By Default bits; the raw flag word is kept. nullopt
    /// (counted) when the radius is past the limit.
    [[nodiscard]] std::optional<openeden::sdk::game::LightComponent> makeLightComponent(
        const LightData& light, ComponentStats& stats, const NeutralLimits& limits);

    /// The interior lighting of an XCLL: ambient, directional and fog colour and the fog distances. nullopt when a
    /// distance is not finite.
    [[nodiscard]] std::optional<openeden::sdk::game::CellLighting> makeCellLighting(const CellLightingData& lighting);

    /// Gives the components of a placement of a cell the view has just loaded. The pointer is valid until the next
    /// loadCell on the same view.
    /// Gives the cell source the terrain index of a view (task 0120, stage E1). Pack-internal, like ComponentSource.
    class TerrainProvider
    {
    public:
        virtual ~TerrainProvider() = default;
        /// Null when the view cannot say (for example its index timed out).
        [[nodiscard]] virtual const SkyrimTerrainIndex* terrainIndex() const = 0;
    };

    class ComponentSource
    {
    public:
        virtual ~ComponentSource() = default;
        [[nodiscard]] virtual const PlacementComponents* find(
            openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldId placement) const = 0;
        /// The interior lighting of the cell the view has just loaded (task 0113), none when the cell has none or is
        /// not the last one loaded.
        [[nodiscard]] virtual std::optional<openeden::sdk::game::CellLighting> findCellLighting(
            openeden::sdk::game::WorldId /*cell*/) const
        {
            return std::nullopt;
        }
    };

    /// Skyrim's stored angles (radians about X, Y and Z) as a unit quaternion: R = Rz * Ry * Rx with every angle
    /// negated (clockwise for a positive angle), the convention Eden Studio's cell viewer uses (apps/opencs/model/mesh/
    /// cellscene.cpp, kAngleSign). Z was settled by frame and X and Y by terrain normals in task 0086 (stage log,
    /// rotation convention). A non-finite angle gives the identity (toNeutral refuses such an object before it gets
    /// here).
    [[nodiscard]] openeden::sdk::game::Quatd eulerToQuat(const openeden::sdk::game::WorldVec3& storedAngles);

    /// One placed object as a neutral object. False with `problem` when a value is not usable (not finite, a coordinate
    /// or bound past the limits, a scale that is not positive or too large); `out` is then untouched.
    /// `components`, if given, supplies the owner, lock and teleport.
    [[nodiscard]] bool toNeutral(const openeden::sdk::game::PlacedObject& placed, openeden::sdk::game::WorldObject& out,
        std::string& problem, const NeutralLimits& limits = {}, const PlacementComponents* components = nullptr);

    /// Loads a cell through `view` and fills `out`; objects that toNeutral refuses are counted in `droppedObjects`.
    /// False with `error` when the view cannot load the cell (`out` is then empty). `components`, if given, is asked
    /// for each placement after the view has loaded the cell (a SkyrimWorldView is its own source); without it the
    /// owner, lock and teleport stay empty.
    [[nodiscard]] bool loadNeutralCell(openeden::sdk::game::WorldView& view, openeden::sdk::game::WorldId cell,
        openeden::sdk::game::WorldCell& out, std::string& error, const NeutralLimits& limits = {},
        const ComponentSource* components = nullptr);

    /// The Skyrim SE side of the core's neutral world store (task 0100 stage N1): a WorldCellSource over any WorldView.
    /// Cell ids are the view's own ids (the load-order form ids), which are already stable for the session. It keeps a
    /// reference to the view, which must outlive it. Main thread only, like the view.
    class SkyrimCellSource final : public openeden::sdk::game::WorldCellSource,
        public openeden::sdk::game::ActorAppearanceSource
    {
    public:
        explicit SkyrimCellSource(openeden::sdk::game::WorldView& view, NeutralLimits limits = {})
            : mView(view)
            , mLimits(limits)
        {
        }

        std::string_view gameId() const override { return "skyrimse"; }
        std::size_t cellCount() const override { return mView.cellCount(); }
        std::size_t listCells(
            std::size_t first, std::size_t maxCount, std::vector<openeden::sdk::game::WorldId>& out) const override;
        bool buildCell(
            openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldCell& out, std::string& error) override;

        // The lookups of world_source.hpp 1.0, through the view (all case-insensitive, as the view's are).
        openeden::sdk::game::WorldId findCellByEditorId(std::string_view editorId) const override;
        openeden::sdk::game::WorldId findCellByGrid(
            openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y) const override;
        openeden::sdk::game::WorldId findWorldByEditorId(std::string_view editorId) const override;
        /// Both games use a unit of about 1.43 cm; the value is compat_skyrimse's
        /// SkyrimGameContract::getUnitsPerMeter() (Morrowind's Constants::UnitsPerMeter), NOT measured for Skyrim:
        /// RECALLED-VERIFY.
        double unitsPerMetre() const override { return 69.99125109; }

        // Terrain (world_source.hpp 1.1), through the view's SkyrimTerrainIndex when the view has one.
        bool worldInfo(openeden::sdk::game::WorldId world, openeden::sdk::game::WorldSpaceInfo& out) const override;
        openeden::sdk::game::TerrainResult buildTerrain(openeden::sdk::game::WorldId world, std::int32_t x,
            std::int32_t y, openeden::sdk::game::TerrainCell& out, std::string& error) override;
        std::size_t listGroundTextures(std::vector<openeden::sdk::game::GroundTexture>& out) const override;
        /// Forwards to the view when it can describe actors (SkyrimWorldView does), else refuses.
        bool describeActor(openeden::sdk::game::WorldId base, std::uint32_t variation,
            openeden::sdk::game::ActorAppearance& out, std::string& error) override;

    private:
        [[nodiscard]] const SkyrimTerrainIndex* terrain() const;
        openeden::sdk::game::WorldView& mView;
        NeutralLimits mLimits;
    };
}

#endif
