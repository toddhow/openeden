#ifndef OPENCS_VIEW_MESHVIEWWIDGET_H
#define OPENCS_VIEW_MESHVIEWWIDGET_H

#include <filesystem>
#include <memory>
#include <string>

#include <apps/opencs/model/mesh/meshviewstate.hpp>
#include <apps/opencs/model/module/overlaybuffer.hpp>
#include <apps/opencs/model/module/overlaypick.hpp>

#include "meshscene.hpp"
#include "scenewidget.hpp"

class QKeyEvent;
class QPointF;
class QMouseEvent;

namespace CSVRender
{
    class PhysicsDebugView;

    /// The 3D view of the mesh viewer (task 0043): a SceneWidget with orbit navigation that shows one MeshScene.
    /// It can be told to open a model before its view exists; the model loads when the view is ready.
    /// Opened with no model (an empty path, task 0069 stage f) it shows the active editor module asset's gizmos
    /// instead, at the origin, as the cell views do (WorldspaceWidget::updateModuleOverlay); a view with a model never
    /// shows them.
    /// Ownership: owns the MeshScene, which it destroys before the view, and the module overlay.
    class MeshViewWidget : public SceneWidget
    {
        Q_OBJECT

    public:
        explicit MeshViewWidget(CSMWorld::Data& worldData, QWidget* parent = nullptr);
        ~MeshViewWidget() override;

        /// \param path a VFS path with or without "meshes/". Loads at once if the view exists, else when it does. An
        /// empty path: no model, the view shows the active editor module asset's gizmos.
        void openMesh(const std::string& path);

        /// True when the view has no model and shows the module asset's gizmos.
        bool showsModuleAsset() const { return mModuleAsset; }
        /// The lines of the module asset's gizmos drawn now (0 without a module asset).
        std::size_t moduleLineCount() const { return mModuleLines; }

        /// Null before a model is loaded.
        MeshScene* scene() const { return mScene.get(); }
        const CSMMesh::MeshViewState& viewState() const { return mState; }
        void setViewState(const CSMMesh::MeshViewState& state);

        /// Points the camera at the whole model ("frame object", key F).
        void frameObject();
        void selectNode(CSMMesh::NodeId node);
        bool saveScreenshot(const std::filesystem::path& file);
        /// The tree node under a point of the view, (0, 0) its top left and (1, 1) its bottom right; kNoNode if none.
        CSMMesh::NodeId pickAt(double x, double y) const;

    signals:
        void modelLoaded();
        void loadFailed(const QString& message);
        /// A click in the view hit this tree node.
        void nodePicked(int node);
        /// A view with no model drew the module asset's gizmos again (`lines` lines).
        void moduleOverlayChanged(int lines);

    protected:
        void hostReady() override;
        void frameUpdate(float dt) override;
        bool isAnimated() const override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;

    private:
        void load();
        void applyViewLighting();
        void showModuleAsset();
        void updateModuleOverlay();
        /// Shows mOverlayData with its labels laid out for the camera as it is now (API 1.2 labels, task 0091).
        void showOverlayWithLabels();
        /// The camera ray through a mouse position (pixels) in the world's unit.
        CSMModule::PickRay mouseRay(const QPointF& position) const;
        double handlePickSlope() const;

        std::unique_ptr<MeshScene> mScene;
        CSMMesh::MeshViewState mState;
        std::string mPendingPath;
        QPoint mPressPosition;
        bool mHandlePressed = false; ///< The press took a module gizmo handle: the camera does not move with the mouse.

        bool mModuleAsset = false;
        CSMModule::OverlayData mOverlayData; ///< What the modules drew last, labels not yet turned into lines.
        vsg::dmat4 mLabelViewMatrix; ///< The view matrix the labels were laid out for.
        std::unique_ptr<PhysicsDebugView> mModuleOverlay; ///< Only in a view with no model.
        CSMMesh::Aabb mModuleBounds; ///< Of the module overlay's lines, for "frame object".
        std::size_t mModuleLines = 0;
        unsigned long mModuleGeneration = ~0ul; ///< The ModuleUi generation framed last: a new asset is framed once.
    };
}

#endif
