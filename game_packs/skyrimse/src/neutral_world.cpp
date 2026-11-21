#include <game_skyrimse/music.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
        // R = Rx * Ry * Rz (a vector is turned about Z first, then Y, then X, all about the world axes), so the
        // quaternion is qx * qy * qz. Task 0151: the old order (Rz * Ry * Rx) tipped every object that also had a yaw
        // the wrong way (the floating mountain trims).
        sdkgame::Quatd q = multiply(
            axisAngle(0, storedAngles.x), multiply(axisAngle(1, storedAngles.y), axisAngle(2, storedAngles.z)));
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

        c.itemCount = ref.mItemCount;

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

    LightData applyLightOverrides(const LightData& base, const LightOverrides& overrides)
    {
        LightData out = base;
        if (overrides.mRadiusDelta && std::isfinite(*overrides.mRadiusDelta)
            && std::abs(static_cast<double>(*overrides.mRadiusDelta)) <= kMaxLightRadiusDelta)
        {
            // Never below 0: no value in the masters is below minus the base radius, a hostile one is cut.
            const double radius = std::max(0.0, static_cast<double>(base.mRadius) + *overrides.mRadiusDelta);
            out.mRadius = static_cast<std::uint32_t>(std::llround(radius));
        }
        if (overrides.mFadeDelta && std::isfinite(*overrides.mFadeDelta)
            && std::abs(static_cast<double>(*overrides.mFadeDelta)) <= kMaxLightFadeDelta)
        {
            const double fade = static_cast<double>(base.mFade) + *overrides.mFadeDelta;
            if (fade > 0.0)
            {
                out.mFade = static_cast<float>(fade);
                out.mHasFade = true;
            }
        }
        return out;
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
        // Task 0355 stage b: the engine's point light is given the radius that makes its falloff end where Skyrim's
        // does (kLightFalloffRadiusScale). A scale that is not finite or not above 0 is taken as 1. The hostile radius
        // check above looks at the record's own radius.
        const double scale = std::isfinite(limits.mLightRadiusScale) && limits.mLightRadiusScale > 0.0
            ? limits.mLightRadiusScale
            : 1.0;
        c.radius = static_cast<double>(light.mRadius) * scale;
        c.red = light.mColor[0];
        c.green = light.mColor[1];
        c.blue = light.mColor[2];
        if (light.mHasFade)
        {
            if (std::isfinite(light.mFade) && light.mFade > 0.0f
                && static_cast<double>(light.mFade) <= limits.mMaxLightFade)
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
        // Task 0355 stage h: the record's own flicker numbers (an animated light only); not finite or negative is 0.
        if (c.animation != sdkgame::LightAnimation::None)
        {
            const auto usable = [](float v) { return std::isfinite(v) && v > 0.f ? static_cast<double>(v) : 0.0; };
            c.flickerPeriod = usable(light.mFlickerPeriod);
            c.flickerIntensity = usable(light.mFlickerIntensity);
            c.flickerMovement = usable(light.mFlickerMovement);
        }
        c.negative = (light.mFlags & LightFlags::Negative) != 0;
        c.offByDefault = (light.mFlags & LightFlags::OffByDefault) != 0;
        c.flags = light.mFlags;
        // Task 0355 stage g: the spot cone and the shadow rule (the bit meanings are in LightFlags).
        const bool spot = (light.mFlags & (LightFlags::SpotLight | LightFlags::SpotShadow)) != 0;
        if (spot && std::isfinite(light.mFov) && light.mFov >= 1.0f && light.mFov <= 170.0f)
            c.spotFov = static_cast<double>(light.mFov);
        const bool shadowed = (light.mFlags
                                  & (LightFlags::SpotShadow | LightFlags::HemisphereShadow
                                      | LightFlags::OmnidirectionalShadow))
            != 0;
        c.shadow = shadowed ? sdkgame::LightShadow::Casts : sdkgame::LightShadow::Never;
        return c;
    }

    CellLightingData applyLightingTemplate(const CellLightingData& own, const CellLightingData* templateLighting)
    {
        CellLightingData out = own;
        if (templateLighting == nullptr)
            return out;
        if ((own.mInherit & LightingInherit::Ambient) != 0)
        {
            out.mAmbient = templateLighting->mAmbient;
            // Task 0355 stage e: the ambient cube comes with the ambient (a template with no DALC leaves the cell's own).
            if (templateLighting->mHasCube)
            {
                out.mCube = templateLighting->mCube;
                out.mHasCube = true;
            }
        }
        if ((own.mInherit & LightingInherit::Directional) != 0)
            out.mDirectional = templateLighting->mDirectional;
        if ((own.mInherit & LightingInherit::FogColor) != 0)
        {
            out.mFogColor = templateLighting->mFogColor;
            // Task 0362 item 1: the far colour goes with the fog colour flag (xEdit has one "Fog Color" flag; RECALLED-VERIFY).
            out.mFogFarColor = templateLighting->mFogFarColor;
            out.mHasFogFar = templateLighting->mHasFogFar;
        }
        if ((own.mInherit & LightingInherit::FogNear) != 0)
            out.mFogNear = templateLighting->mFogNear;
        if ((own.mInherit & LightingInherit::FogFar) != 0)
            out.mFogFar = templateLighting->mFogFar;
        if ((own.mInherit & LightingInherit::FogPower) != 0)
            out.mFogPower = templateLighting->mFogPower;
        if ((own.mInherit & LightingInherit::FogMax) != 0)
            out.mFogMax = templateLighting->mFogMax;
        // Task 0355 stages f and j: the rotation (both angles), the directional fade, the clip distance and the two
        // light fade distances.
        if ((own.mInherit & LightingInherit::Rotation) != 0)
        {
            out.mRotationXY = templateLighting->mRotationXY;
            out.mRotationZ = templateLighting->mRotationZ;
        }
        if ((own.mInherit & LightingInherit::DirectionalFade) != 0)
            out.mDirectionalFade = templateLighting->mDirectionalFade;
        if ((own.mInherit & LightingInherit::ClipDistance) != 0)
            out.mClipDistance = templateLighting->mClipDistance;
        if ((own.mInherit & LightingInherit::LightFade) != 0)
        {
            out.mLightFadeBegin = templateLighting->mLightFadeBegin;
            out.mLightFadeEnd = templateLighting->mLightFadeEnd;
        }
        return out;
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

    bool SkyrimCellSource::cellWater(
        openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y, openeden::sdk::game::CellWater& out) const
    {
        out = openeden::sdk::game::CellWater();
        const SkyrimTerrainIndex* index = terrain();
        return index && index->cellWater(world, x, y, out);
    }

    bool SkyrimCellSource::interiorWater(openeden::sdk::game::WorldId cell, openeden::sdk::game::CellWater& out) const
    {
        out = openeden::sdk::game::CellWater();
        const auto* components = dynamic_cast<const ComponentSource*>(&mView);
        if (components == nullptr)
            return false;
        const std::optional<float> height = components->findInteriorWater(cell);
        if (!height)
            return false;
        out.present = true;
        out.height = *height;
        return true;
    }

    bool SkyrimCellSource::grassForCell(
        openeden::sdk::game::WorldId world, std::int32_t x, std::int32_t y, openeden::sdk::game::GrassCell& out)
    {
        out = openeden::sdk::game::GrassCell();
        const SkyrimTerrainIndex* index = terrain();
        if (!index)
            return false;
        std::string error;
        GrassPlacementOptions options = shippedGrassOptions();
        // OPENEDEN_NEUTRAL_GRASS_DENSITY=<factor> (a developer aid, read once) multiplies the tufts per density point.
        static const float densityFactor = [] {
            const char* text = std::getenv("OPENEDEN_NEUTRAL_GRASS_DENSITY");
            return text ? static_cast<float>(std::atof(text)) : 1.0f;
        }();
        options.mTuftsPerDensityPoint *= densityFactor;
        return index->grassForCell(world, x, y, out, error, options);
    }

    std::size_t SkyrimCellSource::listGroundTextures(std::vector<openeden::sdk::game::GroundTexture>& out) const
    {
        const SkyrimTerrainIndex* index = terrain();
        return index ? index->listGroundTextures(out) : 0;
    }

    bool SkyrimCellSource::buildCell(
        openeden::sdk::game::WorldId cell, openeden::sdk::game::WorldCell& out, std::string& error)
    {
        const bool built = loadNeutralCell(mView, cell, out, error, mLimits);
        if (built && mEnableOverride)
        {
            for (openeden::sdk::game::WorldObject& object : out.objects)
                if (const std::optional<bool> enabled = mEnableOverride(object.id))
                    object.initiallyDisabled = !*enabled;
        }
        if (built && !mTestDragonPlace.empty() && !mTestDragonReplace.empty())
        {
            // Task 0253: a developer aid. A dragon takes the place of the actors of another base (a wolf).
            if (auto* view = dynamic_cast<SkyrimWorldView*>(&mView))
            {
                const FormId dragon = view->npcByEditorId(mTestDragonPlace);
                std::string replace = mTestDragonReplace;
                // "name" replaces every placed actor of that base, "name@x,y" only those within 500 units of (x, y).
                bool atPlace = false;
                double wantX = 0.0, wantY = 0.0;
                if (const std::size_t at = replace.find('@'); at != std::string::npos)
                {
                    atPlace = std::sscanf(replace.c_str() + at + 1, "%lf,%lf", &wantX, &wantY) == 2;
                    replace.resize(at);
                }
                for (char& c : replace)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (dragon != kNullFormId)
                    for (openeden::sdk::game::WorldObject& object : out.objects)
                    {
                        if (object.kind != openeden::sdk::game::ObjectKind::Actor)
                            continue;
                        std::string name = object.baseEditorId;
                        for (char& c : name)
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (name != replace)
                            continue;
                        if (atPlace
                            && std::hypot(object.transform.position.x - wantX, object.transform.position.y - wantY)
                                > 500.0)
                            continue;
                        object.base = dragon;
                        object.baseEditorId = mTestDragonPlace;
                        object.initiallyDisabled = false;
                    }
            }
        }
        if (built && mTaken)
        {
            for (openeden::sdk::game::WorldObject& object : out.objects)
                if (mTaken(object.id))
                    object.initiallyDisabled = true; // task 0208: the player picked it up
        }
        if (built && mHarvested)
        {
            for (openeden::sdk::game::WorldObject& object : out.objects)
                if (mHarvested(object.id))
                    object.initiallyDisabled = true; // task 0221: a harvested plant that has not grown back
        }
        if (built && mMoveOverride)
        {
            // Task 0202: objects a script moved or turned before this cell was built come out in their new place
            for (openeden::sdk::game::WorldObject& object : out.objects)
                if (const std::optional<openeden::sdk::game::ObjectMove> move = mMoveOverride(object.id))
                {
                    if (move->hasPosition)
                        object.transform.position = move->position;
                    if (move->hasRotation)
                        object.transform.rotation = move->rotation;
                }
        }
        if (built && mCellBuilt)
            mCellBuilt(cell);
        return built;
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

    bool SkyrimCellSource::playerBody(openeden::sdk::game::PlayerBodyState& out)
    {
        out = {};
        auto* source = dynamic_cast<openeden::sdk::game::PlayerBodySource*>(&mView);
        if (source == nullptr || !source->playerBody(out))
            return false;
        {
            // Task 0279: one token and one clip name for the body: the combat's swings and the pack's own clips (the
            // beast's transformation, feeding and Howl). The newest source's clip is played.
            const std::uint32_t beastBody = mCombat.playerBeastBodyCount();
            const bool howl = beastBody != mLastBeastBodyCount;
            mLastBeastBodyCount = beastBody;
            const std::uint32_t special = mBodyClips.count() + beastBody;
            const std::string_view specialClip = howl ? std::string_view(mCombat.playerBeastBodyClip())
                                                      : std::string_view(mBodyClips.clip());
            auto composed = mBodyMux.compose(mCombat.playerSwingCount(),
                werewolf::bareClip(mCombat.playerSwingClip()), special, specialClip); // task 0259: no folder
            out.swingToken = composed.token;
            out.swingClip = std::move(composed.clip);
        }
        out.castToken = mCombat.playerCastCount(); // task 0245
        out.castClip = mCombat.playerCastClip();
        {
            // task 0249: the bow and the spell hold
            const PlayerHands hands = mCombat.playerHands();
            out.bowDrawToken = hands.bowDrawToken;
            out.bowReleaseToken = hands.bowReleaseToken;
            out.bowDrawing = hands.bowDrawing;
            out.bowDrawClip = hands.bowDrawClip;
            out.bowReleaseClip = hands.bowReleaseClip;
            out.castHold = hands.castHold;
            out.castHoldSeconds = hands.castHoldSeconds;
            out.castHoldClip = hands.castHoldClip;
        }
        out.ridingClip = std::string(mRiding.clip()); // task 0251
        if (out.ridingClip.empty())
            out.ridingClip = std::string(mKillMoves.playerClip()); // task 0278: his side of a kill move
        out.ridingMount = mRiding.bodyMountReference(); // task 0255 (0267: 0 while a paired clip plays)
        {
            const Point offset = mRiding.bodyOffset(); // task 0267
            const Point blend = mRiding.blendOffset(); // task 0278
            out.bodyOffsetX = static_cast<float>(offset.mX + blend.mX);
            out.bodyOffsetY = static_cast<float>(offset.mY + blend.mY);
            out.bodyOffsetZ = static_cast<float>(offset.mZ + blend.mZ);
        }
        return true;
    }

    bool SkyrimCellSource::playerIdentity(openeden::sdk::game::PlayerIdentityState& out)
    {
        out = {};
        auto* source = dynamic_cast<openeden::sdk::game::PlayerIdentitySource*>(&mView);
        return source != nullptr && source->playerIdentity(out);
    }

    bool SkyrimCellSource::playerImageSpace(openeden::sdk::game::PlayerImageSpaceState& out)
    {
        return mCombat.playerImageSpace(out); // task 0257
    }

    bool SkyrimCellSource::describePlayerBody(openeden::sdk::game::ActorAppearance& out, std::string& error)
    {
        auto* source = dynamic_cast<openeden::sdk::game::PlayerBodySource*>(&mView);
        if (source == nullptr)
        {
            out = {};
            error = "the world view has no player body";
            return false;
        }
        return source->describePlayerBody(out, error);
    }

    bool SkyrimCellSource::describeFirstPersonBody(openeden::sdk::game::ActorAppearance& out, std::string& error)
    {
        auto* source = dynamic_cast<openeden::sdk::game::PlayerBodySource*>(&mView);
        if (source == nullptr)
        {
            out = {};
            error = "the world view has no player body";
            return false;
        }
        return source->describeFirstPersonBody(out, error);
    }

    bool SkyrimCellSource::actorStats(openeden::sdk::game::WorldId base, std::uint32_t variation,
        std::uint16_t playerLevel, openeden::sdk::game::ActorStats& out, std::string& error)
    {
        auto* source = dynamic_cast<openeden::sdk::game::ActorMechanicsSource*>(&mView);
        if (source == nullptr)
        {
            out = {};
            error = "the world view has no actor numbers";
            return false;
        }
        return source->actorStats(base, variation, playerLevel, out, error);
    }

    bool SkyrimCellSource::actorDisposition(openeden::sdk::game::WorldId base, std::uint32_t variation,
        openeden::sdk::game::ActorDisposition& out, std::string& error)
    {
        auto* source = dynamic_cast<openeden::sdk::game::ActorMechanicsSource*>(&mView);
        if (source == nullptr)
        {
            out = {};
            error = "the world view has no actor relations";
            return false;
        }
        return source->actorDisposition(base, variation, out, error);
    }

    openeden::sdk::game::ActorRelation SkyrimCellSource::relationBetween(openeden::sdk::game::WorldId baseA,
        std::uint32_t variationA, openeden::sdk::game::WorldId baseB, std::uint32_t variationB)
    {
        auto* source = dynamic_cast<openeden::sdk::game::ActorMechanicsSource*>(&mView);
        return source != nullptr ? source->relationBetween(baseA, variationA, baseB, variationB)
                                 : openeden::sdk::game::ActorRelation::Neutral;
    }

    std::string SkyrimCellSource::displayNameOf(openeden::sdk::game::WorldId base)
    {
        auto* source = dynamic_cast<openeden::sdk::game::DisplayNameSource*>(&mView);
        return source != nullptr ? source->displayNameOf(base) : std::string();
    }

    bool SkyrimCellSource::speaksAsActivator(openeden::sdk::game::WorldId base)
    {
        auto* view = dynamic_cast<SkyrimWorldView*>(&mView);
        return view != nullptr && view->talkingActivatorVoice(static_cast<FormId>(base)) != kNullFormId;
    }

    bool SkyrimCellSource::beginConversation(openeden::sdk::game::WorldId base, openeden::sdk::game::WorldId reference,
        openeden::sdk::game::WorldId cell, openeden::sdk::game::DialogueTurn& out, std::string& error)
    {
        if (mFeeding != openeden::sdk::game::kNoWorldId)
        {
            // Task 0260: the activation was a feeding: one closing line, and the sleeper keeps sleeping
            const bool mine = mFeeding == reference;
            mFeeding = openeden::sdk::game::kNoWorldId;
            if (mine)
            {
                out = feedTurn();
                return true;
            }
        }
        auto* source = dynamic_cast<openeden::sdk::game::DialogueSource*>(&mView);
        if (source == nullptr)
        {
            out = {};
            error = "the world view cannot run conversations";
            return false;
        }
        const bool begun = source->beginConversation(base, reference, cell, out, error);
        if (begun)
        {
            // Stage S6f: the package brain stops the actor (a sleeper wakes) while the player talks to it.
            if (mTalkingTo != openeden::sdk::game::kNoWorldId && mTalkingTo != reference)
                mActors.onConversation(mTalkingTo, false);
            mTalkingTo = reference;
            mActors.onConversation(reference, true);
            if (mActivated)
                mActivated(reference);
        }
        return begun;
    }

    bool SkyrimCellSource::chooseTopic(
        std::uint32_t topicId, openeden::sdk::game::DialogueTurn& out, std::string& error)
    {
        auto* source = dynamic_cast<openeden::sdk::game::DialogueSource*>(&mView);
        if (source == nullptr)
        {
            out = {};
            error = "the world view cannot run conversations";
            return false;
        }
        return source->chooseTopic(topicId, out, error);
    }

    void SkyrimCellSource::endConversation()
    {
        if (auto* source = dynamic_cast<openeden::sdk::game::DialogueSource*>(&mView))
            source->endConversation();
        if (mTalkingTo != openeden::sdk::game::kNoWorldId)
        {
            mActors.onConversation(mTalkingTo, false);
            mTalkingTo = openeden::sdk::game::kNoWorldId;
        }
    }

    bool SkyrimCellSource::currentPlace(effectconditions::Place& out) const
    {
        if (!mHavePlayerPlace)
            return false;
        out.mKnown = true;
        out.mInterior = mPlayerPlace.interior;
        out.mWorld = mPlayerPlace.interior ? kNullFormId : static_cast<FormId>(mPlayerPlace.world);
        return true;
    }

    openeden::sdk::game::DialogueTurn SkyrimCellSource::feedTurn()
    {
        openeden::sdk::game::DialogueTurn turn;
        turn.text = "You sink your fangs into the sleeper's neck and drink deeply.";
        turn.endsConversation = true;
        return turn;
    }

    bool SkyrimCellSource::prepareConversation(openeden::sdk::game::WorldId reference)
    {
        // Task 0260: a vampire activating a sleeper feeds on it (the sleeper is not woken for a talk)
        if (mFeedHook && !mActors.readyToTalk(reference) && mFeedHook(reference))
        {
            mFeeding = reference;
            return true;
        }
        return mActors.prepareTalk(reference);
    }

    bool SkyrimCellSource::readyForConversation(openeden::sdk::game::WorldId reference)
    {
        return mActors.readyToTalk(reference);
    }

    void SkyrimCellSource::cancelPreparedConversation(openeden::sdk::game::WorldId reference)
    {
        if (mFeeding == reference)
        {
            mFeeding = openeden::sdk::game::kNoWorldId; // task 0260: nothing was prepared
            return;
        }
        mActors.onConversation(reference, false);
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

    bool SkyrimCellSource::imageSpaceForCell(
        openeden::sdk::game::WorldId cell, openeden::sdk::game::ImageSpaceGrade& out)
    {
        out = {};
        auto* source = dynamic_cast<openeden::sdk::game::CellImageSpaceSource*>(&mView);
        return source != nullptr && source->imageSpaceForCell(cell, out);
    }

    bool SkyrimCellSource::ambientCubeForCell(
        openeden::sdk::game::WorldId cell, openeden::sdk::game::AmbientCube& out)
    {
        auto* source = dynamic_cast<openeden::sdk::game::AmbientCubeSource*>(&mView);
        return source != nullptr && source->ambientCubeForCell(cell, out);
    }

    bool SkyrimCellSource::imageSpacePending()
    {
        auto* source = dynamic_cast<openeden::sdk::game::CellImageSpaceSource*>(&mView);
        return source != nullptr && source->imageSpacePending();
    }

    bool SkyrimCellSource::fogCurveForCell(openeden::sdk::game::WorldId cell, double& power, double& max)
    {
        power = 1.0;
        max = 1.0;
        auto* source = dynamic_cast<openeden::sdk::game::CellFogSource*>(&mView);
        return source != nullptr && source->fogCurveForCell(cell, power, max);
    }

    bool SkyrimCellSource::fogFarColorForCell(
        openeden::sdk::game::WorldId cell, std::array<std::uint8_t, 3>& colour)
    {
        colour = {};
        auto* source = dynamic_cast<openeden::sdk::game::CellFogSource*>(&mView);
        return source != nullptr && source->fogFarColorForCell(cell, colour);
    }

    bool SkyrimCellSource::lightSettingsForCell(
        openeden::sdk::game::WorldId cell, openeden::sdk::game::CellLightSettings& out)
    {
        out = {};
        auto* source = dynamic_cast<openeden::sdk::game::CellLightSource*>(&mView);
        return source != nullptr && source->lightSettingsForCell(cell, out);
    }

    // ---- music (task 0209) ----

    namespace
    {
        music::MusicRuntime* musicRuntimeOf(openeden::sdk::game::WorldView& view)
        {
            auto* data = dynamic_cast<music::MusicDataSource*>(&view);
            return data != nullptr ? data->musicRuntime() : nullptr;
        }
    }

    bool SkyrimCellSource::musicReady()
    {
        if (!mMusicOn)
            return false;
        const music::MusicRuntime* runtime = musicRuntimeOf(mView);
        return runtime != nullptr && runtime->ready();
    }

    bool SkyrimCellSource::musicType(
        const openeden::sdk::game::MusicContext& context, openeden::sdk::game::MusicTypeDescription& out)
    {
        music::MusicRuntime* runtime = mMusicOn ? musicRuntimeOf(mView) : nullptr;
        if (runtime == nullptr)
            return false;
        return runtime->type(static_cast<FormId>(context.cell), mCombat.playerEngagedByAi(), out);
    }

    bool SkyrimCellSource::musicPlan(openeden::sdk::game::WorldId typeId,
        const openeden::sdk::game::MusicContext& context, openeden::sdk::game::MusicPlan& out)
    {
        music::MusicRuntime* runtime = mMusicOn ? musicRuntimeOf(mView) : nullptr;
        auto* data = dynamic_cast<music::MusicDataSource*>(&mView);
        if (runtime == nullptr || data == nullptr)
            return false;
        music::ConditionFacts facts;
        facts.mGameHour = context.gameHour;
        return runtime->plan(
            static_cast<FormId>(typeId), facts, mMusicSeed, context.pick,
            [data](const std::string& path) { return data->musicFileExists(path); }, out);
    }

    void SkyrimCellSource::setBarks(barks::BarkSystem* system)
    {
        mBarks = system;
        if (system == nullptr)
            mCombat.setBarkSink({});
        else
            mCombat.setBarkSink([system](barks::Kind kind, openeden::sdk::game::ActorHandle handle) {
                system->onEvent(kind, static_cast<std::uint64_t>(handle.reference));
            });
    }

    bool SkyrimCellSource::addCartRider(FormId rider, FormId cart)
    {
        if (!mCarts.addRider(rider, cart))
            return false;
        if (rider != kCartPlayerReference)
            mActors.setVehicleRider(rider, true);
        return true;
    }

    bool SkyrimCellSource::removeCartRider(FormId rider)
    {
        CartPose pose;
        const FormId cart = mCarts.removeRider(rider);
        if (cart == kNullFormId)
            return false;
        (void)mCarts.cartPose(cart, pose);
        if (mCartExits.size() < CartRides::kMaxRiders)
            mCartExits.push_back({ rider, pose.heading });
        return true;
    }

    void SkyrimCellSource::noteMount(FormId reference)
    {
        const auto origin = mOriginOf.find(reference);
        const FormId root = origin != mOriginOf.end() ? origin->second : reference;
        if (mMountHorse.count(root) != 0)
            return;
        mMountCandidates.insert(root); // the placed table is read on a worker: asked again until it is there
        resolveMountCandidates();
    }

    void SkyrimCellSource::resolveMountCandidates()
    {
        auto* view = dynamic_cast<SkyrimWorldView*>(&mView);
        if (view == nullptr || view->placedRefs() == nullptr)
            return;
        for (const FormId root : mMountCandidates)
        {
            const FormId horse = view->horseOf(root);
            if (horse != kNullFormId && horse != kUnresolvedFormId)
                mMountHorse[root] = horse;
        }
        mMountCandidates.clear();
    }

    namespace
    {
        /// The seat on a horse: the root of a seated actor goes this far above the horse's origin (the saddle is 135
        /// above it, the sit clip puts the pelvis 39 above its root: both measured, task 0255 and 0135).
        constexpr double kMountSeatZ = 96.0;
        constexpr const char* kMountSeatClip = "chair_idlebasevar1";
    }

    void SkyrimCellSource::updateMounts(openeden::sdk::game::ActorRequestWriter& writer)
    {
        namespace sdkgame = openeden::sdk::game;
        if (!mMountCandidates.empty())
            resolveMountCandidates();
        mHorsePlaces.clear(); // refilled here (mounted horses) and by updateCarts (cart horses)
        for (const auto& [riderOriginal, horseOriginal] : mMountHorse)
        {
            const auto cr = mCurrentOf.find(riderOriginal);
            const auto ch = mCurrentOf.find(horseOriginal);
            const FormId riderRef = cr != mCurrentOf.end() ? cr->second : riderOriginal;
            const FormId horseRef = ch != mCurrentOf.end() ? ch->second : horseOriginal;
            const sdkgame::ActorHandle rider = mActors.handleOf(riderRef);
            const sdkgame::ActorHandle horse = mActors.handleOf(horseRef);
            if (!rider.valid() || !horse.valid())
            {
                static int sTmp = 0;
                if (sTmp++ < 6)
                    std::fprintf(stderr, "TMPMOUNT pair %u->%u refs %u %u invalid\n", static_cast<unsigned>(riderOriginal),
                        static_cast<unsigned>(horseOriginal), static_cast<unsigned>(riderRef),
                        static_cast<unsigned>(horseRef));
                continue;
            }
            MountState& state = mMountStates[riderOriginal];
            if (state.mRider != riderRef || state.mHorse != horseRef)
            {
                state = MountState();
                state.mRider = riderRef;
                state.mHorse = horseRef;
                mActors.setMount(riderRef, horse);
                (void)mActors.setVehicleRider(horseRef, true); // the horse is moved by its rider's packages
            }
            double x = 0.0, y = 0.0, z = 0.0;
            if (!writer.actorPosition(horse, x, y, z))
                continue;
            if (state.mHasLast)
            {
                const double dx = x - state.mLastX, dy = y - state.mLastY;
                if (std::hypot(dx, dy) > 0.5)
                    state.mHeading = static_cast<float>(std::atan2(dx, dy));
            }
            state.mLastX = x;
            state.mLastY = y;
            state.mHasLast = true;
            HorsePlace place;
            place.mReference = horseRef;
            place.mX = x;
            place.mY = y;
            place.mZ = z;
            place.mRider = riderRef;
            mHorsePlaces.push_back(place);
            sdkgame::FurnitureUse use;
            use.x = x;
            use.y = y;
            use.z = z + kMountSeatZ;
            use.heading = state.mHeading;
            use.pose = sdkgame::FurniturePose::Sit;
            use.clip = kMountSeatClip;
            use.clipPerSex = true;
            if (!state.mSeated)
                state.mSeated = writer.useFurniture(rider, use);
            else if (!writer.placeFurniture(rider, use.x, use.y, use.z, use.heading))
                state.mSeated = writer.useFurniture(rider, use);
        }
    }

    void SkyrimCellSource::updateCarts(float seconds, openeden::sdk::game::ActorRequestWriter& writer)
    {
        namespace sdkgame = openeden::sdk::game;
        auto* player = dynamic_cast<sdkgame::RidingWriter*>(&writer);
        const auto where = [this, &writer](FormId reference, double& x, double& y, double& z) {
            if (reference == kCartPlayerReference)
                return writer.playerPosition(x, y, z);
            const sdkgame::ActorHandle handle = mActors.handleOf(reference);
            return handle.valid() && writer.actorPosition(handle, x, y, z);
        };
        // Riders that got off: stood up a step to the right of the cart (the side the exit idles of the game use is
        // not known: ours)
        for (const CartExit& exit : mCartExits)
        {
            double x = 0.0, y = 0.0, z = 0.0;
            if (!where(exit.mRider, x, y, z))
                continue;
            const double h = static_cast<double>(exit.mHeading);
            x += std::cos(h) * kCartExitSide;
            y -= std::sin(h) * kCartExitSide;
            if (exit.mRider == kCartPlayerReference)
            {
                if (player != nullptr)
                    (void)player->releasePlayer(x, y, z);
            }
            else
            {
                const sdkgame::ActorHandle handle = mActors.handleOf(exit.mRider);
                if (handle.valid())
                    (void)writer.leaveFurniture(handle, x, y, z);
                mCartSeated.erase(exit.mRider);
                (void)mActors.setVehicleRider(exit.mRider, false);
            }
        }
        mCartExits.clear();
        if (mCarts.rigCount() == 0)
            return;
        for (const FormId horse : mCarts.horses())
        {
            HorsePlace place;
            place.mReference = horse;
            if (where(horse, place.mX, place.mY, place.mZ))
                mHorsePlaces.push_back(place);
        }
        CartRides::Result result;
        mCarts.step(static_cast<double>(seconds), where, result);
        for (const CartRides::CartMove& move : result.mCarts)
        {
            bool found = false;
            for (CartRides::CartMove& old : mCartMoves)
                if (old.mCart == move.mCart)
                {
                    old = move;
                    found = true;
                }
            if (!found && mCartMoves.size() < CartRides::kMaxRigs)
                mCartMoves.push_back(move);
        }
        for (const CartRides::RiderMove& move : result.mRiders)
        {
            if (move.mRider == kCartPlayerReference)
            {
                if (player != nullptr)
                    (void)player->carryPlayer(move.mPose.x, move.mPose.y, move.mPose.z, move.mPose.heading);
                continue;
            }
            const sdkgame::ActorHandle handle = mActors.handleOf(move.mRider);
            if (!handle.valid())
                continue;
            sdkgame::FurnitureUse use;
            use.x = move.mPose.x;
            use.y = move.mPose.y;
            use.z = move.mPose.z;
            use.heading = move.mPose.heading;
            use.pose = sdkgame::FurniturePose::Sit;
            use.clip = kCartSeatClip;
            use.clipPerSex = true;
            if (move.mFirst || mCartSeated.count(move.mRider) == 0)
            {
                if (writer.useFurniture(handle, use))
                    mCartSeated.insert(move.mRider);
            }
            else if (!writer.placeFurniture(handle, use.x, use.y, use.z, use.heading))
                (void)writer.useFurniture(handle, use);
        }
    }

    void SkyrimCellSource::updateBarks(float seconds, openeden::sdk::game::ActorRequestWriter& writer)
    {
        mBarkRoster.clear();
        for (const openeden::sdk::game::WorldId reference : mActors.actorReferences())
        {
            const openeden::sdk::game::ActorHandle handle = mActors.handleOf(reference);
            barks::Actor actor;
            actor.mReference = static_cast<std::uint64_t>(reference);
            if (!handle.valid() || !writer.actorPosition(handle, actor.mX, actor.mY, actor.mZ))
                continue;
            SkyrimCombat::FighterView view;
            actor.mDead = mCombat.fighterView(openeden::sdk::game::CombatSubject::of(handle), view) && view.mDead;
            mBarkRoster.push_back(actor);
        }
        double px = 0.0, py = 0.0, pz = 0.0;
        const bool havePlayer = writer.playerPosition(px, py, pz);
        mBarks->update(seconds, mBarkRoster, havePlayer, px, py, pz);
    }
}
