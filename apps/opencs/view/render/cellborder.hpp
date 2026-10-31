#ifndef OPENCS_VIEW_CELLBORDER_H
#define OPENCS_VIEW_CELLBORDER_H

#include <cstddef>

#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>

namespace ESM
{
    struct Land;
}

namespace CSMWorld
{
    class CellCoordinates;
}

namespace CSVRender
{
    class SceneHost;

    /// The green line that follows the land around a cell. Owned by its Cell.
    class CellBorder
    {
    public:
        CellBorder(SceneHost& host, vsg::Group& cellNode, const CSMWorld::CellCoordinates& coords);
        ~CellBorder();

        void buildShape(const ESM::Land& esmLand);

    private:
        CellBorder(const CellBorder&) = delete;
        CellBorder& operator=(const CellBorder&) = delete;

        std::size_t landIndex(int x, int y);
        float scaleToWorld(int val);

        SceneHost& mHost;
        vsg::Group& mParentNode;
        vsg::ref_ptr<vsg::Node> mMaskNode;
        vsg::ref_ptr<vsg::MatrixTransform> mBaseNode;
        vsg::ref_ptr<vsg::Node> mBorderGeometry;
    };
}

#endif
