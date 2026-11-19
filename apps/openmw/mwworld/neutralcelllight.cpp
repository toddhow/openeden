#include "neutralcelllight.hpp"

#include <algorithm>
#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/settings/values.hpp>
#include <components/worldmodel/store.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwrender/rendermanager.hpp"
#include "cellstore.hpp"

namespace MWWorld
{
    namespace
    {
        constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

        // The pack's source for a neutral cell, or null.
        openeden::sdk::game::CellLightSource* sourceOf(const CellStore& cell)
        {
            if (!MWBase::Environment::exists())
                return nullptr;
            MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
            NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
            if (store == nullptr)
                return nullptr;
            return dynamic_cast<openeden::sdk::game::CellLightSource*>(store->source(cell.neutralGameId()));
        }
    }

    std::optional<NeutralInteriorSun> neutralInteriorSun(
        const openeden::sdk::game::CellLightSettings* settings, bool enabled)
    {
        if (!enabled || settings == nullptr)
            return std::nullopt;
        if (!std::isfinite(settings->rotationXY) || !std::isfinite(settings->rotationZ)
            || !std::isfinite(settings->directionalFade))
            return std::nullopt;
        const double heading = settings->rotationXY * kDegreesToRadians;
        const double elevation = settings->rotationZ * kDegreesToRadians;
        NeutralInteriorSun sun;
        sun.towardLight = { static_cast<float>(std::cos(elevation) * std::sin(heading)),
            static_cast<float>(std::cos(elevation) * std::cos(heading)), static_cast<float>(std::sin(elevation)) };
        // toddhow, 2026-11-19: a fade of 0 is the FULL light; only a fade between 0 and 1 dims it.
        const double fade = settings->directionalFade;
        sun.intensity = fade == 0.0 ? 1.f : static_cast<float>(std::clamp(fade, 0.0, 1.0));
        return sun;
    }

    float neutralClipDistance(const openeden::sdk::game::CellLightSettings* settings, bool enabled)
    {
        if (!enabled || settings == nullptr || !std::isfinite(settings->clipDistance))
            return 0.f;
        if (settings->clipDistance < static_cast<double>(kMinNeutralClipDistance))
            return 0.f;
        return static_cast<float>(settings->clipDistance);
    }

    NeutralLightFade neutralLightFade(const openeden::sdk::game::CellLightSettings* settings, bool enabled)
    {
        if (!enabled || settings == nullptr)
            return {};
        if (!std::isfinite(settings->lightFadeBegin) || !std::isfinite(settings->lightFadeEnd)
            || !(settings->lightFadeEnd > 0.0))
            return {};
        return { static_cast<float>(std::max(settings->lightFadeBegin, 0.0)),
            static_cast<float>(settings->lightFadeEnd) };
    }

    void applyNeutralCellLight(const CellStore& cell, MWRender::RenderManager& rendering)
    {
        // Every cell gives the renderer a value, so nothing an interior set stays on the street; the "none" values are
        // the ones the renderer starts with (Morrowind never sees anything else).
        NeutralInteriorSun sun;
        NeutralLightFade fade;
        float clip = 0.f;
        bool applied = false;
        const openeden::sdk::game::WorldCell* neutral
            = Settings::game().mNeutralWorld.get() ? cell.neutralCell() : nullptr;
        if (neutral != nullptr && neutral->interior)
        {
            openeden::sdk::game::CellLightSettings settings;
            openeden::sdk::game::CellLightSource* source = sourceOf(cell);
            const bool has = source != nullptr && source->lightSettingsForCell(neutral->id, settings);
            if (has)
            {
                if (const auto s = neutralInteriorSun(&settings, Settings::game().mNeutralInteriorSun.get()))
                {
                    sun = *s;
                    applied = true;
                }
                fade = neutralLightFade(&settings, Settings::game().mNeutralLightFade.get());
                clip = neutralClipDistance(&settings, Settings::game().mNeutralLightFade.get());
            }
            // The log line says what the cell got (a run can be compared), also for a cell with nothing.
            Log(Debug::Info) << "NLIGHTFJ " << neutral->editorId << (has ? "" : " none") << " sun "
                             << (applied ? "" : "not applied ") << sun.towardLight[0] << "," << sun.towardLight[1]
                             << "," << sun.towardLight[2] << " intensity " << sun.intensity << " fade " << fade.begin
                             << " to " << fade.end << " clip " << clip;
        }
        if (applied)
            rendering.setNeutralInteriorSun(
                vsg::vec3(sun.towardLight[0], sun.towardLight[1], sun.towardLight[2]), sun.intensity);
        else
            rendering.clearNeutralInteriorSun();
        rendering.setNeutralLightFade(fade.begin, fade.end);
        rendering.setNeutralClipDistance(clip);
    }
}
