#include "cellarrow.hpp"

#include <algorithm>

#include <vsg/maths/transform.h>
#include <vsg/nodes/MatrixTransform.h>

#include "../../model/prefs/state.hpp"

#include <apps/opencs/model/prefs/shortcutmanager.hpp>
#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/view/render/tagbase.hpp>

#include <components/misc/constants.hpp>

#include <components/misc/vsgmath.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include "colouredgeometry.hpp"
#include "mask.hpp"
#include "picking.hpp"
#include "scenehost.hpp"

namespace CSVRender
{
    struct WorldspaceHitResult;
}

CSVRender::CellArrowTag::CellArrowTag(CellArrow* arrow)
    : TagBase(Mask_CellArrow)
    , mArrow(arrow)
{
}

CSVRender::CellArrow* CSVRender::CellArrowTag::getCellArrow() const
{
    return mArrow;
}

QString CSVRender::CellArrowTag::getToolTip(bool hideBasics, const WorldspaceHitResult& /*hit*/) const
{
    QString text("Direction: ");

    switch (mArrow->getDirection())
    {
        case CellArrow::Direction_North:
            text += "North";
            break;
        case CellArrow::Direction_West:
            text += "West";
            break;
        case CellArrow::Direction_South:
            text += "South";
            break;
        case CellArrow::Direction_East:
            text += "East";
            break;
    }

    if (!hideBasics)
    {
        text +=
            "<p>"
            "Modify which cells are shown"
            "<ul><li>{scene-edit-primary}: Add cell in given direction</li>"
            "<li>{scene-edit-secondary}: Add cell and remove old cell</li>"
            "<li>{scene-select-primary}: Add cells in given direction</li>"
            "<li>{scene-select-secondary}: Add cells and remove old cells</li>"
            "<li>{scene-load-cam-cell}: Load cell where camera is located</li>"
            "<li>{scene-load-cam-eastcell}: Load cell to east</li>"
            "<li>{scene-load-cam-northcell}: Load cell to north</li>"
            "<li>{scene-load-cam-westcell}: Load cell to west</li>"
            "<li>{scene-load-cam-southcell}: Load cell to south</li>"
            "</ul>";
    }

    return CSMPrefs::State::get().getShortcutManager().processToolTip(text);
}

vsg::dmat4 CSVRender::CellArrow::transform() const
{
    // position
    const int cellSize = Constants::CellSizeInUnits;
    const int offset = cellSize / 2 + 600;

    int x = mCoordinates.getX() * cellSize + cellSize / 2;
    int y = mCoordinates.getY() * cellSize + cellSize / 2;

    double xr = 0;
    double yr = 0;
    double zr = 0;

    const double angle = vsg::radians(90.0);

    switch (mDirection)
    {
        case Direction_North:
            y += offset;
            xr = -angle;
            zr = angle;
            break;
        case Direction_West:
            x -= offset;
            yr = -angle;
            break;
        case Direction_South:
            y -= offset;
            xr = angle;
            zr = angle;
            break;
        case Direction_East:
            x += offset;
            yr = angle;
            break;
    };

    // osg::Quat's zr * yr * xr turns by x first, then y, then z; so do these matrix products.
    return vsg::translate(static_cast<double>(x), static_cast<double>(y), 0.0) * vsg::rotate(zr, vsg::dvec3(0, 0, 1))
        * vsg::rotate(yr, vsg::dvec3(0, 1, 0)) * vsg::rotate(xr, vsg::dvec3(1, 0, 0));
}

vsg::ref_ptr<vsg::Node> CSVRender::CellArrow::buildShape() const
{
    const float arrowWidth = 2700;
    const float arrowLength = 1350;
    const float arrowHeight = 300;

    auto vertices = vsg::vec3Array::create(12);
    auto colours = vsg::vec4Array::create(12);
    int v = 0;
    for (int i2 = 0; i2 < 2; ++i2)
        for (int i = 0; i < 2; ++i)
        {
            const float height = i ? -arrowHeight / 2 : arrowHeight / 2;
            const vsg::vec4 colour = i2 ? vsg::vec4(0.08f, 0.44f, 0.7f, 1.f) : vsg::vec4(0.11f, 0.6f, 0.95f, 1.f);
            for (const vsg::vec3& point : { vsg::vec3(height, -arrowWidth / 2, 0), vsg::vec3(height, arrowWidth / 2, 0),
                     vsg::vec3(height, 0, arrowLength) })
            {
                vertices->at(v) = point;
                colours->at(v) = colour;
                ++v;
            }
        }

    // top, bottom, back and sides
    auto indices
        = vsg::ushortArray::create({ 0, 1, 2, 5, 4, 3, 9, 10, 7, 9, 7, 6, 6, 8, 11, 6, 11, 9, 10, 11, 8, 10, 8, 7 });

    return createColouredGeometry(
        *mHost.resources().builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, vertices, colours, indices);
}

CSVRender::CellArrow::CellArrow(
    SceneHost& host, vsg::Group& cellNode, Direction direction, const CSMWorld::CellCoordinates& coordinates)
    : mDirection(direction)
    , mHost(host)
    , mParentNode(cellNode)
    , mTag(new CellArrowTag(this))
    , mCoordinates(coordinates)
{
    auto matrix = vsg::MatrixTransform::create(transform());
    matrix->addChild(buildShape());
    mBaseNode = SceneHost::maskNode(Mask_CellArrow, matrix);
    Picking::setTag(*mBaseNode, mTag);
    mHost.compileContext()->compile(mBaseNode);
    mParentNode.addChild(mBaseNode);
}

CSVRender::CellArrow::~CellArrow()
{
    auto& children = mParentNode.children;
    children.erase(std::remove(children.begin(), children.end(), vsg::ref_ptr<vsg::Node>(mBaseNode)), children.end());
    mHost.compileContext()->detach(mBaseNode);
}

CSMWorld::CellCoordinates CSVRender::CellArrow::getCoordinates() const
{
    return mCoordinates;
}

CSVRender::CellArrow::Direction CSVRender::CellArrow::getDirection() const
{
    return mDirection;
}
