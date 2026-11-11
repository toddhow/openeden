#include "neutralcell.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include <components/esm/defs.hpp>
#include <components/esm/position.hpp>

#include "neutrallight.hpp"
#include "neutralstore.hpp"

namespace MWWorld
{
    using openeden::sdk::game::ObjectKind;
    using openeden::sdk::game::Quatd;
    using openeden::sdk::game::WorldCell;
    using openeden::sdk::game::WorldId;
    using openeden::sdk::game::WorldObject;

    namespace
    {
        // The gimbal-lock threshold on |R20| = |sin(middle angle)|: 1 - 1e-9 is about 4.5e-5 rad from a pole, where
        // the float angles can no longer tell the first and third rotation apart.
        constexpr double kGimbalLimit = 1.0 - 1e-9;

        bool finite(double v)
        {
            return std::isfinite(v);
        }
    }

    std::array<float, 3> neutralRotationAngles(const Quatd& q)
    {
        if (!finite(q.x) || !finite(q.y) || !finite(q.z) || !finite(q.w))
            return { 0.f, 0.f, 0.f };
        const double length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (!(length > 1e-12))
            return { 0.f, 0.f, 0.f };
        const double x = q.x / length;
        const double y = q.y / length;
        const double z = q.z / length;
        const double w = q.w / length;

        // The rotation matrix R of the quaternion (active, column vectors). The engine combines its angles as
        // makeOsgQuat does: qz * qy * qx in OSG's order, which is the matrix product Rx(t0) * Ry(t1) * Rz(t2) with the
        // angles negated (t = -a, the axes are -X, -Y, -Z): Z is applied first, as Morrowind stores its angles.
        const double r00 = 1.0 - 2.0 * (y * y + z * z);
        const double r01 = 2.0 * (x * y - z * w);
        const double r02 = 2.0 * (x * z + y * w);
        const double r10 = 2.0 * (x * y + z * w);
        const double r11 = 1.0 - 2.0 * (x * x + z * z);
        const double r12 = 2.0 * (y * z - x * w);
        const double r22 = 1.0 - 2.0 * (x * x + y * y);

        // R = [ cy cz, -cy sz, sy; cx sz + sx sy cz, cx cz - sx sy sz, -sx cy; ..., ..., cx cy ]
        double tx = 0.0;
        double ty = 0.0;
        double tz = 0.0;
        if (std::abs(r02) < kGimbalLimit)
        {
            ty = std::asin(r02);
            tx = std::atan2(-r12, r22);
            tz = std::atan2(-r01, r00);
        }
        else
        {
            // cos(ty) is 0: only tz and tx together matter. Put it all in tz.
            ty = r02 > 0.0 ? std::numbers::pi / 2.0 : -std::numbers::pi / 2.0;
            tx = 0.0;
            tz = std::atan2(r10, r11);
        }
        return { static_cast<float>(-tx), static_cast<float>(-ty), static_cast<float>(-tz) };
    }

    bool neutralRefNum(WorldId placement, ESM::RefNum& refNum)
    {
        if (placement == openeden::sdk::game::kNoWorldId || placement > std::numeric_limits<std::uint32_t>::max())
            return false;
        refNum.mIndex = static_cast<std::uint32_t>(placement);
        refNum.mContentFile = kNeutralRefNumFile;
        return true;
    }

    ESM::RefId neutralCellRecordId(std::string_view gameId, WorldId cell)
    {
        if (gameId.empty()
            || std::any_of(gameId.begin(), gameId.end(),
                [](char c) { return c == ':' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f; }))
            return ESM::RefId();
        std::string key(gameId);
        key += ":c";
        key += std::to_string(cell);
        return ESM::RefId::stringRefId(key);
    }

    ESM::Cell makeNeutralCellRecord(std::string_view gameId, const WorldCell& cell)
    {
        ESM::Cell record;
        record.mId = neutralCellRecordId(gameId, cell.id);
        record.mName = cell.editorId;
        record.mData.mFlags = ESM::Cell::Interior;
        record.mData.mX = 0;
        record.mData.mY = 0;
        record.mAmbi.mAmbient = kNeutralCellAmbient;
        record.mAmbi.mSunlight = kNeutralCellAmbient;
        record.mAmbi.mFog = kNeutralCellAmbient;
        record.mAmbi.mFogDensity = 0.f;
        // Stage N3a-5: a cell that carries its own lighting (Skyrim SE: XCLL) uses it. The directional colour is the
        // "sunlight" of an interior (its direction is the engine's); the fog colour is kept and the fog stays off
        // (density 0), because Morrowind's fog density has no counterpart in a pair of distances.
        if (cell.lighting)
        {
            record.mAmbi.mAmbient = packNeutralColor(cell.lighting->ambient);
            record.mAmbi.mSunlight = packNeutralColor(cell.lighting->directional);
            record.mAmbi.mFog = packNeutralColor(cell.lighting->fogColor);
        }
        record.mWater = 0.f;
        return record;
    }

    bool makeNeutralCellRef(std::string_view gameId, const WorldObject& object, ESM::CellRef& ref, std::string& error)
    {
        const ESM::RefId base = neutralBaseId(gameId, object, error);
        if (base.empty())
            return false;
        ESM::RefNum refNum;
        if (!neutralRefNum(object.id, refNum))
        {
            error = "neutral reference: the placement id " + std::to_string(object.id) + " is none or too large";
            return false;
        }
        const auto& p = object.transform.position;
        if (!finite(p.x) || !finite(p.y) || !finite(p.z) || !finite(object.transform.scale)
            || !(object.transform.scale > 0.0))
        {
            error = "neutral reference: placement " + std::to_string(object.id) + " has a position or scale that is not usable";
            return false;
        }
        const std::array<float, 3> angles = neutralRotationAngles(object.transform.rotation);

        ref = ESM::CellRef();
        ref.mRefNum = refNum;
        ref.mRefID = base;
        ref.mScale = static_cast<float>(object.transform.scale);
        ref.mPos.pos[0] = static_cast<float>(p.x);
        ref.mPos.pos[1] = static_cast<float>(p.y);
        ref.mPos.pos[2] = static_cast<float>(p.z);
        ref.mPos.rot[0] = angles[0];
        ref.mPos.rot[1] = angles[1];
        ref.mPos.rot[2] = angles[2];
        ref.mCount = 1;
        return true;
    }

    namespace
    {
        struct WorldBox
        {
            double minX, minY, minZ, maxX, maxY, maxZ;
        };

        // The active rotation q v q^-1 of a vector by a unit quaternion.
        openeden::sdk::game::Vec3d rotate(const Quatd& q, const openeden::sdk::game::Vec3d& v)
        {
            const double tx = 2.0 * (q.y * v.z - q.z * v.y);
            const double ty = 2.0 * (q.z * v.x - q.x * v.z);
            const double tz = 2.0 * (q.x * v.y - q.y * v.x);
            return { v.x + q.w * tx + (q.y * tz - q.z * ty), v.y + q.w * ty + (q.z * tx - q.x * tz),
                v.z + q.w * tz + (q.x * ty - q.y * tx) };
        }

        bool collides(const WorldObject& object)
        {
            switch (object.kind)
            {
                case ObjectKind::Actor:
                case ObjectKind::Item:
                case ObjectKind::LeveledList:
                case ObjectKind::Light:
                    return false;
                default:
                    break;
            }
            return object.collision.source != openeden::sdk::game::CollisionSource::None && object.collision.hasBounds
                && !object.initiallyDisabled;
        }

        std::optional<WorldBox> worldBox(const WorldObject& object)
        {
            const auto& lo = object.collision.boundsMin;
            const auto& hi = object.collision.boundsMax;
            const auto& t = object.transform;
            const double length = std::sqrt(t.rotation.x * t.rotation.x + t.rotation.y * t.rotation.y
                + t.rotation.z * t.rotation.z + t.rotation.w * t.rotation.w);
            if (!(length > 1e-12) || !finite(length) || !finite(t.scale) || !finite(t.position.x)
                || !finite(t.position.y) || !finite(t.position.z))
                return std::nullopt;
            const Quatd q{ t.rotation.x / length, t.rotation.y / length, t.rotation.z / length, t.rotation.w / length };
            std::optional<WorldBox> box;
            for (int corner = 0; corner < 8; ++corner)
            {
                const openeden::sdk::game::Vec3d local{ (corner & 1 ? hi.x : lo.x) * t.scale,
                    (corner & 2 ? hi.y : lo.y) * t.scale, (corner & 4 ? hi.z : lo.z) * t.scale };
                const openeden::sdk::game::Vec3d r = rotate(q, local);
                const double x = r.x + t.position.x;
                const double y = r.y + t.position.y;
                const double z = r.z + t.position.z;
                if (!finite(x) || !finite(y) || !finite(z))
                    return std::nullopt;
                if (!box)
                    box = WorldBox{ x, y, z, x, y, z };
                else
                {
                    box->minX = std::min(box->minX, x);
                    box->minY = std::min(box->minY, y);
                    box->minZ = std::min(box->minZ, z);
                    box->maxX = std::max(box->maxX, x);
                    box->maxY = std::max(box->maxY, y);
                    box->maxZ = std::max(box->maxZ, z);
                }
            }
            return box;
        }

        double median(std::vector<double> values)
        {
            std::sort(values.begin(), values.end());
            return values[values.size() / 2];
        }
    }

    std::optional<openeden::sdk::game::Vec3d> neutralStartPosition(const WorldCell& cell)
    {
        std::vector<WorldBox> boxes;
        for (const WorldObject& object : cell.objects)
            if (collides(object))
                if (const std::optional<WorldBox> box = worldBox(object))
                    boxes.push_back(*box);
        if (boxes.empty())
            return std::nullopt;

        std::vector<double> xs;
        std::vector<double> ys;
        for (const WorldBox& box : boxes)
        {
            xs.push_back((box.minX + box.maxX) * 0.5);
            ys.push_back((box.minY + box.maxY) * 0.5);
        }
        const double mx = median(xs);
        const double my = median(ys);

        const WorldBox* floor = nullptr;
        for (const WorldBox& box : boxes)
            if (mx >= box.minX && mx <= box.maxX && my >= box.minY && my <= box.maxY
                && (floor == nullptr || box.maxZ < floor->maxZ))
                floor = &box;
        if (floor != nullptr)
            return openeden::sdk::game::Vec3d{ mx, my, floor->maxZ };

        const WorldBox* nearest = &boxes.front();
        double best = std::numeric_limits<double>::max();
        for (const WorldBox& box : boxes)
        {
            const double dx = (box.minX + box.maxX) * 0.5 - mx;
            const double dy = (box.minY + box.maxY) * 0.5 - my;
            if (dx * dx + dy * dy < best)
            {
                best = dx * dx + dy * dy;
                nearest = &box;
            }
        }
        return openeden::sdk::game::Vec3d{ (nearest->minX + nearest->maxX) * 0.5, (nearest->minY + nearest->maxY) * 0.5,
            nearest->maxZ };
    }

    std::string_view neutralKindName(ObjectKind kind)
    {
        switch (kind)
        {
            case ObjectKind::Unknown:
                return "Unknown";
            case ObjectKind::Static:
                return "Static";
            case ObjectKind::MovableStatic:
                return "MovableStatic";
            case ObjectKind::Furniture:
                return "Furniture";
            case ObjectKind::Door:
                return "Door";
            case ObjectKind::Activator:
                return "Activator";
            case ObjectKind::Container:
                return "Container";
            case ObjectKind::Flora:
                return "Flora";
            case ObjectKind::Tree:
                return "Tree";
            case ObjectKind::Light:
                return "Light";
            case ObjectKind::Item:
                return "Item";
            case ObjectKind::Actor:
                return "Actor";
            case ObjectKind::LeveledList:
                return "LeveledList";
            case ObjectKind::Other:
                return "Other";
        }
        return "Unknown";
    }

    void NeutralKindCounts::add(ObjectKind kind)
    {
        const std::size_t index = static_cast<std::size_t>(kind);
        ++mCount[index < kKinds ? index : static_cast<std::size_t>(ObjectKind::Other)];
    }

    std::size_t NeutralKindCounts::total() const
    {
        std::size_t sum = 0;
        for (const std::size_t n : mCount)
            sum += n;
        return sum;
    }

    std::string NeutralKindCounts::describe() const
    {
        std::string text;
        for (std::size_t i = 0; i < kKinds; ++i)
        {
            if (mCount[i] == 0)
                continue;
            if (!text.empty())
                text += ", ";
            text += neutralKindName(static_cast<ObjectKind>(i));
            text += ' ';
            text += std::to_string(mCount[i]);
        }
        return text.empty() ? std::string("none") : text;
    }
}
