#ifndef CSV_RENDER_CELLWATER_H
#define CSV_RENDER_CELLWATER_H

#include <memory>
#include <string>

#include <QObject>

#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>

#include "../../model/world/record.hpp"

class QModelIndex;

namespace CSMWorld
{
    struct Cell;
    class CellCoordinates;
    class Data;
}

namespace Render
{
    class SimpleWater;
}

namespace CSVRender
{
    class SceneHost;

    /// This class is responsible for rendering water in a cell. Owned by its Cell.
    class CellWater : public QObject
    {
        Q_OBJECT

    public:
        CellWater(SceneHost& host, vsg::Group& cellNode, const std::string& id, const CSMWorld::CellCoordinates& cellCoords);

        ~CellWater();

        void updateCellData(const CSMWorld::Record<CSMWorld::Cell>& cellRecord);

        /// Reloads the underlying water texture.
        void reloadAssets();

    private slots:

        void cellDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

    private:
        void recreate();

        static const int CellSize;

        SceneHost& mHost;
        CSMWorld::Data& mData;
        std::string mId;
        vsg::Group& mParentNode;
        vsg::ref_ptr<vsg::Group> mWaterGroup;
        vsg::ref_ptr<vsg::Switch> mMaskNode;
        std::unique_ptr<Render::SimpleWater> mWater;
        vsg::vec2 mCenter;
        float mHeight = -1.f;
        bool mDeleted;
        bool mExterior;
        bool mHasWater;
    };
}

#endif
