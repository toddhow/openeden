#ifndef OPENCS_VIEW_MESHSCENE_H
#define OPENCS_VIEW_MESHSCENE_H

#include <memory>
#include <string>
#include <vector>

#include <vsg/maths/mat4.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>

#include <apps/opencs/model/mesh/animationkeys.hpp>
#include <apps/opencs/model/mesh/framemath.hpp>
#include <apps/opencs/model/mesh/meshgeometry.hpp>
#include <apps/opencs/model/mesh/meshoverlay.hpp>
#include <apps/opencs/model/mesh/meshtree.hpp>
#include <apps/opencs/model/mesh/meshviewstate.hpp>
#include <apps/opencs/model/mesh/scenemodel.hpp>
#include <apps/opencs/model/mesh/texturereport.hpp>

namespace MWAnim
{
    class Object;
}

namespace Render
{
    class DebugGeometry;
}

namespace CSMPhysics
{
    class JoltRuntime;
}

namespace Anim
{
    class ControllerMap;
}

namespace CSVRender
{
    class SceneHost;

    /// One model opened in the mesh viewer (task 0043): loaded through the engine's own reader
    /// (MWAnim::createObject over the document's VFS), analysed into a CSMMesh::MeshTree, and drawn in the view
    /// modes of CSMMesh::MeshViewState. View modes other than Textured never touch the renderer's pipelines: the
    /// textured model is hidden and the mode is drawn from overlay lines (Render::DebugGeometry) or from one
    /// unlit solid mesh made here.
    ///
    /// Ownership: the SceneHost owns what is attached to contentRoot(); this object owns the nodes it made and
    /// detaches them in its destructor, so it must not outlive the host (check SceneHost::aliveToken()).
    /// Threads: main thread only.
    class MeshScene
    {
    public:
        explicit MeshScene(SceneHost& host);
        ~MeshScene();

        MeshScene(const MeshScene&) = delete;
        MeshScene& operator=(const MeshScene&) = delete;

        /// \param path a VFS path, with or without the leading "meshes/". \return false and an error text if the
        /// model is not in the VFS or the reader fails.
        bool load(const std::string& path, std::string& error);
        bool loaded() const { return mRoot != nullptr; }
        const std::string& path() const { return mPath; }

        const CSMMesh::MeshTree& tree() const { return mTree; }
        CSMMesh::Aabb bounds() const { return mBounds; }
        /// Corner positions of the model's bounds (for the default view).
        std::vector<vsg::dvec3> boundsCorners() const;

        /// Texture file names the model's NIF names (empty for other formats), as the NIF says them.
        const std::vector<std::string>& textureNames() const { return mTextureNames; }
        /// Looks each one up in the VFS the way the engine does (.tga falls back to .dds).
        CSMMesh::TextureReport textureReport() const;

        /// Whether the engine builds a collision shape for this model (task 0043, stage 5).
        bool hasCollision() const { return !mCollisionLines.empty(); }
        const CSMMesh::MeshViewState& state() const { return mState; }
        void setState(const CSMMesh::MeshViewState& state);

        void setNodeVisible(CSMMesh::NodeId node, bool visible);
        void setSelected(CSMMesh::NodeId node);
        /// The tree node whose geometry the ray hits first (any view mode, hidden nodes excluded), else kNoNode.
        /// Done on the CPU from the collected triangles, so it needs no tags in the model's (shared) scene graph.
        CSMMesh::NodeId pickNode(const vsg::dvec3& start, const vsg::dvec3& end) const;
        CSMMesh::NodeId selected() const { return mSelected; }

        /// Text of the tree node, for the Properties panel.
        std::string describeNode(CSMMesh::NodeId node) const;

        // Animation (stage 4). Groups and keys come from the .kf files added (the engine's pairing is added at load).
        bool addAnimationFile(const std::string& vfsPath, std::string& error);
        std::vector<CSMMesh::AnimationGroup> animationGroups() const;
        /// The .kf the engine would pair with this model (empty if none).
        std::string defaultAnimationFile() const;
        void playGroup(const std::string& group, float startPoint);
        void setAnimationPaused(bool paused) { mAnimationPaused = paused; }
        bool animationPaused() const { return mAnimationPaused; }
        std::string currentGroup() const { return mCurrentGroup; }
        float animationCompletion() const;
        /// Advances animation by dt seconds if not paused. \return whether anything moves by itself.
        void update(float dt);
        bool isAnimated() const;

    private:
        struct NodeInfo
        {
            vsg::Node* mNode = nullptr;
            vsg::Group* mParent = nullptr;
            vsg::ref_ptr<vsg::Switch> mWrapper;
            vsg::dmat4 mWorld;
            std::vector<std::size_t> mShapes;
        };
        struct Shape
        {
            CSMMesh::NodeId mNode = CSMMesh::kNoNode;
            CSMMesh::IndexedMesh mMesh; // positions and normals in model space (the root's frame)
            std::array<float, 4> mColour{ 0.72f, 0.72f, 0.72f, 1.f }; // scene path only: the material's flat colour
        };
        friend class MeshCollector;

        /// The scene path (task 0086 stage e): a Skyrim SE file is read as a Nif::Scene and drawn from the solid and
        /// line overlays only, at the bind pose; the engine's renderer has no BSTriShape path.
        bool loadScene(const std::string& name, std::string& error);
        /// Shows a converted scene model: the tree, shapes, bounds and overlays of the scene path (a Skyrim file, or a
        /// whole cell, task 0086).
        void applySceneModel(const CSMMesh::SceneModel& model, const std::string& path);
        /// A Skyrim SE cell by editor id (path "skyrimcell:<editor id>"), from the cell source (skyrimcellsource.hpp):
        /// real meshes where they load through the VFS, boxes of the bounds where not.
        bool loadSkyrimCell(const std::string& editorId, std::string& error);
        /// The part of loading both paths share: the scene graph root, the overlay and the state.
        void finishLoad();

        void collect();
        void readTextureNames();
        void rebuildOverlay();
        void buildCollision();
        void rebuildSolid();
        void applyHelperNodes();
        CSMMesh::Aabb nodeBounds(CSMMesh::NodeId node) const;
        bool shapeVisible(const Shape& shape) const;

        SceneHost& mHost;
        std::weak_ptr<void> mHostAlive;
        std::string mPath;
        CSMMesh::MeshTree mTree;
        std::vector<NodeInfo> mInfos;
        std::vector<Shape> mShapes;
        CSMMesh::Aabb mBounds;
        std::vector<std::string> mTextureNames;
        CSMMesh::MeshViewState mState;
        CSMMesh::NodeId mSelected = CSMMesh::kNoNode;

        bool mFromScene = false; // loaded by loadScene: no MWAnim::Object, no textured mode
        std::vector<std::array<std::array<float, 3>, 2>> mSceneHierarchy; // scene path: skeleton lines
        std::vector<std::array<float, 3>> mSceneBones; // scene path: bind pose joints

        std::unique_ptr<MWAnim::Object> mObject;
        std::unique_ptr<CSMPhysics::JoltRuntime> mJolt;
        std::vector<CSMMesh::Line> mCollisionLines;
        vsg::ref_ptr<vsg::Group> mRoot; // under contentRoot()
        vsg::ref_ptr<vsg::Switch> mModelSwitch;
        vsg::ref_ptr<vsg::Group> mSolidHolder;
        vsg::ref_ptr<vsg::Node> mSolid;
        std::unique_ptr<Render::DebugGeometry> mOverlay;
        vsg::ref_ptr<vsg::Node> mOverlayRoot;

        bool mAnimationPaused = true;
        std::string mCurrentGroup;
        std::vector<vsg::ref_ptr<Anim::ControllerMap>> mAnimationSources;
        std::vector<CSMMesh::TextKey> mTextKeys;
        std::vector<std::string> mAnimationFiles;
    };
}

#endif
