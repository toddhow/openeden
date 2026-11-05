#include "debuggeometry.hpp"

#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/state/ArrayState.h>

#include <components/debug/debuglog.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/vsgutil/compilecontext.hpp>

namespace Render
{
    namespace
    {
        // The default shader's unlit colour mode (files/shaders/lib/material/terms.glsl): the vertex colour is the
        // output colour, alpha included, as the OSG build drew debug geometry with lighting off.
        constexpr uint32_t sUnlitColorMode = 3;
    }

    DebugGeometry::DebugGeometry(const Pipeline::Builder& builder, vsg::ref_ptr<vsgUtil::CompileContext> compile,
        std::size_t lineVertexCapacity, std::size_t triangleVertexCapacity, bool depthTest)
        : mBuilder(builder)
        , mCompile(std::move(compile))
        , mRoot(vsg::Group::create())
        , mDepthTest(depthTest)
    {
        mLines.mCapacity = lineVertexCapacity;
        mTriangles.mCapacity = triangleVertexCapacity;
    }

    DebugGeometry::~DebugGeometry() = default;

    vsg::ref_ptr<vsg::Node> DebugGeometry::createBatch(Batch& batch, VkPrimitiveTopology topology)
    {
        batch.mVertices = vsg::vec3Array::create(batch.mCapacity);
        batch.mColors = vsg::vec4Array::create(batch.mCapacity);
        batch.mVertices->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        batch.mColors->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;

        Pipeline::Options options;
        options.shader.addMode(Pipeline::Mode::VERTEX);
        options.shader.addMode(Pipeline::Mode::COLOR);
        options.colorMode = sUnlitColorMode;
        options.primitiveTopology = topology;
        options.cullMode = VK_CULL_MODE_NONE;
        options.blend = true;
        // Destination alpha is left as the scene wrote it: later passes read it.
        options.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        options.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        options.depthWrite = false;
        options.depthTest = mDepthTest;
        // Drawn slightly in front of the surfaces they outline, as the OSG build did with a polygon offset.
        options.depthBias = true;

        batch.mDraw = vsg::VertexDraw::create();
        batch.mDraw->assignArrays({ batch.mVertices, batch.mColors });
        batch.mDraw->vertexCount = 0;
        batch.mDraw->instanceCount = 1;

        auto stateGroup = vsg::StateGroup::create();
        stateGroup->stateCommands = { mBuilder.graphics->getOrCreate(options) };
        // Debug geometry takes no part in intersection tests.
        stateGroup->prototypeArrayState = vsg::NullArrayState::create();
        stateGroup->addChild(batch.mDraw);
        return stateGroup;
    }

    void DebugGeometry::create()
    {
        mContents = vsg::Group::create();
        // Triangles first, so lines outlining them draw over them.
        if (mTriangles.mCapacity > 0)
            mContents->addChild(createBatch(mTriangles, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST));
        if (mLines.mCapacity > 0)
            mContents->addChild(createBatch(mLines, VK_PRIMITIVE_TOPOLOGY_LINE_LIST));
        mCompile->compile(mContents);
    }

    void DebugGeometry::setEnabled(bool enabled)
    {
        if (enabled == mEnabled)
            return;
        std::lock_guard lock(mMutex);
        mEnabled = enabled;
        if (enabled)
        {
            if (!mContents)
                create();
            mRoot->children = { mContents };
        }
        else
        {
            mRoot->children.clear();
            for (Batch* batch : { &mLines, &mTriangles })
            {
                batch->mCount = 0;
                if (batch->mDraw)
                    batch->mDraw->vertexCount = 0;
            }
        }
    }

    void DebugGeometry::begin()
    {
        std::lock_guard lock(mMutex);
        mLines.mCount = 0;
        mTriangles.mCount = 0;
    }

    bool DebugGeometry::reserve(Batch& batch, std::size_t vertices, const char* name)
    {
        if (!mEnabled || !batch.mVertices)
            return false;
        if (batch.mCount + vertices <= batch.mCapacity)
            return true;
        if (!batch.mReportedFull)
        {
            Log(Debug::Warning) << "Debug geometry is full (" << batch.mCapacity / vertices << " " << name
                                << "); drawing fewer";
            batch.mReportedFull = true;
        }
        return false;
    }

    void DebugGeometry::addLine(const vsg::vec3& from, const vsg::vec3& to, const vsg::vec4& color)
    {
        std::lock_guard lock(mMutex);
        if (!reserve(mLines, 2, "lines"))
            return;
        auto& vertices = *mLines.mVertices;
        auto& colors = *mLines.mColors;
        vertices[mLines.mCount] = from;
        vertices[mLines.mCount + 1] = to;
        colors[mLines.mCount] = color;
        colors[mLines.mCount + 1] = color;
        mLines.mCount += 2;
    }

    void DebugGeometry::addTriangle(const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c, const vsg::vec4& color)
    {
        std::lock_guard lock(mMutex);
        if (!reserve(mTriangles, 3, "triangles"))
            return;
        auto& vertices = *mTriangles.mVertices;
        auto& colors = *mTriangles.mColors;
        vertices[mTriangles.mCount] = a;
        vertices[mTriangles.mCount + 1] = b;
        vertices[mTriangles.mCount + 2] = c;
        for (std::size_t i = 0; i < 3; ++i)
            colors[mTriangles.mCount + i] = color;
        mTriangles.mCount += 3;
    }

    void DebugGeometry::end()
    {
        std::lock_guard lock(mMutex);
        if (!mEnabled)
            return;
        for (Batch* batch : { &mLines, &mTriangles })
        {
            if (!batch->mDraw)
                continue;
            batch->mDraw->vertexCount = static_cast<uint32_t>(batch->mCount);
            batch->mVertices->dirty();
            batch->mColors->dirty();
        }
    }
}
