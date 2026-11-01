#ifndef OPENCS_VIEW_CELLMARKER_H
#define OPENCS_VIEW_CELLMARKER_H

#include "tagbase.hpp"

#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>

#include "../../model/world/cellcoordinates.hpp"

namespace CSVRender
{
    class CellMarker;
    class SceneHost;

    class CellMarkerTag : public TagBase
    {
    private:
        CellMarker* mMarker;

    public:
        CellMarkerTag(CellMarker* marker);

        CellMarker* getCellMarker() const;
    };

    /// \brief Marker to display cell coordinates. The build has no text renderer, so QPainter draws the text into an
    /// image, and a quad shows that image at a fixed size on screen, always facing the camera, in front of everything.
    /// Owned by its Cell.
    class CellMarker
    {
    private:
        SceneHost& mHost;
        vsg::Group& mCellNode;
        vsg::ref_ptr<vsg::Node> mMaskNode;
        vsg::ref_ptr<vsg::MatrixTransform> mMarkerNode;
        vsg::ref_ptr<TagBase> mTag;
        CSMWorld::CellCoordinates mCoordinates;
        bool mExists;
        vsg::dvec3 mPosition;

        CellMarker(const CellMarker&) = delete;
        CellMarker& operator=(const CellMarker&) = delete;

        /// \brief Build marker containing cell's coordinates.
        vsg::ref_ptr<vsg::Node> buildMarker() const;

    public:
        /// \brief Constructor.
        /// \param cellNode Cell to create marker for.
        /// \param coordinates Coordinates of cell.
        /// \param cellExists Whether or not cell exists.
        CellMarker(
            SceneHost& host, vsg::Group& cellNode, const CSMWorld::CellCoordinates& coordinates, bool cellExists);

        ~CellMarker();

        /// Faces the camera again, and keeps its size on screen.
        void update();
    };
}

#endif
