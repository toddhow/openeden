#include "cellwater.hpp"

#include <algorithm>

#include <QAbstractItemModel>
#include <QModelIndex>

#include <apps/opencs/model/world/collection.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/universalid.hpp>

#include <components/esm3/loadland.hpp>
#include <components/pipeline/builder.hpp>
#include <components/render/simplewater.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/setbin.hpp>

#include "../../model/world/cell.hpp"
#include "../../model/world/cellcoordinates.hpp"
#include "../../model/world/data.hpp"

#include "mask.hpp"
#include "scenehost.hpp"

namespace
{
    // The OSG editor's sizes (cellwater.cpp in git history): an interior's plane is twenty cells wide.
    constexpr int sInteriorScalar = 20;
}

namespace CSVRender
{
    const int CellWater::CellSize = ESM::Land::REAL_SIZE;

    CellWater::CellWater(
        SceneHost& host, vsg::Group& cellNode, const std::string& id, const CSMWorld::CellCoordinates& cellCoords)
        : mHost(host)
        , mData(host.data())
        , mId(id)
        , mParentNode(cellNode)
        , mCenter(cellCoords.getX() * CellSize + CellSize / 2.f, cellCoords.getY() * CellSize + CellSize / 2.f)
        , mDeleted(false)
        , mExterior(false)
        , mHasWater(false)
    {
        mWaterGroup = vsg::Group::create();
        mMaskNode = SceneHost::maskNode(Mask_Water, mWaterGroup);
        mHost.compileContext()->compile(mMaskNode);
        mParentNode.addChild(mMaskNode);

        const int cellIndex = mData.getCells().searchId(ESM::RefId::stringRefId(mId));
        if (cellIndex > -1)
        {
            updateCellData(mData.getCells().getRecord(cellIndex));
        }

        // Keep water existence/height up to date
        QAbstractItemModel* cells = mData.getTableModel(CSMWorld::UniversalId::Type_Cells);
        connect(cells, &QAbstractItemModel::dataChanged, this, &CellWater::cellDataChanged);
    }

    CellWater::~CellWater()
    {
        auto& children = mParentNode.children;
        children.erase(
            std::remove(children.begin(), children.end(), vsg::ref_ptr<vsg::Node>(mMaskNode)), children.end());
        mHost.compileContext()->detach(mMaskNode);
    }

    void CellWater::updateCellData(const CSMWorld::Record<CSMWorld::Cell>& cellRecord)
    {
        mDeleted = cellRecord.isDeleted();
        if (!mDeleted)
        {
            const CSMWorld::Cell& cell = cellRecord.get();

            if (mExterior != cell.isExterior() || mHasWater != cell.hasWater())
            {
                mExterior = cellRecord.get().isExterior();
                mHasWater = cellRecord.get().hasWater();

                recreate();
            }

            mHeight = mExterior ? -1.f : cellRecord.get().mWater;
            if (mWater)
                mWater->setHeight(mHeight);
        }
        else
        {
            recreate();
        }
        mHost.requestRedraw();
    }

    void CellWater::reloadAssets()
    {
        recreate();
    }

    void CellWater::cellDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
    {
        const CSMWorld::Collection<CSMWorld::Cell>& cells = mData.getCells();

        int rowStart = -1;
        int rowEnd = -1;

        if (topLeft.parent().isValid())
        {
            rowStart = topLeft.parent().row();
            rowEnd = bottomRight.parent().row();
        }
        else
        {
            rowStart = topLeft.row();
            rowEnd = bottomRight.row();
        }

        for (int row = rowStart; row <= rowEnd; ++row)
        {
            const CSMWorld::Record<CSMWorld::Cell>& cellRecord = cells.getRecord(row);

            if (cellRecord.get().mId == ESM::RefId::stringRefId(mId))
                updateCellData(cellRecord);
        }
    }

    void CellWater::recreate()
    {
        for (auto& child : mWaterGroup->children)
            mHost.compileContext()->detach(child);
        mWaterGroup->children.clear();
        mWater.reset();

        if (mDeleted || !mHasWater)
            return;

        const float radius = (mExterior ? CellSize : CellSize * sInteriorScalar) / 2.f;
        Resource::ResourceSystem& resources = mHost.resources();
        mWater = std::make_unique<Render::SimpleWater>(radius, *resources.builder, resources.textureOptions, false, true);
        mWater->setEnabled(true);
        mWater->setPosition(mCenter);
        mWater->setHeight(mHeight);

        // After everything opaque, as the OSG editor's render bin just before the transparent one.
        auto sorted = vsgUtil::createBinSetter(SceneHost::sBinDepthSorted, mWater->node());
        mHost.compileContext()->compile(sorted);
        mWaterGroup->children = { sorted };
    }
}
