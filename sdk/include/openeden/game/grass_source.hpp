#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// Grass on exterior land as plain values (task 0153, milestone M-S2 of docs/design/skyrim-se-implementation-plan.md).
// A game pack that has grass (Skyrim SE: GRAS records named by the ground textures' GNAM lists) places it for one
// exterior grid square and hands the core a list of instances: which model, where, how it stands and how big. The
// pack owns every game rule (which ground carries which grass, density, slopes, the water rule, the random seed); the
// core only draws the instances through its groundcover path (instanced, wind and stomp in the vertex stage).
//
// ABI choice: a NEW interface found by dynamic_cast on the game's registered WorldCellSource, as CellWaterSource is
// (world_water.hpp): the pinned WorldCellSource is untouched. A pack that does not implement it has no grass.
//
// Version policy: DRAFT 0.1. Additive changes (a trailing virtual with a "nothing" default, a trailing field a
// consumer only reads) raise the minor number; anything else raises the major number.
// Units: the game's own length unit, Z up, world coordinates of the requested worldspace (the same grid as
// world_terrain.hpp). Angles in radians. Determinism: the same load order and the same square give the same list, in
// the same order, on every run and platform (no clock, no unseeded random). Threading: main thread only (the pack
// reads its plugins); the returned values are plain data the core may hand to a worker.

namespace openeden::sdk::game
{
    inline constexpr Version kGrassSourceVersion{ 0, 2 };

    /// One kind of grass the square uses: a model and how to draw it.
    struct GrassModel
    {
        /// A VFS path (lower case, backslashes, with the leading `meshes\`).
        std::string modelPath;
        /// GRAS flags the core may honour: lit by the vertex colours of the model (not by the ground).
        bool vertexLighting = false;
    };

    /// One tuft of grass.
    struct GrassInstance
    {
        std::uint32_t model = 0; ///< index into GrassCell::models
        float x = 0.0f; ///< world position of the tuft's root
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f; ///< radians about Z
        float scale = 1.0f;
        // Version 0.2 (task 0153 follow-up 2): trailing fields a consumer only reads; the defaults are "no change".
        /// The unit normal's x and y components the tuft is tilted to (GRAS "fit to slope"); 0, 0 stands upright.
        float tiltX = 0.0f;
        float tiltY = 0.0f;
        /// A colour the tuft's own colours are multiplied by: the land colour under it times the GRAS colour range's
        /// random variation. 1, 1, 1 changes nothing.
        float tintR = 1.0f;
        float tintG = 1.0f;
        float tintB = 1.0f;
        /// Speed of the sway relative to the engine's own wind cycle (1 = the engine's); from the GRAS wave period.
        float waveSpeed = 1.0f;
    };

    /// The grass of one exterior grid square.
    struct GrassCell
    {
        std::vector<GrassModel> models;
        std::vector<GrassInstance> instances;
        /// An upper bound of the tufts the data asks for in the square, counted before the ground test (a tuft only
        /// stays where the ground under it names its grass) and before the pack's per-square cap thinned them (for the
        /// log and tests).
        std::uint32_t requested = 0;
    };

    class GrassSource
    {
    public:
        virtual ~GrassSource() = default;

        /// The grass of square (x, y) of `world` (parent worlds that lend their land are consulted by the source).
        /// False when the world is unknown or the square has no land; `out` is then empty. A square with land and no
        /// grass returns true with an empty list. Never throws.
        [[nodiscard]] virtual bool grassForCell(WorldId world, std::int32_t x, std::int32_t y, GrassCell& out) = 0;
    };
}
