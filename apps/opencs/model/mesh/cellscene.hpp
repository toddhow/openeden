#ifndef CSM_MESH_CELLSCENE_H
#define CSM_MESH_CELLSCENE_H

#include <array>
#include <cstddef>
#include <functional>
#include <string>

#include <openeden/game/world_view.hpp>

#include "scenemodel.hpp"

namespace CSMMesh
{
    /// Eden Studio's read-only cell viewer (task 0086, world model option 1): turns the objects of one cell, as the
    /// game pack's WorldView contract gives them (plain values), into a SceneModel the mesh viewer already draws. A
    /// placed object is drawn with its real mesh when the provider has one and as a box of its bounds when not. No Qt,
    /// no GPU, no game type: the model files are read by the caller's provider.

    /// Gives the converted model of a mesh by the path the data stores (relative to the mesh root), or null when it is
    /// not available (missing from the archives, unreadable, not a supported file). The pointer must stay valid until
    /// buildCellScene returns; the same path may be asked for many times.
    using MeshProvider = std::function<const SceneModel*(const std::string& modelPath)>;

    struct CellSceneLimits
    {
        /// Objects looked at; the rest are counted as skipped by the budget.
        std::size_t maxObjects = 20000;
        /// What the combined scene may hold. Past either, an object is drawn as a box (8 vertices, 12 triangles) while
        /// the box budget lasts, then dropped.
        std::size_t maxTriangles = 1500000;
        std::size_t maxVertices = 1500000;
        /// A mesh with more vertices than this is drawn as a box (a hostile or unexpectedly large model).
        std::size_t maxVerticesPerMesh = 400000;
    };

    struct CellSceneStats
    {
        std::size_t objects = 0; ///< objects in the cell
        std::size_t drawnAsMesh = 0;
        std::size_t drawnAsBox = 0;
        std::size_t skippedDisabled = 0; ///< initially disabled
        std::size_t skippedNoGeometry = 0; ///< actors, lists, markers, anything with neither a model nor bounds
        std::size_t skippedInvalid = 0; ///< a position, rotation, scale or bound that is not usable
        std::size_t skippedByBudget = 0;
        std::size_t meshMisses = 0; ///< objects whose model the provider could not give (drawn as a box if possible)
        std::size_t triangles = 0;
        std::size_t vertices = 0;
    };

    struct CellScene
    {
        SceneModel model;
        CellSceneStats stats;
    };

    /// The placement matrix of an object (column-major 4x4, translation in elements 12..14): scale, then the rotations,
    /// then the translation. The rotation is the game's Euler convention: Rz * Ry * Rx with each angle in radians, the
    /// sign convention in CellRotation (task 0086 stage log has how it was settled).
    std::array<double, 16> placementMatrix(
        const openeden::sdk::game::WorldVec3& position, const openeden::sdk::game::WorldVec3& rotation, double scale);

    /// A flat colour for a box or tint for a mesh of this base kind.
    std::array<float, 4> kindColour(openeden::sdk::game::BaseKind kind);

    CellScene buildCellScene(const openeden::sdk::game::WorldCellContent& cell, const MeshProvider& provider,
        const CellSceneLimits& limits = CellSceneLimits());
}

#endif
