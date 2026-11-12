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

    PlacementComponents makeComponents(
        const PlacedRef& ref, const RefCellIndex& refCells, ComponentStats& stats, const NeutralLimits& limits)
    {
        PlacementComponents c;
        const auto usableId = [](FormId id) { return id != kNullFormId && id != kUnresolvedFormId; };

        if (ref.mOwner)
        {
            if (usableId(ref.mOwner->mOwner))
            {
                sdkgame::OwnerComponent o;
                o.owner = ref.mOwner->mOwner;
                o.requiredRank = ref.mOwner->mRank.value_or(-1);
                c.owner = o;
            }
            else
                ++stats.mOwnersDropped;
        }

        if (ref.mLock)
        {
            // The 20 byte XLOC has no separate "locked" state (its flag byte is 0 or 4, "leveled lock"), so a lock
            // that is present is locked: RECALLED-VERIFY, the level is kept raw so the core can decide otherwise.
            sdkgame::LockComponent l;
            l.locked = true;
            l.level = ref.mLock->mLevel;
            if (usableId(ref.mLock->mKey))
                l.key = ref.mLock->mKey;
            else if (ref.mLock->mKey == kUnresolvedFormId)
                ++stats.mKeysUnresolved;
            c.lock = l;
        }

        if (ref.mTeleport)
        {
            const RefTeleport& t = ref.mTeleport.value();
            const bool finiteNumbers = usable(t.mPosition.x, limits.mMaxCoordinate)
                && usable(t.mPosition.y, limits.mMaxCoordinate) && usable(t.mPosition.z, limits.mMaxCoordinate)
                && finite(t.mRotation.x) && finite(t.mRotation.y) && finite(t.mRotation.z);
            if (!usableId(t.mDoor) || !finiteNumbers)
                ++stats.mTeleportsDropped;
            else
            {
                sdkgame::TeleportComponent tp;
                tp.destinationDoor = t.mDoor;
                const auto cell = refCells.find(t.mDoor);
                if (cell != refCells.end())
                    tp.destinationCell = cell->second;
                else
                    ++stats.mTeleportCellsUnknown;
                tp.destination.position = { static_cast<double>(t.mPosition.x), static_cast<double>(t.mPosition.y),
                    static_cast<double>(t.mPosition.z) };
                tp.destination.rotation = eulerToQuat({ t.mRotation.x, t.mRotation.y, t.mRotation.z });
                tp.destination.scale = 1.0;
                tp.flags = t.mFlags;
                c.teleport = tp;
            }
        }
        return c;
    }

    std::optional<sdkgame::LightComponent> makeLightComponent(
        const LightData& light, ComponentStats& stats, const NeutralLimits& limits)
    {
        if (static_cast<double>(light.mRadius) > limits.mMaxLightRadius)
        {
            ++stats.mLightsDropped;
            return std::nullopt;
        }
        sdkgame::LightComponent c;
        c.radius = static_cast<double>(light.mRadius);
        c.red = light.mColor[0];
        c.green = light.mColor[1];
        c.blue = light.mColor[2];
        if (light.mHasFade)
        {
            if (std::isfinite(light.mFade) && light.mFade > 0.0f && static_cast<double>(light.mFade) <= limits.mMaxLightFade)
                c.fade = static_cast<double>(light.mFade);
            else
                ++stats.mLightFadesReset;
        }
        if ((light.mFlags & LightFlags::Flicker) != 0)
            c.animation = sdkgame::LightAnimation::Flicker;
        else if ((light.mFlags & LightFlags::FlickerSlow) != 0)
            c.animation = sdkgame::LightAnimation::FlickerSlow;
        else if ((light.mFlags & LightFlags::Pulse) != 0)
            c.animation = sdkgame::LightAnimation::Pulse;
        else if ((light.mFlags & LightFlags::PulseSlow) != 0)
            c.animation = sdkgame::LightAnimation::PulseSlow;
        c.negative = (light.mFlags & LightFlags::Negative) != 0;
        c.offByDefault = (light.mFlags & LightFlags::OffByDefault) != 0;
        c.flags = light.mFlags;
        return c;
    }

    std::optional<sdkgame::CellLighting> makeCellLighting(const CellLightingData& lighting)
    {
        if (!std::isfinite(lighting.mFogNear) || !std::isfinite(lighting.mFogFar))
            return std::nullopt;
        sdkgame::CellLighting c;
        c.ambient = lighting.mAmbient;
        c.directional = lighting.mDirectional;
        c.fogColor = lighting.mFogColor;
        c.fogNear = static_cast<double>(lighting.mFogNear);
        c.fogFar = static_cast<double>(lighting.mFogFar);
        return c;
    }

    bool toNeutral(const sdkgame::PlacedObject& placed, sdkgame::WorldObject& out, std::string& problem,
        const NeutralLimits& limits, const PlacementComponents* components)
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
        if (components != nullptr)
        {
            o.owner = components->owner;
            o.lock = components->lock;
            o.teleport = components->teleport;
            o.light = components->light;
        }
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
        const NeutralLimits& limits, const ComponentSource* components)
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
        // A view that is its own component source (SkyrimWorldView) is asked without the caller naming it.
        if (components == nullptr)
            components = dynamic_cast<const ComponentSource*>(&view);
        if (components != nullptr)
            result.lighting = components->findCellLighting(content.cell.id);
        result.objects.reserve(content.objects.size());
        for (const sdkgame::PlacedObject& placed : content.objects)
        {
            sdkgame::WorldObject object;
            std::string problem;
            const PlacementComponents* extra
                = components != nullptr ? components->find(content.cell.id, placed.id) : nullptr;
            if (toNeutral(placed, object, problem, limits, extra))
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

    openeden::sdk::game::WorldId SkyrimCellSource::findCellByEditorId(std::string_view editorId) const
    {
        const auto info = mView.findCellByEditorId(editorId);
        return info ? info->id : openeden::sdk::game::kNoWorldId;
    }

    openeden::sdk::game::WorldId SkyrimCellSource::findCellByGrid(
        openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y) const
    {
        const auto info = mView.findCellByGrid(world, x, y);
        return info ? info->id : openeden::sdk::game::kNoWorldId;
    }

    openeden::sdk::game::WorldId SkyrimCellSource::findWorldByEditorId(std::string_view editorId) const
    {
        const auto info = mView.findWorldByEditorId(editorId);
        return info ? info->id : openeden::sdk::game::kNoWorldId;
    }

    const SkyrimTerrainIndex* SkyrimCellSource::terrain() const
    {
        const auto* provider = dynamic_cast<const TerrainProvider*>(&mView);
        return provider ? provider->terrainIndex() : nullptr;
    }

    bool SkyrimCellSource::worldInfo(openeden::sdk::game::WorldId world, openeden::sdk::game::WorldSpaceInfo& out) const
    {
        const SkyrimTerrainIndex* index = terrain();
        return index && index->worldInfo(world, out);
    }

    openeden::sdk::game::TerrainResult SkyrimCellSource::buildTerrain(openeden::sdk::game::WorldId world,
        std::int32_t x, std::int32_t y, openeden::sdk::game::TerrainCell& out, std::string& error)
    {
        const SkyrimTerrainIndex* index = terrain();
        if (!index)
            return openeden::sdk::game::TerrainResult::NotSupported;
        return index->buildTerrain(world, x, y, out, error);
    }

    bool SkyrimCellSource::cellWater(openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y,
        openeden::sdk::game::CellWater& out) const
    {
        out = openeden::sdk::game::CellWater();
        const SkyrimTerrainIndex* index = terrain();
        return index && index->cellWater(world, x, y, out);
    }

    std::size_t SkyrimCellSource::listGroundTextures(std::vector<openeden::sdk::game::GroundTexture>& out) const
    {
        const SkyrimTerrainIndex* index = terrain();
        return index ? index->listGroundTextures(out) : 0;
    }

    bool SkyrimCellSource::buildCell(
        openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldCell& out, std::string& error)
    {
        return loadNeutralCell(mView, cell, out, error, mLimits);
    }

    bool SkyrimCellSource::describeActor(openeden::sdk::game::WorldId base, std::uint32_t variation,
        openeden::sdk::game::ActorAppearance& out, std::string& error)
    {
        auto* source = dynamic_cast<openeden::sdk::game::ActorAppearanceSource*>(&mView);
        if (source == nullptr)
        {
            out = {};
            error = "the world view cannot describe actors";
            return false;
        }
        return source->describeActor(base, variation, out, error);
    }

    bool SkyrimCellSource::describeWeathers(
        std::vector<openeden::sdk::game::WeatherDescription>& out, std::string& error)
    {
        auto* source = dynamic_cast<openeden::sdk::game::WeatherSource*>(&mView);
        if (source == nullptr)
        {
            out.clear();
            error = "the world view cannot describe weather";
            return false;
        }
        return source->describeWeathers(out, error);
    }

    bool SkyrimCellSource::describeTimeOfDay(openeden::sdk::game::TimeOfDayDescription& out)
    {
        auto* source = dynamic_cast<openeden::sdk::game::WeatherSource*>(&mView);
        return source != nullptr && source->describeTimeOfDay(out);
    }

    bool SkyrimCellSource::weatherListForCell(
        openeden::sdk::game::WorldId cell, openeden::sdk::game::CellWeatherList& out)
    {
        auto* source = dynamic_cast<openeden::sdk::game::WeatherSource*>(&mView);
        if (source == nullptr)
        {
            out = {};
            return false;
        }
        return source->weatherListForCell(cell, out);
    }
}
