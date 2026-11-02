#ifndef OPENCS_VIEW_MESHVIEWWIDGET_H
#define OPENCS_VIEW_MESHVIEWWIDGET_H

#include <filesystem>
#include <memory>
#include <string>

#include <apps/opencs/model/mesh/meshviewstate.hpp>

#include "meshscene.hpp"
#include "scenewidget.hpp"

class QKeyEvent;
class QMouseEvent;

namespace CSVRender
{
    /// The 3D view of the mesh viewer (task 0043): a SceneWidget with orbit navigation that shows one MeshScene.
    /// It can be told to open a model before its view exists; the model loads when the view is ready.
    /// Ownership: owns the MeshScene, which it destroys before the view.
    class MeshViewWidget : public SceneWidget
    {
        Q_OBJECT

    public:
        explicit MeshViewWidget(CSMWorld::Data& worldData, QWidget* parent = nullptr);
        ~MeshViewWidget() override;

        /// \param path a VFS path with or without "meshes/". Loads at once if the view exists, else when it does.
        void openMesh(const std::string& path);

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

    protected:
        void hostReady() override;
        void frameUpdate(float dt) override;
        bool isAnimated() const override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;

    private:
        void load();
        void applyViewLighting();

        std::unique_ptr<MeshScene> mScene;
        CSMMesh::MeshViewState mState;
        std::string mPendingPath;
        QPoint mPressPosition;
    };
}

#endif
