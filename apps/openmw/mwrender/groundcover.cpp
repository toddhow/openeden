#include "groundcover.hpp"

#include <algorithm>
#include <cmath>
#include <typeinfo>

#include <vsg/nodes/CullGroup.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/DepthSorted.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/utils/SharedObjects.h>

#include <components/animation/billboard.hpp>
#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadland.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/override.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/cullnode.hpp>
#include <components/vsgutil/distancelod.hpp>

namespace MWRender
{
    namespace
    {
        // OpenMW's unified groundcover alpha: an AlphaFunc GEQUAL 128/255 and no blending (Groundcover::Groundcover).
        constexpr float sAlphaTestCutoff = 128.f / 255.f;
        constexpr uint32_t sAlphaTestGreaterEqual = 6; // files/shaders/lib/material/alphatest.glsl
        // Margin for the wind and stomp movement, as OpenMW's InstancingVisitor added to each instance's bounds.
        constexpr float sInstanceRadiusMargin = 1.1f;
        // How far the camera moves before the cells in range are worked out again (an eighth of a cell).
        constexpr float sUpdateDistance = 1024.f;

        struct InstanceArrays
        {
            vsg::ref_ptr<vsg::vec4Array> offsets; // position relative to the chunk, and scale
            vsg::ref_ptr<vsg::vec3Array> rotations;
            uint32_t count = 0;
        };

        vsg::ref_ptr<vsg::Node> instantiate(const vsg::ref_ptr<vsg::Node>& node, const InstanceArrays& instances);

        // Copies a group of a groundcover model, keeping its type, with each child instantiated.
        template <class Group>
        vsg::ref_ptr<vsg::Node> copyGroup(const Group& group, const InstanceArrays& instances)
        {
            auto copy = vsg::ref_ptr{ new Group(group) };
            for (auto& child : copy->children)
                child = instantiate(child, instances);
            return copy;
        }

        // Copies a groundcover model for one chunk: each draw gets the chunk's instance arrays, placed before the
        // texture coordinates as the INSTANCE mode binds them (components/pipeline/graphics.cpp). The model's own
        // culling and sorting nodes are dropped: they bound one instance, and the chunk is culled as a whole.
        vsg::ref_ptr<vsg::Node> instantiate(const vsg::ref_ptr<vsg::Node>& node, const InstanceArrays& instances)
        {
            if (auto* draw = dynamic_cast<const vsg::VertexIndexDraw*>(node.get()))
            {
                auto copy = vsg::ref_ptr{ new vsg::VertexIndexDraw(*draw) };
                auto texcoords = std::find_if(copy->arrays.begin(), copy->arrays.end(),
                    [](const auto& array) { return array && dynamic_cast<const vsg::vec2Array*>(array->data.get()); });
                const auto at = copy->arrays.insert(texcoords, vsg::BufferInfo::create(instances.offsets));
                copy->arrays.insert(at + 1, vsg::BufferInfo::create(instances.rotations));
                copy->instanceCount = instances.count;
                return copy;
            }
            if (auto* sorted = dynamic_cast<const vsg::DepthSorted*>(node.get()))
                return instantiate(sorted->child, instances);
            if (auto* cull = dynamic_cast<const vsg::CullNode*>(node.get()))
                return instantiate(cull->child, instances);
            if (auto* stateGroup = dynamic_cast<const vsg::StateGroup*>(node.get()))
            {
                auto copy = vsg::ref_ptr{ new vsg::StateGroup(*stateGroup) };
                for (auto& child : copy->children)
                    child = instantiate(child, instances);
                return copy;
            }
            if (auto* switchNode = dynamic_cast<const vsg::Switch*>(node.get()))
            {
                auto copy = vsg::ref_ptr{ new vsg::Switch(*switchNode) };
                for (auto& child : copy->children)
                    child.node = instantiate(child.node, instances);
                return copy;
            }
            if (auto* transform = dynamic_cast<const Anim::Transform*>(node.get()))
                return copyGroup(*transform, instances);
            if (auto* billboard = dynamic_cast<const Anim::Billboard*>(node.get()))
                return copyGroup(*billboard, instances);
            if (auto* matrix = dynamic_cast<const vsg::MatrixTransform*>(node.get()))
                return copyGroup(*matrix, instances);
            if (auto* lod = dynamic_cast<const vsgUtil::DistanceLOD*>(node.get()))
                return copyGroup(*lod, instances);
            if (auto* group = dynamic_cast<const vsg::Group*>(node.get()))
            {
                if (typeid(*group) != typeid(vsg::Group))
                    Log(Debug::Warning) << "Groundcover: " << typeid(*group).name() << " drawn as a plain group";
                return copyGroup(*group, instances);
            }
            // Draws that cannot take instance arrays, such as a NIF particle system's, are left out: their
            // pipelines would read per-instance inputs nothing provides.
            if (dynamic_cast<const vsg::Command*>(node.get()))
                return vsg::Node::create();
            return node;
        }

    }

    Groundcover::Groundcover(
        const MWAnim::Context& context, const MWWorld::GroundcoverStore& store, float density, float renderingDistance)
        : mContext(context)
        , mStore(store)
        , mDensity(density)
        , mRenderingDistance(renderingDistance)
        , mRoot(vsg::Group::create())
    {
        auto override_ = vsg::ref_ptr{ new Pipeline::Override };
        override_->pipelineOptions = [](Pipeline::Options& options) {
            // Particle systems are not instanced (instantiate() leaves them out).
            if (!options.shader.hasMode(Pipeline::Mode::PARTICLE))
                options.shader.addMode(Pipeline::Mode::INSTANCE);
            options.blend = false;
            options.alphaTestMode = sAlphaTestGreaterEqual;
        };
        override_->materialData
            = [](Pipeline::Data::Material& material) { material.alphaTestCutoff = sAlphaTestCutoff; };
        auto nodeOptions = vsg::Options::create(*mContext.nodeOptions);
        // Its own cache: VSG matches shared reads by comparing options, and every Pipeline::Override compares equal,
        // so the first-person or inventory preview models (also read with an override) could otherwise be swapped.
        nodeOptions->sharedObjects = vsg::SharedObjects::create();
        override_->composite(*nodeOptions);
        mContext.nodeOptions = nodeOptions;
    }

    Groundcover::~Groundcover() = default;

    void Groundcover::clear()
    {
        mRoot->children.clear();
        mChunks.clear();
        mHasUpdated = false;
    }

    void Groundcover::hide()
    {
        mRoot->children.clear();
        mHasUpdated = false;
    }

    void Groundcover::update(const vsg::vec3& cameraPosition)
    {
        // Which cells are in range changes only gradually, so it is worked out again (allocating) only after the
        // camera has moved a little; the shader fades individual instances out at the exact distance.
        if (mHasUpdated
            && vsg::length(vsg::vec2(cameraPosition.x, cameraPosition.y) - mLastUpdatePosition) < sUpdateDistance)
            return;
        mHasUpdated = true;
        mLastUpdatePosition = vsg::vec2(cameraPosition.x, cameraPosition.y);

        const std::vector<std::pair<int, int>> wanted
            = groundcoverCellsInRange(cameraPosition.x, cameraPosition.y, mRenderingDistance);
        std::erase_if(
            mChunks, [&](const auto& chunk) { return !std::binary_search(wanted.begin(), wanted.end(), chunk.first); });
        for (const auto& cell : wanted)
            if (!mChunks.contains(cell))
                mChunks.emplace(cell, createChunk(cell.first, cell.second));

        mRoot->children.clear();
        for (const auto& [cell, chunk] : mChunks)
            if (chunk)
                mRoot->addChild(chunk);
    }

    vsg::ref_ptr<vsg::Node> Groundcover::createChunk(int cellX, int cellY) const
    {
        const vsg::vec2 center(cellX + 0.5f, cellY + 0.5f);
        const GroundcoverInstances instances = collectGroundcoverInstances(mStore, mDensity, 1.f, center);
        if (instances.empty())
            return {};

        const vsg::vec3 origin(center.x * ESM::Land::REAL_SIZE, center.y * ESM::Land::REAL_SIZE, 0.f);
        auto models = vsg::Group::create();
        vsg::dbox bounds;
        for (const auto& [model, entries] : instances)
        {
            vsg::ref_ptr<vsg::Node> prototype;
            try
            {
                prototype = mContext.readNode(std::string(model.value()));
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "Failed to load groundcover model '" << model.value() << "': " << e.what();
                continue;
            }
            if (!prototype)
                continue;

            InstanceArrays arrays;
            arrays.count = static_cast<uint32_t>(entries.size());
            arrays.offsets = vsg::vec4Array::create(arrays.count);
            arrays.rotations = vsg::vec3Array::create(arrays.count);
            const double radius = vsgUtil::getBounds(prototype).radius;
            for (uint32_t i = 0; i < arrays.count; ++i)
            {
                const ESM::Position& position = entries[i].position;
                const vsg::vec3 offset = vsg::vec3(position.pos[0], position.pos[1], position.pos[2]) - origin;
                (*arrays.offsets)[i] = vsg::vec4(offset, entries[i].scale);
                (*arrays.rotations)[i] = vsg::vec3(position.rot[0], position.rot[1], position.rot[2]);
                const double instanceRadius = radius * entries[i].scale * sInstanceRadiusMargin;
                bounds.add(vsg::dvec3(offset) - vsg::dvec3(instanceRadius, instanceRadius, instanceRadius));
                bounds.add(vsg::dvec3(offset) + vsg::dvec3(instanceRadius, instanceRadius, instanceRadius));
            }
            models->addChild(instantiate(prototype, arrays));
        }
        if (models->children.empty())
            return {};

        const vsg::dvec3 boundsCenter = (bounds.min + bounds.max) * 0.5;
        auto cull = vsg::CullGroup::create(vsg::dsphere(boundsCenter, vsg::length(bounds.max - boundsCenter)));
        cull->children = models->children;
        auto transform = vsg::MatrixTransform::create(vsg::translate(vsg::dvec3(origin)));
        transform->addChild(cull);
        if (!mContext.compileContext->compile(transform))
            return {};
        return transform;
    }
}
