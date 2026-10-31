#ifndef CSV_RENDER_BRUSHDRAW_H
#define CSV_RENDER_BRUSHDRAW_H

#include <memory>

#include <vsg/maths/vec3.h>

#include "../widget/brushshapes.hpp"

namespace CSVRender
{
    class OverlayLines;
    class WorldspaceWidget;

    /// The outline of a terrain brush under the pointer. Owned by its edit mode. Its lines hang in the view from the first
    /// update on, so the view must exist by then and outlive this.
    class BrushDraw
    {
    public:
        explicit BrushDraw(WorldspaceWidget& worldspaceWidget, bool textureMode = false);
        ~BrushDraw();

        void update(const vsg::dvec3& point, int brushSize, CSVWidget::BrushShape toolShape);
        void hide();

    private:
        float vertexHeight(int x, int y) const;
        float getTerrainHeight(float worldX, float worldY) const;

        WorldspaceWidget& mWorldspaceWidget;
        std::unique_ptr<OverlayLines> mLines;
        bool mTextureMode;
        float mLandSizeFactor;
    };
}

#endif
