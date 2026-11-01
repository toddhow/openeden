#ifndef CSV_RENDER_TERRAINSELECTION_H
#define CSV_RENDER_TERRAINSELECTION_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CSVRender
{
    class OverlayLines;
    class WorldspaceWidget;

    enum class TerrainSelectionType
    {
        Texture,
        Shape
    };

    enum class SelectionMethod
    {
        OnlySelect,
        AddSelect,
        RemoveSelect,
        ToggleSelect
    };

    /// \brief Class handling the terrain selection data and rendering
    class TerrainSelection
    {
    public:
        TerrainSelection(WorldspaceWidget* worldspaceWidget, TerrainSelectionType type);
        ~TerrainSelection();

        void onlySelect(const std::vector<std::pair<int, int>>& localPositions);
        void addSelect(const std::vector<std::pair<int, int>>& localPositions);
        void removeSelect(const std::vector<std::pair<int, int>>& localPositions);
        void toggleSelect(const std::vector<std::pair<int, int>>& localPositions);
        void clearTemporarySelection();

        void activate();
        void deactivate();

        std::vector<std::pair<int, int>> getTerrainSelection() const;

        void update();

    protected:
        int calculateLandHeight(int x, int y);

    private:
        void handleSelection(const std::vector<std::pair<int, int>>& localPositions, SelectionMethod selectionMethod);

        bool noCell(const std::string& cellId);

        bool noLand(const std::string& cellId);

        bool noLandLoaded(const std::string& cellId);

        bool isLandLoaded(const std::string& cellId);
        WorldspaceWidget* mWorldspaceWidget;
        std::unique_ptr<OverlayLines> mLines;
        std::vector<std::pair<int, int>>
            mSelection; // Global terrain selection coordinate in either vertex or texture units
        std::vector<std::pair<int, int>>
            mTemporarySelection; // Used during toggle to compare the most recent drag operation
        TerrainSelectionType mSelectionType;
    };
}

#endif
