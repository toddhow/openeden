#include "meshscene.hpp"

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/Shape/DecoratedShape.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
// clang-format on

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <vsg/core/Visitor.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/Transform.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/ArrayState.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/InputAssemblyState.h>

#include <components/animation/controllermap.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/create.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/texture.hpp>
#include <components/nifscene/build.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/options.hpp>
#include <components/render/debuggeometry.hpp>
#include <components/resource/niffilemanager.hpp>
#include <components/resource/physicsshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/name.hpp>
#include <components/vsgutil/setbin.hpp>

#include <apps/opencs/model/mesh/cellscene.hpp>
#include <apps/opencs/model/mesh/meshoverlay.hpp>
#include <apps/opencs/model/mesh/scenemodel.hpp>
#include <apps/opencs/model/mesh/skyrimcellsource.hpp>
#include <apps/opencs/model/mesh/texturecolour.hpp>
#include <apps/opencs/model/physics/joltruntime.hpp>

#include "picking.hpp"
#include "scenehost.hpp"

namespace CSVRender
{
    namespace
    {
        // The default shader's unlit colour mode (components/render/debuggeometry.cpp sUnlitColorMode): the vertex
        // colour is the output colour.
        constexpr uint32_t sUnlitColorMode = 3;
        // Limit for the overlay's vertex capacity (28 bytes a vertex): a model with more lines draws fewer.
        constexpr std::size_t sMaxOverlayVertices = 4000000;
        // Where the flat shading's light comes from, in the model's frame (towards the light).
        const std::array<float, 3> sFlatLightDirection{ 0.42f, -0.55f, 0.72f };
        const std::array<float, 4> sWireColour{ 0.85f, 0.88f, 0.92f, 1.f };
        const std::array<float, 4> sNormalColour{ 0.3f, 0.9f, 0.95f, 1.f };
        const std::array<float, 4> sBoundsColour{ 1.f, 0.65f, 0.15f, 1.f };
        const std::array<float, 4> sSelectionColour{ 1.f, 0.95f, 0.2f, 1.f };
        const std::array<float, 4> sSkeletonColour{ 0.95f, 0.35f, 0.95f, 1.f };
        const std::array<float, 4> sNoColourGrey{ 0.72f, 0.72f, 0.72f, 1.f };

        // A texture file read through the virtual file system for the colour of a Skyrim shape (task 0086): at most
        // maxBytes + 1 bytes, so that the cache can tell a file that is too long. Nothing when the file is not there or
        // cannot be read.
        std::optional<std::vector<unsigned char>> readTextureFile(
            const VFS::Manager& vfs, const std::string& key, std::uint64_t maxBytes)
        {
            try
            {
                const VFS::Path::Normalized path(key);
                if (!vfs.exists(path))
                    return std::nullopt;
                const auto stream = vfs.get(path);
                if (!stream)
                    return std::nullopt;
                std::vector<unsigned char> bytes;
                std::vector<char> chunk(1 << 16);
                while (bytes.size() <= maxBytes && *stream)
                {
                    stream->read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                    const auto got = static_cast<std::size_t>(stream->gcount());
                    if (got == 0)
                        break;
                    bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
                }
                return bytes;
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        }

        void logTextureStats(const std::string& what, const CSMMesh::TextureColourStats& t)
        {
            Log(Debug::Info) << "Cell viewer: " << what << ": textures " << t.requested << " asked for, " << t.loaded
                             << " loaded, " << t.missing << " missing, " << t.unsupported << " unreadable, "
                             << t.overBudget << " over budget, " << t.bytesRead << " bytes read; " << t.shapesTextured
                             << " shapes coloured by texture, " << t.shapesFallback << " kept a flat colour";
        }

        std::array<float, 3> transformPoint(const vsg::dmat4& m, const std::array<float, 3>& p)
        {
            return { static_cast<float>(m[0][0] * p[0] + m[1][0] * p[1] + m[2][0] * p[2] + m[3][0]),
                static_cast<float>(m[0][1] * p[0] + m[1][1] * p[1] + m[2][1] * p[2] + m[3][1]),
                static_cast<float>(m[0][2] * p[0] + m[1][2] * p[1] + m[2][2] * p[2] + m[3][2]) };
        }

        std::array<float, 3> transformDirection(const vsg::dmat4& m, const std::array<float, 3>& n)
        {
            std::array<float, 3> r{ static_cast<float>(m[0][0] * n[0] + m[1][0] * n[1] + m[2][0] * n[2]),
                static_cast<float>(m[0][1] * n[0] + m[1][1] * n[1] + m[2][1] * n[2]),
                static_cast<float>(m[0][2] * n[0] + m[1][2] * n[1] + m[2][2] * n[2]) };
            const float length = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            if (length > 0.f)
                for (float& v : r)
                    v /= length;
            return r;
        }

        const vsg::vec3Array* asVec3(const vsg::BufferInfo& info)
        {
            return dynamic_cast<const vsg::vec3Array*>(info.data.get());
        }
        const vsg::vec4Array* asVec4(const vsg::BufferInfo& info)
        {
            return dynamic_cast<const vsg::vec4Array*>(info.data.get());
        }

        // Tangents (components/vsgadapters/nif/nif.cpp createTangents): a unit direction and a sign in w.
        bool looksLikeTangents(const vsg::vec4Array& array)
        {
            if (array.size() == 0)
                return false;
            for (const vsg::vec4& v : array)
            {
                const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                if (std::abs(length - 1.f) > 0.05f || std::abs(std::abs(v.w) - 1.f) > 0.01f)
                    return false;
            }
            return true;
        }

        // Moeller and Trumbore, on either side of the triangle. Returns the ratio along the segment, or a negative.
        double hitTriangle(const vsg::dvec3& start, const vsg::dvec3& direction, const std::array<float, 3>& a,
            const std::array<float, 3>& b, const std::array<float, 3>& c)
        {
            const vsg::dvec3 v0(a[0], a[1], a[2]);
            const vsg::dvec3 edge1 = vsg::dvec3(b[0], b[1], b[2]) - v0;
            const vsg::dvec3 edge2 = vsg::dvec3(c[0], c[1], c[2]) - v0;
            const vsg::dvec3 p = vsg::cross(direction, edge2);
            const double determinant = vsg::dot(edge1, p);
            if (std::abs(determinant) < 1e-12)
                return -1.0;
            const vsg::dvec3 s = start - v0;
            const double u = vsg::dot(s, p) / determinant;
            const vsg::dvec3 q = vsg::cross(s, edge1);
            const double v = vsg::dot(direction, q) / determinant;
            const double ratio = vsg::dot(edge2, q) / determinant;
            if (u < 0.0 || v < 0.0 || u + v > 1.0 || ratio < 0.0 || ratio > 1.0)
                return -1.0;
            return ratio;
        }
    }

    /// Walks the model's scene graph once after loading: fills the MeshTree, the per-node info and the shapes.
    /// Named groups and every transform become tree nodes; each draw of a vertex array becomes a "Geometry" leaf.
    class MeshCollector : public vsg::Visitor
    {
    public:
        MeshCollector(MeshScene& scene)
            : mScene(scene)
        {
            mMatrix.emplace_back();
            mNode.push_back(CSMMesh::kNoNode);
            mGroup.push_back(nullptr);
        }

        void apply(vsg::Node& node) override { node.traverse(*this); }

        void apply(vsg::Group& group) override
        {
            const std::string& name = vsgUtil::getName(group);
            if (name.empty())
            {
                mGroup.push_back(&group);
                group.traverse(*this);
                mGroup.pop_back();
                return;
            }
            enter(group, name, "Group", mMatrix.back());
            group.traverse(*this);
            leave();
        }

        void apply(vsg::Transform& transform) override
        {
            const vsg::dmat4 world = transform.transform(mMatrix.back());
            enter(transform, vsgUtil::getName(transform), "Transform", world);
            transform.traverse(*this);
            leave();
        }

        void apply(vsg::Switch& node) override
        {
            // A switch is not a vsg::Group: its children cannot be hidden by wrapping them.
            std::string name = vsgUtil::getName(node);
            if (name.empty())
                node.getValue("switch", name);
            enter(node, name, "Switch", mMatrix.back(), false);
            mGroup.back() = nullptr;
            node.traverse(*this);
            leave();
        }

        void apply(vsg::StateGroup& group) override
        {
            bool hasGeometry = false;
            for (const auto& child : group.children)
                if (auto* draw = dynamic_cast<vsg::VertexIndexDraw*>(child.get()))
                    hasGeometry = addShape(group, *draw) || hasGeometry;
            if (!hasGeometry)
                group.traverse(*this);
        }

    private:
        void enter(
            vsg::Node& node, const std::string& name, const char* type, const vsg::dmat4& world, bool canHide = true)
        {
            const CSMMesh::NodeId id = mScene.mTree.addNode(mNode.back(), name, type);
            if (mScene.mInfos.size() <= static_cast<std::size_t>(id))
                mScene.mInfos.resize(id + 1);
            auto& info = mScene.mInfos[id];
            info.mNode = &node;
            info.mParent = canHide ? mGroup.back() : nullptr;
            info.mWorld = world;
            auto& treeNode = mScene.mTree.mutableNode(id);
            treeNode.translation = { world[3][0], world[3][1], world[3][2] };
            for (int c = 0; c < 3; ++c)
                treeNode.scale[c]
                    = std::sqrt(world[c][0] * world[c][0] + world[c][1] * world[c][1] + world[c][2] * world[c][2]);
            mMatrix.push_back(world);
            mNode.push_back(id);
            mGroup.push_back(dynamic_cast<vsg::Group*>(&node));
        }

        void leave()
        {
            mMatrix.pop_back();
            mNode.pop_back();
            mGroup.pop_back();
        }

        bool addShape(vsg::StateGroup& group, vsg::VertexIndexDraw& draw)
        {
            if (draw.arrays.empty() || !draw.arrays[0])
                return false;
            const vsg::vec3Array* positions = asVec3(*draw.arrays[0]);
            if (!positions || positions->size() == 0)
                return false;

            MeshScene::Shape shape;
            const vsg::dmat4& world = mMatrix.back();
            shape.mMesh.positions.reserve(positions->size());
            for (const vsg::vec3& p : *positions)
                shape.mMesh.positions.push_back(transformPoint(world, { p.x, p.y, p.z }));

            // Arrays are in the order of components/vsgadapters/nif/nif.cpp (handleGeometry): vertex, normal, colour,
            // skin (two), tangent, then UV sets.
            std::size_t next = 1;
            if (next < draw.arrays.size() && draw.arrays[next])
                if (const vsg::vec3Array* normals = asVec3(*draw.arrays[next]);
                    normals && normals->size() == positions->size())
                {
                    shape.mMesh.normals.reserve(normals->size());
                    for (const vsg::vec3& n : *normals)
                        shape.mMesh.normals.push_back(transformDirection(world, { n.x, n.y, n.z }));
                    ++next;
                }
            // A run of vec4 arrays: [colours], [bone indices, bone weights], [tangent]. A last array whose vertices
            // all have a unit xyz and a w of plus or minus one is a tangent, which is left out of the count; then an
            // odd run starts with the vertex colours.
            std::size_t run = 0;
            while (next + run < draw.arrays.size() && draw.arrays[next + run] && asVec4(*draw.arrays[next + run]))
                ++run;
            if (run > 0 && looksLikeTangents(*asVec4(*draw.arrays[next + run - 1])))
                --run;
            // Two vec4 arrays after the colour are the bone indices and weights.
            const bool skinned = run >= 2;
            if (run % 2 == 1)
                if (const vsg::vec4Array* colours = asVec4(*draw.arrays[next]); colours->size() == positions->size())
                    for (const vsg::vec4& c : *colours)
                        shape.mMesh.colours.push_back({ c.r, c.g, c.b, c.a });

            if (draw.indices && draw.indices->data)
            {
                const vsg::Data* data = draw.indices->data.get();
                if (auto* shorts = dynamic_cast<const vsg::ushortArray*>(data))
                    shape.mMesh.indices.assign(shorts->begin(), shorts->end());
                else if (auto* ints = dynamic_cast<const vsg::uintArray*>(data))
                    shape.mMesh.indices.assign(ints->begin(), ints->end());
                else if (auto* bytes = dynamic_cast<const vsg::ubyteArray*>(data))
                    shape.mMesh.indices.assign(bytes->begin(), bytes->end());
            }
            if (shape.mMesh.indices.size() > draw.indexCount)
                shape.mMesh.indices.resize(draw.indexCount);

            VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            for (const auto& command : group.stateCommands)
                if (auto bind = command.cast<vsg::BindGraphicsPipeline>(); bind && bind->pipeline)
                    for (const auto& state : bind->pipeline->pipelineStates)
                        if (auto assembly = state.cast<vsg::InputAssemblyState>())
                            topology = assembly->topology;
            if (topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
                shape.mMesh.topology = CSMMesh::Topology::TriangleStrip;
            else if (topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
                shape.mMesh.topology = CSMMesh::Topology::LineList;

            const std::size_t triangleCount = CSMMesh::triangles(shape.mMesh).size();
            const std::string parentName
                = mNode.back() == CSMMesh::kNoNode ? std::string() : mScene.mTree.node(mNode.back()).name;
            const CSMMesh::NodeId id = mScene.mTree.addNode(mNode.back(), parentName, "Geometry");
            if (mScene.mInfos.size() <= static_cast<std::size_t>(id))
                mScene.mInfos.resize(id + 1);
            auto& treeNode = mScene.mTree.mutableNode(id);
            treeNode.vertexCount = static_cast<std::uint32_t>(positions->size());
            treeNode.triangleCount = static_cast<std::uint32_t>(triangleCount);
            std::ostringstream flags;
            flags << (shape.mMesh.topology == CSMMesh::Topology::TriangleStrip ? "triangle strip"
                    : shape.mMesh.topology == CSMMesh::Topology::LineList      ? "line list"
                                                                               : "triangle list")
                  << (skinned ? ", skinned (other modes show it unskinned)" : "")
                  << (shape.mMesh.normals.empty() ? "" : ", normals")
                  << (shape.mMesh.colours.empty() ? "" : ", vertex colours");
            treeNode.flags = flags.str();
            auto& info = mScene.mInfos[id];
            info.mNode = &group;
            info.mParent = mGroup.back();
            info.mWorld = world;
            info.mShapes.push_back(mScene.mShapes.size());
            shape.mNode = id;
            mScene.mShapes.push_back(std::move(shape));
            return true;
        }

        MeshScene& mScene;
        std::vector<vsg::dmat4> mMatrix;
        std::vector<CSMMesh::NodeId> mNode;
        std::vector<vsg::Group*> mGroup;
    };

    MeshScene::MeshScene(SceneHost& host)
        : mHost(host)
        , mHostAlive(host.aliveToken())
    {
    }

    MeshScene::~MeshScene()
    {
        // The model's graph is shared with other views of the same model: put it back as it was found.
        for (NodeInfo& info : mInfos)
            if (info.mWrapper && info.mParent)
                for (auto& child : info.mParent->children)
                    if (child.get() == info.mWrapper.get())
                        child = vsg::ref_ptr<vsg::Node>(info.mNode);
        if (mHostAlive.expired())
            return;
        if (mOverlay)
            mHost.compileContext()->detach(mOverlay->node());
        if (mRoot)
        {
            auto& children = mHost.contentRoot().children;
            children.erase(std::remove_if(children.begin(), children.end(),
                               [this](const vsg::ref_ptr<vsg::Node>& child) { return child.get() == mRoot.get(); }),
                children.end());
            mHost.compileContext()->detach(mRoot);
        }
        if (mOverlayRoot)
        {
            auto& children = mHost.contentRoot().children;
            children.erase(
                std::remove_if(children.begin(), children.end(),
                    [this](const vsg::ref_ptr<vsg::Node>& child) { return child.get() == mOverlayRoot.get(); }),
                children.end());
        }
        mHost.requestRedraw();
    }

    bool MeshScene::load(const std::string& path, std::string& error)
    {
        // A scene that is loaded again must not keep what the last file left (the scene path sets these).
        mFromScene = false;
        mSceneBones.clear();
        mSceneHierarchy.clear();
        // A Skyrim SE cell (task 0086): "skyrimcell:<editor id>" draws the cell's placed objects.
        static constexpr std::string_view sCellPrefix = "skyrimcell:";
        if (path.compare(0, sCellPrefix.size(), sCellPrefix) == 0)
            return loadSkyrimCell(path.substr(sCellPrefix.size()), error);
        std::string name = path;
        std::replace(name.begin(), name.end(), '\\', '/');
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower.rfind("meshes/", 0) == 0)
            name = name.substr(7);

        const VFS::Manager* vfs = mHost.resources().getVFS();
        if (!vfs->exists(VFS::Path::Normalized("meshes/" + name)))
        {
            error = "There is no model \"meshes/" + name + "\" in the virtual file system";
            return false;
        }

        // Skyrim SE files have no path in the engine's renderer: they are read as a Nif::Scene (task 0086 stage e).
        // Every other file, Morrowind's included, goes the engine's way exactly as before.
        if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".nif") == 0)
        {
            std::string start(96, '\0');
            try
            {
                if (auto stream = vfs->get(VFS::Path::Normalized("meshes/" + name)))
                {
                    stream->read(start.data(), static_cast<std::streamsize>(start.size()));
                    start.resize(static_cast<std::size_t>(stream->gcount()));
                }
            }
            catch (const std::exception&)
            {
                start.clear();
            }
            if (CSMMesh::chooseNifReader(start) == CSMMesh::NifReader::Scene)
                return loadScene(name, error);
        }

        try
        {
            mObject = MWAnim::createObject(name, true, nullptr, mHost.animContext());
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
        if (!mObject)
        {
            error = "The reader returned nothing for \"" + name + "\"";
            return false;
        }

        mPath = "meshes/" + name;
        mTree = CSMMesh::MeshTree();
        mInfos.clear();
        mShapes.clear();
        {
            MeshCollector collector(*this);
            mObject->node()->accept(collector);
        }
        mBounds = CSMMesh::Aabb();
        for (const Shape& shape : mShapes)
            for (const auto& p : shape.mMesh.positions)
                CSMMesh::expand(mBounds, { p[0], p[1], p[2] });
        readTextureNames();
        buildCollision();
        mHost.compileContext()->compile(mObject->node());
        finishLoad();

        // The engine pairs some models with a .kf of the same name.
        const std::string sidecar = defaultAnimationFile();
        if (!sidecar.empty())
        {
            std::string problem;
            if (!addAnimationFile(sidecar, problem))
                Log(Debug::Info) << "Mesh viewer: no animation file " << sidecar << ": " << problem;
        }

        setState(mState);
        return true;
    }

    bool MeshScene::loadScene(const std::string& name, std::string& error)
    {
        NifScene::Scene scene;
        try
        {
            const VFS::Manager* vfs = mHost.resources().getVFS();
            const VFS::Path::Normalized path("meshes/" + name);
            // The scene reader's own defaults are the Skyrim SE version set and no Morrowind root rule, which is what
            // compat_skyrimse's skyrimSceneOptions() states; the editor does not link the pack.
            scene = NifScene::readScene(vfs->get(path), path, NifScene::SceneOptions{});
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
        CSMMesh::SceneModel model;
        try
        {
            model = CSMMesh::buildSceneModel(scene);
        }
        catch (const std::exception& e)
        {
            // bad_alloc or length_error on a damaged file: the viewer reports it like a reader error.
            error = std::string("The scene could not be converted for the viewer: ") + e.what();
            return false;
        }
        for (const std::string& warning : model.warnings)
            Log(Debug::Warning) << "Mesh viewer: " << name << ": " << warning;

        {
            const VFS::Manager* vfs = mHost.resources().getVFS();
            CSMMesh::TextureColourCache textures(
                [vfs](const std::string& key, std::uint64_t maxBytes) { return readTextureFile(*vfs, key, maxBytes); });
            CSMMesh::applyTextureColours(model, textures);
            logTextureStats(name, textures.stats());
        }
        applySceneModel(model, "meshes/" + name);
        return true;
    }

    void MeshScene::applySceneModel(const CSMMesh::SceneModel& model, const std::string& path)
    {
        mFromScene = true;
        mObject.reset();
        mPath = path;
        mTree = CSMMesh::MeshTree();
        mInfos.clear();
        mShapes.clear();
        mInfos.resize(model.nodes.size());
        for (std::size_t i = 0; i < model.nodes.size(); ++i)
        {
            const CSMMesh::SceneModelNode& source = model.nodes[i];
            const CSMMesh::NodeId id = mTree.addNode(source.parent, source.name, source.type);
            auto& node = mTree.mutableNode(id);
            node.translation = source.translation;
            node.scale = source.scale;
            node.vertexCount = source.vertexCount;
            node.triangleCount = source.triangleCount;
            node.flags = source.flags;
            node.textures = source.textures;
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    mInfos[id].mWorld[column][row] = source.world[column * 4 + row];
            if (source.shape >= 0)
                mInfos[id].mShapes.push_back(static_cast<std::size_t>(source.shape));
        }
        for (const CSMMesh::SceneModelShape& source : model.shapes)
        {
            Shape shape;
            shape.mNode = source.node;
            shape.mMesh = source.mesh;
            shape.mColour = source.colour;
            mShapes.push_back(std::move(shape));
        }
        mBounds = model.bounds;
        mTextureNames = model.textureNames;
        mSceneHierarchy = model.hierarchyLines;
        mSceneBones = model.bonePoints;
        Log(Debug::Info) << "Mesh viewer: " << mPath << " read as a Nif::Scene: " << model.nodes.size() << " nodes, "
                         << model.shapes.size() << " shapes, " << mSceneBones.size() << " bones, "
                         << mSceneHierarchy.size() << " skeleton lines";
        finishLoad();
        setState(mState);
    }

    bool MeshScene::loadSkyrimCell(const std::string& editorId, std::string& error)
    {
        CSMMesh::SkyrimCellSource& source = CSMMesh::SkyrimCellSource::instance();
        if (!source.ready() && !source.configureFromEnvironment(error))
            return false;
        openeden::sdk::game::WorldView& world = *source.view();
        openeden::sdk::game::WorldCellContent content;
        if (!editorId.empty() && editorId[0] == '@')
        {
            // "@<world editor id>,<x>,<y>[,<radius>]": the exterior cells around a grid square, merged into one scene.
            std::vector<std::string> parts;
            std::istringstream spec(editorId.substr(1));
            for (std::string item; std::getline(spec, item, ',');)
                parts.push_back(item);
            int x = 0, y = 0, radius = 0;
            try
            {
                if (parts.size() < 3 || parts.size() > 4)
                    throw std::invalid_argument("count");
                x = std::stoi(parts[1]);
                y = std::stoi(parts[2]);
                radius = parts.size() == 4 ? std::clamp(std::stoi(parts[3]), 0, 4) : 0;
            }
            catch (const std::exception&)
            {
                error = "A cell spec is \"@<world editor id>,<x>,<y>[,<radius>]\" (radius 0 to 4)";
                return false;
            }
            const auto worldInfo = world.findWorldByEditorId(parts[0]);
            if (!worldInfo)
            {
                error = "There is no world with the editor id \"" + parts[0] + "\" (" + source.summary() + ")";
                return false;
            }
            std::size_t found = 0;
            for (int cy = y - radius; cy <= y + radius; ++cy)
                for (int cx = x - radius; cx <= x + radius; ++cx)
                {
                    const auto cell = world.findCellByGrid(worldInfo->id, cx, cy);
                    if (!cell)
                        continue;
                    openeden::sdk::game::WorldCellContent one;
                    if (!world.loadCell(cell->id, one, error))
                        return false;
                    ++found;
                    if (content.cell.id == openeden::sdk::game::kNoWorldId)
                        content.cell = one.cell;
                    content.objects.insert(content.objects.end(), one.objects.begin(), one.objects.end());
                    content.unresolvedBases += one.unresolvedBases;
                }
            if (found == 0)
            {
                error = "The world has no cell at " + std::to_string(x) + ", " + std::to_string(y);
                return false;
            }
            content.cell.editorId
                = parts[0] + " " + std::to_string(x) + "," + std::to_string(y) + " +" + std::to_string(radius);
            content.cell.objectCount = static_cast<std::uint32_t>(content.objects.size());
        }
        else
        {
            const auto info = world.findCellByEditorId(editorId);
            if (!info)
            {
                error = "There is no cell with the editor id \"" + editorId + "\" in the Skyrim load order ("
                    + source.summary() + ")";
                return false;
            }
            if (!world.loadCell(info->id, content, error))
                return false;
        }

        // Meshes come through the VFS, converted once per path; a path that fails is remembered as missing.
        const VFS::Manager* vfs = mHost.resources().getVFS();
        std::map<std::string, std::unique_ptr<CSMMesh::SceneModel>> cache;
        CSMMesh::TextureColourCache textures(
            [vfs](const std::string& key, std::uint64_t maxBytes) { return readTextureFile(*vfs, key, maxBytes); });
        const CSMMesh::MeshProvider provider = [&](const std::string& modelPath) -> const CSMMesh::SceneModel* {
            std::string key = modelPath;
            std::replace(key.begin(), key.end(), '\\', '/');
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
            const auto it = cache.find(key);
            if (it != cache.end())
                return it->second.get();
            std::unique_ptr<CSMMesh::SceneModel> converted;
            try
            {
                const VFS::Path::Normalized path("meshes/" + key);
                if (vfs->exists(path))
                {
                    const NifScene::Scene scene = NifScene::readScene(vfs->get(path), path, NifScene::SceneOptions{});
                    converted = std::make_unique<CSMMesh::SceneModel>(CSMMesh::buildSceneModel(scene));
                    CSMMesh::applyTextureColours(*converted, textures);
                }
            }
            catch (const std::exception& e)
            {
                Log(Debug::Verbose) << "Cell viewer: " << key << ": " << e.what();
            }
            return cache.emplace(key, std::move(converted)).first->second.get();
        };
        CSMMesh::CellScene cell = CSMMesh::buildCellScene(content, provider);
        const CSMMesh::CellSceneStats& st = cell.stats;
        Log(Debug::Info) << "Cell viewer: " << editorId << ": " << st.objects << " objects, " << st.drawnAsMesh
                         << " drawn as meshes, " << st.drawnAsBox << " as boxes, " << st.skippedDisabled
                         << " disabled, " << st.skippedNoGeometry << " without geometry, " << st.skippedInvalid
                         << " invalid, " << st.skippedByBudget << " over budget, " << st.meshMisses
                         << " models not found, " << st.triangles << " triangles, " << cache.size()
                         << " distinct models asked for, bounds (" << cell.model.bounds.min[0] << ", "
                         << cell.model.bounds.min[1] << ", " << cell.model.bounds.min[2] << ") to ("
                         << cell.model.bounds.max[0] << ", " << cell.model.bounds.max[1] << ", "
                         << cell.model.bounds.max[2] << ")";
        logTextureStats(editorId, textures.stats());
        applySceneModel(cell.model, "skyrimcell:" + editorId);
        return true;
    }

    void MeshScene::finishLoad()
    {
        mRoot = vsg::Group::create();
        mModelSwitch = vsg::Switch::create();
        vsg::ref_ptr<vsg::Node> model;
        if (mObject)
            model = mObject->node();
        else
            model = vsg::Group::create();
        mModelSwitch->addChild(vsg::MASK_ALL, model);
        mRoot->addChild(SceneHost::maskNode(Mask_Reference, mModelSwitch));
        mSolidHolder = vsg::Group::create();
        mRoot->addChild(mSolidHolder);
        mHost.contentRoot().addChild(mRoot);

        std::size_t lineVertices = 4096 + mInfos.size() * 8 + mCollisionLines.size() * 2 + mSceneBones.size() * 6
            + mSceneHierarchy.size() * 2;
        for (const Shape& shape : mShapes)
            lineVertices += shape.mMesh.indices.size() * 2 + shape.mMesh.positions.size() * 2;
        lineVertices = std::min(lineVertices, sMaxOverlayVertices);
        mOverlay = std::make_unique<Render::DebugGeometry>(
            *mHost.resources().builder, mHost.compileContext(), lineVertices, 0);
        mOverlayRoot = vsgUtil::createBinSetter(SceneHost::sBinOverlay, mOverlay->node());
        mHost.contentRoot().addChild(mOverlayRoot);
    }

    std::vector<vsg::dvec3> MeshScene::boundsCorners() const
    {
        std::vector<vsg::dvec3> corners;
        if (!mBounds.valid)
            return corners;
        for (int i = 0; i < 8; ++i)
            corners.emplace_back((i & 1) ? mBounds.max[0] : mBounds.min[0], (i & 2) ? mBounds.max[1] : mBounds.min[1],
                (i & 4) ? mBounds.max[2] : mBounds.min[2]);
        return corners;
    }

    void MeshScene::buildCollision()
    {
        mCollisionLines.clear();
        try
        {
            // The shape the game builds: Resource::PhysicsShapeManager over the same VFS (components/resource). Jolt
            // must be running for it.
            mJolt = std::make_unique<CSMPhysics::JoltRuntime>();
            const VFS::Manager* vfs = mHost.resources().getVFS();
            Resource::NifFileManager nifFiles(vfs, nullptr);
            Resource::PhysicsShapeManager shapes(vfs, &nifFiles, 0.0);
            const auto shape = shapes.getShape(VFS::Path::Normalized(mPath));
            if (!shape || !shape->mCollisionShape)
            {
                Log(Debug::Info) << "Mesh viewer: " << mPath << " has no collision shape";
                return;
            }
            const JPH::Shape& collision = *shape->mCollisionShape;
            Log(Debug::Info) << "Mesh viewer: collision shape type " << static_cast<int>(collision.GetSubType())
                             << ", mesh data " << (collision.MustBeStatic() ? "static" : "other") << ", bounds "
                             << collision.GetLocalBounds().GetSize().GetX() << " x "
                             << collision.GetLocalBounds().GetSize().GetY() << " x "
                             << collision.GetLocalBounds().GetSize().GetZ();
            // A decorating shape (rotated, translated, compound) has no triangles itself: collect its leaves, which
            // come with their places, in the frame where the shape's centre of mass is where it says it is.
            const JPH::AABox everything(JPH::Vec3::sReplicate(-1.0e7f), JPH::Vec3::sReplicate(1.0e7f));
            JPH::AllHitCollisionCollector<JPH::TransformedShapeCollector> leaves;
            collision.CollectTransformedShapes(everything, collision.GetCenterOfMass(), JPH::Quat::sIdentity(),
                JPH::Vec3::sReplicate(1.0f), JPH::SubShapeIDCreator(), leaves, JPH::ShapeFilter());
            constexpr int sBatch = 64;
            std::vector<JPH::Float3> vertices(sBatch * 3);
            const std::array<float, 4> colour{ 0.3f, 1.f, 0.4f, 1.f };
            // A model with more collision triangles than this draws the first ones.
            constexpr std::size_t sMaxCollisionLines = 600000;
            for (const JPH::TransformedShape& leaf : leaves.mHits)
            {
                if (mCollisionLines.size() >= sMaxCollisionLines)
                    break;
                JPH::Shape::GetTrianglesContext context;
                leaf.mShape->GetTrianglesStart(context, everything, JPH::Vec3(leaf.mShapePositionCOM),
                    leaf.mShapeRotation, JPH::Vec3(leaf.mShapeScale));
                for (;;)
                {
                    const int count = leaf.mShape->GetTrianglesNext(context, sBatch, vertices.data());
                    if (count == 0)
                        break;
                    for (int i = 0; i < count; ++i)
                    {
                        const JPH::Float3* t = &vertices[i * 3];
                        for (int edge = 0; edge < 3; ++edge)
                        {
                            const JPH::Float3& a = t[edge];
                            const JPH::Float3& b = t[(edge + 1) % 3];
                            mCollisionLines.push_back({ { a.x, a.y, a.z }, { b.x, b.y, b.z }, colour });
                        }
                    }
                }
            }
            Log(Debug::Info) << "Mesh viewer: collision of " << mPath << ": " << mCollisionLines.size() / 3
                             << " triangles";
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Mesh viewer: no collision shape for " << mPath << ": " << e.what();
            mCollisionLines.clear();
        }
    }

    void MeshScene::readTextureNames()
    {
        mTextureNames.clear();
        try
        {
            const VFS::Manager* vfs = mHost.resources().getVFS();
            const VFS::Path::Normalized nifPath(mPath);
            Nif::NIFFile file(nifPath);
            Nif::Reader reader(file);
            reader.parse(vfs->get(VFS::Path::Normalized(mPath)));
            for (const auto& record : file.mRecords)
                if (record && record->mRecordType == Nif::RC_NiSourceTexture)
                {
                    const auto& texture = static_cast<const Nif::NiSourceTexture&>(*record);
                    if (texture.mExternal && !texture.mFile.empty())
                        mTextureNames.push_back(texture.mFile);
                }
        }
        catch (const std::exception& e)
        {
            // Not a NIF (COLLADA, say): no list.
            Log(Debug::Verbose) << "Mesh viewer: no texture list for " << mPath << ": " << e.what();
        }
    }

    CSMMesh::TextureReport MeshScene::textureReport() const
    {
        const VFS::Manager* vfs = mHost.resources().getVFS();
        return CSMMesh::buildTextureReport(
            mTextureNames, [vfs](const std::string& name) -> std::optional<std::pair<std::uint32_t, std::uint32_t>> {
                const VFS::Path::NormalizedView top[] = { VFS::Path::NormalizedView("textures") };
                const VFS::Path::Normalized corrected = Misc::ResourceHelpers::correctResourcePath(
                    top, VFS::Path::Normalized(name), *vfs, VFS::Path::ExtensionView("dds"));
                if (corrected.value().empty() || !vfs->exists(corrected))
                    return std::nullopt;
                std::vector<unsigned char> bytes(32);
                if (auto stream = vfs->get(corrected))
                {
                    stream->read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    bytes.resize(static_cast<std::size_t>(stream->gcount()));
                }
                if (auto size = CSMMesh::imageSizeFromHeader(bytes))
                    return size;
                return std::make_pair(0u, 0u); // found, size unknown
            });
    }

    void MeshScene::setState(const CSMMesh::MeshViewState& state)
    {
        mState = state;
        if (!mRoot)
            return;
        mModelSwitch->children[0].mask = CSMMesh::hidesTexturedModel(mState.mode) ? vsg::MASK_OFF : vsg::MASK_ALL;
        rebuildSolid();
        rebuildOverlay();
        mHost.requestRedraw();
    }

    bool MeshScene::shapeVisible(const Shape& shape) const
    {
        return mTree.isEffectivelyVisible(shape.mNode);
    }

    void MeshScene::setNodeVisible(CSMMesh::NodeId node, bool visible)
    {
        mTree.setVisible(node, visible);
        if (node < 0 || static_cast<std::size_t>(node) >= mInfos.size())
            return;
        NodeInfo& info = mInfos[node];
        if (info.mParent && info.mNode)
        {
            if (!info.mWrapper)
            {
                auto& children = info.mParent->children;
                auto it = std::find_if(children.begin(), children.end(),
                    [&](const vsg::ref_ptr<vsg::Node>& child) { return child.get() == info.mNode; });
                if (it != children.end())
                {
                    info.mWrapper = vsg::Switch::create();
                    info.mWrapper->addChild(vsg::MASK_ALL, *it);
                    *it = info.mWrapper;
                }
            }
            if (info.mWrapper)
                info.mWrapper->children[0].mask = visible ? vsg::MASK_ALL : vsg::MASK_OFF;
        }
        rebuildSolid();
        rebuildOverlay();
        mHost.requestRedraw();
    }

    CSMMesh::NodeId MeshScene::pickNode(const vsg::dvec3& start, const vsg::dvec3& end) const
    {
        const vsg::dvec3 direction = end - start;
        double best = 2.0;
        CSMMesh::NodeId node = CSMMesh::kNoNode;
        for (const Shape& shape : mShapes)
        {
            if (!shapeVisible(shape))
                continue;
            for (const auto& t : CSMMesh::triangles(shape.mMesh))
            {
                const double ratio = hitTriangle(start, direction, shape.mMesh.positions[t[0]],
                    shape.mMesh.positions[t[1]], shape.mMesh.positions[t[2]]);
                if (ratio >= 0.0 && ratio < best)
                {
                    best = ratio;
                    node = shape.mNode;
                }
            }
        }
        return node;
    }

    void MeshScene::setSelected(CSMMesh::NodeId node)
    {
        mSelected = node;
        rebuildOverlay();
        mHost.requestRedraw();
    }

    CSMMesh::Aabb MeshScene::nodeBounds(CSMMesh::NodeId node) const
    {
        CSMMesh::Aabb box;
        if (node == CSMMesh::kNoNode)
            return box;
        std::vector<CSMMesh::NodeId> stack{ node };
        while (!stack.empty())
        {
            const CSMMesh::NodeId current = stack.back();
            stack.pop_back();
            for (std::size_t index : mInfos[current].mShapes)
                for (const auto& p : mShapes[index].mMesh.positions)
                    CSMMesh::expand(box, { p[0], p[1], p[2] });
            for (CSMMesh::NodeId child : mTree.node(current).children)
                stack.push_back(child);
        }
        return box;
    }

    void MeshScene::rebuildSolid()
    {
        if (!mSolidHolder)
            return;
        if (mSolid)
        {
            mHost.compileContext()->detach(mSolid);
            mSolidHolder->children.clear();
            mSolid = nullptr;
        }
        // A scene-path model has no textured picture, so Textured shows its flat material colours.
        const bool sceneSolid = mFromScene && mState.mode == CSMMesh::ViewMode::Textured;
        if (!CSMMesh::needsTriangleOverlay(mState.mode) && !sceneSolid)
            return;

        std::vector<vsg::vec3> positions;
        std::vector<vsg::vec4> colours;
        for (const Shape& shape : mShapes)
        {
            if (!shapeVisible(shape))
                continue;
            for (const auto& t : CSMMesh::triangles(shape.mMesh))
            {
                std::array<float, 4> colour;
                if (mState.mode == CSMMesh::ViewMode::Flat)
                    colour = CSMMesh::flatShadeColour(CSMMesh::faceNormal(shape.mMesh.positions[t[0]],
                                                          shape.mMesh.positions[t[1]], shape.mMesh.positions[t[2]]),
                        sFlatLightDirection);
                else if (sceneSolid)
                {
                    colour = CSMMesh::flatShadeColour(CSMMesh::faceNormal(shape.mMesh.positions[t[0]],
                                                          shape.mMesh.positions[t[1]], shape.mMesh.positions[t[2]]),
                        sFlatLightDirection);
                    for (int c = 0; c < 3; ++c)
                        colour[c] *= shape.mColour[c];
                }
                else
                {
                    colour = shape.mMesh.colours.empty() ? sNoColourGrey : CSMMesh::averageColour(shape.mMesh, t);
                    colour[3] = 1.f;
                }
                for (std::uint32_t index : t)
                {
                    const auto& p = shape.mMesh.positions[index];
                    positions.emplace_back(p[0], p[1], p[2]);
                    colours.emplace_back(colour[0], colour[1], colour[2], colour[3]);
                }
            }
        }
        if (positions.empty())
            return;

        auto vertexArray = vsg::vec3Array::create(static_cast<uint32_t>(positions.size()));
        auto colourArray = vsg::vec4Array::create(static_cast<uint32_t>(colours.size()));
        std::copy(positions.begin(), positions.end(), vertexArray->begin());
        std::copy(colours.begin(), colours.end(), colourArray->begin());

        Pipeline::Options options;
        options.shader.addMode(Pipeline::Mode::VERTEX);
        options.shader.addMode(Pipeline::Mode::COLOR);
        options.colorMode = sUnlitColorMode;
        options.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        options.cullMode = VK_CULL_MODE_NONE;

        auto draw = vsg::VertexDraw::create();
        draw->assignArrays({ vertexArray, colourArray });
        draw->vertexCount = static_cast<uint32_t>(positions.size());
        draw->instanceCount = 1;
        auto group = vsg::StateGroup::create();
        group->stateCommands = { mHost.resources().builder->graphics->getOrCreate(options) };
        group->prototypeArrayState = vsg::NullArrayState::create();
        group->addChild(draw);

        mHost.compileContext()->compile(group);
        mSolid = group;
        mSolidHolder->addChild(mSolid);
    }

    void MeshScene::rebuildOverlay()
    {
        if (!mOverlay)
            return;
        const bool wanted = CSMMesh::needsLineOverlay(mState) || mState.showGrid || mSelected != CSMMesh::kNoNode;
        mOverlay->setEnabled(wanted);
        if (!wanted)
            return;
        mOverlay->begin();
        auto add = [this](const std::vector<CSMMesh::Line>& lines) {
            for (const CSMMesh::Line& line : lines)
                mOverlay->addLine(vsg::vec3(line.a[0], line.a[1], line.a[2]),
                    vsg::vec3(line.b[0], line.b[1], line.b[2]),
                    vsg::vec4(line.colour[0], line.colour[1], line.colour[2], line.colour[3]));
        };

        const double radius = std::max(CSMMesh::radius(mBounds), 1.0);
        if (mState.mode == CSMMesh::ViewMode::Wireframe)
            for (const Shape& shape : mShapes)
            {
                if (!shapeVisible(shape))
                    continue;
                for (const auto& edge : CSMMesh::uniqueEdges(shape.mMesh))
                {
                    if (edge[0] >= shape.mMesh.positions.size() || edge[1] >= shape.mMesh.positions.size())
                        continue;
                    add({ { shape.mMesh.positions[edge[0]], shape.mMesh.positions[edge[1]], sWireColour } });
                }
            }
        if (mState.mode == CSMMesh::ViewMode::Normals)
            for (const Shape& shape : mShapes)
                if (shapeVisible(shape))
                    add(CSMMesh::normalLines(shape.mMesh, static_cast<float>(radius * 0.04), sNormalColour));
        if (mState.showCollision)
            add(mCollisionLines);
        if (mState.showBounds)
            add(CSMMesh::boxLines(mBounds, sBoundsColour));
        if (mSelected != CSMMesh::kNoNode)
            add(CSMMesh::boxLines(nodeBounds(mSelected), sSelectionColour));
        if (mState.showSkeleton || mState.showNodeAxes)
            for (std::size_t id = 0; id < mInfos.size(); ++id)
            {
                const auto& node = mTree.node(static_cast<CSMMesh::NodeId>(id));
                if (node.type != "Transform" || !mTree.isEffectivelyVisible(static_cast<CSMMesh::NodeId>(id)))
                    continue;
                const auto& world = mInfos[id].mWorld;
                const std::array<float, 3> origin{ static_cast<float>(world[3][0]), static_cast<float>(world[3][1]),
                    static_cast<float>(world[3][2]) };
                if (mState.showNodeAxes)
                    add(CSMMesh::axesLines(origin, static_cast<float>(radius * 0.04)));
                if (mState.showSkeleton && !mFromScene)
                    for (CSMMesh::NodeId parent = node.parent; parent != CSMMesh::kNoNode;
                         parent = mTree.node(parent).parent)
                        if (mTree.node(parent).type == "Transform")
                        {
                            const auto& pw = mInfos[parent].mWorld;
                            add({ { { static_cast<float>(pw[3][0]), static_cast<float>(pw[3][1]),
                                        static_cast<float>(pw[3][2]) },
                                origin, sSkeletonColour } });
                            break;
                        }
            }
        if (mState.showSkeleton && mFromScene)
        {
            // The node hierarchy of a skeleton file, and a small cross at each skin bone's bind position.
            for (const auto& line : mSceneHierarchy)
                add({ { line[0], line[1], sSkeletonColour } });
            const float arm = static_cast<float>(radius * 0.012);
            for (const auto& p : mSceneBones)
                for (int axis = 0; axis < 3; ++axis)
                {
                    std::array<float, 3> a = p;
                    std::array<float, 3> b = p;
                    a[axis] -= arm;
                    b[axis] += arm;
                    add({ { a, b, sSkeletonColour } });
                }
        }
        if (mState.showGrid)
            add(CSMMesh::gridLines(CSMMesh::gridSpacing(radius), 10));
        mOverlay->end();
    }

    std::string MeshScene::describeNode(CSMMesh::NodeId node) const
    {
        if (node == CSMMesh::kNoNode || static_cast<std::size_t>(node) >= mTree.size())
            return "Nothing selected.";
        const CSMMesh::MeshNode& n = mTree.node(node);
        const auto totals = mTree.subtreeTotals(node);
        std::ostringstream out;
        out << "Name: " << (n.name.empty() ? "<unnamed>" : n.name) << "\nType: " << n.type
            << "\nPath: " << mTree.path(node) << "\nVisible: " << (mTree.isEffectivelyVisible(node) ? "yes" : "no")
            << "\nTranslation (world): " << n.translation[0] << ", " << n.translation[1] << ", " << n.translation[2]
            << "\nScale (world): " << n.scale[0] << ", " << n.scale[1] << ", " << n.scale[2];
        if (!n.flags.empty())
            out << "\nFlags: " << n.flags;
        out << "\nThis node: " << n.vertexCount << " vertices, " << n.triangleCount << " triangles"
            << "\nWith children: " << totals.nodes << " nodes, " << totals.vertices << " vertices, " << totals.triangles
            << " triangles";
        const CSMMesh::Aabb box = nodeBounds(node);
        if (box.valid)
            out << "\nBounds: (" << box.min[0] << ", " << box.min[1] << ", " << box.min[2] << ") to (" << box.max[0]
                << ", " << box.max[1] << ", " << box.max[2] << ")";
        return out.str();
    }

    std::string MeshScene::defaultAnimationFile() const
    {
        return CSMMesh::kfSidecarFor(mPath);
    }

    bool MeshScene::addAnimationFile(const std::string& vfsPath, std::string& error)
    {
        if (!mObject || !mObject->animation)
        {
            error = "This model has no skeleton to animate";
            return false;
        }
        const VFS::Manager* vfs = mHost.resources().getVFS();
        if (!vfs->exists(VFS::Path::Normalized(vfsPath)))
        {
            error = "There is no animation file \"" + vfsPath + "\"";
            return false;
        }
        vsg::ref_ptr<Anim::ControllerMap> map;
        try
        {
            std::string name = vfsPath;
            if (name.rfind("meshes/", 0) == 0)
                name = name.substr(7);
            map = mHost.animContext().readAnimation(name);
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
        if (!map)
        {
            error = "The animation reader returned nothing for \"" + vfsPath + "\"";
            return false;
        }
        mObject->animation->addSingleAnimSource(map, vfsPath);
        Log(Debug::Info) << "Mesh viewer: animation " << vfsPath << ": " << map->map.size() << " tracks, "
                         << (map->tags ? "text keys" : "no text keys");
        mAnimationSources.push_back(map);
        mAnimationFiles.push_back(vfsPath);
        if (map->tags)
            for (const auto& [time, text] : *map->tags)
                mTextKeys.push_back({ time, text });
        return true;
    }

    std::vector<CSMMesh::AnimationGroup> MeshScene::animationGroups() const
    {
        return CSMMesh::groupTextKeys(mTextKeys);
    }

    void MeshScene::playGroup(const std::string& group, float startPoint)
    {
        if (!mObject || !mObject->animation)
            return;
        mObject->animation->play(
            group, MWAnim::Play::Priority(1), 1, false, 1.f, "start", "stop", startPoint, 0xFFFFFFFFu, true);
        mCurrentGroup = group;
        mObject->animation->runAnimation(0.f);
        mHost.requestRedraw();
    }

    float MeshScene::animationCompletion() const
    {
        float completion = 0.f;
        if (mObject && mObject->animation && !mCurrentGroup.empty())
            mObject->animation->getInfo(mCurrentGroup, &completion);
        return completion;
    }

    void MeshScene::update(float dt)
    {
        if (!mObject)
            return;
        mObject->update(dt);
        if (mObject->animation && !mCurrentGroup.empty() && !mAnimationPaused)
        {
            mObject->animation->runAnimation(dt);
            mHost.requestRedraw();
        }
    }

    bool MeshScene::isAnimated() const
    {
        if (!mObject)
            return false;
        if (mObject->animation && !mCurrentGroup.empty() && !mAnimationPaused)
            return true;
        const auto& groups = mObject->autoPlay.groups;
        return std::any_of(groups.begin(), groups.end(), [](const auto& group) { return !group.controllers.empty(); });
    }
}
