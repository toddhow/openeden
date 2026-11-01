#ifndef CSV_RENDER_INSTANCEMODE_H
#define CSV_RENDER_INSTANCEMODE_H

#include <QString>

#include <string>
#include <vector>

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>

#include "editmode.hpp"
#include "instancedragmodes.hpp"
#include <apps/opencs/model/world/idtable.hpp>
#include <components/esm3/selectiongroup.hpp>

class QDragEnterEvent;
class QDropEvent;
class QObject;
class QPoint;
class QWidget;

namespace CSVWidget
{
    class SceneToolMode;
    class SceneToolbar;
}

namespace CSVRender
{
    class TagBase;
    class InstanceSelectionMode;
    class Object;
    class WorldspaceWidget;
    struct WorldspaceHitResult;

    class InstanceMode : public EditMode
    {
        Q_OBJECT

        CSVWidget::SceneToolMode* mSubMode;
        std::string mSubModeId;
        InstanceSelectionMode* mSelectionMode;
        DragMode mDragMode;
        int mDragAxis;
        bool mLocked;
        float mUnitScaleDist;
        vsg::vec3 mDragStart;
        std::vector<vsg::vec3> mObjectsAtDragStart;
        CSMWorld::IdTable* mSelectionGroups;

        QString getTooltip();
        int getSubModeFromId(const std::string& id) const;

        vsg::vec3 getSelectionCenter(const std::vector<vsg::ref_ptr<TagBase>>& selection) const;
        vsg::vec3 getScreenCoords(const vsg::vec3& pos);
        vsg::vec3 getProjectionSpaceCoords(const vsg::vec3& pos);
        vsg::vec3 getMousePlaneCoords(const QPoint& point, const vsg::dvec3& dragStart);
        void handleSelectDrag(const QPoint& pos);
        void dropInstance(CSVRender::Object* object, float dropHeight);
        float calculateDropHeight(CSVRender::Object* object, float objectHeight);

    public:
        InstanceMode(WorldspaceWidget* worldspaceWidget, QWidget* parent = nullptr);

        void activate(CSVWidget::SceneToolbar* toolbar) override;

        void deactivate(CSVWidget::SceneToolbar* toolbar) override;

        void setEditLock(bool locked) override;

        void primaryOpenPressed(const WorldspaceHitResult& hit) override;

        void primaryEditPressed(const WorldspaceHitResult& hit) override;

        void secondaryEditPressed(const WorldspaceHitResult& hit) override;

        void primarySelectPressed(const WorldspaceHitResult& hit) override;

        void secondarySelectPressed(const WorldspaceHitResult& hit) override;

        void tertiarySelectPressed(const WorldspaceHitResult& hit) override;

        bool primaryEditStartDrag(const QPoint& pos) override;

        bool secondaryEditStartDrag(const QPoint& pos) override;

        bool primarySelectStartDrag(const QPoint& pos) override;

        bool secondarySelectStartDrag(const QPoint& pos) override;

        void drag(const QPoint& pos, int diffX, int diffY, double speedFactor) override;

        void dragCompleted(const QPoint& pos) override;

        /// \note dragAborted will not be called, if the drag is aborted via changing
        /// editing mode
        void dragAborted() override;

        void dragWheel(int diff, double speedFactor) override;

        void dragEnterEvent(QDragEnterEvent* event) override;

        void dropEvent(QDropEvent* event) override;

        int getSubMode() const override;

    signals:

        void requestFocus(const std::string& id);

    private slots:

        void setDragAxis(const char axis);
        void subModeChanged(const std::string& id);
        void deleteSelectedInstances();
        void cloneSelectedInstances();
        void getSelectionGroup(const int group);
        void saveSelectionGroup(const int group);
        void dropToCollision();
    };

    /// \brief Helper class to handle object mask data in safe way
    class DropObjectHeightHandler
    {
    public:
        DropObjectHeightHandler(WorldspaceWidget* worldspacewidget);
        ~DropObjectHeightHandler();
        std::vector<float> mObjectHeights;

    private:
        WorldspaceWidget* mWorldspaceWidget;
        std::vector<bool> mHidden;
    };
}

#endif
