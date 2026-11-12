#include "curvestrip.hpp"

#include "audioplayer.hpp"

#include <algorithm>
#include <cmath>

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <apps/opencs/view/module/moduleui.hpp>

namespace CSVDoc
{
    using namespace openeden::editor;

    namespace
    {
        constexpr int cTickMs = 30;
        constexpr double cMinDuration = 0.001;

        QColor toQt(const Colour& c, float alpha = -1.f)
        {
            return QColor::fromRgbF(std::clamp(c.r, 0.f, 1.f), std::clamp(c.g, 0.f, 1.f), std::clamp(c.b, 0.f, 1.f),
                alpha >= 0.f ? alpha : std::clamp(c.a, 0.f, 1.f));
        }
    }

    // ---------------------------------------------------------------------------------------------------------------

    CurveCanvas::CurveCanvas(CSVModule::ModuleUi& ui, QWidget* parent)
        : QWidget(parent)
        , mUi(ui)
    {
        setObjectName("moduleCurveCanvas");
        setFocusPolicy(Qt::StrongFocus);
        setMinimumHeight(sRulerHeight + 80);
        setMouseTracking(false);
        refresh();
    }

    void CurveCanvas::refresh()
    {
        mTracks = mUi.timelineTracks();
        mUi.syncPlayheadDuration();
        // Keep the chosen channel if the track still has it, else the first one.
        std::string chosen;
        if (!mTracks.mCurves.empty())
        {
            const auto channels = mTracks.mCurves.front()->channels();
            for (const auto& c : channels)
                if (c.id == mChannel)
                    chosen = c.id;
            if (chosen.empty() && !channels.empty())
                chosen = channels.front().id;
        }
        if (chosen != mChannel)
        {
            mChannel = chosen;
            mSelectedKey = -1;
        }
        const std::size_t keyCount = currentKeys().size();
        if (mSelectedKey >= static_cast<int>(keyCount))
            mSelectedKey = -1;
        if (!mDragging)
            update();
    }

    void CurveCanvas::setChannel(const std::string& id)
    {
        if (id == mChannel)
            return;
        mChannel = id;
        mSelectedKey = -1;
        mDragging = false;
        update();
    }

    CSMModule::TimeAxis CurveCanvas::axis() const
    {
        return CSMModule::TimeAxis{ 0.0, std::max(mTracks.duration(), cMinDuration), std::max(1, width() - 2 * sMargin) };
    }

    int CurveCanvas::curveTop() const
    {
        return sRulerHeight + (mTracks.mWaves.empty() ? 0 : sWaveHeight);
    }

    int CurveCanvas::curveHeight() const
    {
        return std::max(1, height() - curveTop() - 4);
    }

    std::optional<CurveChannelInfo> CurveCanvas::channelInfo() const
    {
        if (mTracks.mCurves.empty())
            return std::nullopt;
        for (auto& c : mTracks.mCurves.front()->channels())
            if (c.id == mChannel)
                return c;
        return std::nullopt;
    }

    std::vector<CurveKey> CurveCanvas::currentKeys() const
    {
        if (mTracks.mCurves.empty() || mChannel.empty())
            return {};
        return mTracks.mCurves.front()->channelKeys(mChannel);
    }

    CurveKey CurveCanvas::keyAt(const QPoint& p, const CurveChannelInfo& info) const
    {
        const CSMModule::TimeAxis a = axis();
        CurveKey key;
        key.timeSeconds = a.toTime(p.x() - sMargin);
        key.value = CSMModule::yToValue(info, p.y() - curveTop(), curveHeight());
        return CSMModule::clampKey(info, key, mTracks.mCurves.front()->durationSeconds());
    }

    QPoint CurveCanvas::keyPixel(int index) const
    {
        const auto info = channelInfo();
        const auto keys = currentKeys();
        if (!info || index < 0 || index >= static_cast<int>(keys.size()))
            return QPoint();
        const auto& k = keys[static_cast<std::size_t>(index)];
        return QPoint(sMargin + static_cast<int>(std::lround(axis().toX(k.timeSeconds))),
            curveTop() + static_cast<int>(std::lround(CSMModule::valueToY(*info, k.value, curveHeight()))));
    }

    QPoint CurveCanvas::rulerPixel(double seconds) const
    {
        return QPoint(sMargin + static_cast<int>(std::lround(axis().toX(seconds))), sRulerHeight / 2);
    }

    void CurveCanvas::paintEvent(QPaintEvent*)
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        const CSMModule::TimeAxis a = axis();
        const QColor text = palette().color(QPalette::Text);
        const QColor mid = palette().color(QPalette::Mid);

        // Ruler: a tick per second (or per tenth of a second when zoomed in far), labelled.
        painter.setPen(mid);
        painter.drawLine(sMargin, sRulerHeight - 1, width() - sMargin, sRulerHeight - 1);
        const double span = a.mT1 - a.mT0;
        const double step = span > 20.0 ? 5.0 : (span > 4.0 ? 1.0 : (span > 1.0 ? 0.5 : 0.1));
        for (double t = 0.0; t <= span + 1e-9; t += step)
        {
            const int x = sMargin + static_cast<int>(std::lround(a.toX(t)));
            painter.setPen(mid);
            painter.drawLine(x, sRulerHeight - 6, x, sRulerHeight - 1);
            painter.setPen(text);
            painter.drawText(x + 3, sRulerHeight - 7, QString::number(t, 'f', step < 1.0 ? 1 : 0) + "s");
        }

        // Waveform band.
        if (!mTracks.mWaves.empty())
        {
            const WaveformData wave = mTracks.mWaves.front()->waveform();
            const int centre = sRulerHeight + sWaveHeight / 2;
            painter.setPen(mid);
            painter.drawLine(sMargin, centre, width() - sMargin, centre);
            const auto peaks = CSMModule::waveformPeaks(wave, a);
            painter.setPen(QColor(90, 150, 210));
            const double scale = sWaveHeight / 2.0 - 3.0;
            for (std::size_t x = 0; x < peaks.size(); ++x)
            {
                const int px = sMargin + static_cast<int>(x);
                painter.drawLine(px, centre - static_cast<int>(std::lround(peaks[x].mMax * scale)), px,
                    centre - static_cast<int>(std::lround(peaks[x].mMin * scale)));
            }
        }

        // Curves.
        const int top = curveTop();
        const int h = curveHeight();
        painter.setPen(mid);
        painter.drawLine(sMargin, top + h, width() - sMargin, top + h);
        if (!mTracks.mCurves.empty())
        {
            CurveTrack* track = mTracks.mCurves.front();
            for (const CurveChannelInfo& info : track->channels())
            {
                const bool chosen = info.id == mChannel;
                std::vector<CurveKey> keys = track->channelKeys(info.id);
                if (chosen && mDragging && mSelectedKey >= 0 && mSelectedKey < static_cast<int>(keys.size()))
                    keys[static_cast<std::size_t>(mSelectedKey)] = mPreview;
                if (keys.empty())
                    continue;
                QPainterPath path;
                auto point = [&](const CurveKey& k) {
                    return QPointF(sMargin + a.toX(k.timeSeconds), top + CSMModule::valueToY(info, k.value, h));
                };
                path.moveTo(QPointF(sMargin, point(keys.front()).y()));
                for (const CurveKey& k : keys)
                    path.lineTo(point(k));
                path.lineTo(QPointF(width() - sMargin, point(keys.back()).y()));
                painter.setPen(QPen(toQt(info.colour, chosen ? 1.f : 0.35f), chosen ? 2.0 : 1.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(path);
                if (chosen)
                    for (std::size_t i = 0; i < keys.size(); ++i)
                    {
                        const QPointF p = point(keys[i]);
                        const bool sel = static_cast<int>(i) == mSelectedKey;
                        painter.setPen(QPen(text, 1.0));
                        painter.setBrush(sel ? QBrush(palette().color(QPalette::Highlight)) : QBrush(toQt(info.colour)));
                        painter.drawRect(QRectF(p.x() - 3.5, p.y() - 3.5, 7.0, 7.0));
                    }
            }
        }

        // Playhead.
        const int x = sMargin + static_cast<int>(std::lround(a.toX(mUi.playheadSeconds())));
        painter.setPen(QPen(QColor(230, 60, 60), 2.0));
        painter.drawLine(x, 0, x, height());
    }

    void CurveCanvas::mousePressEvent(QMouseEvent* event)
    {
        if (event->button() != Qt::LeftButton)
            return;
        setFocus();
        const QPoint p = event->position().toPoint();
        const CSMModule::TimeAxis a = axis();
        if (p.y() < curveTop())
        {
            mScrubbing = true;
            mUi.setPlayhead(a.toTime(p.x() - sMargin));
            update();
            return;
        }
        const auto info = channelInfo();
        if (!info)
            return;
        const auto hit = CSMModule::pickKey(currentKeys(), *info, CSMModule::TimeAxis{ a.mT0, a.mT1, a.mWidthPx },
            curveHeight(), p.x() - sMargin, p.y() - curveTop(), sPickTolerancePx);
        if (hit && !info->readOnly)
        {
            mSelectedKey = static_cast<int>(hit->mIndex);
            mDragging = true;
            mPreview = currentKeys()[hit->mIndex];
        }
        else
        {
            mSelectedKey = hit ? static_cast<int>(hit->mIndex) : -1;
        }
        update();
    }

    void CurveCanvas::mouseMoveEvent(QMouseEvent* event)
    {
        const QPoint p = event->position().toPoint();
        if (mScrubbing)
        {
            mUi.setPlayhead(axis().toTime(p.x() - sMargin));
            update();
            return;
        }
        if (!mDragging)
            return;
        if (const auto info = channelInfo())
        {
            mPreview = keyAt(p, *info);
            update();
        }
    }

    void CurveCanvas::mouseReleaseEvent(QMouseEvent* event)
    {
        if (event->button() != Qt::LeftButton)
            return;
        mScrubbing = false;
        if (!mDragging)
            return;
        mDragging = false;
        if (mTracks.mCurves.empty() || mSelectedKey < 0)
            return;
        // One command = one undo step, however far the drag went. The host clamps the time between the neighbours,
        // so the key keeps its index.
        CSMModule::CurveEditor editor(mUi, *mTracks.mCurves.front());
        editor.moveKey(mChannel, static_cast<std::size_t>(mSelectedKey), mPreview);
        refresh();
    }

    void CurveCanvas::mouseDoubleClickEvent(QMouseEvent* event)
    {
        if (event->button() != Qt::LeftButton || mTracks.mCurves.empty())
            return;
        const QPoint p = event->position().toPoint();
        if (p.y() < curveTop())
            return;
        const auto info = channelInfo();
        if (!info || info->readOnly)
            return;
        const CSMModule::TimeAxis a = axis();
        const auto hit = CSMModule::pickKey(currentKeys(), *info, a, curveHeight(), p.x() - sMargin, p.y() - curveTop(),
            sPickTolerancePx);
        if (hit)
            return; // a double click on a key is two clicks on it
        const CurveKey key = keyAt(p, *info);
        CSMModule::CurveEditor editor(mUi, *mTracks.mCurves.front());
        if (editor.addKey(mChannel, key))
        {
            refresh();
            const auto keys = currentKeys();
            for (std::size_t i = 0; i < keys.size(); ++i)
                if (keys[i].timeSeconds == key.timeSeconds && keys[i].value == key.value)
                    mSelectedKey = static_cast<int>(i);
            update();
        }
    }

    void CurveCanvas::keyPressEvent(QKeyEvent* event)
    {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && mSelectedKey >= 0
            && !mTracks.mCurves.empty())
        {
            CSMModule::CurveEditor editor(mUi, *mTracks.mCurves.front());
            const int index = mSelectedKey;
            if (editor.deleteKey(mChannel, static_cast<std::size_t>(index)))
            {
                mSelectedKey = -1;
                refresh();
            }
            return;
        }
        QWidget::keyPressEvent(event);
    }

    // ---------------------------------------------------------------------------------------------------------------

    CurveStrip::CurveStrip(CSVModule::ModuleUi& ui, QWidget* parent)
        : QWidget(parent)
        , mUi(ui)
        , mCanvas(new CurveCanvas(ui, this))
        , mChannels(new QComboBox(this))
        , mPlay(new QPushButton(tr("Play"), this))
        , mStatus(new QLabel(this))
        , mTimer(new QTimer(this))
        , mPlayer(new AudioPlayer())
    {
        setObjectName("moduleCurveStrip");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(2, 2, 2, 2);
        auto* bar = new QHBoxLayout();
        mPlay->setObjectName("moduleCurvePlay");
        mPlay->setToolTip(AudioPlayer::available() ? tr("Plays the voice audio from the playhead.")
                                                   : tr("Audio playback is not available on this platform yet."));
        mChannels->setObjectName("moduleCurveChannel");
        mChannels->setToolTip(tr("The channel whose keys you edit. Drag a key to move it, double click to add one, "
                                 "Delete removes the chosen key; click the ruler or the waveform to move the playhead."));
        bar->addWidget(mPlay);
        bar->addWidget(new QLabel(tr("Channel:"), this));
        bar->addWidget(mChannels, 1);
        bar->addWidget(mStatus, 2);
        layout->addLayout(bar);
        layout->addWidget(mCanvas, 1);

        mTimer->setInterval(cTickMs);
        connect(mTimer, &QTimer::timeout, this, [this] { onTick(); });
        connect(mPlay, &QPushButton::clicked, this, [this] { togglePlayback(); });
        connect(mChannels, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index >= 0)
            {
                mCanvas->setChannel(mChannels->itemData(index).toString().toStdString());
                refresh();
            }
        });
        connect(&ui, &CSVModule::ModuleUi::changed, this, [this] {
            stopPlayback();
            refresh();
        });
        connect(&ui, &CSVModule::ModuleUi::playheadChanged, this, [this] { mCanvas->update(); refresh(); });
        refresh();
    }

    CurveStrip::~CurveStrip()
    {
        delete mPlayer;
    }

    QString CurveStrip::selectedChannel() const
    {
        return QString::fromStdString(mCanvas->channel());
    }

    void CurveStrip::selectChannel(const QString& id)
    {
        const int index = mChannels->findData(id);
        if (index >= 0)
            mChannels->setCurrentIndex(index);
    }

    int CurveStrip::channelCount() const
    {
        return mChannels->count();
    }

    void CurveStrip::refresh()
    {
        mCanvas->refresh();
        const auto& tracks = mCanvas->tracks();
        std::string key;
        if (!tracks.mCurves.empty())
            for (const auto& c : tracks.mCurves.front()->channels())
                key += c.id + "\n";
        if (key != mChannelListKey)
        {
            mChannelListKey = key;
            mChannels->blockSignals(true);
            mChannels->clear();
            if (!tracks.mCurves.empty())
                for (const auto& c : tracks.mCurves.front()->channels())
                    mChannels->addItem(QString::fromStdString(c.label.empty() ? c.id : c.label), QString::fromStdString(c.id));
            mChannels->blockSignals(false);
        }
        const int index = mChannels->findData(QString::fromStdString(mCanvas->channel()));
        if (index >= 0 && mChannels->currentIndex() != index)
        {
            mChannels->blockSignals(true);
            mChannels->setCurrentIndex(index);
            mChannels->blockSignals(false);
        }
        const bool hasAudio = !tracks.mWaves.empty();
        mPlay->setEnabled(hasAudio && AudioPlayer::available());
        mPlay->setText(mPlaying ? tr("Stop") : tr("Play"));
        mStatus->setText(tr("%1 s of %2 s").arg(mUi.playheadSeconds(), 0, 'f', 3).arg(tracks.duration(), 0, 'f', 3));
    }

    bool CurveStrip::togglePlayback()
    {
        if (mPlaying)
        {
            stopPlayback();
            refresh();
            return true;
        }
        const auto& tracks = mCanvas->tracks();
        if (tracks.mWaves.empty() || !AudioPlayer::available())
            return false;
        const WaveformData wave = tracks.mWaves.front()->waveform();
        const double length = wave.sampleRate != 0 ? static_cast<double>(wave.sampleCount) / wave.sampleRate : 0.0;
        double from = mUi.playheadSeconds();
        if (from >= length - 0.01)
        {
            from = 0.0;
            mUi.setPlayhead(0.0);
        }
        if (!mPlayer->play(wave.samples, wave.sampleCount, wave.sampleRate, from))
            return false;
        mPlaying = true;
        mPlayStartPlayhead = from;
        mPlayStartMs = QDateTime::currentMSecsSinceEpoch();
        mTimer->start();
        refresh();
        return true;
    }

    void CurveStrip::stopPlayback()
    {
        if (!mPlaying)
            return;
        mPlaying = false;
        mTimer->stop();
        mPlayer->stop();
    }

    void CurveStrip::onTick()
    {
        if (!mPlaying)
            return;
        const double elapsed = (QDateTime::currentMSecsSinceEpoch() - mPlayStartMs) / 1000.0;
        const double at = mPlayStartPlayhead + elapsed;
        mUi.setPlayhead(at);
        if (at >= mUi.playheadSeconds() + 1e-9 || mUi.playheadSeconds() >= mCanvas->tracks().duration() - 1e-6)
        {
            stopPlayback();
            refresh();
        }
    }
}
