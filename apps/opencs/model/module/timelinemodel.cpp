#include "timelinemodel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <variant>

namespace CSMModule
{
    using namespace openeden::editor;

    namespace
    {
        double span(const TimeAxis& axis)
        {
            const double s = axis.mT1 - axis.mT0;
            return s > 1e-12 ? s : 1e-12;
        }

        double rangeOf(const CurveChannelInfo& channel)
        {
            const double r = channel.maxValue - channel.minValue;
            return r > 1e-12 ? r : 1e-12;
        }
    }

    double TimeAxis::toX(double seconds) const
    {
        return (seconds - mT0) / span(*this) * mWidthPx;
    }

    double TimeAxis::toTime(double x) const
    {
        return mT0 + x / std::max(mWidthPx, 1) * span(*this);
    }

    double valueToY(const CurveChannelInfo& channel, double value, int heightPx)
    {
        return (channel.maxValue - value) / rangeOf(channel) * heightPx;
    }

    double yToValue(const CurveChannelInfo& channel, double y, int heightPx)
    {
        return channel.maxValue - y / std::max(heightPx, 1) * rangeOf(channel);
    }

    std::vector<PeakColumn> waveformPeaks(const WaveformData& data, const TimeAxis& axis)
    {
        std::vector<PeakColumn> columns;
        if (axis.mWidthPx <= 0)
            return columns;
        columns.resize(static_cast<std::size_t>(axis.mWidthPx));
        if (data.samples == nullptr || data.sampleCount == 0 || data.sampleRate == 0)
            return columns;

        auto sampleAt = [&](long long index) {
            const long long clamped = std::clamp<long long>(index, 0, static_cast<long long>(data.sampleCount) - 1);
            const float v = data.samples[clamped];
            return std::isfinite(v) ? v : 0.f;
        };
        for (int x = 0; x < axis.mWidthPx; ++x)
        {
            const double t0 = axis.toTime(x);
            const double t1 = axis.toTime(x + 1);
            // The samples whose index lies in [first, last]; a column narrower than one sample takes the nearest one.
            const double firstD = std::ceil(t0 * data.sampleRate);
            const double lastD = std::ceil(t1 * data.sampleRate) - 1.0;
            PeakColumn& column = columns[static_cast<std::size_t>(x)];
            const double limit = static_cast<double>(data.sampleCount);
            if (lastD < 0.0 || firstD >= limit)
            {
                // Entirely outside the audio: silence.
                continue;
            }
            if (lastD < firstD)
            {
                const float v = sampleAt(static_cast<long long>(std::llround(t0 * data.sampleRate)));
                column.mMin = column.mMax = v;
                continue;
            }
            const long long first = std::max<long long>(0, static_cast<long long>(firstD));
            const long long last = std::min<long long>(
                static_cast<long long>(data.sampleCount) - 1, static_cast<long long>(lastD));
            float lo = std::numeric_limits<float>::max();
            float hi = std::numeric_limits<float>::lowest();
            for (long long i = first; i <= last; ++i)
            {
                const float v = sampleAt(i);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            column.mMin = lo;
            column.mMax = hi;
        }
        return columns;
    }

    std::optional<KeyHit> pickKey(const std::vector<CurveKey>& keys, const CurveChannelInfo& channel,
        const TimeAxis& axis, int heightPx, double px, double py, double tolPx)
    {
        std::optional<KeyHit> best;
        for (std::size_t i = 0; i < keys.size(); ++i)
        {
            const double dx = axis.toX(keys[i].timeSeconds) - px;
            const double dy = valueToY(channel, keys[i].value, heightPx) - py;
            const double d = std::sqrt(dx * dx + dy * dy);
            if (!std::isfinite(d) || d > tolPx)
                continue;
            if (!best || d < best->mDistancePx)
                best = KeyHit{ i, d };
        }
        return best;
    }

    std::string checkCurveKey(const CurveChannelInfo& channel, const CurveKey& key, double duration)
    {
        if (!std::isfinite(key.timeSeconds) || !std::isfinite(key.value))
            return "the key's time or value is not a finite number";
        if (key.timeSeconds < 0.0 || key.timeSeconds > duration)
            return "the key's time is outside the track";
        if (key.value < channel.minValue || key.value > channel.maxValue)
            return "the key's value is outside the channel's range";
        return {};
    }

    CurveKey clampKey(const CurveChannelInfo& channel, const CurveKey& key, double duration)
    {
        CurveKey out = key;
        out.timeSeconds = std::clamp(std::isfinite(key.timeSeconds) ? key.timeSeconds : 0.0, 0.0, std::max(duration, 0.0));
        out.value = std::clamp(std::isfinite(key.value) ? key.value : channel.minValue, channel.minValue, channel.maxValue);
        return out;
    }

    std::string checkDialog(const DialogDesc& desc)
    {
        if (desc.title.empty())
            return "the dialog has no title";
        if (desc.fields.empty())
            return "the dialog has no fields";
        if (desc.fields.size() > kMaxDialogFields)
            return "the dialog has too many fields";
        std::set<std::string> ids;
        for (const PropertyDesc& field : desc.fields)
        {
            if (field.id.empty())
                return "a field has no id";
            if (!ids.insert(field.id).second)
                return "the field id '" + field.id + "' is used twice";
            if (field.type == PropertyType::Vec3)
                return "the field '" + field.id + "' is a Vec3, which a dialog does not take";
            if (field.type == PropertyType::Choice && field.choices.empty())
                return "the choice field '" + field.id + "' has no choices";
            if ((field.type == PropertyType::Int || field.type == PropertyType::Float)
                && (std::isnan(field.min) || std::isnan(field.max) || field.min > field.max))
                return "the field '" + field.id + "' has an empty range";
        }
        return {};
    }

    namespace
    {
        double lowerBound(const PropertyDesc& field)
        {
            return field.min == std::numeric_limits<double>::lowest() ? 0.0 : field.min;
        }

        PropertyValue zeroOf(const PropertyDesc& field)
        {
            switch (field.type)
            {
                case PropertyType::Bool:
                    return false;
                case PropertyType::Int:
                    return static_cast<std::int64_t>(std::clamp(lowerBound(field), field.min, field.max));
                case PropertyType::Float:
                    return std::clamp(lowerBound(field), field.min, field.max);
                case PropertyType::Choice:
                    return field.choices.empty() ? std::string() : field.choices.front();
                case PropertyType::String:
                case PropertyType::Vec3:
                    break;
            }
            return std::string();
        }

        bool fits(const PropertyDesc& field, const PropertyValue& value)
        {
            switch (field.type)
            {
                case PropertyType::Bool:
                    return std::holds_alternative<bool>(value);
                case PropertyType::Int:
                    return std::holds_alternative<std::int64_t>(value)
                        && static_cast<double>(std::get<std::int64_t>(value)) >= field.min
                        && static_cast<double>(std::get<std::int64_t>(value)) <= field.max;
                case PropertyType::Float:
                    return std::holds_alternative<double>(value) && std::isfinite(std::get<double>(value))
                        && std::get<double>(value) >= field.min && std::get<double>(value) <= field.max;
                case PropertyType::String:
                    return std::holds_alternative<std::string>(value);
                case PropertyType::Choice:
                    return std::holds_alternative<std::string>(value)
                        && std::find(field.choices.begin(), field.choices.end(), std::get<std::string>(value))
                        != field.choices.end();
                case PropertyType::Vec3:
                    break;
            }
            return false;
        }
    }

    std::vector<PropertyValue> dialogStartValues(const DialogDesc& desc)
    {
        std::vector<PropertyValue> values;
        for (std::size_t i = 0; i < desc.fields.size(); ++i)
        {
            if (i < desc.initial.size() && fits(desc.fields[i], desc.initial[i]))
                values.push_back(desc.initial[i]);
            else
                values.push_back(zeroOf(desc.fields[i]));
        }
        return values;
    }

    bool dialogValuesValid(const DialogDesc& desc, const std::vector<PropertyValue>& values)
    {
        if (values.size() != desc.fields.size())
            return false;
        for (std::size_t i = 0; i < values.size(); ++i)
            if (!fits(desc.fields[i], values[i]))
                return false;
        return true;
    }

    std::optional<std::vector<PropertyValue>> DialogService::show(const DialogDesc& desc)
    {
        mRefusal.clear();
        if (const std::string why = checkDialog(desc); !why.empty())
        {
            mRefusal = why;
            return std::nullopt;
        }
        if (!mRunner)
        {
            mRefusal = "this host cannot show dialogs";
            return std::nullopt;
        }
        auto answer = mRunner(desc, dialogStartValues(desc));
        if (!answer)
            return std::nullopt; // cancelled
        if (!dialogValuesValid(desc, *answer))
        {
            mRefusal = "the dialog returned values that do not fit its fields";
            return std::nullopt;
        }
        return answer;
    }

    bool Playhead::setDuration(double seconds)
    {
        mDuration = std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
        if (mSeconds > mDuration)
        {
            mSeconds = mDuration;
            return true;
        }
        return false;
    }

    bool Playhead::set(double seconds)
    {
        if (!std::isfinite(seconds))
            return false;
        const double clamped = std::clamp(seconds, 0.0, mDuration);
        if (clamped == mSeconds)
            return false;
        mSeconds = clamped;
        return true;
    }

    std::optional<CurveChannelInfo> CurveEditor::channel(std::string_view id) const
    {
        for (CurveChannelInfo& info : mTrack.channels())
            if (info.id == id)
                return info;
        return std::nullopt;
    }

    bool CurveEditor::addKey(std::string_view channelId, const CurveKey& key)
    {
        mRefusal.clear();
        auto info = channel(channelId);
        if (!info)
        {
            mRefusal = "unknown channel '" + std::string(channelId) + "'";
            return false;
        }
        if (std::string why = checkCurveKey(*info, key, mTrack.durationSeconds()); !why.empty())
        {
            mRefusal = std::move(why);
            return false;
        }
        auto command = mTrack.makeAddKeyCommand(channelId, key);
        if (!command)
        {
            mRefusal = "the track refused the new key";
            return false;
        }
        mContext.pushCommand(std::move(command));
        return true;
    }

    bool CurveEditor::moveKey(std::string_view channelId, std::size_t index, const CurveKey& newKey)
    {
        mRefusal.clear();
        auto info = channel(channelId);
        if (!info)
        {
            mRefusal = "unknown channel '" + std::string(channelId) + "'";
            return false;
        }
        const auto keys = mTrack.channelKeys(channelId);
        if (index >= keys.size())
        {
            mRefusal = "there is no key " + std::to_string(index);
            return false;
        }
        if (!std::isfinite(newKey.timeSeconds) || !std::isfinite(newKey.value))
        {
            mRefusal = "the key's time or value is not a finite number";
            return false;
        }
        const CurveKey clamped = clampKey(*info, newKey, mTrack.durationSeconds());
        if (clamped.timeSeconds == keys[index].timeSeconds && clamped.value == keys[index].value)
        {
            mRefusal = "the key did not move";
            return false;
        }
        auto command = mTrack.makeMoveKeyCommand(channelId, index, clamped);
        if (!command)
        {
            mRefusal = "the track refused the move";
            return false;
        }
        mContext.pushCommand(std::move(command));
        return true;
    }

    bool CurveEditor::deleteKey(std::string_view channelId, std::size_t index)
    {
        mRefusal.clear();
        if (!channel(channelId))
        {
            mRefusal = "unknown channel '" + std::string(channelId) + "'";
            return false;
        }
        if (index >= mTrack.channelKeys(channelId).size())
        {
            mRefusal = "there is no key " + std::to_string(index);
            return false;
        }
        auto command = mTrack.makeDeleteKeyCommand(channelId, index);
        if (!command)
        {
            mRefusal = "the track refused the delete";
            return false;
        }
        mContext.pushCommand(std::move(command));
        return true;
    }

    double TimelineTracks::duration() const
    {
        double longest = 0.0;
        for (const CurveTrack* track : mCurves)
            longest = std::max(longest, track->durationSeconds());
        for (const WaveformTrack* track : mWaves)
        {
            const WaveformData data = track->waveform();
            if (data.sampleRate != 0)
                longest = std::max(longest, static_cast<double>(data.sampleCount) / data.sampleRate);
        }
        return longest;
    }

    TimelineTracks classifyTracks(const std::vector<TimelineTrack*>& tracks)
    {
        TimelineTracks result;
        for (TimelineTrack* track : tracks)
        {
            if (track == nullptr)
                continue;
            if (auto* curve = dynamic_cast<CurveTrack*>(track))
                result.mCurves.push_back(curve);
            if (auto* wave = dynamic_cast<WaveformTrack*>(track))
                result.mWaves.push_back(wave);
        }
        return result;
    }

    std::vector<char> pcmToWav16(const float* samples, std::size_t count, std::uint32_t rate, double fromSeconds)
    {
        if (samples == nullptr || rate == 0 || !std::isfinite(fromSeconds))
            return {};
        const double startD = std::max(0.0, fromSeconds) * rate;
        if (startD >= static_cast<double>(count))
            return {};
        const std::size_t start = static_cast<std::size_t>(startD);
        const std::size_t n = count - start;
        std::vector<char> wav;
        wav.reserve(44 + n * 2);
        auto put32 = [&](std::uint32_t v) {
            for (int i = 0; i < 4; ++i)
                wav.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
        };
        auto put16 = [&](std::uint16_t v) {
            wav.push_back(static_cast<char>(v & 0xff));
            wav.push_back(static_cast<char>(v >> 8));
        };
        auto text = [&](const char* s) {
            for (; *s != 0; ++s)
                wav.push_back(*s);
        };
        text("RIFF");
        put32(static_cast<std::uint32_t>(36 + n * 2));
        text("WAVEfmt ");
        put32(16);
        put16(1);
        put16(1);
        put32(rate);
        put32(rate * 2);
        put16(2);
        put16(16);
        text("data");
        put32(static_cast<std::uint32_t>(n * 2));
        for (std::size_t i = 0; i < n; ++i)
        {
            const float v = std::isfinite(samples[start + i]) ? std::clamp(samples[start + i], -1.f, 1.f) : 0.f;
            put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v * 32767.f))));
        }
        return wav;
    }
}
