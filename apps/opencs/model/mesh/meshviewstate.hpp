#ifndef CSM_MESH_MESHVIEWSTATE_H
#define CSM_MESH_MESHVIEWSTATE_H

#include <array>
#include <optional>
#include <string_view>

namespace CSMMesh
{
    /// How the mesh viewer draws the model (task 0043). Only Textured shows the renderer's own materials; the other
    /// modes are drawn from overlay lines or triangles, so that no renderer pipeline changes.
    enum class ViewMode
    {
        Textured,
        Wireframe,
        Flat,
        VertexColours,
        Normals,
    };

    enum class LightingPreset
    {
        Headlight,
        Sun,
        Flat,
    };

    /// Plain data: what the viewer shows. No Qt, no GPU.
    struct MeshViewState
    {
        ViewMode mode = ViewMode::Textured;
        LightingPreset lighting = LightingPreset::Headlight;
        bool showCollision = false;
        bool showBounds = false;
        bool showSkeleton = false;
        bool showNodeAxes = false;
        bool showGrid = true;
        std::array<float, 3> background{ 0.16f, 0.18f, 0.22f };
    };

    const char* viewModeName(ViewMode mode);
    std::optional<ViewMode> parseViewMode(std::string_view text);
    const char* lightingPresetName(LightingPreset preset);
    std::optional<LightingPreset> parseLightingPreset(std::string_view text);

    /// Every mode but Textured hides the textured model and draws its own picture.
    bool hidesTexturedModel(ViewMode mode);
    /// Flat and vertex colours are drawn as unlit triangles.
    bool needsTriangleOverlay(ViewMode mode);
    /// Wireframe and normals are lines, and so are the helper overlays (the grid is its own node).
    bool needsLineOverlay(const MeshViewState& state);
}

#endif
