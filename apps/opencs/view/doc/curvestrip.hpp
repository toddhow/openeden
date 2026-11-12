#ifndef CSV_DOC_CURVESTRIP_H
#define CSV_DOC_CURVESTRIP_H

#include <QPoint>
#include <QWidget>

#include <optional>
#include <string>

#include <apps/opencs/model/module/timelinemodel.hpp>
#include <openeden/editor_module/types.hpp>

class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

namespace CSVModule
{
    class ModuleUi;
}

namespace CSVDoc
{
    class AudioPlayer;

    /// The drawing and mouse part of the timeline canvas (Editor Module API 1.4, task 0147): a ruler, the waveform of
    /// the first WaveformTrack, the channels of the first CurveTrack as curves (the chosen channel thick, with its
    /// keys), the playhead. No signals: it reads the model on every refresh() and edits through CSMModule::CurveEditor.
    class CurveCanvas final : public QWidget
    {
    public:
        explicit CurveCanvas(CSVModule::ModuleUi& ui, QWidget* parent = nullptr);

        /// Takes the tracks from the host again and repaints.
        void refresh();
        const CSMModule::TimelineTracks& tracks() const { return mTracks; }
        const std::string& channel() const { return mChannel; }
        void setChannel(const std::string& id);
        int selectedKey() const { return mSelectedKey; }

        /// Widget coordinates of key \a index of the chosen channel (for the unattended checks), or a null point.
        QPoint keyPixel(int index) const;
        /// Widget coordinates of a point on the ruler at \a seconds.
        QPoint rulerPixel(double seconds) const;

        static constexpr int sRulerHeight = 20;
        static constexpr int sWaveHeight = 64;
        static constexpr int sMargin = 8;
        static constexpr double sPickTolerancePx = 8.0;

    protected:
        void paintEvent(QPaintEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void mouseDoubleClickEvent(QMouseEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        QSize sizeHint() const override { return QSize(640, 220); }

    private:
        CSMModule::TimeAxis axis() const;
        int curveTop() const;
        int curveHeight() const;
        std::optional<openeden::editor::CurveChannelInfo> channelInfo() const;
        std::vector<openeden::editor::CurveKey> currentKeys() const;
        openeden::editor::CurveKey keyAt(const QPoint& p, const openeden::editor::CurveChannelInfo& info) const;

        CSVModule::ModuleUi& mUi;
        CSMModule::TimelineTracks mTracks;
        std::string mChannel;
        int mSelectedKey = -1;
        bool mDragging = false;
        bool mScrubbing = false;
        openeden::editor::CurveKey mPreview;
    };

    /// The dock's content: a toolbar (Play/Stop, the channel to edit, a status line) over the canvas. Playback uses
    /// AudioPlayer on the waveform track's buffer from the playhead and moves the playhead with a timer.
    class CurveStrip final : public QWidget
    {
        Q_OBJECT

    public:
        explicit CurveStrip(CSVModule::ModuleUi& ui, QWidget* parent = nullptr);
        ~CurveStrip() override;

        /// True while the active asset has a curve or a waveform track (the dock shows then).
        bool hasTracks() const { return !mCanvas->tracks().empty(); }

        // For unattended checks.
        CurveCanvas* canvas() const { return mCanvas; }
        QString selectedChannel() const;
        void selectChannel(const QString& id);
        int channelCount() const;
        /// Starts or stops playback as the Play button does; false when nothing can be played.
        bool togglePlayback();
        bool playing() const { return mPlaying; }

    private:
        void refresh();
        void onTick();
        void stopPlayback();

        CSVModule::ModuleUi& mUi;
        CurveCanvas* mCanvas;
        QComboBox* mChannels;
        QPushButton* mPlay;
        QLabel* mStatus;
        QTimer* mTimer;
        AudioPlayer* mPlayer;
        bool mPlaying = false;
        double mPlayStartPlayhead = 0.0;
        qint64 mPlayStartMs = 0;
        std::string mChannelListKey; // the channel ids the combo box shows
    };
}

#endif
