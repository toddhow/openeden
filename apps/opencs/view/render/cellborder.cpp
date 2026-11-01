#include "cellborder.hpp"

#include <algorithm>

#include <vsg/maths/transform.h>

#include <components/esm3/loadland.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include "colouredgeometry.hpp"
#include "mask.hpp"
#include "scenehost.hpp"

#include "../../model/world/cellcoordinates.hpp"

namespace
{
    constexpr int CellSize = ESM::Land::REAL_SIZE;
    // Above the land by this much, so that it does not sink into it.
    constexpr double BorderHeight = 10;

    /*
        The number of vertices per cell border is equal to the number of vertices per edge
        minus the duplicated corner vertices, plus one to close the loop.
    */
    constexpr unsigned VertexCount = (ESM::Land::LAND_SIZE * 4) - 4 + 1;
}

CSVRender::CellBorder::CellBorder(SceneHost& host, vsg::Group& cellNode, const CSMWorld::CellCoordinates& coords)
    : mHost(host)
    , mParentNode(cellNode)
{
    mBaseNode = vsg::MatrixTransform::create(vsg::translate(
        static_cast<double>(coords.getX() * CellSize), static_cast<double>(coords.getY() * CellSize), BorderHeight));
    mMaskNode = SceneHost::maskNode(Mask_CellBorder, mBaseNode);
    mHost.compileContext()->compile(mMaskNode);
    mParentNode.addChild(mMaskNode);
}

CSVRender::CellBorder::~CellBorder()
{
    auto& children = mParentNode.children;
    children.erase(std::remove(children.begin(), children.end(), mMaskNode), children.end());
    mHost.compileContext()->detach(mMaskNode);
}

void CSVRender::CellBorder::buildShape(const ESM::Land& esmLand)
{
    const ESM::Land::LandData* landData = esmLand.getLandData(ESM::Land::DATA_VHGT);

    auto vertices = vsg::vec3Array::create(VertexCount);
    unsigned count = 0;
    const auto add = [&](int x, int y) {
        const float height
            = landData ? landData->mHeights[landIndex(x, y)] : static_cast<float>(ESM::Land::DEFAULT_HEIGHT);
        vertices->at(count++) = vsg::vec3(scaleToWorld(x), scaleToWorld(y), height);
    };

    int x = 0;
    int y = 0;

    // Traverse the cell border counter-clockwise starting at the SW corner vertex (0, 0).
    // Each loop starts at a corner vertex and ends right before the next corner vertex.
    for (; x < ESM::Land::LAND_SIZE - 1; ++x)
        add(x, y);
    x = ESM::Land::LAND_SIZE - 1;
    for (; y < ESM::Land::LAND_SIZE - 1; ++y)
        add(x, y);
    y = ESM::Land::LAND_SIZE - 1;
    for (; x > 0; --x)
        add(x, y);
    x = 0;
    for (; y > 0; --y)
        add(x, y);
    vertices->at(count) = vertices->at(0);

    auto colours = vsg::vec4Array::create(VertexCount, vsg::vec4(0.f, 0.5f, 0.f, 1.f));

    auto geometry
        = createColouredGeometry(*mHost.resources().builder, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, vertices, colours);
    mHost.compileContext()->compile(geometry);
    if (mBorderGeometry)
        mHost.compileContext()->detach(mBorderGeometry);
    mBaseNode->children = { geometry };
    mBorderGeometry = geometry;
}

size_t CSVRender::CellBorder::landIndex(int x, int y)
{
    return static_cast<size_t>(y) * ESM::Land::LAND_SIZE + x;
}

float CSVRender::CellBorder::scaleToWorld(int value)
{
    return (CellSize + 128) * (float)value / ESM::Land::LAND_SIZE;
}
