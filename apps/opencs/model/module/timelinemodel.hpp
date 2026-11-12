#ifndef CSM_MODULE_TIMELINEMODEL_H
#define CSM_MODULE_TIMELINEMODEL_H

// Editor Module API 1.4 (task 0147): the pure part of Studio's curve canvas, waveform track, playhead and dialogs. No Qt,
// no GPU: the widget (view/doc/curvestrip.*) draws what this computes, and eden-studio-tests run it without a display.

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

namespace CSMModule
{
    /// Maps time to pixels and back: [t0, t1] seconds across widthPx pixels.
    struct TimeAxis
    {
        double mT0 = 0.0;
        double mT1 = 1.0;
        int mWidthPx = 1;

        double toX(double seconds) const;
        double toTime(double x) const;
    };

    /// Maps a channel's value range to pixel rows: maxValue at row 0, minValue at heightPx.
    double valueToY(const openeden::editor::CurveChannelInfo& channel, double value, int heightPx);
    double yToValue(const openeden::editor::CurveChannelInfo& channel, double y, int heightPx);

    struct PeakColumn
    {
        float mMin = 0.f;
        float mMax = 0.f;
    };

    /// One min/max pair per pixel column of the axis. A column that no sample falls in (past the end of the audio, or
    /// zoomed in so far that no sample lies in it) holds the nearest sample's value; an empty buffer gives zeros.
    /// Non-finite samples count as 0. Never reads outside the buffer.
    std::vector<PeakColumn> waveformPeaks(const openeden::editor::WaveformData& data, const TimeAxis& axis);

    struct KeyHit
    {
        std::size_t mIndex = 0;
        double mDistancePx = 0.0;
    };

    /// The key nearest to the point (px, py) within tolPx pixels, or nothing. Ties go to the lower index.
    std::optional<KeyHit> pickKey(const std::vector<openeden::editor::CurveKey>& keys,
        const openeden::editor::CurveChannelInfo& channel, const TimeAxis& axis, int heightPx, double px, double py,
        double tolPx);

    /// Empty when the key is acceptable for the channel: finite, time in [0, duration], value inside the channel's
    /// range. Otherwise the reason.
    std::string checkCurveKey(
        const openeden::editor::CurveChannelInfo& channel, const openeden::editor::CurveKey& key, double duration);

    /// Clamps a value to the channel's range (a drag past the edge).
    openeden::editor::CurveKey clampKey(const openeden::editor::CurveChannelInfo& channel,
        const openeden::editor::CurveKey& key, double duration);

    /// Empty when a dialog description can be shown, otherwise why not (no title, no fields, a Vec3 field, an empty or
    /// duplicate id, too many fields, a Choice with no choices, a number range that is empty or not finite).
    std::string checkDialog(const openeden::editor::DialogDesc& desc);

    /// One value per field: the initial value when it has the field's type (and a Choice listed), else the type's zero
    /// (the first choice for a Choice, min or 0 for a number).
    std::vector<openeden::editor::PropertyValue> dialogStartValues(const openeden::editor::DialogDesc& desc);

    /// True when each value has its field's type and is inside its range (a Choice is a listed one).
    bool dialogValuesValid(
        const openeden::editor::DialogDesc& desc, const std::vector<openeden::editor::PropertyValue>& values);

    inline constexpr std::size_t kMaxDialogFields = 32;

    /// The dialog service behind EditorContext::showDialog: checks the description, hands it to the runner (Studio's
    /// Qt dialog; a test's fake) and refuses an answer whose values are not valid.
    class DialogService
    {
    public:
        using Runner = std::function<std::optional<std::vector<openeden::editor::PropertyValue>>(
            const openeden::editor::DialogDesc&, const std::vector<openeden::editor::PropertyValue>& start)>;

        void setRunner(Runner runner) { mRunner = std::move(runner); }
        std::optional<std::vector<openeden::editor::PropertyValue>> show(const openeden::editor::DialogDesc& desc);
        /// The reason the last show() refused, empty if it did not.
        const std::string& lastRefusal() const { return mRefusal; }

    private:
        Runner mRunner;
        std::string mRefusal;
    };

    /// The playhead: seconds, clamped to [0, duration] (a non-finite request is ignored). Studio owns it.
    class Playhead
    {
    public:
        double seconds() const { return mSeconds; }
        double duration() const { return mDuration; }
        /// A new length (the active tracks'); the playhead is clamped into it. Returns whether the position moved.
        bool setDuration(double seconds);
        /// Returns whether the position changed.
        bool set(double seconds);

    private:
        double mSeconds = 0.0;
        double mDuration = 0.0;
    };

    /// The edits of a curve track as undo steps: checks the key, asks the track for the command and pushes it on the
    /// context (one command = one step). Returns false, pushing nothing, when the edit was refused.
    class CurveEditor
    {
    public:
        CurveEditor(openeden::editor::EditorContext& context, openeden::editor::CurveTrack& track)
            : mContext(context)
            , mTrack(track)
        {
        }

        bool addKey(std::string_view channelId, const openeden::editor::CurveKey& key);
        /// The new key is clamped to the channel's range and the track's length; nothing is pushed when it equals the
        /// old one.
        bool moveKey(std::string_view channelId, std::size_t index, const openeden::editor::CurveKey& newKey);
        bool deleteKey(std::string_view channelId, std::size_t index);
        const std::string& lastRefusal() const { return mRefusal; }

    private:
        std::optional<openeden::editor::CurveChannelInfo> channel(std::string_view id) const;

        openeden::editor::EditorContext& mContext;
        openeden::editor::CurveTrack& mTrack;
        std::string mRefusal;
    };

    /// Whether Studio has anything to draw in the timeline canvas for these tracks.
    struct TimelineTracks
    {
        std::vector<openeden::editor::CurveTrack*> mCurves;
        std::vector<openeden::editor::WaveformTrack*> mWaves;
        bool empty() const { return mCurves.empty() && mWaves.empty(); }
        double duration() const;
    };

    /// A 16 bit mono WAV file of samples[from..] for the system's audio output; empty for no samples, a start at or past
    /// the end, a zero rate or a non-finite start. Non-finite samples are silence.
    std::vector<char> pcmToWav16(const float* samples, std::size_t count, std::uint32_t rate, double fromSeconds);

    /// Splits tracks into curve and waveform ones with dynamic_cast (a track may be both).
    TimelineTracks classifyTracks(const std::vector<openeden::editor::TimelineTrack*>& tracks);
}

#endif
