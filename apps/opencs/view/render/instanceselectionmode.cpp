#include "instanceselectionmode.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/render/mask.hpp>
#include <apps/opencs/view/render/selectionmode.hpp>

#include <QAction>
#include <QMenu>
#include <QPoint>

#include <vsg/maths/transform.h>
#include <vsg/nodes/MatrixTransform.h>

#include <components/misc/vsgmath.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/setbin.hpp>

#include "../../model/world/commands.hpp"
#include "../../model/world/idtable.hpp"

#include "colouredgeometry.hpp"
#include "instancedragmodes.hpp"
#include "object.hpp"
#include "scenehost.hpp"
#include "worldspacewidget.hpp"

namespace CSVWidget
{
    class SceneToolbar;
}

namespace
{
    constexpr int sSphereResolution = 32;
    constexpr float sSphereAlpha = 0.3f;

    // The eight corners' colours and twelve faces of the OSG editor's selection box.
    vsg::ref_ptr<vsg::vec4Array> boxColours()
    {
        return vsg::vec4Array::create({ { 0.3f, 0.3f, 0.5f, 0.2f }, { 0.9f, 0.9f, 1.0f, 0.2f },
            { 0.3f, 0.3f, 0.4f, 0.2f }, { 0.9f, 0.9f, 1.0f, 0.2f }, { 0.3f, 0.3f, 0.4f, 0.2f },
            { 0.9f, 0.9f, 1.0f, 0.2f }, { 0.3f, 0.3f, 0.4f, 0.2f }, { 0.9f, 0.9f, 1.0f, 0.2f } });
    }

    vsg::ref_ptr<vsg::ushortArray> boxIndices()
    {
        return vsg::ushortArray::create({ 2, 1, 0, 3, 1, 2, 4, 5, 6, 6, 5, 7, 1, 4, 0, 4, 1, 5, 4, 2, 0, 6, 2, 4, 6, 3,
            2, 7, 3, 6, 1, 3, 5, 5, 3, 7 });
    }
}

namespace CSVRender
{
    class TagBase;

    InstanceSelectionMode::InstanceSelectionMode(CSVWidget::SceneToolbar* parent, WorldspaceWidget& worldspaceWidget)
        : SelectionMode(parent, worldspaceWidget, Mask_Reference)
    {
        mSelectSame = new QAction("Extend selection to instances with same object ID", this);
        mDeleteSelection = new QAction("Delete selected instances", this);

        connect(mSelectSame, &QAction::triggered, this, &InstanceSelectionMode::selectSame);
        connect(mDeleteSelection, &QAction::triggered, this, &InstanceSelectionMode::deleteSelection);
    }

    InstanceSelectionMode::~InstanceSelectionMode()
    {
        removeVolume();
    }

    void InstanceSelectionMode::removeVolume()
    {
        if (!mBaseNode)
            return;
        // Edit modes belong to the toolbar, which Qt destroys after the view: the widget may be gone.
        if (mBaseNodeHostAlive.expired())
        {
            mBaseNode = nullptr;
            return;
        }
        SceneHost* host = getWorldspaceWidget().getHost();
        auto& children = host->contentRoot().children;
        children.erase(std::remove(children.begin(), children.end(), mBaseNode), children.end());
        host->compileContext()->detach(mBaseNode);
        mBaseNode = nullptr;
        host->requestRedraw();
    }

    void InstanceSelectionMode::showVolume(const vsg::dvec3& position, VkPrimitiveTopology topology,
        vsg::ref_ptr<vsg::vec3Array> vertices, vsg::ref_ptr<vsg::vec4Array> colours, vsg::ref_ptr<vsg::Data> indices)
    {
        removeVolume();
        SceneHost* host = getWorldspaceWidget().getHost();
        if (!host)
            return;

        ColouredOptions options;
        options.mBlend = true;
        options.mPickable = false;
        auto transform = vsg::MatrixTransform::create(vsg::translate(position));
        transform->addChild(
            createColouredGeometry(*host->resources().builder, topology, vertices, colours, indices, options));
        mBaseNode = vsgUtil::createBinSetter(SceneHost::sBinDepthSorted, transform);
        mBaseNodeHostAlive = host->aliveToken();
        host->compileContext()->compile(mBaseNode);
        host->contentRoot().addChild(mBaseNode);
        host->requestRedraw();
    }

    void InstanceSelectionMode::setDragStart(const vsg::dvec3& dragStart)
    {
        mDragStart = dragStart;
    }

    const vsg::dvec3& InstanceSelectionMode::getDragStart()
    {
        return mDragStart;
    }

    void InstanceSelectionMode::dragEnded(const vsg::dvec3& dragEndPoint, DragMode dragMode)
    {
        const float dragDistance = static_cast<float>(vsg::length(mDragStart - dragEndPoint));
        removeVolume();
        if (getCurrentId() == "cube-centre")
        {
            const vsg::dvec3 pointA(
                mDragStart[0] - dragDistance, mDragStart[1] - dragDistance, mDragStart[2] - dragDistance);
            const vsg::dvec3 pointB(
                mDragStart[0] + dragDistance, mDragStart[1] + dragDistance, mDragStart[2] + dragDistance);
            getWorldspaceWidget().selectInsideCube(pointA, pointB, dragMode);
        }
        else if (getCurrentId() == "cube-corner")
        {
            getWorldspaceWidget().selectInsideCube(mDragStart, dragEndPoint, dragMode);
        }
        else if (getCurrentId() == "sphere")
        {
            getWorldspaceWidget().selectWithinDistance(mDragStart, dragDistance, dragMode);
        }
    }

    void InstanceSelectionMode::drawSelectionCubeCentre(const vsg::vec3& mousePlanePoint)
    {
        const float dragDistance = static_cast<float>(vsg::length(mDragStart - vsg::dvec3(mousePlanePoint)));
        drawSelectionCube(mDragStart, dragDistance);
    }

    void InstanceSelectionMode::drawSelectionCubeCorner(const vsg::vec3& mousePlanePoint)
    {
        drawSelectionBox(mDragStart, vsg::dvec3(mousePlanePoint));
    }

    void InstanceSelectionMode::drawSelectionBox(const vsg::dvec3& pointA, const vsg::dvec3& pointB)
    {
        const float x = static_cast<float>(pointB[0] - pointA[0]);
        const float y = static_cast<float>(pointB[1] - pointA[1]);
        const float z = static_cast<float>(pointB[2] - pointA[2]);
        auto vertices = vsg::vec3Array::create(
            { { 0, 0, 0 }, { 0, 0, z }, { 0, y, 0 }, { 0, y, z }, { x, 0, 0 }, { x, 0, z }, { x, y, 0 }, { x, y, z } });
        showVolume(pointA, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, vertices, boxColours(), boxIndices());
    }

    void InstanceSelectionMode::drawSelectionCube(const vsg::dvec3& point, float radius)
    {
        const float r = radius;
        auto vertices = vsg::vec3Array::create({ { r, -r, -r }, { r, -r, r }, { r, r, -r }, { r, r, r }, { -r, -r, -r },
            { -r, -r, r }, { -r, r, -r }, { -r, r, r } });
        showVolume(point, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, vertices, boxColours(), boxIndices());
    }

    void InstanceSelectionMode::drawSelectionSphere(const vsg::vec3& mousePlanePoint)
    {
        const float dragDistance = static_cast<float>(vsg::length(mDragStart - vsg::dvec3(mousePlanePoint)));
        drawSelectionSphere(mDragStart, dragDistance);
    }

    void InstanceSelectionMode::drawSelectionSphere(const vsg::dvec3& point, float radius)
    {
        constexpr int resolution = sSphereResolution;
        const float radiusPerResolution = radius / resolution;
        const float reciprocalResolution = 1.0f / resolution;
        const float doubleReciprocalRes = reciprocalResolution * 2;

        std::vector<vsg::vec3> points;
        std::vector<vsg::vec4> pointColours;

        for (int i = 0; i <= resolution; i += 2)
        {
            const float iShifted = (static_cast<float>(i) - resolution / 2.0f); // i - 16 = -16 ... 16
            const float xPercentile = iShifted * doubleReciprocalRes;
            const float x = xPercentile * radius;
            const float thisRadius = std::sqrt(std::max(radius * radius - x * x, 0.f));

            // the next row
            const float iShifted2 = (static_cast<float>(i + 1) - resolution / 2.0f);
            const float xPercentile2 = iShifted2 * doubleReciprocalRes;
            const float x2 = xPercentile2 * radius;
            const float thisRadius2 = std::sqrt(std::max(radius * radius - x2 * x2, 0.f));

            for (int j = 0; j < resolution; ++j)
            {
                const float angle = j * reciprocalResolution * Misc::VsgMath::PIf * 2;
                const float vertexX = thisRadius * std::sin(angle);
                const float vertexY = i * radiusPerResolution * 2 - radius;
                const float vertexZ = thisRadius * std::cos(angle);
                const float heightPercentage = (vertexZ + radius) / (radius * 2);
                points.emplace_back(vertexX, vertexY, vertexZ);
                pointColours.emplace_back(heightPercentage, heightPercentage, heightPercentage, sSphereAlpha);

                points.emplace_back(thisRadius2 * std::sin(angle), (i + 1) * radiusPerResolution * 2 - radius,
                    thisRadius2 * std::cos(angle));
                pointColours.emplace_back(heightPercentage, heightPercentage, heightPercentage, sSphereAlpha);
            }
        }

        std::vector<unsigned short> strip;
        const int count = static_cast<int>(points.size());
        const auto add = [&](int index) { strip.push_back(static_cast<unsigned short>(index)); };

        for (int i = 0; i < resolution; ++i)
        {
            // Even
            for (int j = 0; j < resolution * 2; ++j)
            {
                if (i * resolution * 2 + j > count - 1)
                    continue;
                add(i * resolution * 2 + j);
            }
            if (i * resolution * 2 > count - 1)
                continue;
            add(i * resolution * 2);
            add(i * resolution * 2 + 1);

            // Odd
            for (int j = 1; j < resolution * 2 - 2; j += 2)
            {
                if ((i + 1) * resolution * 2 + j - 1 > count - 1)
                    continue;
                add((i + 1) * resolution * 2 + j - 1);
                add(i * resolution * 2 + j + 2);
            }
            if ((i + 2) * resolution * 2 - 2 > count - 1)
                continue;
            add((i + 2) * resolution * 2 - 2);
            add(i * resolution * 2 + 1);
            add((i + 1) * resolution * 2);
        }

        auto vertices = vsg::vec3Array::create(static_cast<uint32_t>(points.size()));
        std::copy(points.begin(), points.end(), vertices->begin());
        auto colours = vsg::vec4Array::create(static_cast<uint32_t>(pointColours.size()));
        std::copy(pointColours.begin(), pointColours.end(), colours->begin());
        auto indices = vsg::ushortArray::create(static_cast<uint32_t>(strip.size()));
        std::copy(strip.begin(), strip.end(), indices->begin());
        showVolume(point, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, vertices, colours, indices);
    }

    bool InstanceSelectionMode::createContextMenu(QMenu* menu)
    {
        if (menu)
        {
            SelectionMode::createContextMenu(menu);

            menu->addAction(mSelectSame);
            menu->addAction(mDeleteSelection);
        }

        return true;
    }

    void InstanceSelectionMode::selectSame()
    {
        getWorldspaceWidget().selectAllWithSameParentId(Mask_Reference);
    }

    void InstanceSelectionMode::deleteSelection()
    {
        std::vector<vsg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getSelection(Mask_Reference);

        CSMWorld::IdTable& referencesTable = dynamic_cast<CSMWorld::IdTable&>(
            *getWorldspaceWidget().getDocument().getData().getTableModel(CSMWorld::UniversalId::Type_References));

        for (std::vector<vsg::ref_ptr<TagBase>>::iterator iter = selection.begin(); iter != selection.end(); ++iter)
        {
            CSMWorld::DeleteCommand* command = new CSMWorld::DeleteCommand(
                referencesTable, static_cast<ObjectTag*>(iter->get())->mObject->getReferenceId());

            getWorldspaceWidget().getDocument().getUndoStack().push(command);
        }
    }
}
