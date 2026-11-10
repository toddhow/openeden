#include "validate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace NeutralWorld
{
    namespace
    {
        using namespace openeden::sdk::game;

        bool finite(const Vec3d& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool within(const Vec3d& v, double limit)
        {
            return finite(v) && std::abs(v.x) <= limit && std::abs(v.y) <= limit && std::abs(v.z) <= limit;
        }

        bool validTransform(const WorldTransform& t, const Limits& limits, std::string& why)
        {
            if (!within(t.position, limits.mMaxCoordinate))
            {
                why = "position not finite or past the limit";
                return false;
            }
            const Quatd& q = t.rotation;
            if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w))
            {
                why = "rotation not finite";
                return false;
            }
            const double norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            if (std::abs(norm - 1.0) > limits.mQuaternionTolerance)
            {
                why = "rotation is not a unit quaternion";
                return false;
            }
            if (!(t.scale > 0.0) || !std::isfinite(t.scale) || t.scale > limits.mMaxScale)
            {
                why = "scale not positive, not finite or past the limit";
                return false;
            }
            return true;
        }

        bool validString(const std::string& s, const Limits& limits)
        {
            return s.size() <= limits.mMaxStringBytes && s.find('\0') == std::string::npos;
        }
    }

    bool validateObject(const WorldObject& o, const Limits& limits, std::string& problem)
    {
        if (o.id == kNoWorldId)
        {
            problem = "id 0";
            return false;
        }
        if (static_cast<unsigned>(o.kind) > static_cast<unsigned>(ObjectKind::Other))
        {
            problem = "kind out of range";
            return false;
        }
        if (!validString(o.baseEditorId, limits) || !validString(o.modelPath, limits))
        {
            problem = "a string is too long or holds a NUL";
            return false;
        }
        std::string why;
        if (!validTransform(o.transform, limits, why))
        {
            problem = why;
            return false;
        }
        if (static_cast<unsigned>(o.collision.source) > static_cast<unsigned>(CollisionSource::Box))
        {
            problem = "collision source out of range";
            return false;
        }
        if (o.collision.hasBounds)
        {
            if (!within(o.collision.boundsMin, limits.mMaxCoordinate)
                || !within(o.collision.boundsMax, limits.mMaxCoordinate)
                || o.collision.boundsMin.x > o.collision.boundsMax.x
                || o.collision.boundsMin.y > o.collision.boundsMax.y
                || o.collision.boundsMin.z > o.collision.boundsMax.z)
            {
                problem = "collision bounds not finite, past the limit or inverted";
                return false;
            }
        }
        else if (o.collision.source == CollisionSource::Box)
        {
            problem = "collision box without bounds";
            return false;
        }
        if (o.owner && o.owner->requiredRank < -1)
        {
            problem = "owner rank below -1";
            return false;
        }
        if (o.lock && o.lock->level < 0)
        {
            problem = "lock level below 0";
            return false;
        }
        if (o.teleport && !validTransform(o.teleport->destination, limits, why))
        {
            problem = "teleport destination: " + why;
            return false;
        }
        return true;
    }

    bool validateCell(const WorldCell& cell, const Limits& limits, std::string& problem)
    {
        if (cell.id == kNoWorldId)
        {
            problem = "cell id 0";
            return false;
        }
        const std::string where = "cell " + std::to_string(cell.id) + ": ";
        if (!validString(cell.editorId, limits) || !validString(cell.name, limits))
        {
            problem = where + "a string is too long or holds a NUL";
            return false;
        }
        if (std::abs(static_cast<std::int64_t>(cell.gridX)) > limits.mMaxGrid
            || std::abs(static_cast<std::int64_t>(cell.gridY)) > limits.mMaxGrid)
        {
            problem = where + "grid past the limit";
            return false;
        }
        if (cell.objects.size() > limits.mMaxObjectsPerCell)
        {
            problem = where + "more objects than the limit (" + std::to_string(cell.objects.size()) + ")";
            return false;
        }
        WorldId previous = kNoWorldId;
        for (std::size_t i = 0; i < cell.objects.size(); ++i)
        {
            const WorldObject& o = cell.objects[i];
            std::string why;
            if (!validateObject(o, limits, why))
            {
                problem = where + "object " + std::to_string(i) + ": " + why;
                return false;
            }
            if (i > 0 && o.id <= previous)
            {
                problem = where + "object " + std::to_string(i) + ": ids not strictly ascending";
                return false;
            }
            previous = o.id;
        }
        return true;
    }

    const WorldObject* findObject(const WorldCell& cell, WorldId id)
    {
        const auto it = std::lower_bound(cell.objects.begin(), cell.objects.end(), id,
            [](const WorldObject& object, WorldId wanted) { return object.id < wanted; });
        return it != cell.objects.end() && it->id == id ? &*it : nullptr;
    }
}
