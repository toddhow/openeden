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

#include <openeden/game/world_model.hpp>
#include <openeden/game/world_view.hpp>

#include <string>

namespace game_skyrimse
{
    /// Values past these are refused as hostile (a Skyrim world is about 1e5 units across).
    struct NeutralLimits
    {
        double mMaxCoordinate = 1.0e7;
        double mMaxScale = 1.0e3;
    };

    /// Skyrim's stored angles (radians about X, Y and Z) as a unit quaternion: R = Rz * Ry * Rx with every angle
    /// negated (clockwise for a positive angle), the convention Eden Studio's cell viewer uses (apps/opencs/model/mesh/
    /// cellscene.cpp, kAngleSign). Z was settled by frame in task 0086; X and Y are RECALLED-VERIFY. A non-finite angle
    /// gives the identity (toNeutral refuses such an object before it gets here).
    [[nodiscard]] openeden::sdk::game::Quatd eulerToQuat(const openeden::sdk::game::WorldVec3& storedAngles);

    /// One placed object as a neutral object. False with `problem` when a value is not usable (not finite, a coordinate
    /// or bound past the limits, a scale that is not positive or too large); `out` is then untouched.
    [[nodiscard]] bool toNeutral(const openeden::sdk::game::PlacedObject& placed, openeden::sdk::game::WorldObject& out,
        std::string& problem, const NeutralLimits& limits = {});

    /// Loads a cell through `view` and fills `out`; objects that toNeutral refuses are counted in `droppedObjects`.
    /// False with `error` when the view cannot load the cell (`out` is then empty).
    [[nodiscard]] bool loadNeutralCell(openeden::sdk::game::WorldView& view, openeden::sdk::game::WorldId cell,
        openeden::sdk::game::WorldCell& out, std::string& error, const NeutralLimits& limits = {});
}

#endif
