#ifndef OPENCS_VIEW_CELLARROW_H
#define OPENCS_VIEW_CELLARROW_H

#include "tagbase.hpp"

#include <QString>

#include <vsg/maths/mat4.h>

#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>

#include "../../model/world/cellcoordinates.hpp"

namespace CSVRender
{
    class CellArrow;
    class SceneHost;
    struct WorldspaceHitResult;

    class CellArrowTag : public TagBase
    {
        CellArrow* mArrow;

    public:
        CellArrowTag(CellArrow* arrow);

        CellArrow* getCellArrow() const;

        QString getToolTip(bool hideBasics, const WorldspaceHitResult& hit) const override;
    };

    class CellArrow
    {
    public:
        enum Direction
        {
            Direction_North = 1,
            Direction_West = 2,
            Direction_South = 4,
            Direction_East = 8
        };

    private:
        // not implemented
        CellArrow(const CellArrow&);
        CellArrow& operator=(const CellArrow&);

        Direction mDirection;
        SceneHost& mHost;
        vsg::Group& mParentNode;
        vsg::ref_ptr<vsg::Switch> mBaseNode;
        vsg::ref_ptr<TagBase> mTag;
        CSMWorld::CellCoordinates mCoordinates;

        vsg::dmat4 transform() const;

        vsg::ref_ptr<vsg::Node> buildShape() const;

    public:
        CellArrow(
            SceneHost& host, vsg::Group& cellNode, Direction direction, const CSMWorld::CellCoordinates& coordinates);

        ~CellArrow();

        CSMWorld::CellCoordinates getCoordinates() const;

        Direction getDirection() const;
    };
}

#endif
