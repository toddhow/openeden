#ifndef CSV_RENDER_INSTANCE_SELECTION_MODE_H
#define CSV_RENDER_INSTANCE_SELECTION_MODE_H

#include <memory>

#include <vsg/core/Array.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Node.h>
#include <vsg/vk/vulkan.h>

class QAction;
class QMenu;
class QObject;
class QPoint;

namespace CSVWidget
{
    class SceneToolbar;
}

#include "instancedragmodes.hpp"
#include "selectionmode.hpp"

namespace CSVRender
{
    class WorldspaceWidget;
    class InstanceSelectionMode : public SelectionMode
    {
        Q_OBJECT

    public:
        InstanceSelectionMode(CSVWidget::SceneToolbar* parent, WorldspaceWidget& worldspaceWidget);

        ~InstanceSelectionMode();

        /// Store the worldspace-coordinate when drag begins
        void setDragStart(const vsg::dvec3& dragStart);

        /// Store the worldspace-coordinate when drag begins
        const vsg::dvec3& getDragStart();

        /// Store the screen-coordinate when drag begins
        void setScreenDragStart(const QPoint& dragStartPoint);

        /// Apply instance selection changes
        void dragEnded(const vsg::dvec3& dragEndPoint, DragMode dragMode);

        void drawSelectionCubeCentre(const vsg::vec3& mousePlanePoint);
        void drawSelectionCubeCorner(const vsg::vec3& mousePlanePoint);
        void drawSelectionSphere(const vsg::vec3& mousePlanePoint);

    protected:
        /// Add context menu items to \a menu.
        ///
        /// \attention menu can be a 0-pointer
        ///
        /// \return Have there been any menu items to be added (if menu is 0 and there
        /// items to be added, the function must return true anyway.
        bool createContextMenu(QMenu* menu) override;

    private:
        void drawSelectionBox(const vsg::dvec3& pointA, const vsg::dvec3& pointB);
        void drawSelectionCube(const vsg::dvec3& point, float radius);
        void drawSelectionSphere(const vsg::dvec3& point, float radius);
        void showVolume(const vsg::dvec3& position, VkPrimitiveTopology topology, vsg::ref_ptr<vsg::vec3Array> vertices,
            vsg::ref_ptr<vsg::vec4Array> colours, vsg::ref_ptr<vsg::Data> indices);
        void removeVolume();

        QAction* mDeleteSelection;
        QAction* mSelectSame;
        vsg::dvec3 mDragStart;
        /// The volume being dragged, rebuilt at each step of the drag as the OSG editor did.
        vsg::ref_ptr<vsg::Node> mBaseNode;
        /// Whether the view that mBaseNode hangs in is still there (SceneHost::aliveToken).
        std::weak_ptr<void> mBaseNodeHostAlive;

    private slots:

        void deleteSelection();
        void selectSame();
    };
}

#endif
