#include "overlaylines.hpp"

#include <algorithm>

#include <vsg/nodes/VertexDraw.h>

#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/setbin.hpp>

#include "colouredgeometry.hpp"
#include "scenehost.hpp"

namespace
{
    constexpr std::size_t sMinimumCapacity = 256;
}

CSVRender::OverlayLines::OverlayLines(SceneHost& host)
    : mHost(host)
    , mHostAlive(host.aliveToken())
    , mRoot(vsg::Group::create())
{
    mHost.compileContext()->compile(mRoot);
    mHost.contentRoot().addChild(mRoot);
}

CSVRender::OverlayLines::~OverlayLines()
{
    if (mHostAlive.expired())
        return;
    auto& children = mHost.contentRoot().children;
    children.erase(std::remove(children.begin(), children.end(), vsg::ref_ptr<vsg::Node>(mRoot)), children.end());
    mHost.compileContext()->detach(mRoot);
    mHost.requestRedraw();
}

void CSVRender::OverlayLines::reserve(std::size_t count)
{
    if (mVertices && mVertices->size() >= count)
        return;

    const std::size_t capacity = std::max(sMinimumCapacity, std::max(count, mVertices ? mVertices->size() * 2 : 0));
    mVertices = vsg::vec3Array::create(capacity);
    mColours = vsg::vec4Array::create(capacity);
    mVertices->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
    mColours->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;

    mDraw = vsg::VertexDraw::create();
    mDraw->assignArrays({ mVertices, mColours });
    mDraw->vertexCount = 0;
    mDraw->instanceCount = 1;

    ColouredOptions options;
    options.mNoDepth = true;
    options.mBlend = true;
    options.mPickable = false;
    auto node = vsgUtil::createBinSetter(SceneHost::sBinOverlay,
        wrapColouredDraw(*mHost.resources().builder, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, mDraw, options));
    mHost.compileContext()->compile(node);
    if (mNode)
    {
        mHost.compileContext()->detach(mNode);
        mRoot->children.clear();
    }
    mNode = node;
    mRoot->addChild(mNode);
}

void CSVRender::OverlayLines::set(const std::vector<vsg::vec3>& vertices, const vsg::vec4& colour)
{
    reserve(vertices.size());
    std::copy(vertices.begin(), vertices.end(), mVertices->begin());
    std::fill_n(mColours->begin(), vertices.size(), colour);
    mDraw->vertexCount = static_cast<uint32_t>(vertices.size());
    mVertices->dirty();
    mColours->dirty();
    mHost.requestRedraw();
}

void CSVRender::OverlayLines::set(const std::vector<vsg::vec3>& vertices, const std::vector<vsg::vec4>& colours)
{
    reserve(vertices.size());
    std::copy(vertices.begin(), vertices.end(), mVertices->begin());
    std::copy(colours.begin(), colours.end(), mColours->begin());
    mDraw->vertexCount = static_cast<uint32_t>(vertices.size());
    mVertices->dirty();
    mColours->dirty();
    mHost.requestRedraw();
}

void CSVRender::OverlayLines::clear()
{
    if (mDraw && mDraw->vertexCount != 0)
    {
        mDraw->vertexCount = 0;
        mHost.requestRedraw();
    }
}
