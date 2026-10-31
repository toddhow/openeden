#include "pathgrid.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <stddef.h>


#include <apps/opencs/model/world/cell.hpp>
#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/pathgrid.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/subcellcollection.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/render/mask.hpp>
#include <apps/opencs/view/render/tagbase.hpp>

#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/loadpgrd.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/compilecontext.hpp>

#include "../../model/world/commandmacro.hpp"
#include "../../model/world/commands.hpp"
#include "../../model/world/data.hpp"
#include "../../model/world/idtree.hpp"
#include "colouredgeometry.hpp"
#include "mask.hpp"
#include "pathgridgeometry.hpp"
#include "picking.hpp"
#include "scenehost.hpp"
#include "worldspacewidget.hpp"

#include <vsg/maths/transform.h>
#include <vsg/nodes/VertexIndexDraw.h>

namespace CSVRender
{
    namespace
    {
        vsg::ref_ptr<vsg::vec3Array> toArray(const std::vector<vsg::vec3>& values)
        {
            auto array = vsg::vec3Array::create(static_cast<uint32_t>(values.size()));
            std::copy(values.begin(), values.end(), array->begin());
            return array;
        }

        vsg::ref_ptr<vsg::vec4Array> toArray(const std::vector<vsg::vec4>& values)
        {
            auto array = vsg::vec4Array::create(static_cast<uint32_t>(values.size()));
            std::copy(values.begin(), values.end(), array->begin());
            return array;
        }

        vsg::ref_ptr<vsg::Node> indexedDraw(SceneHost& host, VkPrimitiveTopology topology, vsg::ref_ptr<vsg::vec3Array> vertices,
            vsg::ref_ptr<vsg::vec4Array> colours, const std::vector<unsigned>& indices)
        {
            auto indexArray = vsg::uintArray::create(static_cast<uint32_t>(indices.size()));
            std::copy(indices.begin(), indices.end(), indexArray->begin());
            ColouredOptions options;
            // The diamonds keep their old faces for rays, and cull nothing, as the OSG editor set no culling.
            return createColouredGeometry(*host.resources().builder, topology, vertices, colours, indexArray, options);
        }
    }

    PathgridTag::PathgridTag(Pathgrid* pathgrid)
        : TagBase(Mask_Pathgrid)
        , mPathgrid(pathgrid)
    {
    }

    Pathgrid* PathgridTag::getPathgrid() const
    {
        return mPathgrid;
    }

    QString PathgridTag::getToolTip(bool /*hideBasics*/, const WorldspaceHitResult& hit) const
    {
        QString text("Pathgrid: ");
        text += mPathgrid->getId().c_str();
        text += " (";
        text += QString::number(PathgridGeometry::nodeOfVertex(hit.index0));
        text += ")";

        return text;
    }

    Pathgrid::Pathgrid(CSMWorld::Data& data, SceneHost& host, vsg::Group& parent, const std::string& pathgridId,
        const CSMWorld::CellCoordinates& coordinates)
        : mData(data)
        , mPathgridCollection(mData.getPathgrids())
        , mId(ESM::RefId::stringRefId(pathgridId))
        , mCoords(coordinates)
        , mInterior(false)
        , mDragOrigin(0)
        , mChangeGeometry(true)
        , mRemoveGeometry(false)
        , mUseOffset(true)
        , mHost(host)
        , mParent(parent)
        , mTag(new PathgridTag(this))
    {
        const float coordScalar = ESM::Land::REAL_SIZE;

        mBasePosition = vsg::dvec3(mCoords.getX() * coordScalar, mCoords.getY() * coordScalar, 0.0);
        mBaseNode = vsg::MatrixTransform::create(vsg::translate(mBasePosition));
        Picking::setTag(*mBaseNode, mTag);
        mMaskNode = SceneHost::maskNode(Mask_Pathgrid, mBaseNode);
        mHost.compileContext()->compile(mMaskNode);
        mParent.addChild(mMaskNode);

        recreateGeometry();

        int index = mData.getCells().searchId(mId);
        if (index != -1)
        {
            const CSMWorld::Cell& cell = mData.getCells().getRecord(index).get();
            mInterior = cell.mData.mFlags & ESM::Cell::Interior;
        }
    }

    Pathgrid::~Pathgrid()
    {
        auto& children = mParent.children;
        children.erase(std::remove(children.begin(), children.end(), mMaskNode), children.end());
        mHost.compileContext()->detach(mMaskNode);
    }

    const CSMWorld::CellCoordinates& Pathgrid::getCoordinates() const
    {
        return mCoords;
    }

    const std::string& Pathgrid::getId() const
    {
        return mId.getRefIdString();
    }

    bool Pathgrid::isSelected() const
    {
        return !mSelected.empty();
    }

    const Pathgrid::NodeList& Pathgrid::getSelected() const
    {
        return mSelected;
    }

    void Pathgrid::selectAll()
    {
        mSelected.clear();

        const CSMWorld::Pathgrid* source = getPathgridSource();
        if (source)
        {
            for (unsigned short i = 0; i < static_cast<unsigned short>(source->mPoints.size()); ++i)
                mSelected.push_back(i);

            createSelectedGeometry(*source);
        }
        else
        {
            removeSelectedGeometry();
        }
    }

    void Pathgrid::toggleSelected(unsigned short node)
    {
        NodeList::iterator searchResult = std::find(mSelected.begin(), mSelected.end(), node);
        if (searchResult != mSelected.end())
        {
            mSelected.erase(searchResult);
        }
        else
        {
            mSelected.push_back(node);
        }

        createSelectedGeometry();
    }

    void Pathgrid::invertSelected()
    {
        NodeList temp = NodeList(mSelected);
        mSelected.clear();

        const CSMWorld::Pathgrid* source = getPathgridSource();
        if (source)
        {
            for (unsigned short i = 0; i < static_cast<unsigned short>(source->mPoints.size()); ++i)
            {
                if (std::find(temp.begin(), temp.end(), i) == temp.end())
                    mSelected.push_back(i);
            }

            createSelectedGeometry(*source);
        }
        else
        {
            removeSelectedGeometry();
        }
    }

    void Pathgrid::clearSelected()
    {
        mSelected.clear();
        removeSelectedGeometry();
    }

    void Pathgrid::moveSelected(const vsg::dvec3& offset)
    {
        mUseOffset = true;
        mMoveOffset = mMoveOffset + offset;

        recreateGeometry();
    }

    void Pathgrid::setDragOrigin(unsigned short node)
    {
        mDragOrigin = node;
    }

    void Pathgrid::setDragEndpoint(unsigned short node)
    {
        const CSMWorld::Pathgrid* source = getPathgridSource();
        if (source)
        {
            const CSMWorld::Pathgrid::Point& pointA = source->mPoints[mDragOrigin];
            const CSMWorld::Pathgrid::Point& pointB = source->mPoints[node];

            const vsg::vec3 start(pointA.mX, pointA.mY, pointA.mZ + PathgridGeometry::sDiamondHalfHeight);
            const vsg::vec3 end(pointB.mX, pointB.mY, pointB.mZ + PathgridGeometry::sDiamondHalfHeight);

            createDragGeometry(start, end, true);
        }
    }

    void Pathgrid::setDragEndpoint(const vsg::dvec3& pos)
    {
        const CSMWorld::Pathgrid* source = getPathgridSource();
        if (source)
        {
            const CSMWorld::Pathgrid::Point& point = source->mPoints[mDragOrigin];

            const vsg::vec3 start(point.mX, point.mY, point.mZ + PathgridGeometry::sDiamondHalfHeight);
            const vsg::vec3 end(pos - mBasePosition);
            createDragGeometry(start, end, false);
        }
    }

    void Pathgrid::resetIndicators()
    {
        mUseOffset = false;
        mMoveOffset = vsg::dvec3();

        removeNode(mDragGeometry);
    }

    void Pathgrid::applyPoint(CSMWorld::CommandMacro& commands, const vsg::dvec3& worldPos)
    {
        CSMWorld::IdTree* model
            = &dynamic_cast<CSMWorld::IdTree&>(*mData.getTableModel(CSMWorld::UniversalId::Type_Pathgrids));
        const std::string& idString = mId.getRefIdString();
        const CSMWorld::Pathgrid* source = getPathgridSource();
        if (source)
        {
            const vsg::dvec3 localCoords = worldPos - mBasePosition;

            int posX = clampToCell(static_cast<int>(localCoords.x));
            int posY = clampToCell(static_cast<int>(localCoords.y));
            int posZ = clampToCell(static_cast<int>(localCoords.z));

            int recordIndex = mPathgridCollection.getIndex(mId);
            int parentColumn = mPathgridCollection.findColumnIndex(CSMWorld::Columns::ColumnId_PathgridPoints);

            int posXColumn
                = mPathgridCollection.searchNestedColumnIndex(parentColumn, CSMWorld::Columns::ColumnId_PathgridPosX);

            int posYColumn
                = mPathgridCollection.searchNestedColumnIndex(parentColumn, CSMWorld::Columns::ColumnId_PathgridPosY);

            int posZColumn
                = mPathgridCollection.searchNestedColumnIndex(parentColumn, CSMWorld::Columns::ColumnId_PathgridPosZ);

            QModelIndex parent = model->index(recordIndex, parentColumn);
            int row = static_cast<int>(source->mPoints.size());

            // Add node to end of list
            commands.push(new CSMWorld::AddNestedCommand(*model, idString, row, parentColumn));
            commands.push(new CSMWorld::ModifyCommand(*model, model->index(row, posXColumn, parent), posX));
            commands.push(new CSMWorld::ModifyCommand(*model, model->index(row, posYColumn, parent), posY));
            commands.push(new CSMWorld::ModifyCommand(*model, model->index(row, posZColumn, parent), posZ));
        }
        else
        {
            int index = mPathgridCollection.searchId(mId);
            if (index == -1)
            {
                // Does not exist
                commands.push(new CSMWorld::CreatePathgridCommand(*model, idString));
            }
            else
            {
                // A copy: the commands replace the record.
                const CSMWorld::Pathgrid copy = mPathgridCollection.getRecord(index).get();
                source = &copy;

                // Deleted, so revert and remove all data
                commands.push(new CSMWorld::RevertCommand(*model, idString));

                int parentColumn = mPathgridCollection.findColumnIndex(CSMWorld::Columns::ColumnId_PathgridPoints);
                for (size_t row = source->mPoints.size(); row > 0; --row)
                {
                    commands.push(
                        new CSMWorld::DeleteNestedCommand(*model, idString, static_cast<int>(row - 1), parentColumn));
                }

                parentColumn = mPathgridCollection.findColumnIndex(CSMWorld::Columns::ColumnId_PathgridEdges);
                for (size_t row = source->mEdges.size(); row > 0; --row)
                {
                    commands.push(
                        new CSMWorld::DeleteNestedCommand(*model, idString, static_cast<int>(row - 1), parentColumn));
                }
            }
        }
    }

    void Pathgrid::applyPosition(CSMWorld::CommandMacro& commands)
    {
        const CSMWorld::Pathgrid* recordSource = getPathgridSource();
        if (recordSource)
        {
            // A copy: the commands replace the record, so a pointer into it would dangle after the first push.
            const CSMWorld::Pathgrid copy = *recordSource;
            const CSMWorld::Pathgrid* source = &copy;
            const vsg::dvec3 localCoords = mMoveOffset;

            int offsetX = static_cast<int>(localCoords.x);
            int offsetY = static_cast<int>(localCoords.y);
            int offsetZ = static_cast<int>(localCoords.z);

            QAbstractItemModel* model = mData.getTableModel(CSMWorld::UniversalId::Type_Pathgrids);

            int recordIndex = mPathgridCollection.getIndex(mId);
            int parentColumn = mPathgridCollection.findColumnIndex(CSMWorld::Columns::ColumnId_PathgridPoints);

            int posXColumn
                = mPathgridCollection.searchNestedColumnIndex(parentColumn, CSMWorld::Columns::ColumnId_PathgridPosX);

            int posYColumn
                = mPathgridCollection.searchNestedColumnIndex(parentColumn, CSMWorld::Columns::ColumnId_PathgridPosY);

            int posZColumn
                = mPathgridCollection.searchNestedColumnIndex(parentColumn, CSMWorld::Columns::ColumnId_PathgridPosZ);

            QModelIndex parent = model->index(recordIndex, parentColumn);

            for (const auto& selected : mSelected)
            {
                const CSMWorld::Pathgrid::Point& point = source->mPoints[selected];
                int row = static_cast<int>(selected);

                commands.push(new CSMWorld::ModifyCommand(
                    *model, model->index(row, posXColumn, parent), clampToCell(point.mX + offsetX)));

                commands.push(new CSMWorld::ModifyCommand(
                    *model, model->index(row, posYColumn, parent), clampToCell(point.mY + offsetY)));

                commands.push(new CSMWorld::ModifyCommand(
                    *model, model->index(row, posZColumn, parent), clampToCell(point.mZ + offsetZ)));
            }
        }
    }

    void Pathgrid::applyEdge(CSMWorld::CommandMacro& commands, unsigned short node1, unsigned short node2)
    {
        const CSMWorld::Pathgrid* recordSource = getPathgridSource();
        if (recordSource)
        {
            // A copy: the commands replace the record, so a pointer into it would dangle after the first push.
            const CSMWorld::Pathgrid copy = *recordSource;
            const CSMWorld::Pathgrid* source = &copy;
            addEdge(commands, *source, node1, node2);
        }
    }

    void Pathgrid::applyEdges(CSMWorld::CommandMacro& commands, unsigned short node)
    {
        const CSMWorld::Pathgrid* recordSource = getPathgridSource();
        if (recordSource)
        {
            // A copy: the commands replace the record, so a pointer into it would dangle after the first push.
            const CSMWorld::Pathgrid copy = *recordSource;
            const CSMWorld::Pathgrid* source = &copy;
            for (const auto& selected : mSelected)
            {
                addEdge(commands, *source, node, selected);
            }
        }
    }

    void Pathgrid::applyRemoveNodes(CSMWorld::CommandMacro& commands)
    {
        const CSMWorld::Pathgrid* recordSource = getPathgridSource();
        if (recordSource)
        {
            // A copy: the commands replace the record, so a pointer into it would dangle after the first push.
            const CSMWorld::Pathgrid copy = *recordSource;
            const CSMWorld::Pathgrid* source = &copy;
            CSMWorld::IdTree* model
                = &dynamic_cast<CSMWorld::IdTree&>(*mData.getTableModel(CSMWorld::UniversalId::Type_Pathgrids));

            // Want to remove nodes from end of list first
            std::sort(mSelected.begin(), mSelected.end(), std::greater<int>());

            int recordIndex = mPathgridCollection.getIndex(mId);
            int parentColumn = mPathgridCollection.findColumnIndex(CSMWorld::Columns::ColumnId_PathgridPoints);

            for (std::vector<unsigned short>::iterator row = mSelected.begin(); row != mSelected.end(); ++row)
            {
                commands.push(new CSMWorld::DeleteNestedCommand(
                    *model, mId.getRefIdString(), static_cast<int>(*row), parentColumn));
            }

            // Fix/remove edges
            std::set<int, std::greater<int>> edgeRowsToRemove;

            parentColumn = mPathgridCollection.findColumnIndex(CSMWorld::Columns::ColumnId_PathgridEdges);

            int edge0Column
                = mPathgridCollection.searchNestedColumnIndex(parentColumn, CSMWorld::Columns::ColumnId_PathgridEdge0);

            int edge1Column
                = mPathgridCollection.searchNestedColumnIndex(parentColumn, CSMWorld::Columns::ColumnId_PathgridEdge1);

            QModelIndex parent = model->index(recordIndex, parentColumn);

            for (size_t edge = 0; edge < source->mEdges.size(); ++edge)
            {
                size_t adjustment0 = 0;
                size_t adjustment1 = 0;

                // Determine necessary adjustment
                for (const auto point : mSelected)
                {
                    if (source->mEdges[edge].mV0 == point || source->mEdges[edge].mV1 == point)
                    {
                        edgeRowsToRemove.insert(static_cast<int>(edge));

                        adjustment0 = 0; // No need to adjust, its getting removed
                        adjustment1 = 0;
                        break;
                    }

                    if (source->mEdges[edge].mV0 > point)
                        ++adjustment0;

                    if (source->mEdges[edge].mV1 > point)
                        ++adjustment1;
                }

                if (adjustment0 != 0)
                {
                    int adjustedEdge = static_cast<int>(source->mEdges[edge].mV0 - adjustment0);
                    commands.push(new CSMWorld::ModifyCommand(
                        *model, model->index(static_cast<int>(edge), edge0Column, parent), adjustedEdge));
                }

                if (adjustment1 != 0)
                {
                    int adjustedEdge = static_cast<int>(source->mEdges[edge].mV1 - adjustment1);
                    commands.push(new CSMWorld::ModifyCommand(
                        *model, model->index(static_cast<int>(edge), edge1Column, parent), adjustedEdge));
                }
            }

            for (const auto row : edgeRowsToRemove)
            {
                commands.push(new CSMWorld::DeleteNestedCommand(*model, mId.getRefIdString(), row, parentColumn));
            }
        }

        clearSelected();
    }

    void Pathgrid::applyRemoveEdges(CSMWorld::CommandMacro& commands)
    {
        const CSMWorld::Pathgrid* source = getPathgridSource();
        if (source)
        {
            // Want to remove from end of row first
            std::set<int, std::greater<int>> rowsToRemove;
            for (size_t i = 0; i <= mSelected.size(); ++i)
            {
                for (size_t j = i + 1; j < mSelected.size(); ++j)
                {
                    int row = edgeExists(*source, mSelected[i], mSelected[j]);
                    if (row != -1)
                    {
                        rowsToRemove.insert(row);
                    }

                    row = edgeExists(*source, mSelected[j], mSelected[i]);
                    if (row != -1)
                    {
                        rowsToRemove.insert(row);
                    }
                }
            }

            CSMWorld::IdTree* model
                = &dynamic_cast<CSMWorld::IdTree&>(*mData.getTableModel(CSMWorld::UniversalId::Type_Pathgrids));
            int parentColumn = mPathgridCollection.findColumnIndex(CSMWorld::Columns::ColumnId_PathgridEdges);

            std::set<int, std::greater<int>>::iterator row;
            for (row = rowsToRemove.begin(); row != rowsToRemove.end(); ++row)
            {
                commands.push(new CSMWorld::DeleteNestedCommand(*model, mId.getRefIdString(), *row, parentColumn));
            }
        }
    }

    vsg::ref_ptr<PathgridTag> Pathgrid::getTag() const
    {
        return mTag;
    }

    void Pathgrid::recreateGeometry()
    {
        mChangeGeometry = true;
    }

    void Pathgrid::removeGeometry()
    {
        mRemoveGeometry = true;
    }

    void Pathgrid::update()
    {
        if (mRemoveGeometry)
        {
            removePathgridGeometry();
            removeSelectedGeometry();
        }
        else if (mChangeGeometry)
        {
            createGeometry();
        }

        mChangeGeometry = false;
        mRemoveGeometry = false;
    }

    void Pathgrid::createGeometry()
    {
        const CSMWorld::Pathgrid* source = getPathgridSource();
        if (source)
        {
            CSMWorld::Pathgrid temp;
            if (mUseOffset)
            {
                temp = *source;

                for (NodeList::iterator it = mSelected.begin(); it != mSelected.end(); ++it)
                {
                    temp.mPoints[*it].mX += mMoveOffset.x;
                    temp.mPoints[*it].mY += mMoveOffset.y;
                    temp.mPoints[*it].mZ += mMoveOffset.z;
                }

                source = &temp;
            }

            removePathgridGeometry();
            const PathgridGeometry::Grid grid = PathgridGeometry::createGrid(*source);
            if (!grid.mVertices.empty())
            {
                auto vertices = toArray(grid.mVertices);
                auto colours = toArray(grid.mColours);
                auto group = vsg::Group::create();
                group->addChild(indexedDraw(mHost, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, vertices, colours, grid.mTriangles));
                if (!grid.mLines.empty())
                    group->addChild(indexedDraw(mHost, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, vertices, colours, grid.mLines));
                mPathgridGeometry = group;
                addGeometry(mPathgridGeometry);
            }

            createSelectedGeometry(*source);
        }
        else
        {
            removePathgridGeometry();
            removeSelectedGeometry();
        }
    }

    void Pathgrid::createSelectedGeometry()
    {
        const CSMWorld::Pathgrid* source = getPathgridSource();
        if (source)
        {
            createSelectedGeometry(*source);
        }
        else
        {
            removeSelectedGeometry();
        }
    }

    void Pathgrid::createSelectedGeometry(const CSMWorld::Pathgrid& source)
    {
        removeSelectedGeometry();

        const PathgridGeometry::Grid wire = PathgridGeometry::createSelectedWireframe(source, mSelected);
        if (wire.mVertices.empty())
            return;
        mSelectedGeometry
            = indexedDraw(mHost, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, toArray(wire.mVertices), toArray(wire.mColours), wire.mLines);
        addGeometry(mSelectedGeometry);
    }

    void Pathgrid::removePathgridGeometry()
    {
        removeNode(mPathgridGeometry);
    }

    void Pathgrid::removeSelectedGeometry()
    {
        removeNode(mSelectedGeometry);
    }

    void Pathgrid::addGeometry(vsg::ref_ptr<vsg::Node> node)
    {
        mHost.compileContext()->compile(node);
        mBaseNode->addChild(node);
        mHost.requestRedraw();
    }

    void Pathgrid::removeNode(vsg::ref_ptr<vsg::Node>& node)
    {
        if (!node)
            return;
        auto& children = mBaseNode->children;
        children.erase(std::remove(children.begin(), children.end(), node), children.end());
        mHost.compileContext()->detach(node);
        node = nullptr;
        mHost.requestRedraw();
    }

    void Pathgrid::createDragGeometry(const vsg::vec3& start, const vsg::vec3& end, bool valid)
    {
        removeNode(mDragGeometry);

        const vsg::vec4 colour = valid ? vsg::vec4(0.91f, 0.66f, 1.f, 1.f) : vsg::vec4(1.f, 0.f, 0.f, 1.f);
        ColouredOptions options;
        options.mPickable = false;
        mDragGeometry = createColouredGeometry(*mHost.resources().builder, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            vsg::vec3Array::create({ start, end }), vsg::vec4Array::create(2, colour), vsg::ref_ptr<vsg::Data>(), options);
        addGeometry(mDragGeometry);
    }

    const CSMWorld::Pathgrid* Pathgrid::getPathgridSource()
    {
        int index = mPathgridCollection.searchId(mId);
        if (index != -1 && !mPathgridCollection.getRecord(index).isDeleted())
        {
            return &mPathgridCollection.getRecord(index).get();
        }

        return nullptr;
    }

    int Pathgrid::edgeExists(const CSMWorld::Pathgrid& source, unsigned short node1, unsigned short node2)
    {
        for (size_t i = 0; i < source.mEdges.size(); ++i)
        {
            if (source.mEdges[i].mV0 == node1 && source.mEdges[i].mV1 == node2)
                return static_cast<int>(i);
        }

        return -1;
    }

    void Pathgrid::addEdge(
        CSMWorld::CommandMacro& commands, const CSMWorld::Pathgrid& source, unsigned short node1, unsigned short node2)
    {
        CSMWorld::IdTree* model
            = &dynamic_cast<CSMWorld::IdTree&>(*mData.getTableModel(CSMWorld::UniversalId::Type_Pathgrids));

        int recordIndex = mPathgridCollection.getIndex(mId);
        int parentColumn = mPathgridCollection.findColumnIndex(CSMWorld::Columns::ColumnId_PathgridEdges);

        int edge0Column
            = mPathgridCollection.searchNestedColumnIndex(parentColumn, CSMWorld::Columns::ColumnId_PathgridEdge0);

        int edge1Column
            = mPathgridCollection.searchNestedColumnIndex(parentColumn, CSMWorld::Columns::ColumnId_PathgridEdge1);

        QModelIndex parent = model->index(recordIndex, parentColumn);
        int row = static_cast<int>(source.mEdges.size());

        if (edgeExists(source, node1, node2) == -1)
        {
            commands.push(new CSMWorld::AddNestedCommand(*model, mId.getRefIdString(), row, parentColumn));
            commands.push(new CSMWorld::ModifyCommand(*model, model->index(row, edge0Column, parent), node1));
            commands.push(new CSMWorld::ModifyCommand(*model, model->index(row, edge1Column, parent), node2));
            ++row;
        }

        if (edgeExists(source, node2, node1) == -1)
        {
            commands.push(new CSMWorld::AddNestedCommand(*model, mId.getRefIdString(), row, parentColumn));
            commands.push(new CSMWorld::ModifyCommand(*model, model->index(row, edge0Column, parent), node2));
            commands.push(new CSMWorld::ModifyCommand(*model, model->index(row, edge1Column, parent), node1));
        }
    }

    int Pathgrid::clampToCell(int v)
    {
        const int cellExtent = ESM::Land::REAL_SIZE;

        if (mInterior)
            return v;
        else if (v > cellExtent)
            return cellExtent;
        else if (v < 0)
            return 0;
        else
            return v;
    }
}
