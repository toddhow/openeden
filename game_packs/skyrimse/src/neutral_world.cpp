#include <game_skyrimse/neutral_world.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace game_skyrimse
{
    namespace sdkgame = openeden::sdk::game;

    namespace
    {
        // Skyrim turns an object clockwise for a positive stored angle, so the angle is negated (see eulerToQuat's
        // comment in the header). One constant for the three axes, so that a later check of X and Y by frame changes
        // one line.
        constexpr double kAngleSign = -1.0;

        bool finite(float v)
        {
            return std::isfinite(v);
        }

        bool usable(float v, double limit)
        {
            return std::isfinite(v) && std::abs(static_cast<double>(v)) <= limit;
        }

        bool usable(const sdkgame::WorldVec3& v, double limit)
        {
            return usable(v.x, limit) && usable(v.y, limit) && usable(v.z, limit);
        }

        sdkgame::Vec3d widen(const sdkgame::WorldVec3& v)
        {
            return { static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z) };
        }

        // The Hamilton product a * b.
        sdkgame::Quatd multiply(const sdkgame::Quatd& a, const sdkgame::Quatd& b)
        {
            return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
        }

        sdkgame::Quatd axisAngle(int axis, double angle)
        {
            const double half = 0.5 * kAngleSign * angle;
            sdkgame::Quatd q{ 0.0, 0.0, 0.0, std::cos(half) };
            const double s = std::sin(half);
            if (axis == 0)
                q.x = s;
            else if (axis == 1)
                q.y = s;
            else
                q.z = s;
            return q;
        }
    }

    sdkgame::Quatd eulerToQuat(const sdkgame::WorldVec3& storedAngles)
    {
        if (!finite(storedAngles.x) || !finite(storedAngles.y) || !finite(storedAngles.z))
            return {};
        // R = Rz * Ry * Rx, so the quaternion is qz * qy * qx.
        sdkgame::Quatd q = multiply(
            axisAngle(2, storedAngles.z), multiply(axisAngle(1, storedAngles.y), axisAngle(0, storedAngles.x)));
        const double length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (length > 0.0)
        {
            q.x /= length;
            q.y /= length;
            q.z /= length;
            q.w /= length;
        }
        return q;
    }

    bool toNeutral(const sdkgame::PlacedObject& placed, sdkgame::WorldObject& out, std::string& problem,
        const NeutralLimits& limits)
    {
        if (!usable(placed.position, limits.mMaxCoordinate))
        {
            problem = "position is not finite or is past the limit";
            return false;
        }
        if (!finite(placed.rotation.x) || !finite(placed.rotation.y) || !finite(placed.rotation.z))
        {
            problem = "rotation is not finite";
            return false;
        }
        if (!std::isfinite(placed.scale) || placed.scale <= 0.f || static_cast<double>(placed.scale) > limits.mMaxScale)
        {
            problem = "scale is not finite, not positive or past the limit";
            return false;
        }
        if (placed.hasBounds
            && (!usable(placed.boundsMin, limits.mMaxCoordinate) || !usable(placed.boundsMax, limits.mMaxCoordinate)))
        {
            problem = "bounds are not finite or are past the limit";
            return false;
        }

        sdkgame::WorldObject o;
        o.id = placed.id;
        o.base = placed.base;
        o.kind = placed.baseKind;
        o.baseEditorId = placed.baseEditorId;
        o.modelPath = placed.modelPath;
        o.transform.position = widen(placed.position);
        o.transform.rotation = eulerToQuat(placed.rotation);
        o.transform.scale = static_cast<double>(placed.scale);
        o.persistent = placed.persistent;
        o.initiallyDisabled = placed.initiallyDisabled;
        // The store decodes no owner, lock or teleport yet (XOWN, XLOC and XTEL are not read): the components stay
        // empty.
        o.payload.handle = placed.base;

        if (!placed.modelPath.empty())
            o.collision.source = sdkgame::CollisionSource::FromModel;
        else if (placed.hasBounds)
            o.collision.source = sdkgame::CollisionSource::Box;
        if (placed.hasBounds)
        {
            o.collision.hasBounds = true;
            o.collision.boundsMin = widen(placed.boundsMin);
            o.collision.boundsMax = widen(placed.boundsMax);
        }

        out = std::move(o);
        problem.clear();
        return true;
    }

    bool loadNeutralCell(sdkgame::WorldView& view, sdkgame::WorldId cell, sdkgame::WorldCell& out, std::string& error,
        const NeutralLimits& limits)
    {
        out = {};
        if (cell == sdkgame::kNoWorldId)
        {
            error = "no cell id given";
            return false;
        }
        sdkgame::WorldCellContent content;
        if (!view.loadCell(cell, content, error))
        {
            if (error.empty())
                error = "the view could not load the cell";
            return false;
        }

        sdkgame::WorldCell result;
        result.id = content.cell.id;
        result.editorId = content.cell.editorId;
        result.name = content.cell.name;
        result.interior = content.cell.interior;
        result.hasGrid = content.cell.hasGrid;
        result.gridX = content.cell.gridX;
        result.gridY = content.cell.gridY;
        result.world = content.cell.world;
        result.unresolvedBases = content.unresolvedBases;
        result.objects.reserve(content.objects.size());
        for (const sdkgame::PlacedObject& placed : content.objects)
        {
            sdkgame::WorldObject object;
            std::string problem;
            if (toNeutral(placed, object, problem, limits))
                result.objects.push_back(std::move(object));
            else
                ++result.droppedObjects;
        }
        out = std::move(result);
        error.clear();
        return true;
    }

    std::size_t SkyrimCellSource::listCells(
        std::size_t first, std::size_t maxCount, std::vector<openeden::sdk::game::WorldId>& out) const
    {
        std::vector<openeden::sdk::game::WorldCellInfo> infos;
        const std::size_t appended = mView.listCells(first, maxCount, infos);
        for (const auto& info : infos)
            out.push_back(info.id);
        return appended;
    }

    bool SkyrimCellSource::buildCell(
        openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldCell& out, std::string& error)
    {
        return loadNeutralCell(mView, cell, out, error, mLimits);
    }
}
