#include "meshviewstate.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace CSMMesh
{
    namespace
    {
        std::string lower(std::string_view text)
        {
            std::string result(text);
            std::transform(result.begin(), result.end(), result.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }
    }

    const char* viewModeName(ViewMode mode)
    {
        switch (mode)
        {
            case ViewMode::Textured:
                return "textured";
            case ViewMode::Wireframe:
                return "wireframe";
            case ViewMode::Flat:
                return "flat";
            case ViewMode::VertexColours:
                return "vertexcolours";
            case ViewMode::Normals:
                return "normals";
        }
        return "textured";
    }

    std::optional<ViewMode> parseViewMode(std::string_view text)
    {
        const std::string name = lower(text);
        for (ViewMode mode : { ViewMode::Textured, ViewMode::Wireframe, ViewMode::Flat, ViewMode::VertexColours,
                 ViewMode::Normals })
            if (name == viewModeName(mode))
                return mode;
        return std::nullopt;
    }

    const char* lightingPresetName(LightingPreset preset)
    {
        switch (preset)
        {
            case LightingPreset::Headlight:
                return "headlight";
            case LightingPreset::Sun:
                return "sun";
            case LightingPreset::Flat:
                return "flat";
        }
        return "headlight";
    }

    std::optional<LightingPreset> parseLightingPreset(std::string_view text)
    {
        const std::string name = lower(text);
        for (LightingPreset preset : { LightingPreset::Headlight, LightingPreset::Sun, LightingPreset::Flat })
            if (name == lightingPresetName(preset))
                return preset;
        return std::nullopt;
    }

    bool hidesTexturedModel(ViewMode mode)
    {
        return mode != ViewMode::Textured;
    }

    bool needsTriangleOverlay(ViewMode mode)
    {
        return mode == ViewMode::Flat || mode == ViewMode::VertexColours;
    }

    bool needsLineOverlay(const MeshViewState& state)
    {
        return state.mode == ViewMode::Wireframe || state.mode == ViewMode::Normals || state.showCollision
            || state.showBounds || state.showSkeleton || state.showNodeAxes;
    }
}
