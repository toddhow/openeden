#include "groundcover.hpp"

#include "groundcoverbuild.hpp"

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
#include <components/vsgutil/operation.hpp>

#include <vsg/threading/OperationThreads.h>

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
        // Built chunks kept after their cell leaves range, so walking back does not rebuild them; chosen here, about
        // a ring of cells at the default rendering distance (OpenMW expired its chunk cache by time instead).
        constexpr std::size_t sCachedChunks = 32;

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

        // Builds one cell's chunk, not yet compiled; null when the cell has no groundcover. Runs on an operation
        // thread: it reads the store, the content files and the models, and touches nothing the main thread writes.
        vsg::ref_ptr<vsg::Node> buildChunk(const MWAnim::Context& context, const MWWorld::GroundcoverStore& store,
            float density, const std::pair<int, int>& cell)
        {
            const vsg::vec2 center(cell.first + 0.5f, cell.second + 0.5f);
            const GroundcoverInstances instances = collectGroundcoverInstances(store, density, 1.f, center);
            if (instances.empty())
                return {};

            const vsg::vec3 origin(center.x * ESM::Land::REAL_SIZE, center.y * ESM::Land::REAL_SIZE, 0.f);
            return buildGroundcoverChunk(context, instances, origin);
        }
    }

    // Task 0153: the part of the chunk build that does not depend on where the instances come from (the Morrowind
    // store, or a game pack's grass), moved out of buildChunk unchanged so the neutral grass shares it.
    vsg::ref_ptr<vsg::Node> buildGroundcoverChunk(
        const MWAnim::Context& context, const GroundcoverInstances& instances, const vsg::vec3& origin)
    {
        {
            auto models = vsg::Group::create();
            vsg::dbox bounds;
            for (const auto& [model, entries] : instances)
            {
                vsg::ref_ptr<vsg::Node> prototype;
                try
                {
                    prototype = context.readNode(std::string(model.value()));
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
            return transform;
        }
    }

    MWAnim::Context makeGroundcoverContext(const MWAnim::Context& base)
    {
        MWAnim::Context result = base;
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
        auto nodeOptions = vsg::Options::create(*result.nodeOptions);
        // Its own cache: VSG matches shared reads by comparing options, and every Pipeline::Override compares equal,
        // so the first-person or inventory preview models (also read with an override) could otherwise be swapped.
        nodeOptions->sharedObjects = vsg::SharedObjects::create();
        override_->composite(*nodeOptions);
        result.nodeOptions = nodeOptions;
        return result;
    }

    // One cell's chunk being built on an operation thread; the main thread reads chunk only once done is set.
    struct Groundcover::ChunkBuild : public vsgUtil::Operation
    {
        const MWAnim::Context& context;
        const MWWorld::GroundcoverStore& store;
        float density;
        std::pair<int, int> cell;
        vsg::ref_ptr<vsg::Node> chunk;

        ChunkBuild(const MWAnim::Context& in_context, const MWWorld::GroundcoverStore& in_store, float in_density,
            const std::pair<int, int>& in_cell)
            : context(in_context)
            , store(in_store)
            , density(in_density)
            , cell(in_cell)
        {
        }

        void operate() override
        {
            try
            {
                chunk = buildChunk(context, store, density, cell);
            }
            catch (const std::exception& e)
            {
                // The cell is then treated as having no groundcover, and not built again.
                Log(Debug::Error) << "Failed to build groundcover for cell " << cell.first << ", " << cell.second
                                  << ": " << e.what();
            }
        }
    };

    Groundcover::Groundcover(
        const MWAnim::Context& context, const MWWorld::GroundcoverStore& store, float density, float renderingDistance)
        : mContext(makeGroundcoverContext(context))
        , mStore(store)
        , mDensity(density)
        , mRenderingDistance(renderingDistance)
        , mThreads(vsg::OperationThreads::create(1))
        , mRoot(vsg::Group::create())
        , mTracker(sCachedChunks)
    {
    }

    Groundcover::~Groundcover()
    {
        waitForBuilds();
        setShownChunks({});
    }

    void Groundcover::waitForBuilds()
    {
        for (const auto& build : mBuilds)
            build->wait();
    }

    void Groundcover::hide()
    {
        setShownChunks({});
        mHasUpdated = false;
    }

    void Groundcover::update(const vsg::vec3& cameraPosition)
    {
        bool changed = collectBuilds();

        // Which cells are in range changes only gradually, so it is worked out again (allocating) only after the
        // camera has moved a little; the shader fades individual instances out at the exact distance.
        if (!mHasUpdated
            || vsg::length(vsg::vec2(cameraPosition.x, cameraPosition.y) - mLastUpdatePosition) >= sUpdateDistance)
        {
            changed = true;
            mHasUpdated = true;
            mLastUpdatePosition = vsg::vec2(cameraPosition.x, cameraPosition.y);
            const std::vector<std::pair<int, int>> wanted
                = groundcoverCellsInRange(cameraPosition.x, cameraPosition.y, mRenderingDistance);
            for (const auto& cell : mTracker.setWanted(wanted))
            {
                auto build = vsg::ref_ptr{ new ChunkBuild(mContext, mStore, mDensity, cell) };
                mBuilds.push_back(build);
                mThreads->add(build);
            }
        }
        if (!changed)
            return;

        vsg::Group::Children chunks;
        for (const auto& [cell, chunk] : mTracker.shown())
            if (chunk)
                chunks.push_back(chunk);
        setShownChunks(std::move(chunks));
    }

    bool Groundcover::collectBuilds()
    {
        return std::erase_if(mBuilds, [this](const vsg::ref_ptr<ChunkBuild>& build) {
            if (!build->done)
                return false;
            vsg::ref_ptr<vsg::Node> chunk = build->chunk;
            if (chunk && !mContext.compileContext->compile(chunk))
                chunk = {};
            mTracker.finish(build->cell, chunk);
            return true;
        }) > 0;
    }

    void Groundcover::setShownChunks(vsg::Group::Children chunks)
    {
        for (const auto& child : mRoot->children)
            if (std::find(chunks.begin(), chunks.end(), child) == chunks.end())
                mContext.compileContext->detach(child);
        mRoot->children = std::move(chunks);
    }
}
