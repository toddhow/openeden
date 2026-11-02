#include "tileviewer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace CSVLod
{
    namespace
    {
        // A tile picture is shown at least this many pixels wide (nearest neighbour), so 33 x 33 stays readable.
        constexpr int minPictureWidth = 198;
        // The map is first drawn about this many pixels along its longer side.
        constexpr int wantedMapSize = 512;
        constexpr int maxMapScale = 16;
        constexpr double defaultCameraHeight = 3000.0;
        constexpr int tableColumns = 9;

        QImage toQImage(const CSMLod::RgbaImage& image)
        {
            if (image.empty())
                return {};
            return QImage(image.mPixels.data(), static_cast<int>(image.mWidth), static_cast<int>(image.mHeight),
                static_cast<qsizetype>(image.mWidth) * 4, QImage::Format_RGBA8888)
                .copy();
        }

        QDoubleSpinBox* makeSpin(double min, double max, double value, int decimals, double step)
        {
            auto* spin = new QDoubleSpinBox;
            spin->setDecimals(decimals);
            spin->setRange(min, max);
            spin->setSingleStep(step);
            spin->setValue(value);
            return spin;
        }

        int modeIndex(CSMLod::ForcedMode mode)
        {
            switch (mode)
            {
                case CSMLod::ForcedMode::AllTile:
                    return 1;
                case CSMLod::ForcedMode::AllLive:
                    return 2;
                case CSMLod::ForcedMode::Auto:
                    break;
            }
            return 0;
        }
    }

    TileViewer::TileViewer(QWidget* parent)
        : QWidget(parent, Qt::Window)
    {
        setWindowTitle(tr("LOD Tile Viewer"));
        setObjectName("lod-tile-viewer");

        auto* split = new QSplitter(Qt::Horizontal);

        mTable = new QTableWidget(0, tableColumns);
        mTable->setHorizontalHeaderLabels({ tr("Key"), tr("Level"), tr("Tile X"), tr("Tile Y"), tr("Cell X"),
            tr("Cell Y"), tr("Error"), tr("Bytes"), tr("Flags") });
        mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        mTable->setSelectionMode(QAbstractItemView::SingleSelection);
        mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        mTable->verticalHeader()->setVisible(false);
        connect(mTable, &QTableWidget::currentCellChanged, this, [this](int row) { showTile(row); });
        split->addWidget(mTable);

        auto* right = new QWidget;
        auto* rightLayout = new QVBoxLayout(right);

        auto* pictures = new QHBoxLayout;
        const QString names[3] = { tr("Height"), tr("Normals"), tr("Albedo") };
        for (int i = 0; i < 3; ++i)
        {
            auto* column = new QVBoxLayout;
            column->addWidget(new QLabel(names[i]));
            mPictureLabels[i] = new QLabel;
            mPictureLabels[i]->setMinimumSize(minPictureWidth, minPictureWidth);
            mPictureLabels[i]->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            column->addWidget(mPictureLabels[i]);
            pictures->addLayout(column);
        }
        rightLayout->addLayout(pictures);

        auto* form = new QFormLayout;
        const QString cameraNames[3] = { tr("Camera X"), tr("Camera Y"), tr("Camera Z") };
        for (int i = 0; i < 3; ++i)
        {
            mCamera[i] = makeSpin(-1e7, 1e7, 0, 1, 256);
            form->addRow(cameraNames[i], mCamera[i]);
        }
        mHeading = makeSpin(-360, 360, 0, 1, 15);
        mFov = makeSpin(1, 179, 60, 1, 5);
        mViewport = makeSpin(1, 16384, 720, 0, 60);
        mThreshold = makeSpin(0, 100000, CSMLod::WorldGridOptions{}.mThreshold, 2, 1);
        mMode = new QComboBox;
        mMode->addItems({ tr("Auto"), tr("Force all Tile"), tr("Force all Live") });
        mScale = new QSpinBox;
        mScale->setRange(1, maxMapScale);
        mScale->setValue(2);
        form->addRow(tr("Heading (deg, 0 north, 90 east)"), mHeading);
        form->addRow(tr("Vertical fov (deg)"), mFov);
        form->addRow(tr("Viewport height (px)"), mViewport);
        form->addRow(tr("Threshold (px)"), mThreshold);
        form->addRow(tr("Mode"), mMode);
        form->addRow(tr("Map pixels per cell"), mScale);
        rightLayout->addLayout(form);

        for (QDoubleSpinBox* spin : { mCamera[0], mCamera[1], mCamera[2], mHeading, mFov, mViewport, mThreshold })
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this]() { controlsChanged(); });
        connect(mMode, &QComboBox::currentIndexChanged, this, [this]() { controlsChanged(); });
        connect(mScale, &QSpinBox::valueChanged, this, [this]() { controlsChanged(); });

        mSummary = new QLabel;
        rightLayout->addWidget(mSummary);
        mMapLabel = new QLabel;
        mMapLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        auto* scroll = new QScrollArea;
        scroll->setWidget(mMapLabel);
        scroll->setMinimumHeight(240);
        rightLayout->addWidget(scroll, 1);

        split->addWidget(right);
        split->setStretchFactor(1, 1);

        auto* layout = new QVBoxLayout(this);
        layout->addWidget(split);
        resize(1100, 800);
    }

    TileViewer::~TileViewer() = default;

    bool TileViewer::openPack(const QString& path, QString& error)
    {
        Lod::LodError failure;
        std::unique_ptr<CSMLod::TileModel> model
            = CSMLod::TileModel::open(std::filesystem::path(path.toStdU16String()), failure);
        if (!model)
        {
            error = QString::fromStdString(failure.describe());
            return false;
        }
        mGrid.reset();
        mModel = std::move(model);
        mGrid = std::make_unique<CSMLod::WorldGridView>(*mModel);
        rebuildTable();

        // Start with the camera over the middle of the tiles and a map scale that fills the picture.
        const auto& rows = mModel->rows();
        CSMLod::WorldGridOptions defaults;
        if (!rows.empty())
        {
            double minX = rows.front().mOriginX, maxX = minX, minY = rows.front().mOriginY, maxY = minY;
            for (const CSMLod::TileRow& row : rows)
            {
                minX = std::min<double>(minX, row.mOriginX);
                minY = std::min<double>(minY, row.mOriginY);
                maxX = std::max<double>(maxX, row.mOriginX + row.mSide);
                maxY = std::max<double>(maxY, row.mOriginY + row.mSide);
            }
            defaults.mCamera.mX = (minX + maxX) / 2;
            defaults.mCamera.mY = (minY + maxY) / 2;
            defaults.mCamera.mZ = defaultCameraHeight;
            const double cells = std::max(maxX - minX, maxY - minY) / mModel->cellSize();
            defaults.mScale = static_cast<std::uint32_t>(
                std::clamp(static_cast<int>(wantedMapSize / std::max(cells, 1.0)), 1, maxMapScale));
        }
        setOptions(defaults);
        if (mTable->rowCount() > 0)
            selectRow(0);
        return true;
    }

    CSMLod::WorldGridOptions TileViewer::options() const
    {
        return mWanted;
    }

    CSMLod::WorldGridOptions TileViewer::readControls() const
    {
        CSMLod::WorldGridOptions result;
        result.mCamera.mX = mCamera[0]->value();
        result.mCamera.mY = mCamera[1]->value();
        result.mCamera.mZ = mCamera[2]->value();
        const double heading = mHeading->value() * std::numbers::pi / 180.0;
        result.mCamera.mForwardX = std::sin(heading);
        result.mCamera.mForwardY = std::cos(heading);
        result.mCamera.mForwardZ = 0;
        result.mCamera.mFovY = mFov->value() * std::numbers::pi / 180.0;
        result.mCamera.mViewportHeight = mViewport->value();
        result.mThreshold = static_cast<float>(mThreshold->value());
        const int mode = mMode->currentIndex();
        result.mMode = mode == 1 ? CSMLod::ForcedMode::AllTile
                                 : (mode == 2 ? CSMLod::ForcedMode::AllLive : CSMLod::ForcedMode::Auto);
        result.mScale = static_cast<std::uint32_t>(mScale->value());
        return result;
    }

    void TileViewer::setOptions(const CSMLod::WorldGridOptions& wanted)
    {
        mWanted = wanted; // kept as given: the controls round it, and only an edit of a control replaces it
        mUpdating = true;
        mCamera[0]->setValue(wanted.mCamera.mX);
        mCamera[1]->setValue(wanted.mCamera.mY);
        mCamera[2]->setValue(wanted.mCamera.mZ);
        mHeading->setValue(std::atan2(wanted.mCamera.mForwardX, wanted.mCamera.mForwardY) * 180.0 / std::numbers::pi);
        mFov->setValue(wanted.mCamera.mFovY * 180.0 / std::numbers::pi);
        mViewport->setValue(wanted.mCamera.mViewportHeight);
        mThreshold->setValue(wanted.mThreshold);
        mMode->setCurrentIndex(modeIndex(wanted.mMode));
        mScale->setValue(static_cast<int>(wanted.mScale));
        mUpdating = false;
        refreshMap();
    }

    void TileViewer::controlsChanged()
    {
        if (mUpdating)
            return;
        mWanted = readControls();
        refreshMap();
    }

    void TileViewer::selectRow(int row)
    {
        if (row >= 0 && row < mTable->rowCount())
            mTable->selectRow(row);
        showTile(row);
    }

    const CSMLod::WorldGridResult& TileViewer::refreshMap()
    {
        if (!mGrid)
            return mResult;
        mResult = mGrid->update(options());
        mMapImage = toQImage(mResult.mMap);
        mMapLabel->setPixmap(QPixmap::fromImage(mMapImage));
        mMapLabel->adjustSize();
        mSummary->setText(QString::fromStdString(CSMLod::summarise(mResult)));
        return mResult;
    }

    QImage TileViewer::tileImage(Picture picture) const
    {
        return mPictures[static_cast<int>(picture)];
    }

    void TileViewer::rebuildTable()
    {
        const auto& rows = mModel->rows();
        mTable->setRowCount(static_cast<int>(rows.size()));
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        {
            const CSMLod::TileRow& row = rows[static_cast<std::size_t>(i)];
            const QString flags = QString(row.mFlags & Lod::flagHasNormals ? "N" : "-")
                + QString(row.mFlags & Lod::flagHasAlbedo ? "A" : "-");
            const QString cells[tableColumns]
                = { QString::number(row.mKey, 16), QString::number(row.mLevel), QString::number(row.mTileX),
                      QString::number(row.mTileY), QString::number(row.mCellX), QString::number(row.mCellY),
                      QString::number(row.mError, 'f', 1), QString::number(row.mSizeBytes), flags };
            for (int c = 0; c < tableColumns; ++c)
                mTable->setItem(i, c, new QTableWidgetItem(cells[c]));
        }
        mTable->resizeColumnsToContents();
    }

    void TileViewer::showTile(int row)
    {
        for (QImage& picture : mPictures)
            picture = QImage();
        if (mModel && row >= 0 && row < static_cast<int>(mModel->rows().size()))
        {
            Lod::TerrainTile tile;
            if (!mModel->readTile(static_cast<std::size_t>(row), tile))
            {
                mPictures[0] = toQImage(CSMLod::heightImage(tile));
                mPictures[1] = toQImage(CSMLod::normalImage(tile));
                mPictures[2] = toQImage(CSMLod::albedoImage(tile));
            }
        }
        for (int i = 0; i < 3; ++i)
        {
            if (mPictures[i].isNull())
            {
                mPictureLabels[i]->setPixmap(QPixmap());
                mPictureLabels[i]->setText(i == 2 ? tr("(no albedo in this tile)") : QString());
                continue;
            }
            const int factor = std::max(1, minPictureWidth / mPictures[i].width());
            mPictureLabels[i]->setPixmap(QPixmap::fromImage(
                mPictures[i].scaled(mPictures[i].size() * factor, Qt::KeepAspectRatio, Qt::FastTransformation)));
        }
    }
}
