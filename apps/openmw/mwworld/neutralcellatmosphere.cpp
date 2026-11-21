#include "neutralcellatmosphere.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/render/fogcurve.hpp>
#include <components/render/imagespacemath.hpp>
#include <components/settings/values.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/game/ambient_cube_source.hpp>
#include <openeden/game/cell_image_space_source.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwrender/rendermanager.hpp"
#include "cell.hpp"
#include "cellstore.hpp"

namespace MWWorld
{
    namespace
    {
        float finiteOr(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }

        void tellRenderer(MWRender::RenderManager& rendering, const NeutralCellGrade& g)
        {
            rendering.setImageSpaceStops(g.stops);
            rendering.setImageSpaceGrade(
                g.on, g.saturation, g.contrast, g.tintAmount, vsg::vec3(g.tint[0], g.tint[1], g.tint[2]));
        }

        // The pack's source for a neutral cell, or null.
        openeden::sdk::game::CellImageSpaceSource* sourceOf(const CellStore& cell)
        {
            if (!MWBase::Environment::exists())
                return nullptr;
            MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
            NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
            if (store == nullptr)
                return nullptr;
            return dynamic_cast<openeden::sdk::game::CellImageSpaceSource*>(store->source(cell.neutralGameId()));
        }
    }

    NeutralCellGrade neutralCellGrade(const openeden::sdk::game::ImageSpaceGrade* grade, float contrastShare)
    {
        NeutralCellGrade out;
        if (grade == nullptr)
            return out;
        out.on = true;
        // The weather manager's rule: brightness is a linear multiplier of the exposure, floored, in stops.
        const float brightness = std::max(finiteOr(grade->brightness, 1.f), Render::ImageSpace::kMinBrightness);
        out.stops = std::clamp(std::log2(brightness), -Render::ImageSpace::kMaxStops, Render::ImageSpace::kMaxStops);
        out.saturation = finiteOr(grade->saturation, 1.f);
        out.contrast = 1.f + (finiteOr(grade->contrast, 1.f) - 1.f) * finiteOr(contrastShare, 0.f);
        out.tintAmount = finiteOr(grade->tintAmount, 0.f);
        out.tint = { finiteOr(grade->tint.r, 1.f), finiteOr(grade->tint.g, 1.f), finiteOr(grade->tint.b, 1.f) };
        return out;
    }

    std::optional<NeutralInteriorFog> neutralInteriorFog(
        const openeden::sdk::game::CellLighting* lighting, bool enabled)
    {
        if (!enabled || lighting == nullptr)
            return std::nullopt;
        const double start = lighting->fogNear;
        const double end = lighting->fogFar;
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0.0 || !(end > start))
            return std::nullopt;
        return NeutralInteriorFog{ static_cast<float>(start), static_cast<float>(end) };
    }

    std::optional<NeutralFogCurve> neutralFogCurve(std::optional<std::pair<double, double>> powerAndMax, bool enabled)
    {
        if (!enabled || !powerAndMax)
            return std::nullopt;
        NeutralFogCurve curve;
        const double power = powerAndMax->first;
        const double max = powerAndMax->second;
        if (std::isfinite(power) && power >= static_cast<double>(Render::FogCurve::kMinPower)
            && power <= static_cast<double>(Render::FogCurve::kMaxPower))
            curve.power = static_cast<float>(power);
        if (std::isfinite(max) && max > 0.0 && max <= 1.0)
            curve.max = static_cast<float>(max);
        if (curve.power == 1.f && curve.max == 1.f)
            return std::nullopt;
        return curve;
    }

    Cell makeNeutralInteriorCell(
        const ESM::Cell& record, const openeden::sdk::game::WorldCell& cell, std::optional<NeutralFogCurve> curve)
    {
        Cell out(record);
        const auto lighting = cell.lighting ? &*cell.lighting : nullptr;
        if (const auto fog = neutralInteriorFog(lighting, Settings::game().mNeutralInteriorFog.get()))
            out.setNeutralFog(fog->start, fog->end, curve ? curve->power : 1.f, curve ? curve->max : 1.f);
        return out;
    }

    bool neutralGradeShaderWanted(
        bool neutralWorld, bool weatherImageSpace, bool imageSpaceEffects, bool cellImageSpace)
    {
        return weatherImageSpace || imageSpaceEffects || (neutralWorld && cellImageSpace);
    }

    bool applyNeutralCellImageSpace(const CellStore& cell, MWRender::RenderManager& rendering)
    {
        if (!Settings::game().mNeutralWorld.get() || !Settings::game().mNeutralCellImageSpace.get())
            return false;
        const openeden::sdk::game::WorldCell* neutral = cell.neutralCell();
        if (neutral == nullptr || !neutral->interior)
            return false;
        openeden::sdk::game::ImageSpaceGrade grade;
        openeden::sdk::game::CellImageSpaceSource* source = sourceOf(cell);
        if (source != nullptr && source->imageSpacePending())
            return true; // the pack is reading its records on a worker: nothing waits, we ask again next frame
        // The ask is timed so a slow one shows in the log (it must stay far below the freeze monitor's 5 s).
        const auto begun = std::chrono::steady_clock::now();
        const bool has = source != nullptr && source->imageSpaceForCell(neutral->id, grade);
        const auto micros
            = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begun).count();
        const NeutralCellGrade g = neutralCellGrade(has ? &grade : nullptr, kContrastShareWithAveragePivot);
        tellRenderer(rendering, g);
        Log(Debug::Info) << "NIMAGESPACE " << neutral->editorId << (has ? " grade" : " none") << " saturation "
                         << g.saturation << " stops " << g.stops << " contrast " << g.contrast << " tint "
                         << g.tintAmount << " lookup " << micros << " us";
        return false;
    }

    void applyNeutralAmbientCube(const CellStore& cell, MWRender::RenderManager& rendering)
    {
        if (!rendering.ambientCubeEnabled())
            return;
        const openeden::sdk::game::WorldCell* neutral = cell.neutralCell();
        openeden::sdk::game::AmbientCube cube;
        bool has = false;
        if (neutral != nullptr && neutral->interior && MWBase::Environment::exists())
        {
            MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
            NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
            auto* source = store != nullptr
                ? dynamic_cast<openeden::sdk::game::AmbientCubeSource*>(store->source(cell.neutralGameId()))
                : nullptr;
            has = source != nullptr && source->ambientCubeForCell(neutral->id, cube);
        }
        rendering.setAmbientCube(has ? &cube.colour : nullptr);
        if (neutral != nullptr && neutral->interior)
            Log(Debug::Info) << "NAMBIENTCUBE " << neutral->editorId << (has ? " cube" : " none");
    }

    void resetNeutralCellImageSpace(const CellStore& cell, MWRender::RenderManager& rendering)
    {
        if (!Settings::game().mNeutralWorld.get() || !Settings::game().mNeutralCellImageSpace.get())
            return;
        const openeden::sdk::game::WorldCell* neutral = cell.neutralCell();
        if (neutral == nullptr || neutral->interior)
            return;
        tellRenderer(rendering, NeutralCellGrade{});
    }
}
