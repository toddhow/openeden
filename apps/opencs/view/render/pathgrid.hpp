#ifndef CSV_RENDER_PATHGRID_H
#define CSV_RENDER_PATHGRID_H

#include <algorithm>
#include <string>
#include <vector>

#include <QString>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>

#include "../../model/world/cellcoordinates.hpp"
#include "../../model/world/subcellcollection.hpp"
#include "tagbase.hpp"
#include <components/esm/refid.hpp>

namespace CSMWorld
{
    class CommandMacro;
    class Data;
    struct Pathgrid;
}

namespace CSVRender
{
    class SceneHost;
    class Pathgrid;

    struct WorldspaceHitResult;

    class PathgridTag : public TagBase
    {
    public:
        PathgridTag(Pathgrid* pathgrid);

        Pathgrid* getPathgrid() const;

        QString getToolTip(bool hideBasics, const WorldspaceHitResult& hit) const override;

    private:
        Pathgrid* mPathgrid;
    };

    class Pathgrid
    {
    public:
        typedef std::vector<unsigned short> NodeList;

        Pathgrid(CSMWorld::Data& data, SceneHost& host, vsg::Group& parent, const std::string& pathgridId,
            const CSMWorld::CellCoordinates& coordinates);

        ~Pathgrid();

        const CSMWorld::CellCoordinates& getCoordinates() const;
        const std::string& getId() const;

        bool isSelected() const;
        const NodeList& getSelected() const;
        void selectAll();
        void toggleSelected(unsigned short node); // Adds to end of vector
        void invertSelected();
        void clearSelected();

        void moveSelected(const vsg::dvec3& offset);
        void setDragOrigin(unsigned short node);
        void setDragEndpoint(unsigned short node);
        void setDragEndpoint(const vsg::dvec3& pos);

        void resetIndicators();

        void applyPoint(CSMWorld::CommandMacro& commands, const vsg::dvec3& worldPos);
        void applyPosition(CSMWorld::CommandMacro& commands);
        void applyEdge(CSMWorld::CommandMacro& commands, unsigned short node1, unsigned short node2);
        void applyEdges(CSMWorld::CommandMacro& commands, unsigned short node);
        void applyRemoveNodes(CSMWorld::CommandMacro& commands);
        void applyRemoveEdges(CSMWorld::CommandMacro& commands);

        vsg::ref_ptr<PathgridTag> getTag() const;

        void recreateGeometry();
        void removeGeometry();

        void update();

    private:
        CSMWorld::Data& mData;
        CSMWorld::SubCellCollection<CSMWorld::Pathgrid>& mPathgridCollection;
        ESM::RefId mId;
        CSMWorld::CellCoordinates mCoords;
        bool mInterior;

        NodeList mSelected;
        vsg::dvec3 mMoveOffset;
        unsigned short mDragOrigin;

        bool mChangeGeometry;
        bool mRemoveGeometry;
        bool mUseOffset;

        SceneHost& mHost;
        vsg::Group& mParent;
        vsg::dvec3 mBasePosition;
        vsg::ref_ptr<vsg::Node> mMaskNode;
        vsg::ref_ptr<vsg::MatrixTransform> mBaseNode;
        vsg::ref_ptr<vsg::Node> mPathgridGeometry;
        vsg::ref_ptr<vsg::Node> mSelectedGeometry;
        vsg::ref_ptr<vsg::Node> mDragGeometry;

        vsg::ref_ptr<PathgridTag> mTag;

        void createGeometry();
        void createSelectedGeometry();
        void createSelectedGeometry(const CSMWorld::Pathgrid& source);
        void removePathgridGeometry();
        void removeSelectedGeometry();

        void createDragGeometry(const vsg::vec3& start, const vsg::vec3& end, bool valid);
        void addGeometry(vsg::ref_ptr<vsg::Node> node);
        void removeNode(vsg::ref_ptr<vsg::Node>& node);

        const CSMWorld::Pathgrid* getPathgridSource();

        int edgeExists(const CSMWorld::Pathgrid& source, unsigned short node1, unsigned short node2);
        void addEdge(CSMWorld::CommandMacro& commands, const CSMWorld::Pathgrid& source, unsigned short node1,
            unsigned short node2);
        void removeEdge(CSMWorld::CommandMacro& commands, const CSMWorld::Pathgrid& source, unsigned short node1,
            unsigned short node2);

        int clampToCell(int v);
    };
}

#endif
