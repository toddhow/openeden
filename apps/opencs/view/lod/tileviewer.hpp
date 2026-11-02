#ifndef CSV_LOD_TILEVIEWER_H
#define CSV_LOD_TILEVIEWER_H

// Eden Studio's tile viewer panel (task 0055, stage S1e). A plain Qt window over CSMLod: the table of a baked tile
// pack, the selected tile's height, normal and albedo pictures, and the world map of what the tile selection decides
// for a camera. No GPU: every picture is a QImage built from the model's RGBA8 arrays.

#include <memory>

#include <QImage>
#include <QString>
#include <QWidget>

#include "../../model/lod/tilemodel.hpp"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace CSVLod
{
    class TileViewer : public QWidget
    {
        Q_OBJECT

    public:
        enum class Picture
        {
            Height,
            Normal,
            Albedo,
        };

        explicit TileViewer(QWidget* parent = nullptr);
        ~TileViewer() override;

        /// Opens a pack; false and a message when it cannot. The camera goes to the middle of the tiles.
        bool openPack(const QString& path, QString& error);

        /// The controls' state as selection options, and the other way (the controls follow, with one refresh).
        CSMLod::WorldGridOptions options() const;
        void setOptions(const CSMLod::WorldGridOptions& options);

        void selectRow(int row);
        /// Runs the selection and redraws the map; returns the result of this run.
        const CSMLod::WorldGridResult& refreshMap();

        QImage worldMapImage() const { return mMapImage; }
        QImage tileImage(Picture picture) const;
        const CSMLod::TileModel* model() const { return mModel.get(); }

    private:
        void rebuildTable();
        void showTile(int row);
        void controlsChanged();
        CSMLod::WorldGridOptions readControls() const;

        std::unique_ptr<CSMLod::TileModel> mModel;
        std::unique_ptr<CSMLod::WorldGridView> mGrid;
        CSMLod::WorldGridResult mResult;
        QImage mMapImage;
        QImage mPictures[3];

        QTableWidget* mTable = nullptr;
        QLabel* mPictureLabels[3] = {};
        QLabel* mMapLabel = nullptr;
        QLabel* mSummary = nullptr;
        QDoubleSpinBox* mCamera[3] = {};
        QDoubleSpinBox* mHeading = nullptr;
        QDoubleSpinBox* mFov = nullptr;
        QDoubleSpinBox* mViewport = nullptr;
        QDoubleSpinBox* mThreshold = nullptr;
        QComboBox* mMode = nullptr;
        QSpinBox* mScale = nullptr;
        CSMLod::WorldGridOptions mWanted;
        bool mUpdating = false;
    };
}

#endif
