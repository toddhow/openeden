#include "neutralcellatmosphere.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/render/imagespacemath.hpp>
#include <components/settings/values.hpp>
#include <components/worldmodel/store.hpp>

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

    Cell makeNeutralInteriorCell(const ESM::Cell& record, const openeden::sdk::game::WorldCell& cell)
    {
        Cell out(record);
        const auto lighting = cell.lighting ? &*cell.lighting : nullptr;
        if (const auto fog = neutralInteriorFog(lighting, Settings::game().mNeutralInteriorFog.get()))
            out.setNeutralFog(fog->start, fog->end);
        return out;
    }

    bool neutralGradeShaderWanted(
        bool neutralWorld, bool weatherImageSpace, bool imageSpaceEffects, bool cellImageSpace)
    {
        return weatherImageSpace || imageSpaceEffects || (neutralWorld && cellImageSpace);
    }

    void applyNeutralCellImageSpace(const CellStore& cell, MWRender::RenderManager& rendering)
    {
        if (!Settings::game().mNeutralWorld.get() || !Settings::game().mNeutralCellImageSpace.get())
            return;
        const openeden::sdk::game::WorldCell* neutral = cell.neutralCell();
        if (neutral == nullptr || !neutral->interior)
            return;
        openeden::sdk::game::ImageSpaceGrade grade;
        openeden::sdk::game::CellImageSpaceSource* source = sourceOf(cell);
        // The first ask makes the pack read the weather records of every plugin (a second pass over the files); the
        // time is logged so a slow first entry shows (the freeze monitor allows 5 s on the main thread).
        const auto begun = std::chrono::steady_clock::now();
        const bool has = source != nullptr && source->imageSpaceForCell(neutral->id, grade);
        const auto micros
            = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begun).count();
        const NeutralCellGrade g = neutralCellGrade(has ? &grade : nullptr);
        tellRenderer(rendering, g);
        Log(Debug::Info) << "NIMAGESPACE " << neutral->editorId << (has ? " grade" : " none") << " saturation "
                         << g.saturation << " stops " << g.stops << " contrast " << g.contrast << " tint "
                         << g.tintAmount << " lookup " << micros / 1000 << " ms";
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
