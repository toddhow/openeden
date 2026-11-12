#include "water.hpp"

#include <algorithm>

#include <vsg/commands/Draw.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/utils/SharedObjects.h>

#include <components/animation/transform.hpp>
#include <components/pipeline/bindings.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/sets.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/share.hpp>

namespace MWRender
{

    Water::Water(Resource::ResourceSystem* resourceSystem)
    {
        mTransform = new Anim::Transform;

        Pipeline::Options pipelineOptions{ .shader = { "water", { Pipeline::modes({ Pipeline::Mode::BUMP_MAP }) } },
            .primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            .depthWrite = false,
            .cullMode = VK_CULL_MODE_NONE };

        auto pipeline = resourceSystem->builder->graphics->getOrCreate(pipelineOptions);
        auto layout = pipeline->pipeline->layout;

        auto image = vsgUtil::readImage("water_nm.png", resourceSystem->textureOptions);
        auto sampler = resourceSystem->builder->createSampler();
        vsgUtil::share_if(resourceSystem->textureOptions->sharedObjects, sampler);
        auto bumpMap = vsg::DescriptorImage::create(sampler, image, Pipeline::Descriptors::BUMP_UNIT);
        auto bds = vsg::BindDescriptorSet::create(
            VK_PIPELINE_BIND_POINT_GRAPHICS, layout, Pipeline::TEXTURE_SET, vsg::Descriptors{ bumpMap });

        auto sg = vsg::StateGroup::create();
        sg->stateCommands = { pipeline, bds };
        sg->children = { vsg::Draw::create(4, 1, 0, 0) };

        mTransform->addChild(sg);

        // The cell-bounded surface: the same shader and bump map with WATER_CELL, which places a unit quad by the node's
        // transform (water/main.vert). The pipeline is made here so that it is compiled with the rest of the water.
        Pipeline::Options cellOptions = pipelineOptions;
        cellOptions.shader = { "water", { Pipeline::modes({ Pipeline::Mode::BUMP_MAP, Pipeline::Mode::WATER_CELL }) } };
        auto cellPipeline = resourceSystem->builder->graphics->getOrCreate(cellOptions);
        auto cellBds = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, cellPipeline->pipeline->layout,
            Pipeline::TEXTURE_SET, vsg::Descriptors{ bumpMap });
        mCellGroup = vsg::StateGroup::create();
        mCellGroup->stateCommands = { cellPipeline, cellBds };
        mCellDraw = vsg::Draw::create(4, 1, 0, 0);

        mGlobal = vsg::Switch::create();
        mGlobal->addChild(vsg::MASK_ALL, mTransform);
        auto root = vsg::Group::create();
        root->children = { mGlobal, mCellGroup };
        mNode = root;
    }

    Water::~Water() {}

    void Water::setHeight(float h)
    {
        mTransform->translation.z = h;
    }

    void Water::setGlobalVisible(bool visible)
    {
        mGlobalVisible = visible;
        mGlobal->setAllChildren(visible);
    }

    bool Water::isUnderwater(const vsg::vec3& pos) const
    {
        if (!mCells.empty() && !mGlobalVisible)
            return mCells.isUnderwater(pos.x, pos.y, pos.z);
        return pos.z < mTransform->translation.z;
    }

    void Water::setCellWater(std::int32_t x, std::int32_t y, float size, float height)
    {
        if (!mCells.set(x, y, size, height))
            return;
        auto found = std::find_if(mCellNodes.begin(), mCellNodes.end(),
            [&](const CellNode& n) { return n.mX == x && n.mY == y; });
        if (found == mCellNodes.end())
        {
            CellNode node{ x, y, vsg::ref_ptr<Anim::Transform>(new Anim::Transform) };
            node.mTransform->addChild(mCellDraw);
            mCellGroup->addChild(node.mTransform);
            mCellNodes.push_back(std::move(node));
            found = mCellNodes.end() - 1;
        }
        // The unit quad's south-west corner is at the transform's translation; the scale makes it a cell.
        found->mTransform->translation = { x * size, y * size, height };
        found->mTransform->scale = { size, size, 1.f };
    }

    void Water::removeCellWater(std::int32_t x, std::int32_t y)
    {
        if (!mCells.remove(x, y))
            return;
        const auto found = std::find_if(mCellNodes.begin(), mCellNodes.end(),
            [&](const CellNode& n) { return n.mX == x && n.mY == y; });
        if (found == mCellNodes.end())
            return;
        auto& children = mCellGroup->children;
        const vsg::Node* target = found->mTransform.get();
        children.erase(std::remove_if(children.begin(), children.end(),
                           [target](const vsg::ref_ptr<vsg::Node>& child) { return child.get() == target; }),
            children.end());
        mCellNodes.erase(found);
    }
}
